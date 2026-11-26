#include "morwha/engine/ChimeraMeshTinter.h"

#include "morwha/core/MorwhaCore.h"
#include "morwha/engine/ChimeraAccessoryCatalog.h"
#include "morwha/engine/EngineLogging.h"
#include "morwha/engine/VfsAssetReader.h"
#include "morwha/state/RegistryTypes.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace MORWHA::Engine {

    namespace {

        constexpr std::size_t kDdsHeaderReadBytes = 148u;

        struct CachedDiffuseMip {
            bool hasDecoded{ false };
            std::array<std::uint8_t, 32u * 32u * 4u> pixels{};
            Foundation::RgbColor wholeMean{ 128u, 128u, 128u };
        };

        std::mutex g_diffuseMipCacheMutex;
        std::unordered_map<std::string, std::shared_ptr<const CachedDiffuseMip>> g_diffuseMipCache;

        [[nodiscard]] std::string ToLowerAscii(std::string_view sv) {
            std::string out;
            out.reserve(sv.size());
            for (const unsigned char ch : sv) {
                out.push_back(static_cast<char>(std::tolower(ch)));
            }
            return out;
        }

        [[nodiscard]] std::string ResolveMaterialDiffuseVfsPath(
            const RE::BSLightingShaderMaterialBase* mat) {
            if (!mat) {
                return {};
            }
            if (mat->diffuseTexture && mat->diffuseTexture->name.c_str() != nullptr &&
                mat->diffuseTexture->name.c_str()[0] != '\0') {
                return BuildTextureVfsPath(mat->diffuseTexture->name.c_str());
            }
            if (mat->textureSet) {
                if (const char* txPath =
                        mat->textureSet->GetTexturePath(RE::BSTextureSet::Texture::kDiffuse);
                    txPath != nullptr && txPath[0] != '\0') {
                    return BuildTextureVfsPath(txPath);
                }
            }
            return {};
        }

        [[nodiscard]] std::shared_ptr<const CachedDiffuseMip> GetOrDecodeDiffuseMip(
            std::string_view vfsPath) {
            if (vfsPath.empty()) {
                return nullptr;
            }
            const std::string cacheKey = ToLowerAscii(vfsPath);
            {
                std::lock_guard lock(g_diffuseMipCacheMutex);
                if (const auto it = g_diffuseMipCache.find(cacheKey);
                    it != g_diffuseMipCache.end()) {
                    return it->second;
                }
            }

            auto entry = std::make_shared<CachedDiffuseMip>();
            std::array<std::uint8_t, kDdsHeaderReadBytes> hdrBuf{};
            std::size_t hdrBytesRead = 0u;
            std::size_t totalFileBytes = 0u;
            if (ReadVfsFileHeaderAndTotalSize(vfsPath, hdrBuf, hdrBytesRead, totalFileBytes) &&
                hdrBytesRead >= 128u) {
                Core::DdsSlicePlan plan{};
                if (Core::PlanDdsSlice(
                        std::span<const std::uint8_t>(hdrBuf.data(), hdrBytesRead),
                        totalFileBytes,
                        plan,
                        /*targetDim=*/32u)) {
                    std::vector<std::uint8_t> mipBytes;
                    if (ReadVfsFileSlice(vfsPath, plan.fileByteOffset, plan.sliceByteSize, mipBytes) &&
                        Core::DecodeDdsSlice32x32(plan, mipBytes, entry->pixels)) {
                        entry->hasDecoded = true;
                        std::uint64_t sumR = 0u;
                        std::uint64_t sumG = 0u;
                        std::uint64_t sumB = 0u;
                        std::uint64_t count = 0u;
                        for (std::size_t i = 0u; i + 3u < entry->pixels.size(); i += 4u) {
                            const std::uint8_t r = entry->pixels[i + 0u];
                            const std::uint8_t g = entry->pixels[i + 1u];
                            const std::uint8_t b = entry->pixels[i + 2u];
                            const std::uint8_t a = entry->pixels[i + 3u];
                            if (a >= 32u && (static_cast<unsigned>(r) + g + b) > 12u) {
                                sumR += r;
                                sumG += g;
                                sumB += b;
                                ++count;
                            }
                        }
                        if (count > 0u) {
                            entry->wholeMean = Foundation::RgbColor{
                                static_cast<std::uint8_t>(sumR / count),
                                static_cast<std::uint8_t>(sumG / count),
                                static_cast<std::uint8_t>(sumB / count)
                            };
                        }
                    }
                }
            }

            {
                std::lock_guard lock(g_diffuseMipCacheMutex);
                const auto [it, inserted] = g_diffuseMipCache.try_emplace(cacheKey, entry);
                return it->second;
            }
        }

        void ExtractGeometryUvs(RE::BSGeometry* geom, std::vector<float>& outUvs) {
            outUvs.clear();
            auto* triShape = geom ? geom->AsTriShape() : nullptr;
            if (!triShape) {
                return;
            }
            const std::uint16_t numVertices = triShape->GetTrishapeRuntimeData().vertexCount;
            if (numVertices == 0u) {
                return;
            }

            auto tryDecodeUvs = [&](const std::uint8_t* rawData,
                                    std::uint16_t count,
                                    RE::BSGraphics::VertexDesc vDesc) {
                if (!rawData || count == 0u || !vDesc.HasFlag(RE::BSGraphics::Vertex::VF_UV)) {
                    return;
                }
                const std::uint32_t stride = vDesc.GetSize();
                const std::uint32_t uvOff =
                    vDesc.GetAttributeOffset(RE::BSGraphics::Vertex::VA_TEXCOORD0);
                if (stride < 8u || uvOff + 4u > stride) {
                    return;
                }
                outUvs.reserve(outUvs.size() + static_cast<std::size_t>(count) * 2u);
                for (std::uint16_t v = 0u; v < count; ++v) {
                    const std::uint8_t* ptr = rawData + static_cast<std::size_t>(v) * stride + uvOff;
                    std::uint16_t hu = 0u;
                    std::uint16_t hv = 0u;
                    std::memcpy(&hu, ptr + 0u, sizeof(std::uint16_t));
                    std::memcpy(&hv, ptr + 2u, sizeof(std::uint16_t));
                    const float u = Core::DecodeHalfFloat(hu);
                    const float vv = Core::DecodeHalfFloat(hv);
                    if (std::isfinite(u) && std::isfinite(vv)) {
                        outUvs.push_back(u);
                        outUvs.push_back(vv);
                    }
                }
            };

            const auto& geomData = geom->GetGeometryRuntimeData();
            if (geomData.rendererData && geomData.rendererData->rawVertexData) {
                tryDecodeUvs(
                    geomData.rendererData->rawVertexData,
                    numVertices,
                    geomData.rendererData->vertexDesc);
            }
            if (outUvs.empty()) {
                const auto* skinInst = geomData.skinInstance.get();
                const auto* skinPartition = skinInst ? skinInst->skinPartition.get() : nullptr;
                if (skinPartition && skinPartition->numPartitions > 0u) {
                    for (std::uint32_t p = 0u; p < skinPartition->numPartitions; ++p) {
                        const auto& part = skinPartition->partitions[p];
                        if (part.buffData && part.buffData->rawVertexData) {
                            tryDecodeUvs(
                                part.buffData->rawVertexData, part.vertices, part.vertexDesc);
                        }
                    }
                }
            }
        }

        [[nodiscard]] Foundation::RgbColor ResolveGeometryDiffuseRgb(
            RE::BSGeometry* geom,
            const RE::BSLightingShaderMaterialBase* mat) {
            constexpr Foundation::RgbColor kNeutralFallback{ 128u, 128u, 128u };
            const std::string vfsPath = ResolveMaterialDiffuseVfsPath(mat);
            const auto cached = GetOrDecodeDiffuseMip(vfsPath);
            if (!cached || !cached->hasDecoded) {
                return kNeutralFallback;
            }

            std::vector<float> uvs;
            ExtractGeometryUvs(geom, uvs);
            if (uvs.size() >= 8u) {
                std::uint64_t sumR = 0u;
                std::uint64_t sumG = 0u;
                std::uint64_t sumB = 0u;
                std::uint64_t count = 0u;
                for (std::size_t i = 0u; i + 1u < uvs.size(); i += 2u) {
                    const float uf = uvs[i + 0u] - std::floor(uvs[i + 0u]);
                    const float vf = uvs[i + 1u] - std::floor(uvs[i + 1u]);
                    const int px = std::clamp(static_cast<int>(uf * 32.0f), 0, 31);
                    const int py = std::clamp(static_cast<int>(vf * 32.0f), 0, 31);
                    const std::size_t idx =
                        (static_cast<std::size_t>(py) * 32u + static_cast<std::size_t>(px)) * 4u;
                    const std::uint8_t r = cached->pixels[idx + 0u];
                    const std::uint8_t g = cached->pixels[idx + 1u];
                    const std::uint8_t b = cached->pixels[idx + 2u];
                    const std::uint8_t a = cached->pixels[idx + 3u];
                    if (a >= 32u && (static_cast<unsigned>(r) + g + b) > 12u) {
                        sumR += r;
                        sumG += g;
                        sumB += b;
                        ++count;
                    }
                }
                if (count >= 4u) {
                    return Foundation::RgbColor{
                        static_cast<std::uint8_t>(sumR / count),
                        static_cast<std::uint8_t>(sumG / count),
                        static_cast<std::uint8_t>(sumB / count)
                    };
                }
            }

            return cached->wholeMean;
        }

        [[nodiscard]] RE::BSLightingShaderProperty* ResolveLightingShader(RE::BSGeometry* geom) noexcept {
            if (!geom) {
                return nullptr;
            }
            auto* shaderProp = geom->GetGeometryRuntimeData().shaderProperty.get();
            auto* lightingShader = shaderProp
                                       ? netimmerse_cast<RE::BSLightingShaderProperty*>(shaderProp)
                                       : nullptr;
            return (lightingShader && lightingShader->material) ? lightingShader : nullptr;
        }

        [[nodiscard]] RE::BSLightingShaderProperty* EnsureUniqueLightingShader(
            RE::BSGeometry* geom,
            RE::BSLightingShaderProperty* lightingShader) {
            if (lightingShader->GetRefCount() > 1u) {
                if (auto* clonedObj = lightingShader->Clone()) {
                    if (auto* clonedShader =
                            netimmerse_cast<RE::BSLightingShaderProperty*>(clonedObj)) {
                        geom->GetGeometryRuntimeData().shaderProperty =
                            RE::NiPointer<RE::BSShaderProperty>(clonedShader);
                        lightingShader = clonedShader;
                    } else {
                        clonedObj->IncRefCount();
                        clonedObj->DecRefCount();
                    }
                }
            }
            return lightingShader;
        }

        void ClearSubsurfaceAndRimFlags(RE::BSLightingShaderProperty* lightingShader) noexcept {
            lightingShader->SetFlags(
                RE::BSShaderProperty::EShaderPropertyFlag8::kSoftLighting, false);
            lightingShader->SetFlags(
                RE::BSShaderProperty::EShaderPropertyFlag8::kRimLighting, false);
            lightingShader->SetFlags(
                RE::BSShaderProperty::EShaderPropertyFlag8::kBackLighting, false);
        }

        void AdoptUniqueShaderMaterial(
            RE::BSLightingShaderProperty* lightingShader,
            RE::BSLightingShaderMaterialBase* temp) {
            lightingShader->SetMaterial(temp, /*a_unique=*/true);
            if (lightingShader->material != temp) {
                std::destroy_at(temp);
                if (auto* scrap = RE::MemoryManager::GetSingleton()->GetThreadScrapHeap()) {
                    scrap->Deallocate(temp);
                }
            }
        }

        void RebuildGeometryShaderPasses(
            RE::BSLightingShaderProperty* lightingShader,
            RE::BSGeometry* geom) {
            lightingShader->lastRenderPassState = (std::numeric_limits<std::int32_t>::max)();
            lightingShader->DoClearRenderPasses();
            lightingShader->SetupGeometry(geom);
            lightingShader->FinishSetupGeometry(geom);
        }

        template <typename TMat>
        [[nodiscard]] bool InstallUniqueTintedMaterial(
            RE::BSLightingShaderProperty* lightingShader,
            RE::BSLightingShaderMaterialBase* oldBaseMat,
            const RE::NiColor& tintColor,
            bool matteKeratinFinish) {
            auto* temp = RE::BSLightingShaderMaterialBase::CreateMaterial<TMat>();
            if (!temp) {
                return false;
            }
            temp->CopyBaseMembers(oldBaseMat);
            temp->tintColor = tintColor;
            if (matteKeratinFinish) {
                temp->specularColorScale = (std::min)(temp->specularColorScale, 0.32f);
                temp->specularPower = std::clamp(temp->specularPower, 10.0f, 22.0f);
                temp->subSurfaceLightRolloff = 0.0f;
                temp->rimLightPower = 0.0f;
            }
            AdoptUniqueShaderMaterial(lightingShader, temp);
            return true;
        }

        bool ApplyTintToGeometry(
            RE::BSGeometry* geom,
            Foundation::RgbColor targetColor,
            bool compensatePrecoloredDiffuse) {
            auto* lightingShader = ResolveLightingShader(geom);
            if (!lightingShader) {
                return false;
            }
            const auto feature = lightingShader->material->GetFeature();
            if (feature != RE::BSShaderMaterial::Feature::kHairTint &&
                feature != RE::BSShaderMaterial::Feature::kFaceGenRGBTint &&
                feature != RE::BSShaderMaterial::Feature::kDefault &&
                feature != RE::BSShaderMaterial::Feature::kEnvironmentMap) {
                return false;
            }

            lightingShader = EnsureUniqueLightingShader(geom, lightingShader);
            auto* oldBaseMat = static_cast<RE::BSLightingShaderMaterialBase*>(lightingShader->material);
            if (!oldBaseMat) {
                return false;
            }

            auto computeTintColor = [&]() -> RE::NiColor {
                if (compensatePrecoloredDiffuse) {
                    const auto meanRgb = ResolveGeometryDiffuseRgb(geom, oldBaseMat);
                    const auto m = Core::ComputeShaderTintFactors(targetColor, meanRgb);
                    return RE::NiColor{ m.x, m.y, m.z };
                }
                return RE::NiColor{
                    static_cast<float>(targetColor.r) / 255.0f,
                    static_cast<float>(targetColor.g) / 255.0f,
                    static_cast<float>(targetColor.b) / 255.0f
                };
            };

            if (feature == RE::BSShaderMaterial::Feature::kHairTint) {
                const RE::NiColor hc = computeTintColor();
                if (!InstallUniqueTintedMaterial<RE::BSLightingShaderMaterialHairTint>(
                        lightingShader, oldBaseMat, hc, compensatePrecoloredDiffuse)) {
                    return false;
                }
                RebuildGeometryShaderPasses(lightingShader, geom);
                return true;
            }

            const RE::NiColor sc = computeTintColor();
            if (feature != RE::BSShaderMaterial::Feature::kFaceGenRGBTint) {
                lightingShader->SetFlags(
                    RE::BSShaderProperty::EShaderPropertyFlag8::kFaceGenRGBTint, true);
                lightingShader->SetFlags(
                    RE::BSShaderProperty::EShaderPropertyFlag8::kHairTint, false);
                lightingShader->SetFlags(
                    RE::BSShaderProperty::EShaderPropertyFlag8::kEnvMap, false);
                lightingShader->SetFlags(
                    RE::BSShaderProperty::EShaderPropertyFlag8::kEnvmapLightFade, false);
            }
            if (compensatePrecoloredDiffuse) {
                ClearSubsurfaceAndRimFlags(lightingShader);
            }

            if (!InstallUniqueTintedMaterial<RE::BSLightingShaderMaterialFacegenTint>(
                    lightingShader, oldBaseMat, sc, compensatePrecoloredDiffuse)) {
                return false;
            }

            RebuildGeometryShaderPasses(lightingShader, geom);
            return true;
        }

        std::size_t TintSubtreeGeometries(
            RE::NiAVObject* subtree,
            Foundation::RgbColor color,
            bool compensatePrecoloredDiffuse) {
            if (!subtree) {
                return 0u;
            }
            std::size_t tinted = 0u;
            RE::BSVisit::TraverseScenegraphGeometries(
                subtree,
                [&](RE::BSGeometry* geom) -> RE::BSVisit::BSVisitControl {
                    if (ApplyTintToGeometry(geom, color, compensatePrecoloredDiffuse)) {
                        ++tinted;
                    }
                    return RE::BSVisit::BSVisitControl::kContinue;
                });
            return tinted;
        }

        std::size_t TintEarFurSubtreeGeometries(
            RE::NiAVObject* subtree,
            Foundation::RgbColor earHairTintRgb) {
            if (!subtree) {
                return 0u;
            }
            const RE::NiColor hc{
                static_cast<float>(earHairTintRgb.r) / 128.0f,
                static_cast<float>(earHairTintRgb.g) / 128.0f,
                static_cast<float>(earHairTintRgb.b) / 128.0f
            };
            std::size_t tinted = 0u;
            RE::BSVisit::TraverseScenegraphGeometries(
                subtree,
                [&](RE::BSGeometry* geom) -> RE::BSVisit::BSVisitControl {
                    auto* lightingShader = ResolveLightingShader(geom);
                    if (!lightingShader ||
                        lightingShader->material->GetFeature() !=
                            RE::BSShaderMaterial::Feature::kHairTint) {
                        // Leave kDefault inner-ear pink cartilage (e.g. Ear2.nif) untouched!
                        return RE::BSVisit::BSVisitControl::kContinue;
                    }
                    lightingShader = EnsureUniqueLightingShader(geom, lightingShader);
                    auto* oldBaseMat =
                        static_cast<RE::BSLightingShaderMaterialBase*>(lightingShader->material);
                    if (oldBaseMat &&
                        InstallUniqueTintedMaterial<RE::BSLightingShaderMaterialHairTint>(
                            lightingShader, oldBaseMat, hc, /*matteKeratinFinish=*/false)) {
                        RebuildGeometryShaderPasses(lightingShader, geom);
                        ++tinted;
                    }
                    return RE::BSVisit::BSVisitControl::kContinue;
                });
            return tinted;
        }

    }  // namespace

    void ApplyActorChimeraPartTints(
        RE::Actor* actor,
        RE::NiAVObject* root3D,
        RE::BSFaceGenNiNode* faceNode,
        std::optional<Foundation::RgbColor> tailColor,
        std::optional<Foundation::RgbColor> hornColor,
        std::optional<Foundation::RgbColor> earColor) {
        if (!actor || !root3D ||
            (!tailColor.has_value() && !hornColor.has_value() && !earColor.has_value())) {
            return;
        }

        auto& catalog = ChimeraAccessoryCatalog::GetSingleton();
        std::size_t tailGeomTinted = 0u;
        std::size_t hornGeomTinted = 0u;
        std::size_t earGeomTinted = 0u;

        if (tailColor.has_value()) {
            RE::NiAVObject* tailNode = nullptr;
            if (const auto& biped = actor->GetCurrentBiped()) {
                tailNode = biped->objects[RE::BIPED_OBJECTS::kTail].partClone.get();
            }
            if (tailNode) {
                tailGeomTinted += TintSubtreeGeometries(
                    tailNode, *tailColor, /*compensatePrecoloredDiffuse=*/true);
            } else if (auto* baseNpc = actor->GetActorBase();
                       baseNpc != nullptr && baseNpc->skin != nullptr) {
                for (const auto kind : { State::BeastTailKind::Khajiit, State::BeastTailKind::Argonian }) {
                    if (auto* tailArma = catalog.GetClonedTailArma(kind)) {
                        if (std::ranges::find(baseNpc->skin->armorAddons, tailArma) !=
                            baseNpc->skin->armorAddons.end()) {
                            actor->VisitArmorAddon(
                                baseNpc->skin,
                                tailArma,
                                [&](bool firstPerson, RE::NiAVObject& obj) {
                                    if (!firstPerson) {
                                        tailGeomTinted += TintSubtreeGeometries(
                                            &obj, *tailColor, /*compensatePrecoloredDiffuse=*/true);
                                    }
                                });
                            break;
                        }
                    }
                }
            }
        }

        std::string matchedHornEditorId;
        if (hornColor.has_value() && faceNode != nullptr) {
            if (const auto* baseNpc = actor->GetActorBase();
                baseNpc != nullptr && baseNpc->headParts != nullptr && baseNpc->numHeadParts > 0) {
                for (std::int8_t i = 0; i < baseNpc->numHeadParts; ++i) {
                    const RE::BGSHeadPart* hp = baseNpc->headParts[i];
                    if (!hp) {
                        continue;
                    }
                    const bool isHornHp =
                        catalog.IsRuntimePromotedHornPart(hp) || catalog.IsCatalogedHornHeadPart(hp);
                    if (!isHornHp) {
                        continue;
                    }
                    if (matchedHornEditorId.empty()) {
                        matchedHornEditorId =
                            !hp->formEditorID.empty() ? hp->formEditorID.c_str() : "<empty>";
                    }
                    if (!hp->formEditorID.empty()) {
                        hornGeomTinted += TintSubtreeGeometries(
                            faceNode->GetObjectByName(hp->formEditorID),
                            *hornColor,
                            /*compensatePrecoloredDiffuse=*/true);
                    }
                    for (const auto* extra : hp->extraParts) {
                        if (extra && !extra->formEditorID.empty()) {
                            hornGeomTinted += TintSubtreeGeometries(
                                faceNode->GetObjectByName(extra->formEditorID),
                                *hornColor,
                                /*compensatePrecoloredDiffuse=*/true);
                        }
                    }
                }
            }
        }

        if (earColor.has_value() && faceNode != nullptr) {
            if (const auto* baseNpc = actor->GetActorBase();
                baseNpc != nullptr && baseNpc->headParts != nullptr && baseNpc->numHeadParts > 0) {
                for (std::int8_t i = 0; i < baseNpc->numHeadParts; ++i) {
                    const RE::BGSHeadPart* hp = baseNpc->headParts[i];
                    if (!hp || !catalog.IsCatalogedEarHeadPart(hp)) {
                        continue;
                    }
                    if (!hp->formEditorID.empty()) {
                        earGeomTinted += TintEarFurSubtreeGeometries(
                            faceNode->GetObjectByName(hp->formEditorID), *earColor);
                    }
                    for (const auto* extra : hp->extraParts) {
                        if (extra && !extra->formEditorID.empty()) {
                            earGeomTinted += TintEarFurSubtreeGeometries(
                                faceNode->GetObjectByName(extra->formEditorID), *earColor);
                        }
                    }
                }
            }
        }

        if (hornColor.has_value() && hornGeomTinted == 0u && !matchedHornEditorId.empty()) {
            MORWHA_LOG_WARN(
                "ChimeraMeshTinter: Horn part '{}' present on actor {:08X} (base {:08X}) but 0 horn geometries were tinted",
                matchedHornEditorId,
                actor->GetFormID(),
                actor->GetActorBase() ? actor->GetActorBase()->GetFormID() : 0u);
        }

        if (tailGeomTinted > 0u || hornGeomTinted > 0u || earGeomTinted > 0u) {
            MORWHA_LOG_INFO(
                "ChimeraMeshTinter: Tinted actor {:08X} (tailGeoms={}, hornGeoms={}, earGeoms={})",
                actor->GetFormID(),
                tailGeomTinted,
                hornGeomTinted,
                earGeomTinted);
        }
    }

}  // namespace MORWHA::Engine
