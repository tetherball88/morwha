#include "morwha/engine/DiagnosticBundleBuilder.h"

#include "morwha/core/MorwhaCore.h"
#include "morwha/engine/ChimeraAccessoryCatalog.h"
#include "morwha/engine/ChimeraEngineSupport.h"
#include "morwha/engine/DonorPhenotypeExtractor.h"
#include "morwha/engine/DpfClient.h"
#include "morwha/engine/EngineLogging.h"
#include "morwha/engine/FormRefResolver.h"
#include "morwha/engine/HeadPartPoolIndexer.h"
#include "morwha/engine/HybridSkinService.h"
#include "morwha/engine/SkeeOverlayClient.h"
#include "morwha/engine/SkseSerializationCallbacks.h"
#include "morwha/engine/TesNpcHydrator.h"
#include "morwha/engine/VfsAssetReader.h"
#include "morwha/state/DiagnosticZipWriter.h"
#include "morwha/state/DynamicNpcRegistry.h"
#include "morwha/state/MorwhaIniConfig.h"

#include <RE/B/BSFaceGenBaseMorphExtraData.h>
#include <RE/B/BSLightingShaderMaterialBase.h>
#include <RE/B/BSLightingShaderMaterialEnvmap.h>
#include <RE/B/BSLightingShaderMaterialEye.h>
#include <RE/B/BSLightingShaderMaterialFacegen.h>
#include <RE/B/BSLightingShaderMaterialFacegenTint.h>
#include <RE/B/BSLightingShaderMaterialHairTint.h>
#include <RE/B/BSLightingShaderProperty.h>
#include <RE/B/BSTextureSet.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <TlHelp32.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <format>
#include <fstream>
#include <unordered_set>
#include <vector>

namespace MORWHA::Engine {

    namespace {

        constexpr std::string_view kDiagOutputDir = "Data/SKSE/Plugins/MORWHA/Diagnostics";
        constexpr std::size_t kMaxInspectedTextures = 48u;
        constexpr std::size_t kMaxThumbnailBmps = 18u;
        constexpr std::size_t kMaxInspectedMeshes = 28u;
        constexpr std::size_t kMaxBundledLogBytes = 3ull * 1024ull * 1024ull;

        struct QueuedAssetProbe {
            std::string role{};
            std::string rawPath{};
            bool emitThumbnail{ false };
        };

        [[nodiscard]] std::string ToLowerAscii(std::string_view sv) {
            std::string out;
            out.reserve(sv.size());
            for (const unsigned char ch : sv) {
                out.push_back(static_cast<char>(std::tolower(ch)));
            }
            return out;
        }

        [[nodiscard]] std::string SanitizeSlug(std::string_view raw, std::size_t maxLen = 28u) {
            std::string out;
            out.reserve(std::min(raw.size(), maxLen));
            for (const unsigned char ch : raw) {
                if (out.size() >= maxLen) {
                    break;
                }
                if (std::isalnum(ch) || ch == '-' || ch == '_') {
                    out.push_back(static_cast<char>(ch));
                } else if (ch == ' ' && !out.empty() && out.back() != '_') {
                    out.push_back('_');
                }
            }
            while (!out.empty() && (out.back() == '_' || out.back() == '-')) {
                out.pop_back();
            }
            return out.empty() ? std::string("global") : out;
        }

        [[nodiscard]] std::string FormatHexId(std::uint32_t formId) {
            return std::format("{:08X}", formId);
        }

        [[nodiscard]] std::string SafeStr(const char* s) {
            return s ? std::string(s) : std::string{};
        }

        [[nodiscard]] std::string WideToUtf8(const wchar_t* wstr) {
            if (!wstr || wstr[0] == L'\0') {
                return {};
            }
            const int needed = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, nullptr, 0, nullptr, nullptr);
            if (needed <= 1) {
                return {};
            }
            std::string out(static_cast<std::size_t>(needed - 1), '\0');
            WideCharToMultiByte(CP_UTF8, 0, wstr, -1, out.data(), needed - 1, nullptr, nullptr);
            return out;
        }

        [[nodiscard]] nlohmann::json DescribeFormOrigin(const RE::TESForm* form) {
            if (!form) {
                return nullptr;
            }
            const auto* srcFile = form->GetFile(0);
            const auto* winFile = form->GetDescriptionOwnerFile();
            const char* edid = form->GetFormEditorID();
            return nlohmann::json{
                { "formId", FormatHexId(form->GetFormID()) },
                { "editorId", SafeStr(edid) },
                { "sourcePlugin", srcFile ? SafeStr(srcFile->GetFilename().data()) : "" },
                { "winningPlugin", winFile ? SafeStr(winFile->GetFilename().data()) : "" }
            };
        }

        [[nodiscard]] nlohmann::json DescribeTextureSet(
            RE::BGSTextureSet* txst,
            std::string_view rolePrefix,
            std::vector<QueuedAssetProbe>& ioTexQueue) {
            if (!txst) {
                return nullptr;
            }
            static constexpr std::array<std::pair<RE::BSTextureSet::Texture, std::string_view>, 8> kSlots{ {
                { RE::BSTextureSet::Texture::kDiffuse, "0_diffuse" },
                { RE::BSTextureSet::Texture::kNormal, "1_normal" },
                { RE::BSTextureSet::Texture::kSubsurfaceTint, "2_subsurface" },
                { RE::BSTextureSet::Texture::kDetailMap, "3_detailOrGlow" },
                { RE::BSTextureSet::Texture::kHeight, "4_height" },
                { RE::BSTextureSet::Texture::kEnvironment, "5_environment" },
                { RE::BSTextureSet::Texture::kMultilayer, "6_multilayer" },
                { RE::BSTextureSet::Texture::kSpecular, "7_specular" }
            } };

            nlohmann::json slotsObj = nlohmann::json::object();
            for (const auto& [slotEnum, slotName] : kSlots) {
                const char* p = txst->GetTexturePath(slotEnum);
                const std::string pathStr = SafeStr(p);
                slotsObj[std::string(slotName)] = pathStr;
                if (!pathStr.empty() && !rolePrefix.empty()) {
                    const bool thumb =
                        (slotEnum == RE::BSTextureSet::Texture::kDiffuse ||
                         slotEnum == RE::BSTextureSet::Texture::kSubsurfaceTint ||
                         slotEnum == RE::BSTextureSet::Texture::kDetailMap ||
                         slotEnum == RE::BSTextureSet::Texture::kSpecular);
                    ioTexQueue.push_back(QueuedAssetProbe{
                        .role = std::format("{}_{}", rolePrefix, slotName),
                        .rawPath = pathStr,
                        .emitThumbnail = thumb
                    });
                }
            }

            return nlohmann::json{
                { "form", DescribeFormOrigin(txst) },
                { "dnamFlags", std::format("0x{:04X}", static_cast<std::uint16_t>(txst->flags.underlying())) },
                { "isRuntimeHybridFaceTxst",
                  HybridSkinService::GetSingleton().IsRuntimeHybridFaceTextureSet(txst) },
                { "slots", std::move(slotsObj) }
            };
        }

        [[nodiscard]] std::string_view ShaderFeatureName(RE::BSShaderMaterial::Feature f) noexcept {
            using F = RE::BSShaderMaterial::Feature;
            switch (f) {
            case F::kDefault: return "Default";
            case F::kEnvironmentMap: return "EnvironmentMap";
            case F::kGlowMap: return "GlowMap";
            case F::kParallax: return "Parallax";
            case F::kFaceGen: return "FaceGen";
            case F::kFaceGenRGBTint: return "FaceGenRGBTint_or_SkinTint";
            case F::kHairTint: return "HairTint";
            case F::kParallaxOcc: return "ParallaxOcc";
            case F::kMultiTexLand: return "MultiTexLand";
            case F::kLODLand: return "LODLand";
            case F::kMultilayerParallax: return "MultilayerParallax";
            case F::kTreeAnim: return "TreeAnim";
            case F::kMultiIndexTriShapeSnow: return "MultiIndexTriShapeSnow";
            case F::kLODObjectsHD: return "LODObjectsHD";
            case F::kEye: return "Eye";
            case F::kCloud: return "Cloud";
            case F::kLODLandNoise: return "LODLandNoise";
            case F::kMultiTexLandLODBlend: return "MultiTexLandLODBlend";
            default: return "Other";
            }
        }

        [[nodiscard]] nlohmann::json CollectLoadedModulesJson() {
            nlohmann::json dlls = nlohmann::json::array();
            HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetCurrentProcessId());
            if (snap == INVALID_HANDLE_VALUE) {
                return dlls;
            }
            MODULEENTRY32W me{};
            me.dwSize = sizeof(me);
            if (Module32FirstW(snap, &me)) {
                do {
                    const std::string modName = WideToUtf8(me.szModule);
                    const std::string modPath = WideToUtf8(me.szExePath);
                    const std::string lowPath = ToLowerAscii(modPath);
                    const std::string lowName = ToLowerAscii(modName);
                    if (lowName == "skyrimse.exe" ||
                        lowName == "d3d11.dll" ||
                        lowName == "dxgi.dll" ||
                        lowPath.find("skse\\plugins") != std::string::npos ||
                        lowPath.find("skse/plugins") != std::string::npos ||
                        lowName.find("skse") != std::string::npos) {
                        dlls.push_back(nlohmann::json{
                            { "name", modName },
                            { "byteSize", static_cast<std::uint64_t>(me.modBaseSize) }
                        });
                    }
                } while (Module32NextW(snap, &me));
            }
            CloseHandle(snap);
            return dlls;
        }

        [[nodiscard]] nlohmann::json CollectLoadOrderJson() {
            nlohmann::json out{
                { "regularPlugins", nlohmann::json::array() },
                { "lightPlugins", nlohmann::json::array() }
            };
            auto* dh = RE::TESDataHandler::GetSingleton();
            if (!dh) {
                return out;
            }
            for (const auto* f : dh->files) {
                if (!f) {
                    continue;
                }
                const std::uint8_t idx = f->GetCompileIndex();
                if (idx == 0xFFu) {
                    continue;
                }
                if (f->IsLight()) {
                    out["lightPlugins"].push_back(nlohmann::json{
                        { "smallIndex", std::format("FE:{:03X}", f->GetSmallFileCompileIndex()) },
                        { "name", SafeStr(f->GetFilename().data()) }
                    });
                } else {
                    out["regularPlugins"].push_back(nlohmann::json{
                        { "index", std::format("{:02X}", idx) },
                        { "name", SafeStr(f->GetFilename().data()) }
                    });
                }
            }
            return out;
        }

        [[nodiscard]] nlohmann::json InspectNpcAndActorRecords(
            RE::Actor* actor,
            RE::TESNPC* npc,
            std::string_view label,
            std::vector<QueuedAssetProbe>& ioTexQueue,
            std::vector<QueuedAssetProbe>& ioMeshQueue) {
            if (!npc && actor) {
                npc = actor->GetActorBase();
            }
            if (!npc) {
                return nullptr;
            }

            const Foundation::Sex sex =
                (npc->GetSex() == RE::SEXES::kFemale) ? Foundation::Sex::Female : Foundation::Sex::Male;
            const auto reSex = (sex == Foundation::Sex::Female) ? RE::SEXES::kFemale : RE::SEXES::kMale;
            RE::TESRace* race = npc->race;
            RE::TESRace* unwrappedRace = UnwrapToPlayableSynthesisRace(race, sex);

            nlohmann::json kwList = nlohmann::json::array();
            for (const auto* kw : npc->GetKeywords()) {
                if (kw) {
                    kwList.push_back(SafeStr(kw->GetFormEditorID()));
                }
            }

            // HeadPartPoolIndexer stats for this NPC's race/sex
            nlohmann::json poolInfo = nlohmann::json::object();
            if (HeadPartPoolIndexer::GetSingleton().IsBuilt() && (unwrappedRace || race)) {
                const RE::TESRace* pRace = unwrappedRace ? unwrappedRace : race;
                const auto prefTopo = HeadPartPoolIndexer::GetSingleton().SelectPreferredTopology(pRace, sex);
                poolInfo["preferredTopology"] = static_cast<unsigned>(prefTopo);
                for (const auto [topo, tName] : {
                         std::pair{ Foundation::TopologyPool::Vanilla, "Vanilla" },
                         std::pair{ Foundation::TopologyPool::HighPolyHead, "HighPolyHead" } }) {
                    if (const auto* p = HeadPartPoolIndexer::GetSingleton().GetPool(pRace, sex, topo)) {
                        poolInfo[tName] = nlohmann::json{
                            { "faces", p->faces.size() },
                            { "eyes", p->eyes.size() },
                            { "hairs", p->hairs.size() },
                            { "mouths", p->mouths.size() },
                            { "eyebrows", p->eyebrows.size() },
                            { "facialHairs", p->facialHairs.size() },
                            { "scars", p->scars.size() }
                        };
                    }
                }
            }

            // Skin ARMO + ARMAs
            RE::TESObjectARMO* ownSkin = npc->skin;
            RE::TESObjectARMO* effSkin = ownSkin ? ownSkin : (race ? race->skin : nullptr);
            nlohmann::json skinJson = nullptr;
            if (effSkin) {
                nlohmann::json skinKws = nlohmann::json::array();
                for (const auto* kw : effSkin->GetKeywords()) {
                    if (kw) {
                        skinKws.push_back(SafeStr(kw->GetFormEditorID()));
                    }
                }
                nlohmann::json addonsJson = nlohmann::json::array();
                for (auto* arma : effSkin->armorAddons) {
                    if (!arma || (race && !arma->IsValidRace(race))) {
                        continue;
                    }
                    const std::string modelPath = SafeStr(arma->bipedModels[reSex].GetModel());
                    if (!modelPath.empty()) {
                        ioMeshQueue.push_back(QueuedAssetProbe{
                            .role = std::format("{}_arma_{:08X}", label, arma->GetFormID()),
                            .rawPath = modelPath,
                            .emitThumbnail = false
                        });
                    }
                    addonsJson.push_back(nlohmann::json{
                        { "form", DescribeFormOrigin(arma) },
                        { "slotMask", std::format("0x{:08X}", static_cast<std::uint32_t>(arma->GetSlotMask().underlying())) },
                        { "model", modelPath },
                        { "skinTextureSet",
                          DescribeTextureSet(
                              arma->skinTextures[reSex],
                              std::format("{}_arma_{:08X}", label, arma->GetFormID()),
                              ioTexQueue) }
                    });
                }
                skinJson = nlohmann::json{
                    { "npcOwnSkin", DescribeFormOrigin(ownSkin) },
                    { "effectiveSkin", DescribeFormOrigin(effSkin) },
                    { "isRuntimeHybridSkin",
                      HybridSkinService::GetSingleton().IsRuntimeHybridSkinArmor(effSkin) },
                    { "slotMask", std::format("0x{:08X}", static_cast<std::uint32_t>(effSkin->GetSlotMask().underlying())) },
                    { "keywords", std::move(skinKws) },
                    { "validRaceAddons", std::move(addonsJson) }
                };
            }

            // HeadParts
            nlohmann::json headPartsJson = nlohmann::json::array();
            if (npc->headParts && npc->numHeadParts > 0) {
                for (std::int8_t i = 0; i < npc->numHeadParts; ++i) {
                    RE::BGSHeadPart* hp = npc->headParts[i];
                    if (!hp) {
                        continue;
                    }
                    const std::string hpModel = SafeStr(hp->model.c_str());
                    if (!hpModel.empty()) {
                        ioMeshQueue.push_back(QueuedAssetProbe{
                            .role = std::format("{}_hp_{}", label, i),
                            .rawPath = hpModel,
                            .emitThumbnail = false
                        });
                    }
                    nlohmann::json trisJson = nlohmann::json::object();
                    constexpr std::array<std::string_view, 3> kTriNames{ "raceTri", "defaultTri", "chargenTri" };
                    for (std::size_t m = 0u; m < 3u; ++m) {
                        const std::string triPath = SafeStr(hp->morphs[m].model.c_str());
                        trisJson[std::string(kTriNames[m])] = triPath;
                        if (!triPath.empty()) {
                            ioMeshQueue.push_back(QueuedAssetProbe{
                                .role = std::format("{}_hp_{}_{}", label, i, kTriNames[m]),
                                .rawPath = triPath,
                                .emitThumbnail = false
                            });
                        }
                    }

                    nlohmann::json extraArr = nlohmann::json::array();
                    for (const auto* ep : hp->extraParts) {
                        if (ep) {
                            extraArr.push_back(DescribeFormOrigin(ep));
                        }
                    }

                    nlohmann::json skeeNodeOvr = nlohmann::json::array();
                    if (actor && !hp->formEditorID.empty()) {
                        for (const auto& [slotIdx, val] :
                             SkeeOverlayClient::GetSingleton().InspectHeadNodeTextureOverrides(
                                 actor, hp->formEditorID.c_str())) {
                            skeeNodeOvr.push_back(nlohmann::json{
                                { "slot", slotIdx },
                                { "value", val }
                            });
                        }
                    }

                    headPartsJson.push_back(nlohmann::json{
                        { "form", DescribeFormOrigin(hp) },
                        { "type", static_cast<std::int32_t>(hp->type.underlying()) },
                        { "flags", std::format("0x{:02X}", static_cast<std::uint32_t>(hp->flags.underlying())) },
                        { "classifiedTopology",
                          static_cast<unsigned>(HeadPartPoolIndexer::ClassifyHeadPart(hp)) },
                        { "isRuntimeHybridFace",
                          HybridSkinService::GetSingleton().IsRuntimeHybridFacePart(hp) },
                        { "model", hpModel },
                        { "morphs", std::move(trisJson) },
                        { "textureSet",
                          DescribeTextureSet(
                              hp->textureSet,
                              std::format("{}_hp_{}", label, i),
                              ioTexQueue) },
                        { "extraParts", std::move(extraArr) },
                        { "skeeNodeTextureOverrides", std::move(skeeNodeOvr) }
                    });
                }
            }

            // FaceData sliders, presets & tint layers
            nlohmann::json morphsJson = nullptr;
            if (npc->faceData) {
                nlohmann::json sliders = nlohmann::json::array();
                for (std::size_t s = 0u; s < RE::TESNPC::FaceData::Morphs::kTotal; ++s) {
                    sliders.push_back(npc->faceData->morphs[s]);
                }
                morphsJson = nlohmann::json{
                    { "sliders", std::move(sliders) },
                    { "parts",
                      nlohmann::json{
                          { "nose", npc->faceData->parts[RE::TESNPC::FaceData::Parts::kNose] },
                          { "unknown", npc->faceData->parts[RE::TESNPC::FaceData::Parts::kUnknown] },
                          { "eyes", npc->faceData->parts[RE::TESNPC::FaceData::Parts::kEyes] },
                          { "mouth", npc->faceData->parts[RE::TESNPC::FaceData::Parts::kMouth] }
                      } }
                };
            }

            nlohmann::json tintsJson = nlohmann::json::array();
            if (npc->tintLayers) {
                for (const auto* layer : *npc->tintLayers) {
                    if (!layer) {
                        continue;
                    }
                    tintsJson.push_back(nlohmann::json{
                        { "index", layer->tintIndex },
                        { "color", { layer->tintColor.red, layer->tintColor.green, layer->tintColor.blue } },
                        { "interpolationValue", layer->interpolationValue },
                        { "preset", layer->preset }
                    });
                }
            }

            RE::BGSTextureSet* faceDetails =
                npc->headRelatedData ? npc->headRelatedData->faceDetails : nullptr;
            RE::BGSColorForm* hairColForm =
                npc->headRelatedData ? npc->headRelatedData->hairColor : nullptr;

            const std::string faceGeomPath = BuildNpcFaceGeomVfsPath(npc->faceNPC ? npc->faceNPC : npc);
            if (!faceGeomPath.empty()) {
                ioMeshQueue.push_back(QueuedAssetProbe{
                    .role = std::format("{}_static_facegeom", label),
                    .rawPath = faceGeomPath,
                    .emitThumbnail = false
                });
            }

            nlohmann::json result{
                { "npc", DescribeFormOrigin(npc) },
                { "displayName", SafeStr(npc->GetName()) },
                { "sex", (sex == Foundation::Sex::Female) ? "Female" : "Male" },
                { "weight", npc->weight },
                { "height", npc->height },
                { "race", DescribeFormOrigin(race) },
                { "morphRace", DescribeFormOrigin(race ? race->morphRace : nullptr) },
                { "unwrappedSynthesisRace", DescribeFormOrigin(unwrappedRace) },
                { "headPartPools", std::move(poolInfo) },
                { "keywords", std::move(kwList) },
                { "bodyTintColor",
                  { npc->bodyTintColor.red,
                    npc->bodyTintColor.green,
                    npc->bodyTintColor.blue,
                    npc->bodyTintColor.alpha } },
                { "hairColorForm",
                  hairColForm
                      ? nlohmann::json{
                            { "form", DescribeFormOrigin(hairColForm) },
                            { "rgb",
                              { hairColForm->color.red,
                                hairColForm->color.green,
                                hairColForm->color.blue } }
                        }
                      : nlohmann::json(nullptr) },
                { "faceDetailsTextureSet",
                  DescribeTextureSet(faceDetails, std::format("{}_faceDetails", label), ioTexQueue) },
                { "skin", std::move(skinJson) },
                { "headParts", std::move(headPartsJson) },
                { "faceData", std::move(morphsJson) },
                { "tintLayers", std::move(tintsJson) },
                { "expectedStaticFaceGeomVfs", faceGeomPath }
            };

            if (actor) {
                nlohmann::json skeeSkinOvr = nlohmann::json::object();
                for (const auto [slotBit, sName] : {
                         std::pair{ std::uint32_t{ 1u << 2u }, "Slot32_Body" },
                         std::pair{ std::uint32_t{ 1u << 3u }, "Slot33_Hands" },
                         std::pair{ std::uint32_t{ 1u << 7u }, "Slot37_Feet" } }) {
                    auto ovr = SkeeOverlayClient::GetSingleton().InspectSkinTextureOverrides(actor, slotBit);
                    if (!ovr.empty()) {
                        nlohmann::json arr = nlohmann::json::array();
                        for (const auto& [idx, v] : ovr) {
                            arr.push_back(nlohmann::json{ { "slot", idx }, { "value", v } });
                        }
                        skeeSkinOvr[sName] = std::move(arr);
                    }
                }

                nlohmann::json wornArmors = nlohmann::json::array();
                for (std::uint32_t bit = 0u; bit < 32u; ++bit) {
                    const auto slot = static_cast<RE::BGSBipedObjectForm::BipedObjectSlot>(1u << bit);
                    if (auto* worn = actor->GetWornArmor(slot)) {
                        wornArmors.push_back(nlohmann::json{
                            { "bipedSlotNumber", 30u + bit },
                            { "armor", DescribeFormOrigin(worn) },
                            { "name", SafeStr(worn->GetName()) }
                        });
                    }
                }

                const auto* cell = actor->GetParentCell();
                result["actor"] = nlohmann::json{
                    { "formId", FormatHexId(actor->GetFormID()) },
                    { "isPlayer", actor->IsPlayerRef() },
                    { "is3DLoaded", actor->Is3DLoaded() },
                    { "isDisabled", actor->IsDisabled() },
                    { "isDeleted", actor->IsDeleted() },
                    { "refScale", actor->GetScale() },
                    { "parentCell", DescribeFormOrigin(cell) },
                    { "wornBipedArmors", std::move(wornArmors) },
                    { "skeeSkinTextureOverrides", std::move(skeeSkinOvr) }
                };
            }

            return result;
        }

        [[nodiscard]] nlohmann::json InspectLiveActor3dScene(
            RE::Actor* actor,
            std::vector<QueuedAssetProbe>& ioTexQueue) {
            if (!actor || !actor->Is3DLoaded()) {
                return nlohmann::json{ { "loaded3D", false } };
            }

            auto dumpSubtree = [&](RE::NiAVObject* rootNode, std::string_view treeTag) {
                nlohmann::json geoms = nlohmann::json::array();
                if (!rootNode) {
                    return geoms;
                }
                std::unordered_set<const RE::BSGeometry*> visited;
                std::uint32_t geomOrdinal = 0u;

                RE::BSVisit::TraverseScenegraphGeometries(
                    rootNode,
                    [&](RE::BSGeometry* geom) -> RE::BSVisit::BSVisitControl {
                        if (!geom || !visited.insert(geom).second) {
                            return RE::BSVisit::BSVisitControl::kContinue;
                        }
                        auto* triShape = geom->AsTriShape();
                        const std::uint16_t vCount =
                            triShape ? triShape->GetTrishapeRuntimeData().vertexCount : 0u;
                        const std::uint16_t tCount =
                            triShape ? triShape->GetTrishapeRuntimeData().triangleCount : 0u;
                        const bool isDyn = (geom->AsDynamicTriShape() != nullptr);

                        const auto& gd = geom->GetGeometryRuntimeData();
                        nlohmann::json shaderJson = nullptr;

                        if (auto* sp = gd.shaderProperty.get()) {
                            if (auto* lsp = netimmerse_cast<RE::BSLightingShaderProperty*>(sp)) {
                                const std::uint64_t rawFlags = lsp->flags.underlying();
                                const auto sf1 = static_cast<std::uint32_t>(rawFlags & 0xFFFFFFFFull);
                                const auto sf2 = static_cast<std::uint32_t>((rawFlags >> 32u) & 0xFFFFFFFFull);

                                shaderJson = nlohmann::json{
                                    { "shaderFlags1", std::format("0x{:08X}", sf1) },
                                    { "shaderFlags2", std::format("0x{:08X}", sf2) },
                                    { "emissiveMult", lsp->emissiveMult },
                                    { "emissiveColor",
                                      lsp->emissiveColor
                                          ? nlohmann::json{
                                                lsp->emissiveColor->red,
                                                lsp->emissiveColor->green,
                                                lsp->emissiveColor->blue
                                            }
                                          : nlohmann::json(nullptr) }
                                };

                                if (const auto* mat = lsp->material) {
                                    const auto feat = mat->GetFeature();
                                    shaderJson["feature"] = ShaderFeatureName(feat);
                                    shaderJson["featureId"] = static_cast<std::uint32_t>(feat);

                                    if (mat->GetType() == RE::BSShaderMaterial::Type::kLighting) {
                                        const auto* bm =
                                            static_cast<const RE::BSLightingShaderMaterialBase*>(mat);
                                        auto texName = [](const RE::NiPointer<RE::NiSourceTexture>& t) {
                                            return (t && t->name.c_str()) ? std::string(t->name.c_str())
                                                                          : std::string{};
                                        };
                                        const std::string diffTex = texName(bm->diffuseTexture);
                                        const std::string normTex = texName(bm->normalTexture);
                                        const std::string softTex = texName(bm->rimSoftLightingTexture);
                                        const std::string specTex = texName(bm->specularBackLightingTexture);

                                        const bool isKeySkinOrEye =
                                            (feat == RE::BSShaderMaterial::Feature::kFaceGen ||
                                             feat == RE::BSShaderMaterial::Feature::kFaceGenRGBTint ||
                                             feat == RE::BSShaderMaterial::Feature::kEye);
                                        const std::string pfx =
                                            std::format("live_{}_{}", treeTag, geomOrdinal);
                                        if (!diffTex.empty()) {
                                            ioTexQueue.push_back({ pfx + "_diff", diffTex, isKeySkinOrEye });
                                        }
                                        if (!normTex.empty()) {
                                            ioTexQueue.push_back({ pfx + "_norm", normTex, false });
                                        }
                                        if (!softTex.empty()) {
                                            ioTexQueue.push_back({ pfx + "_subsurf", softTex, isKeySkinOrEye });
                                        }
                                        if (!specTex.empty()) {
                                            ioTexQueue.push_back({ pfx + "_spec", specTex, isKeySkinOrEye });
                                        }

                                        shaderJson["materialAlpha"] = bm->materialAlpha;
                                        shaderJson["specularPower_glossiness"] = bm->specularPower;
                                        shaderJson["specularColorScale_strength"] = bm->specularColorScale;
                                        shaderJson["specularColor"] = {
                                            bm->specularColor.red,
                                            bm->specularColor.green,
                                            bm->specularColor.blue
                                        };
                                        shaderJson["subSurfaceLightRolloff_softLighting"] =
                                            bm->subSurfaceLightRolloff;
                                        shaderJson["rimLightPower"] = bm->rimLightPower;
                                        shaderJson["boundTextures"] = nlohmann::json{
                                            { "diffuse", diffTex },
                                            { "normal", normTex },
                                            { "rimSoftLighting", softTex },
                                            { "specularBackLighting", specTex }
                                        };

                                        if (feat == RE::BSShaderMaterial::Feature::kFaceGen) {
                                            const auto* fg =
                                                static_cast<const RE::BSLightingShaderMaterialFacegen*>(bm);
                                            const std::string tintT = texName(fg->tintTexture);
                                            const std::string detT = texName(fg->detailTexture);
                                            const std::string subT = texName(fg->subsurfaceTexture);
                                            shaderJson["facegenTextures"] = nlohmann::json{
                                                { "tint", tintT },
                                                { "detail", detT },
                                                { "subsurface", subT }
                                            };
                                            if (!detT.empty()) {
                                                ioTexQueue.push_back({ pfx + "_detail", detT, true });
                                            }
                                            if (!subT.empty()) {
                                                ioTexQueue.push_back({ pfx + "_fg_subsurf", subT, true });
                                            }
                                        } else if (feat == RE::BSShaderMaterial::Feature::kFaceGenRGBTint) {
                                            const auto* st =
                                                static_cast<const RE::BSLightingShaderMaterialFacegenTint*>(bm);
                                            shaderJson["rgbTintColor"] = {
                                                st->tintColor.red,
                                                st->tintColor.green,
                                                st->tintColor.blue
                                            };
                                        } else if (feat == RE::BSShaderMaterial::Feature::kHairTint) {
                                            const auto* ht =
                                                static_cast<const RE::BSLightingShaderMaterialHairTint*>(bm);
                                            shaderJson["hairTintColor"] = {
                                                ht->tintColor.red,
                                                ht->tintColor.green,
                                                ht->tintColor.blue
                                            };
                                        } else if (feat == RE::BSShaderMaterial::Feature::kEye) {
                                            const auto* em =
                                                static_cast<const RE::BSLightingShaderMaterialEye*>(bm);
                                            shaderJson["eyeEnvTextures"] = nlohmann::json{
                                                { "env", texName(em->envTexture) },
                                                { "envMask", texName(em->envMaskTexture) }
                                            };
                                        }

                                        if (const auto ts = bm->GetTextureSet()) {
                                            nlohmann::json tsArr = nlohmann::json::array();
                                            for (std::uint32_t s = 0u; s < 8u; ++s) {
                                                const char* p = ts->GetTexturePath(
                                                    static_cast<RE::BSTextureSet::Texture>(s));
                                                tsArr.push_back(SafeStr(p));
                                            }
                                            shaderJson["materialTextureSetSlots"] = std::move(tsArr);
                                        }
                                    }
                                }
                            }
                        }

                        const auto* parentNode = geom->parent;
                        geoms.push_back(nlohmann::json{
                            { "ordinal", geomOrdinal },
                            { "name", SafeStr(geom->name.c_str()) },
                            { "parentName", parentNode ? SafeStr(parentNode->name.c_str()) : "" },
                            { "blockType", isDyn ? "BSDynamicTriShape" : "BSTriShape" },
                            { "vertexCount", vCount },
                            { "triangleCount", tCount },
                            { "localScale", geom->local.scale },
                            { "worldScale", geom->world.scale },
                            { "hasFgbmExtra",
                              geom->GetExtraData<RE::BSFaceGenBaseMorphExtraData>("FGBM") != nullptr },
                            { "hasMorwhaEarMorph", geom->HasExtraData("MORWHA_EarMorph") },
                            { "shader", std::move(shaderJson) }
                        });
                        ++geomOrdinal;
                        return RE::BSVisit::BSVisitControl::kContinue;
                    });
                return geoms;
            };

            RE::BSFaceGenNiNode* faceNode = actor->GetFaceNodeSkinned();
            RE::NiAVObject* root3D = actor->Get3D(false);
            return nlohmann::json{
                { "loaded3D", true },
                { "hasFaceNodeSkinned", faceNode != nullptr },
                { "faceNodeGeometries", dumpSubtree(faceNode, "face") },
                { "root3DGeometries", dumpSubtree(root3D, "body") }
            };
        }

        [[nodiscard]] nlohmann::json InspectQueuedVfsAssetsAndThumbnails(
            const std::vector<QueuedAssetProbe>& texQueue,
            const std::vector<QueuedAssetProbe>& meshQueue,
            State::DiagnosticZipArchive& ioZip) {
            nlohmann::json texturesJson = nlohmann::json::array();
            std::unordered_set<std::string> seenTexVfs;
            std::size_t emittedThumbs = 0u;

            for (const auto& item : texQueue) {
                if (texturesJson.size() >= kMaxInspectedTextures) {
                    break;
                }
                const std::string vfsPath = BuildTextureVfsPath(item.rawPath);
                if (vfsPath.empty()) {
                    continue;
                }
                const std::string key = ToLowerAscii(vfsPath);
                if (!seenTexVfs.insert(key).second) {
                    continue;
                }

                std::array<std::uint8_t, 148u> hdr{};
                std::size_t hdrRead = 0u, totalBytes = 0u;
                const bool exists =
                    ReadVfsFileHeaderAndTotalSize(vfsPath, hdr, hdrRead, totalBytes) && hdrRead >= 128u;

                nlohmann::json entry{
                    { "firstReferencedBy", item.role },
                    { "vfsPath", vfsPath },
                    { "existsInVfs", exists },
                    { "totalFileBytes", totalBytes }
                };

                if (exists) {
                    std::uint32_t height = 0u, width = 0u, mips = 0u, pfFlags = 0u, rgbBits = 0u, dxgi = 0u;
                    std::memcpy(&height, hdr.data() + 12u, 4u);
                    std::memcpy(&width, hdr.data() + 16u, 4u);
                    std::memcpy(&mips, hdr.data() + 28u, 4u);
                    std::memcpy(&pfFlags, hdr.data() + 80u, 4u);
                    std::memcpy(&rgbBits, hdr.data() + 88u, 4u);
                    char fourCcBuf[5]{};
                    std::memcpy(fourCcBuf, hdr.data() + 84u, 4u);
                    for (int c = 0; c < 4; ++c) {
                        const auto uc = static_cast<unsigned char>(fourCcBuf[c]);
                        if (uc < 0x20u || uc > 0x7Eu) {
                            fourCcBuf[c] = '.';
                        }
                    }
                    if (std::memcmp(hdr.data() + 84u, "DX10", 4u) == 0 && hdrRead >= 132u) {
                        std::memcpy(&dxgi, hdr.data() + 128u, 4u);
                    }

                    entry["header"] = nlohmann::json{
                        { "width", width },
                        { "height", height },
                        { "mipCount", mips },
                        { "pfFlags", std::format("0x{:08X}", pfFlags) },
                        { "fourCC", std::string(fourCcBuf) },
                        { "dxgiFormat", dxgi },
                        { "rgbBitCount", rgbBits },
                        { "headerCrc32",
                          std::format("0x{:08X}", State::ComputeCrc32(std::span(hdr.data(), hdrRead))) }
                    };

                    Core::DdsSlicePlan plan64{};
                    const bool decodeSupported = Core::PlanDdsSlice(
                        std::span<const std::uint8_t>(hdr.data(), hdrRead),
                        totalBytes,
                        plan64,
                        64u);
                    entry["decoderSupported"] = decodeSupported;

                    if (decodeSupported) {
                        std::vector<std::uint8_t> sliceBytes;
                        std::array<std::uint8_t, 64u * 64u * 4u> px64{};
                        if (ReadVfsFileSlice(vfsPath, plan64.fileByteOffset, plan64.sliceByteSize, sliceBytes) &&
                            Core::DecodeDdsSlice64x64(plan64, sliceBytes, px64)) {
                            std::array<std::uint32_t, 4> minCh{ 255u, 255u, 255u, 255u };
                            std::array<std::uint32_t, 4> maxCh{ 0u, 0u, 0u, 0u };
                            std::array<std::uint64_t, 4> sumCh{ 0u, 0u, 0u, 0u };
                            for (std::size_t p = 0u; p < 64u * 64u; ++p) {
                                for (std::size_t c = 0u; c < 4u; ++c) {
                                    const std::uint8_t v = px64[p * 4u + c];
                                    minCh[c] = (std::min<std::uint32_t>)(minCh[c], v);
                                    maxCh[c] = (std::max<std::uint32_t>)(maxCh[c], v);
                                    sumCh[c] += v;
                                }
                            }
                            entry["pixelStats64x64"] = nlohmann::json{
                                { "minRgba", minCh },
                                { "maxRgba", maxCh },
                                { "meanRgba",
                                  { static_cast<double>(sumCh[0]) / 4096.0,
                                    static_cast<double>(sumCh[1]) / 4096.0,
                                    static_cast<double>(sumCh[2]) / 4096.0,
                                    static_cast<double>(sumCh[3]) / 4096.0 } }
                            };

                            if (item.emitThumbnail && emittedThumbs < kMaxThumbnailBmps) {
                                std::vector<std::uint8_t> bmpBytes;
                                if (State::EncodeRgbaToBmp24(px64, 64u, 64u, bmpBytes)) {
                                    const std::string stem = SanitizeSlug(
                                        std::filesystem::path(vfsPath).stem().string(), 24u);
                                    const std::string bmpRel = std::format(
                                        "thumbnails/{:02}_{}_{}.bmp",
                                        emittedThumbs,
                                        SanitizeSlug(item.role, 24u),
                                        stem);
                                    ioZip.AddFileBytes(bmpRel, bmpBytes);
                                    entry["thumbnailBmp"] = bmpRel;
                                    ++emittedThumbs;
                                }
                            }
                        }

                        // Also run 32x32 eye/complexion probes when path/role is eye or detail
                        Core::DdsSlicePlan plan32{};
                        std::vector<std::uint8_t> slice32;
                        std::array<std::uint8_t, 32u * 32u * 4u> px32{};
                        if (Core::PlanDdsSlice(
                                std::span<const std::uint8_t>(hdr.data(), hdrRead),
                                totalBytes,
                                plan32,
                                32u) &&
                            ReadVfsFileSlice(vfsPath, plan32.fileByteOffset, plan32.sliceByteSize, slice32) &&
                            Core::DecodeDdsSlice32x32(plan32, slice32, px32)) {
                            const auto compSig = Core::AnalyzeComplexionMipmap(px32);
                            entry["isNonBaselineDetail"] = Core::IsComplexionNonBaseline(compSig);
                        }
                    }
                }
                texturesJson.push_back(std::move(entry));
            }

            nlohmann::json meshesJson = nlohmann::json::array();
            std::unordered_set<std::string> seenMeshVfs;
            for (const auto& item : meshQueue) {
                if (meshesJson.size() >= kMaxInspectedMeshes) {
                    break;
                }
                const std::string vfsPath = BuildMeshVfsPath(item.rawPath);
                if (vfsPath.empty()) {
                    continue;
                }
                const std::string low = ToLowerAscii(vfsPath);
                if (!seenMeshVfs.insert(low).second) {
                    continue;
                }

                std::vector<std::uint8_t> bytes;
                const bool exists = ReadVfsFileBytes(vfsPath, bytes, 16ull * 1024ull * 1024ull);
                nlohmann::json entry{
                    { "firstReferencedBy", item.role },
                    { "vfsPath", vfsPath },
                    { "existsInVfs", exists },
                    { "byteSize", bytes.size() }
                };

                if (exists && !bytes.empty()) {
                    if (low.ends_with(".tri")) {
                        if (const auto tri = Core::LoadTriAsset(bytes)) {
                            entry["triVertexCount"] = Core::GetTriAssetVertexCount(tri.get());
                            entry["triTriangleIndexCount"] =
                                Core::GetTriAssetTriangles(tri.get()).size();
                        } else {
                            entry["triParseOk"] = false;
                        }
                    } else if (low.ends_with(".nif")) {
                        Core::ExtractedHeadScene nifScene{};
                        if (Core::ParseHeadSceneNif(bytes, nifScene)) {
                            nlohmann::json blocks = nlohmann::json::array();
                            for (const auto& b : nifScene.allMeshes) {
                                blocks.push_back(nlohmann::json{
                                    { "name", b.name },
                                    { "blockType", b.blockType },
                                    { "vertexCount", b.vertexCount },
                                    { "triangleCount", b.triangles.size() / 3u },
                                    { "shaderType", b.shaderType },
                                    { "diffuseTexture", b.diffuseTexturePath }
                                });
                            }
                            entry["nifBlocks"] = std::move(blocks);
                        }
                        const auto eyeProbe = Core::ProbeEyeMeshNif(bytes);
                        entry["isBilateralEyeNif"] =
                            Core::IsEyeMeshProbeValidAndBilateral(eyeProbe);
                        entry["uvFamilyIfFemaleBody"] = Core::BodyUvFamilyStem(
                            Core::ClassifyBodyMeshUvFamily(bytes, Foundation::Sex::Female));
                        entry["uvFamilyIfMaleBody"] = Core::BodyUvFamilyStem(
                            Core::ClassifyBodyMeshUvFamily(bytes, Foundation::Sex::Male));
                    }
                }
                meshesJson.push_back(std::move(entry));
            }

            return nlohmann::json{
                { "inspectedTextures", std::move(texturesJson) },
                { "inspectedMeshesAndTris", std::move(meshesJson) }
            };
        }

        void BundleDiskFileIfPresent(
            State::DiagnosticZipArchive& ioZip,
            const std::filesystem::path& diskPath,
            std::string archivePath,
            std::size_t maxBytes) {
            std::error_code ec;
            if (!std::filesystem::is_regular_file(diskPath, ec)) {
                return;
            }
            std::ifstream in(diskPath, std::ios::binary | std::ios::ate);
            if (!in.is_open()) {
                return;
            }
            const auto fileSize = static_cast<std::size_t>(in.tellg());
            if (fileSize == 0u) {
                ioZip.AddTextFile(std::move(archivePath), "");
                return;
            }
            if (fileSize <= maxBytes) {
                std::vector<std::uint8_t> buf(fileSize);
                in.seekg(0, std::ios::beg);
                in.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(fileSize));
                ioZip.AddFileBytes(std::move(archivePath), buf);
                return;
            }

            // Keep leading 256 KB boot header + trailing (maxBytes - 256 KB) recent lines
            const std::size_t headSize = (std::min<std::size_t>)(256ull * 1024ull, maxBytes / 4u);
            const std::size_t tailSize = maxBytes - headSize;
            std::vector<std::uint8_t> combined;
            combined.resize(headSize);
            in.seekg(0, std::ios::beg);
            in.read(reinterpret_cast<char*>(combined.data()), static_cast<std::streamsize>(headSize));

            constexpr std::string_view kMidBanner = "\n... [MORWHA diag: middle log section truncated] ...\n";
            combined.insert(
                combined.end(),
                reinterpret_cast<const std::uint8_t*>(kMidBanner.data()),
                reinterpret_cast<const std::uint8_t*>(kMidBanner.data() + kMidBanner.size()));

            const std::size_t tailOff = combined.size();
            combined.resize(tailOff + tailSize);
            in.seekg(static_cast<std::streamoff>(fileSize - tailSize), std::ios::beg);
            in.read(
                reinterpret_cast<char*>(combined.data() + tailOff),
                static_cast<std::streamsize>(tailSize));
            ioZip.AddFileBytes(std::move(archivePath), combined);
        }

    }  // namespace

    DiagnosticBundleResult BuildDiagnosticBundle(const DiagnosticBundleInput& input) {
        DiagnosticBundleResult res{};

        // Timestamp for filename & summary
        const auto nowSys = std::chrono::system_clock::now();
        const std::time_t nowTt = std::chrono::system_clock::to_time_t(nowSys);
        std::tm localTm{};
        localtime_s(&localTm, &nowTt);
        const std::string tsTag = std::format(
            "{:04d}{:02d}{:02d}_{:02d}{:02d}{:02d}",
            localTm.tm_year + 1900,
            localTm.tm_mon + 1,
            localTm.tm_mday,
            localTm.tm_hour,
            localTm.tm_min,
            localTm.tm_sec);

        RE::Actor* actor = input.targetActor;
        std::string targetSlug = "global";
        if (actor) {
            const char* nm = actor->GetName();
            targetSlug = std::format(
                "{}_{:08X}",
                SanitizeSlug((nm && nm[0] != '\0') ? nm : "Actor", 20u),
                actor->GetFormID());
            res.targetSummary = std::format(
                "'{}' [{:08X}] ({})",
                (nm && nm[0] != '\0') ? nm : "Actor",
                actor->GetFormID(),
                input.targetResolutionSource);
        } else {
            res.targetSummary = "no actor selected (environment & log bundle)";
        }

        MORWHA_LOG_INFO(
            "DiagnosticBundleBuilder: Capturing diagnostic bundle (target={})",
            res.targetSummary);
        if (const auto& logger = GetLogger()) {
            logger->flush();
        }

        State::DiagnosticZipArchive zip;
        std::vector<QueuedAssetProbe> texQueue;
        std::vector<QueuedAssetProbe> meshQueue;

        // 1. Active links & summary.json
        const auto activeLinks = Serialization::GetActiveLinksSnapshot();
        nlohmann::json linksJson = nlohmann::json::array();
        std::optional<State::SpawnedMortalLink> targetLink;
        for (const auto& lk : activeLinks) {
            if (actor && lk.actorRefId.raw() == actor->GetFormID()) {
                targetLink = lk;
            }
            linksJson.push_back(nlohmann::json{
                { "slotKey", lk.slotKey },
                { "actorRefId", FormatHexId(lk.actorRefId.raw()) },
                { "dpfLocalId", std::format("{:06X}", lk.dpfLocalId) },
                { "donorMode", static_cast<unsigned>(lk.donorMode) },
                { "seed", std::format("0x{:016X}", lk.seed.raw()) },
                { "donorARefId", FormatHexId(lk.donorARefId.raw()) },
                { "donorBRefId", FormatHexId(lk.donorBRefId.raw()) }
            });
        }

        const auto& dpf = DpfClient::GetSingleton();
        const auto chimeraMods = InspectInstalledChimeraMods();
        const auto tierAvail = QueryActiveChimeraTierAvailability(input.activeSettings);

        nlohmann::json summaryJson{
            { "timestampLocal", tsTag },
            { "morwhaVersion", input.modVersion },
            { "coordinatorReady", input.coordinatorReady },
            { "dpfAvailable", dpf.IsDpfAvailable() },
            { "faceGenFixesLoaded", dpf.IsFaceGenFixesLoaded() },
            { "hdtSmpLoaded", dpf.IsSmpLoaded() },
            { "skeeOverrideAvailable", SkeeOverlayClient::GetSingleton().IsAvailable() },
            { "highPolyHeadIndexed", HeadPartPoolIndexer::GetSingleton().IsHighPolyHeadAvailable() },
            { "registeredSpecCount", input.registeredSpecCount },
            { "uncommittedSpecCount", input.uncommittedSpecCount },
            { "targetResolutionSource", input.targetResolutionSource },
            { "targetSummary", res.targetSummary },
            { "settings",
              nlohmann::json{
                  { "allowSupernaturalEyes", input.activeSettings.allowSupernaturalEyes },
                  { "allowScars", input.activeSettings.allowScars },
                  { "beastChimeraTier", static_cast<unsigned>(input.activeSettings.beastChimeraTier) },
                  { "enableGeneratedHybridSkins", input.activeSettings.enableGeneratedHybridSkins },
                  { "generatedSkinResolution",
                    State::GeneratedSkinResolutionToString(input.activeSettings.generatedSkinResolution) }
              } },
            { "chimeraAvailability",
              nlohmann::json{
                  { "wearableBeastPartsLoaded", chimeraMods.wearableBeastPartsLoaded },
                  { "tier2KhajiitReady", tierAvail.tier2KhajiitAccessoriesReady },
                  { "tier2ArgonianReady", tierAvail.tier2ArgonianAccessoriesReady },
                  { "tier2KhajiitOnArgonianReady", tierAvail.tier2KhajiitOnArgonianReady },
                  { "tier2ArgonianOnKhajiitReady", tierAvail.tier2ArgonianOnKhajiitReady }
              } },
            { "activeSaveLinks", std::move(linksJson) },
            { "loadedModules", CollectLoadedModulesJson() }
        };
        zip.AddTextFile("summary.json", summaryJson.dump(2));
        zip.AddTextFile("environment/load_order.json", CollectLoadOrderJson().dump(2));
        zip.AddTextFile(
            "environment/hybrid_skin_service.json",
            HybridSkinService::GetSingleton().DumpDiagnosticSummaryJson());

        // 2. Target Actor, Matched Spec, Donor Records & Live 3D Scene
        if (actor) {
            nlohmann::json actorBundle = nlohmann::json::object();
            actorBundle["target"] =
                InspectNpcAndActorRecords(actor, actor->GetActorBase(), "target", texQueue, meshQueue);

            if (input.matchedSpec) {
                const State::DynamicNpcSpec& spec = *input.matchedSpec;
                zip.AddTextFile(
                    "actor_registry_spec.json",
                    State::SerializeRegistryJson(std::span<const State::DynamicNpcSpec>(&spec, 1u)));
            }

            auto inspectDonorSlot = [&](std::string_view slotTag,
                                        Foundation::FormRawId linkDonorId,
                                        const State::PluginFormRef* specDonorRef) -> nlohmann::json {
                RE::Actor* donorActor = nullptr;
                RE::TESNPC* donorNpc = nullptr;
                if (!linkDonorId.IsNull()) {
                    if (auto* form = RE::TESForm::LookupByID(linkDonorId.raw())) {
                        donorActor = form->As<RE::Actor>();
                        donorNpc = donorActor ? donorActor->GetActorBase() : form->As<RE::TESNPC>();
                    }
                }
                if (!donorNpc && specDonorRef && !specDonorRef->IsEmpty()) {
                    if (auto* form = ResolvePluginForm<RE::TESForm>(*specDonorRef)) {
                        donorActor = form->As<RE::Actor>();
                        donorNpc = donorActor ? donorActor->GetActorBase() : form->As<RE::TESNPC>();
                    }
                }
                if (!donorActor && !donorNpc) {
                    return nullptr;
                }
                return InspectNpcAndActorRecords(donorActor, donorNpc, slotTag, texQueue, meshQueue);
            };

            const Foundation::FormRawId linkA = targetLink ? targetLink->donorARefId : Foundation::FormRawId{};
            const Foundation::FormRawId linkB = targetLink ? targetLink->donorBRefId : Foundation::FormRawId{};
            const State::PluginFormRef* refA =
                input.matchedSpec ? &input.matchedSpec->donorARef : nullptr;
            const State::PluginFormRef* refB =
                input.matchedSpec ? &input.matchedSpec->donorBRef : nullptr;

            actorBundle["donorA"] = inspectDonorSlot("donorA", linkA, refA);
            actorBundle["donorB"] = inspectDonorSlot("donorB", linkB, refB);
            zip.AddTextFile("actor_records.json", actorBundle.dump(2));

            const nlohmann::json live3d = InspectLiveActor3dScene(actor, texQueue);
            zip.AddTextFile("actor_3d_scene.json", live3d.dump(2));
        }

        // 3. VFS Asset Headers + 64x64 BMP Previews
        if (!texQueue.empty() || !meshQueue.empty()) {
            const nlohmann::json assetManifest =
                InspectQueuedVfsAssetsAndThumbnails(texQueue, meshQueue, zip);
            zip.AddTextFile("assets/vfs_manifest.json", assetManifest.dump(2));
        }

        // 4. Logs & Shipped/User Configs
        if (auto logDir = SKSE::log::log_directory()) {
            std::filesystem::path logPath = *logDir;
            if (!std::filesystem::is_directory(logPath)) {
                logPath = logPath.parent_path();
            }
            BundleDiskFileIfPresent(
                zip, logPath / "MORWHA.log", "logs/MORWHA.log", kMaxBundledLogBytes);
        }
        BundleDiskFileIfPresent(
            zip,
            std::filesystem::path(State::kDefaultMorwhaIniPath),
            "configs/MORWHA.ini",
            256ull * 1024ull);
        BundleDiskFileIfPresent(
            zip,
            std::filesystem::path("Data/SKSE/Plugins/MORWHA/GeneratedSkinCache.json"),
            "configs/GeneratedSkinCache.json",
            2ull * 1024ull * 1024ull);

        const std::string zipFilename = std::format("morwha_diag_{}_{}.zip", tsTag, targetSlug);
        const std::filesystem::path outZipPath =
            std::filesystem::path(kDiagOutputDir) / zipFilename;

        std::size_t zipBytes = 0u;
        std::string writeErr;
        if (!zip.WriteZipFileAtomic(outZipPath, &zipBytes, &writeErr)) {
            res.ok = false;
            res.error = writeErr.empty() ? "Failed writing diagnostic .zip file" : writeErr;
            MORWHA_LOG_ERROR(
                "DiagnosticBundleBuilder: Failed writing '{}': {}",
                outZipPath.string(),
                res.error);
            return res;
        }

        res.ok = true;
        res.zipRelativePath = std::format("{}/{}", kDiagOutputDir, zipFilename);
        res.archivedFileCount = zip.FileCount();
        res.zipByteSize = zipBytes;

        MORWHA_LOG_INFO(
            "DiagnosticBundleBuilder: Wrote '{}' ({} files, {} bytes)",
            res.zipRelativePath,
            res.archivedFileCount,
            res.zipByteSize);
        return res;
    }

}  // namespace MORWHA::Engine
