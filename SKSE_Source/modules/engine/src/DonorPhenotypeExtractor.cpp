#include "morwha/engine/DonorPhenotypeExtractor.h"

#include "morwha/engine/EngineLogging.h"
#include "morwha/engine/FaceAnchorCatalog.h"
#include "morwha/engine/HeadPartPoolIndexer.h"
#include "morwha/engine/Mortal3dSyncService.h"
#include "morwha/engine/TesNpcHydrator.h"
#include "morwha/state/FaceAnchorPreset.h"
#include "morwha/state/RegistryTypes.h"

#include <algorithm>
#include <cmath>
#include <string_view>

namespace MORWHA::Engine {

    namespace {

        [[nodiscard]] bool HasNonZeroRgb(RE::Color c) noexcept {
            return (c.red | c.green | c.blue) != 0;
        }

        [[nodiscard]] std::optional<Foundation::RgbColor> TryExtractSkinFromSceneGraph(RE::NiAVObject* root) {
            if (!root) {
                return std::nullopt;
            }
            std::optional<Foundation::RgbColor> found;
            RE::BSVisit::TraverseScenegraphGeometries(
                root,
                [&](RE::BSGeometry* geom) -> RE::BSVisit::BSVisitControl {
                    if (!geom) {
                        return RE::BSVisit::BSVisitControl::kContinue;
                    }
                    auto* shaderProp = geom->GetGeometryRuntimeData().shaderProperty.get();
                    auto* lighting = shaderProp
                                         ? netimmerse_cast<RE::BSLightingShaderProperty*>(shaderProp)
                                         : nullptr;
                    auto* material = lighting ? lighting->material : nullptr;
                    if (material &&
                        material->GetFeature() == RE::BSShaderMaterial::Feature::kFaceGenRGBTint) {
                        const auto* faceTint =
                            static_cast<const RE::BSLightingShaderMaterialFacegenTint*>(material);
                        const RE::NiColor& c = faceTint->tintColor;
                        if (std::isfinite(c.red) && std::isfinite(c.green) && std::isfinite(c.blue)) {
                            auto toByte = [](float v) noexcept -> std::uint8_t {
                                return static_cast<std::uint8_t>(
                                    std::clamp<long>(std::lround(v * 255.0f), 0l, 255l));
                            };
                            found = Foundation::RgbColor{
                                toByte(c.red),
                                toByte(c.green),
                                toByte(c.blue)
                            };
                            return RE::BSVisit::BSVisitControl::kStop;
                        }
                    }
                    return RE::BSVisit::BSVisitControl::kContinue;
                });
            return found;
        }

        [[nodiscard]] std::optional<Foundation::RgbColor> TryExtractHairFromSceneGraph(RE::NiAVObject* root) {
            if (!root) {
                return std::nullopt;
            }
            std::optional<Foundation::RgbColor> preferredHair;
            std::optional<Foundation::RgbColor> fallbackHair;
            RE::BSVisit::TraverseScenegraphGeometries(
                root,
                [&](RE::BSGeometry* geom) -> RE::BSVisit::BSVisitControl {
                    if (!geom) {
                        return RE::BSVisit::BSVisitControl::kContinue;
                    }
                    auto* shaderProp = geom->GetGeometryRuntimeData().shaderProperty.get();
                    auto* lighting = shaderProp
                                         ? netimmerse_cast<RE::BSLightingShaderProperty*>(shaderProp)
                                         : nullptr;
                    auto* material = lighting ? lighting->material : nullptr;
                    if (material &&
                        material->GetFeature() == RE::BSShaderMaterial::Feature::kHairTint) {
                        const auto* hairTint =
                            static_cast<const RE::BSLightingShaderMaterialHairTint*>(material);
                        const RE::NiColor& c = hairTint->tintColor;
                        if (std::isfinite(c.red) && std::isfinite(c.green) && std::isfinite(c.blue)) {
                            // Engine divides RgbColor by 128.0f in Actor::UpdateHairColor() / NiAVObject::UpdateHairColor().
                            auto toHairByte = [](float v) noexcept -> std::uint8_t {
                                return static_cast<std::uint8_t>(
                                    std::clamp<long>(std::lround(v * 128.0f), 0l, 255l));
                            };
                            const Foundation::RgbColor rgb{
                                toHairByte(c.red),
                                toHairByte(c.green),
                                toHairByte(c.blue)
                            };
                            const std::string_view geomName =
                                geom->name.c_str() ? geom->name.c_str() : "";
                            const bool isSecondaryHair =
                                HeadPartPoolIndexer::ContainsCaseInsensitive(geomName, "Brow") ||
                                HeadPartPoolIndexer::ContainsCaseInsensitive(geomName, "Beard") ||
                                HeadPartPoolIndexer::ContainsCaseInsensitive(geomName, "Mustache");
                            if (!fallbackHair.has_value()) {
                                fallbackHair = rgb;
                            }
                            if (!isSecondaryHair) {
                                preferredHair = rgb;
                                return RE::BSVisit::BSVisitControl::kStop;
                            }
                        }
                    }
                    return RE::BSVisit::BSVisitControl::kContinue;
                });
            return preferredHair.has_value() ? preferredHair : fallbackHair;
        }

        [[nodiscard]] Foundation::RgbColor ExtractSkinColor(
            RE::Actor* optionalActor,
            const RE::TESNPC* baseNpc,
            const RE::TESNPC* rootFaceNpc,
            const RE::TESRace::FaceRelatedData* faceRelated,
            bool isPlayer) {
            if (const auto cached = Mortal3dSyncService::GetSingleton().LookupEntry(baseNpc->GetFormID())) {
                return cached->skinColor;
            }

            if (isPlayer) {
                if (auto* player = RE::PlayerCharacter::GetSingleton()) {
                    const RE::BSTArray<RE::TintMask*>* masks = nullptr;
                    if (REL::Module::IsVR()) {
                        if (const auto* vrData = player->GetVRPlayerRuntimeData()) {
                            masks = vrData->overlayTintMasks ? vrData->overlayTintMasks
                                                             : &vrData->tintMasks;
                        }
                    } else {
                        const auto& flatData = player->GetPlayerRuntimeData();
                        masks = flatData.overlayTintMasks ? flatData.overlayTintMasks
                                                          : &flatData.tintMasks;
                    }
                    if (masks) {
                        for (const auto* mask : *masks) {
                            if (mask &&
                                mask->type.get() == RE::TintMask::Type::kSkinTone &&
                                std::isfinite(mask->alpha) &&
                                mask->alpha > 0.001f) {
                                return CompositeTintOverMidGrey(mask->color, mask->alpha);
                            }
                        }
                    }
                }
            }

            if (rootFaceNpc->tintLayers && faceRelated && faceRelated->tintMasks) {
                for (const auto* layer : *rootFaceNpc->tintLayers) {
                    if (!layer || layer->interpolationValue == 0) {
                        continue;
                    }
                    for (const auto* asset : *faceRelated->tintMasks) {
                        if (asset &&
                            asset->texture.index == layer->tintIndex &&
                            asset->texture.skinTone.underlying() ==
                                static_cast<std::uint8_t>(State::TintSemanticType::SkinTone)) {
                            const float alpha = static_cast<float>(layer->interpolationValue) / 100.0f;
                            return CompositeTintOverMidGrey(layer->tintColor, alpha);
                        }
                    }
                }
            }

            if (HasNonZeroRgb(rootFaceNpc->bodyTintColor)) {
                return ToRgbColor(rootFaceNpc->bodyTintColor);
            }
            if (HasNonZeroRgb(baseNpc->bodyTintColor)) {
                return ToRgbColor(baseNpc->bodyTintColor);
            }

            if (optionalActor && optionalActor->Is3DLoaded()) {
                if (const auto liveSkin = TryExtractSkinFromSceneGraph(optionalActor->GetFaceNodeSkinned())) {
                    return *liveSkin;
                }
                if (const auto liveSkin = TryExtractSkinFromSceneGraph(optionalActor->Get3D(false))) {
                    return *liveSkin;
                }
            }

            if (faceRelated && faceRelated->tintMasks) {
                for (const auto* asset : *faceRelated->tintMasks) {
                    if (asset &&
                        asset->texture.skinTone.underlying() ==
                            static_cast<std::uint8_t>(State::TintSemanticType::SkinTone) &&
                        !asset->presets.colors.empty() &&
                        asset->presets.colors[0] != nullptr) {
                        const float alpha = (!asset->presets.defaultValues.empty() &&
                                             std::isfinite(asset->presets.defaultValues[0]))
                                                ? asset->presets.defaultValues[0]
                                                : 1.0f;
                        return CompositeTintOverMidGrey(asset->presets.colors[0]->color, alpha);
                    }
                }
            }

            return Foundation::RgbColor{ 180, 150, 135 };
        }

        [[nodiscard]] Foundation::RgbColor ExtractHairColor(
            RE::Actor* optionalActor,
            const RE::TESNPC* baseNpc,
            const RE::TESNPC* rootFaceNpc,
            const RE::TESRace::FaceRelatedData* faceRelated,
            const std::optional<Foundation::RgbColor>& nifHairShaderColor,
            bool& outUsedLive3dShader) {
            outUsedLive3dShader = false;
            if (optionalActor && optionalActor->Is3DLoaded()) {
                if (const auto liveHair = TryExtractHairFromSceneGraph(optionalActor->GetFaceNodeSkinned())) {
                    outUsedLive3dShader = true;
                    return *liveHair;
                }
                if (const auto liveHair = TryExtractHairFromSceneGraph(optionalActor->Get3D(false))) {
                    outUsedLive3dShader = true;
                    return *liveHair;
                }
            }

            if (const auto cached = Mortal3dSyncService::GetSingleton().LookupEntry(baseNpc->GetFormID())) {
                return cached->hairColor;
            }
            if (nifHairShaderColor.has_value()) {
                return *nifHairShaderColor;
            }
            if (rootFaceNpc->headRelatedData && rootFaceNpc->headRelatedData->hairColor) {
                return ToRgbColor(rootFaceNpc->headRelatedData->hairColor->color);
            }
            if (baseNpc->headRelatedData && baseNpc->headRelatedData->hairColor) {
                return ToRgbColor(baseNpc->headRelatedData->hairColor->color);
            }
            if (faceRelated && faceRelated->defaultHairColor) {
                return ToRgbColor(faceRelated->defaultHairColor->color);
            }
            return Foundation::RgbColor{ 64, 44, 32 };
        }

        [[nodiscard]] std::optional<Foundation::RgbColor> ExtractBeastSecondaryColor(
            const RE::TESNPC* rootFaceNpc,
            const RE::TESRace::FaceRelatedData* faceRelated,
            Foundation::RgbColor baseSkinColor,
            bool isPlayer) {
            if (!rootFaceNpc || !faceRelated || !faceRelated->tintMasks) {
                return std::nullopt;
            }

            float bestScore = 0.0f;
            std::optional<Foundation::RgbColor> bestRgb;

            auto considerLayer = [&](std::uint8_t semRaw, Foundation::RgbColor layerRgb, float alpha) {
                if (alpha < 0.20f) {
                    return;
                }
                const auto sem = static_cast<State::TintSemanticType>(semRaw);
                if (sem == State::TintSemanticType::None ||
                    sem == State::TintSemanticType::SkinTone ||
                    sem == State::TintSemanticType::Dirt) {
                    return;
                }
                float bonus = 0.0f;
                if (sem == State::TintSemanticType::Neck ||
                    sem == State::TintSemanticType::Chin ||
                    sem == State::TintSemanticType::CheekColorLower) {
                    bonus = 0.25f;
                } else if (sem == State::TintSemanticType::Paint ||
                           sem == State::TintSemanticType::Forehead) {
                    bonus = 0.15f;
                }
                const float score = bonus + std::clamp(alpha, 0.0f, 1.0f);
                if (score <= bestScore) {
                    return;
                }
                bestScore = score;
                if (alpha >= 0.75f) {
                    bestRgb = layerRgb;
                } else {
                    const float t = std::clamp(alpha / 0.75f, 0.35f, 1.0f);
                    auto lerpByte = [t](std::uint8_t a, std::uint8_t b) noexcept -> std::uint8_t {
                        const float v =
                            static_cast<float>(a) + t * (static_cast<float>(b) - static_cast<float>(a));
                        return static_cast<std::uint8_t>(std::clamp<long>(std::lround(v), 0l, 255l));
                    };
                    bestRgb = Foundation::RgbColor{
                        lerpByte(baseSkinColor.r, layerRgb.r),
                        lerpByte(baseSkinColor.g, layerRgb.g),
                        lerpByte(baseSkinColor.b, layerRgb.b)
                    };
                }
            };

            if (rootFaceNpc->tintLayers) {
                for (const auto* layer : *rootFaceNpc->tintLayers) {
                    if (!layer || layer->interpolationValue < 20u) {
                        continue;
                    }
                    const auto* asset = FindRaceTintAsset(
                        faceRelated,
                        State::TintSemanticType::None,
                        layer->tintIndex);
                    if (!asset) {
                        continue;
                    }
                    const float alpha =
                        static_cast<float>((std::min<std::uint16_t>)(100u, layer->interpolationValue)) / 100.0f;
                    considerLayer(
                        asset->texture.skinTone.underlying(),
                        ToRgbColor(layer->tintColor),
                        alpha);
                }
            }

            if (!bestRgb.has_value() && isPlayer) {
                if (auto* player = RE::PlayerCharacter::GetSingleton()) {
                    const RE::BSTArray<RE::TintMask*>* masks = nullptr;
                    if (REL::Module::IsVR()) {
                        if (const auto* vrData = player->GetVRPlayerRuntimeData()) {
                            masks = vrData->overlayTintMasks ? vrData->overlayTintMasks
                                                             : &vrData->tintMasks;
                        }
                    } else {
                        const auto& flatData = player->GetPlayerRuntimeData();
                        masks = flatData.overlayTintMasks ? flatData.overlayTintMasks
                                                          : &flatData.tintMasks;
                    }
                    if (masks) {
                        for (const auto* mask : *masks) {
                            if (!mask || !std::isfinite(mask->alpha) || mask->alpha < 0.20f) {
                                continue;
                            }
                            const auto semRaw = static_cast<std::uint8_t>(mask->type.get());
                            considerLayer(semRaw, ToRgbColor(mask->color), mask->alpha);
                        }
                    }
                }
            }

            return bestRgb;
        }

        [[nodiscard]] std::optional<ExtractedEngineDonor> ExtractDonorInternal(
            RE::Actor* optionalActor,
            RE::TESNPC* baseNpc) {
            if (!baseNpc) {
                return std::nullopt;
            }

            RE::TESRace* race = optionalActor ? optionalActor->GetRace() : baseNpc->GetRace();
            if (!race) {
                race = baseNpc->GetRace();
            }
            if (!race) {
                return std::nullopt;
            }

            if (race->data.flags.all(RE::RACE_DATA::Flag::kChild)) {
                MORWHA_LOG_WARN(
                    "ExtractDonorPhenotype: Child race {:08X} ('{}') on donor {:08X} is unsupported",
                    race->GetFormID(),
                    race->GetFormEditorID() ? race->GetFormEditorID() : "",
                    baseNpc->GetFormID());
                return std::nullopt;
            }

            RE::TESNPC* rootFaceNpc = baseNpc->GetRootFaceNPC();
            if (!rootFaceNpc) {
                rootFaceNpc = baseNpc;
            }

            const bool isPlayer = (optionalActor != nullptr) && optionalActor->IsPlayerRef();
            const RE::SEX reSex = isPlayer ? baseNpc->GetSex() : rootFaceNpc->GetSex();
            const Foundation::Sex sex =
                (reSex == RE::SEX::kFemale) ? Foundation::Sex::Female : Foundation::Sex::Male;
            const auto* faceRelated = ResolveRaceFaceRelatedData(race, sex);

            ExtractedEngineDonor out{};
            out.race = race;
            out.baseNpc = baseNpc;
            out.rootFaceNpc = rootFaceNpc;
            out.phenotype.sex = sex;

            Foundation::FaceMorphVector tier1Morphs{};
            bool tier1HasNonZero = false;
            if (rootFaceNpc->faceData) {
                for (std::size_t i = 0; i + 1 < Foundation::kVanillaFaceMorphCount; ++i) {
                    const float raw = rootFaceNpc->faceData->morphs[i];
                    const float clamped = std::isfinite(raw) ? std::clamp(raw, -1.0f, 1.0f) : 0.0f;
                    tier1Morphs.morphs[i] = clamped;
                    if (std::fabs(clamped) > 1e-4f) {
                        tier1HasNonZero = true;
                    }
                }
                tier1Morphs.morphs[Foundation::kVanillaFaceMorphCount - 1] = 0.0f;

                tier1Morphs.parts[1] = Foundation::kUnusedFacePartPreset;
                for (const std::size_t slot : { std::size_t{ 0 }, std::size_t{ 2 }, std::size_t{ 3 } }) {
                    const std::int32_t presetVal = rootFaceNpc->faceData->parts[slot];
                    tier1Morphs.parts[slot] =
                        (presetVal < 0 || presetVal == RE::TESNPC::FaceData::kDefault)
                            ? Foundation::kUnusedFacePartPreset
                            : presetVal;
                }
            }

            out.eyePart = rootFaceNpc->GetCurrentHeadPartByType(RE::BGSHeadPart::HeadPartType::kEyes);
            if (!out.eyePart) {
                out.eyePart = rootFaceNpc->GetHeadPartByType(RE::BGSHeadPart::HeadPartType::kEyes);
            }
            if (!out.eyePart && baseNpc != rootFaceNpc) {
                out.eyePart = baseNpc->GetCurrentHeadPartByType(RE::BGSHeadPart::HeadPartType::kEyes);
            }

            out.geometryBundle = TriProjectorCache::GetSingleton().ExtractDonorGeometryAndIris(
                optionalActor,
                rootFaceNpc,
                race,
                sex,
                isPlayer,
                tier1Morphs,
                tier1HasNonZero,
                out.eyePart);

            out.phenotype.raceGroup = ResolveRaceGroup(race);
            if (out.phenotype.raceGroup == Core::RaceGroup::Other &&
                out.geometryBundle.unwrappedDonorRace != nullptr) {
                out.phenotype.raceGroup = ResolveRaceGroup(out.geometryBundle.unwrappedDonorRace);
            }
            const RE::TESRace* bioRace =
                out.geometryBundle.unwrappedDonorRace ? out.geometryBundle.unwrappedDonorRace : race;
            if (bioRace) {
                out.phenotype.raceId = Foundation::FormRawId{ bioRace->GetFormID() };
            }

            out.extractionTier = out.geometryBundle.sourceTier;
            out.phenotype.faceMorphs = tier1Morphs;
            out.phenotype.hasNonZeroMorphs = tier1HasNonZero;
            if (!tier1HasNonZero && out.geometryBundle.unwrappedDonorRace != nullptr) {
                const auto anchorPool = FaceAnchorCatalog::GetSingleton().GatherMorphPoolForRace(
                    out.geometryBundle.unwrappedDonorRace, sex);
                if (!anchorPool.empty()) {
                    const std::uint64_t seed =
                        static_cast<std::uint64_t>(rootFaceNpc->GetFormID()) ^ 0x71E3FACE5EED1234ull;
                    out.phenotype.faceMorphs = Core::SampleAnchorFromPool(
                        anchorPool,
                        Foundation::Seed64{ seed != 0ull ? seed : 1ull });
                    out.phenotype.hasNonZeroMorphs = true;
                }
            }

            // Read TESNPC::weight directly; Actor::GetWeight() returns TESObjectREFR item mass, not body weight.
            const float rawWeight = isPlayer ? baseNpc->weight : rootFaceNpc->weight;
            out.phenotype.weight =
                std::isfinite(rawWeight) ? std::clamp(rawWeight, 0.0f, 100.0f) : 50.0f;

            const RE::TESRace* heightRace =
                (race && std::isfinite(race->data.height[0]) && race->data.height[0] > 0.2f)
                    ? race
                    : (out.geometryBundle.unwrappedDonorRace ? out.geometryBundle.unwrappedDonorRace : race);
            if (heightRace) {
                for (std::size_t s = 0; s < 2u; ++s) {
                    const float rh = heightRace->data.height[s];
                    out.phenotype.raceBaseHeights[s] =
                        (std::isfinite(rh) && rh > 0.2f) ? std::clamp(rh, 0.75f, 1.35f) : 1.0f;
                }
            }

            const float rawHeight = isPlayer ? baseNpc->height : rootFaceNpc->height;
            const float npcHeight = (std::isfinite(rawHeight) && rawHeight > 0.2f)
                                        ? std::clamp(rawHeight, 0.80f, 1.25f)
                                        : 1.0f;
            float refScaleMult = 1.0f;
            if (optionalActor) {
                const std::uint16_t rawRefScale = optionalActor->GetReferenceRuntimeData().refScale;
                if (rawRefScale > 0u) {
                    const float s = static_cast<float>(rawRefScale) / 100.0f;
                    if (std::isfinite(s) && s > 0.2f) {
                        refScaleMult = std::clamp(s, 0.80f, 1.25f);
                    }
                }
            }
            float skelScaleMult = 1.0f;
            if (optionalActor && optionalActor->Is3DLoaded()) {
                if (auto* root3d = optionalActor->Get3D(false)) {
                    if (auto* npcNode = root3d->GetObjectByName("NPC")) {
                        const float nodeScale = npcNode->local.scale;
                        if (std::isfinite(nodeScale) && nodeScale > 0.2f) {
                            skelScaleMult = std::clamp(nodeScale, 0.80f, 1.25f);
                        }
                    }
                }
            }
            out.phenotype.height = std::clamp(npcHeight * refScaleMult * skelScaleMult, 0.80f, 1.25f);

            out.phenotype.skinColor =
                ExtractSkinColor(optionalActor, baseNpc, rootFaceNpc, faceRelated, isPlayer);
            out.phenotype.hairColor = ExtractHairColor(
                optionalActor,
                baseNpc,
                rootFaceNpc,
                faceRelated,
                out.geometryBundle.nifHairShaderColor,
                out.extractedLive3dHairShader);
            if (out.phenotype.raceGroup == Core::RaceGroup::Argonian ||
                out.phenotype.raceGroup == Core::RaceGroup::Khajiit) {
                out.phenotype.secondaryColor = ExtractBeastSecondaryColor(
                    rootFaceNpc, faceRelated, out.phenotype.skinColor, isPlayer);
            }

            return out;
        }

    }  // namespace

    std::optional<ExtractedEngineDonor> ExtractDonorPhenotype(RE::Actor* actor) {
        if (!actor) {
            return std::nullopt;
        }
        return ExtractDonorInternal(actor, actor->GetActorBase());
    }

    std::optional<ExtractedEngineDonor> ExtractDonorPhenotypeFromBase(RE::TESNPC* npc) {
        if (!npc) {
            return std::nullopt;
        }
        return ExtractDonorInternal(nullptr, npc);
    }

}  // namespace MORWHA::Engine
