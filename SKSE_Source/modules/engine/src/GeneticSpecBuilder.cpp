#include "morwha/engine/GeneticSpecBuilder.h"

#include "SpecAssemblyInternal.h"

#include "morwha/core/MorwhaCore.h"
#include "morwha/engine/AgeProfileResolver.h"
#include "morwha/engine/BeastTintSynthesizer.h"
#include "morwha/engine/ChimeraSpecBuilder.h"
#include "morwha/engine/DpfClient.h"
#include "morwha/engine/EngineLogging.h"
#include "morwha/engine/FaceAnchorCatalog.h"
#include "morwha/engine/FormRefResolver.h"
#include "morwha/engine/HeadPartPoolIndexer.h"
#include "morwha/engine/TesNpcHydrator.h"
#include "morwha/engine/TriProjectorCache.h"
#include "morwha/state/DynamicNpcRegistry.h"
#include "morwha/state/FaceAnchorPreset.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <random>
#include <string>
#include <string_view>
#include <vector>

namespace MORWHA::Engine {

    namespace {

        constexpr std::uint32_t kCitizenClassFormId = 0x01326Bu;
        constexpr std::uint32_t kFarmClothesOutfitFormId = 0x01DC10u;

        [[nodiscard]] State::PluginFormRef MatchNearestHairColorForm(
            const RE::TESRace::FaceRelatedData* faceRelated,
            Foundation::RgbColor targetHairColor) {
            if (faceRelated && faceRelated->availableHairColors &&
                !faceRelated->availableHairColors->empty()) {
                RE::BGSColorForm* bestForm = nullptr;
                std::uint32_t bestDist = (std::numeric_limits<std::uint32_t>::max)();
                for (auto* cand : *faceRelated->availableHairColors) {
                    if (!cand) {
                        continue;
                    }
                    const auto dist = ColorDistanceSq(targetHairColor, ToRgbColor(cand->color));
                    if (dist < bestDist) {
                        bestDist = dist;
                        bestForm = cand;
                    }
                }
                if (bestForm) {
                    return MakePluginFormRef(bestForm);
                }
            }
            if (faceRelated && faceRelated->defaultHairColor) {
                return MakePluginFormRef(faceRelated->defaultHairColor);
            }
            return {};
        }

        [[nodiscard]] RE::BGSHeadPart* SelectInheritedOrPooledEye(
            const std::vector<RE::BGSHeadPart*>& pooledEyes,
            std::size_t nativePoolCount,
            const std::optional<Core::EyeTarget>& targetIris,
            bool allowSupernaturalEyes,
            bool allowDarkScleraEyes,
            bool preferSlitPupil,
            Foundation::TopologyPool topology,
            RE::TESNPC* presetNpc,
            RE::TESRace* targetRace,
            const RE::TESRace::FaceRelatedData* faceRelated,
            Foundation::Sex targetSex,
            bool smpLoaded,
            std::mt19937_64& rng) {
            if (!pooledEyes.empty()) {
                auto& cache = TriProjectorCache::GetSingleton();
                std::vector<Core::EyeSample> poolSigs;
                poolSigs.reserve(pooledEyes.size());
                for (const auto* cand : pooledEyes) {
                    poolSigs.push_back(cache.GetOrDecodeEyeHeadPartIris(cand));
                }

                const std::uint64_t choiceRoll = rng();
                if (const auto chosenIdx = Core::SelectEyePoolIndex(
                        poolSigs,
                        targetIris,
                        allowSupernaturalEyes,
                        allowDarkScleraEyes,
                        preferSlitPupil,
                        choiceRoll,
                        nativePoolCount);
                    chosenIdx.has_value() && *chosenIdx < pooledEyes.size()) {
                    RE::BGSHeadPart* chosen = pooledEyes[*chosenIdx];
                    const auto& sig = poolSigs[*chosenIdx];
                    const auto previewRgb = Core::GetEyeSamplePreviewRgb(sig);
                    MORWHA_LOG_INFO(
                        "GeneticSpecBuilder: Selected pooled eye {:08X} ('{}') (altPupil={}, previewRgb=({},{},{}))",
                        chosen->GetFormID(),
                        chosen->GetFormEditorID() ? chosen->GetFormEditorID() : "",
                        Core::HasEyeSampleAltPupil(sig),
                        previewRgb.r,
                        previewRgb.g,
                        previewRgb.b);
                    return chosen;
                }

                std::uniform_int_distribution<std::size_t> dist(0, pooledEyes.size() - 1);
                return pooledEyes[dist(rng)];
            }
            return FindFallbackHeadPart(
                RE::BGSHeadPart::HeadPartType::kEyes,
                topology,
                presetNpc,
                targetRace,
                faceRelated,
                targetSex,
                smpLoaded);
        }

    }  // namespace

    namespace Detail {

        float ResolveRaceSexHeight(const RE::TESRace* race, Foundation::Sex sex) noexcept {
            if (!race) {
                return 1.0f;
            }
            const auto idx = static_cast<std::size_t>(sex);
            const float h = (idx < RE::SEXES::kTotal) ? race->data.height[idx] : 1.0f;
            return (std::isfinite(h) && h > 0.2f) ? std::clamp(h, 0.75f, 1.35f) : 1.0f;
        }

        RacialAnchorContext BuildTargetAnchorContext(
            RE::TESRace* race,
            const RE::TESRace::FaceRelatedData* faceRelated,
            Foundation::Sex sex,
            std::uint64_t rngSeed,
            std::mt19937_64& rng) {
            RacialAnchorContext ctx{};
            std::vector<RE::TESNPC*> validPresets;
            if (faceRelated && faceRelated->presetNPCs && !faceRelated->presetNPCs->empty()) {
                validPresets.reserve(faceRelated->presetNPCs->size());
                for (auto* candidate : *faceRelated->presetNPCs) {
                    if (candidate && candidate->faceData) {
                        validPresets.push_back(candidate);
                    }
                }
                if (!validPresets.empty()) {
                    std::uniform_int_distribution<std::size_t> dist(0, validPresets.size() - 1);
                    ctx.presetNpc = validPresets[dist(rng)];
                }
            }

            const auto anchorPool = FaceAnchorCatalog::GetSingleton().GatherMorphPoolForRace(race, sex);
            if (!anchorPool.empty()) {
                ctx.anchorMorphs = Core::SampleAnchorFromPool(
                    anchorPool,
                    Foundation::Seed64{ rngSeed ^ 0xA4C0FFEE12345678ull });
                ctx.usedCatalogAnchors = true;
            } else {
                std::vector<RE::TESNPC*> morphSamplePresets = validPresets;
                if (!Core::HasSufficientPresetDiversity(morphSamplePresets.size())) {
                    const auto peerPresets =
                        HeadPartPoolIndexer::GetSingleton().GatherCompatiblePeerPresets(race, sex);
                    for (auto* peer : peerPresets) {
                        if (peer && std::ranges::find(morphSamplePresets, peer) == morphSamplePresets.end()) {
                            morphSamplePresets.push_back(peer);
                        }
                    }
                }
                RE::TESNPC* morphAnchorNpc = ctx.presetNpc;
                if (!morphSamplePresets.empty()) {
                    std::uniform_int_distribution<std::size_t> mDist(0, morphSamplePresets.size() - 1);
                    morphAnchorNpc = morphSamplePresets[mDist(rng)];
                }
                if (morphAnchorNpc && morphAnchorNpc->faceData) {
                    for (std::size_t i = 0; i < Foundation::kVanillaFaceMorphCount; ++i) {
                        const float rawMorph = morphAnchorNpc->faceData->morphs[i];
                        ctx.anchorMorphs.morphs[i] = std::isfinite(rawMorph) ? rawMorph : 0.0f;
                    }
                    ctx.anchorMorphs.parts[0] = morphAnchorNpc->faceData->parts[0];
                    ctx.anchorMorphs.parts[1] = Foundation::kUnusedFacePartPreset;
                    ctx.anchorMorphs.parts[2] = morphAnchorNpc->faceData->parts[2];
                    ctx.anchorMorphs.parts[3] = morphAnchorNpc->faceData->parts[3];
                }
            }
            return ctx;
        }

        Foundation::RgbColor SampleRacialSkinColor(
            const RE::TESRace::FaceRelatedData* faceRelated,
            std::mt19937_64& rng) {
            if (const auto* skinAsset = FindRaceTintAsset(
                    faceRelated, State::TintSemanticType::SkinTone, State::kUnsetExplicitTintIndex)) {
                std::vector<Foundation::RgbColor> skinSwatches;
                skinSwatches.reserve(skinAsset->presets.colors.size());
                for (std::uint32_t i = 0; i < skinAsset->presets.colors.size(); ++i) {
                    const auto* col = skinAsset->presets.colors[i];
                    if (!col) {
                        continue;
                    }
                    const float rawAlpha =
                        (i < skinAsset->presets.defaultValues.size() &&
                         std::isfinite(skinAsset->presets.defaultValues[i]))
                            ? skinAsset->presets.defaultValues[i]
                            : 1.0f;
                    skinSwatches.push_back(CompositeTintOverMidGrey(col->color, rawAlpha));
                }
                if (!skinSwatches.empty()) {
                    std::uniform_int_distribution<std::size_t> idxDist(0, skinSwatches.size() - 1);
                    std::uniform_real_distribution<float> blendDist(0.25f, 0.75f);
                    const auto c1 = skinSwatches[idxDist(rng)];
                    const auto c2 = skinSwatches[idxDist(rng)];
                    return Core::MixColors(c1, c2, blendDist(rng));
                }
            }
            return Foundation::RgbColor{ 128, 128, 128 };
        }

        Foundation::RgbColor SampleRacialHairColor(
            const RE::TESRace::FaceRelatedData* faceRelated,
            std::mt19937_64& rng) {
            if (faceRelated && faceRelated->availableHairColors &&
                !faceRelated->availableHairColors->empty()) {
                std::vector<RE::BGSColorForm*> hairSwatches;
                for (auto* col : *faceRelated->availableHairColors) {
                    if (col) {
                        hairSwatches.push_back(col);
                    }
                }
                if (!hairSwatches.empty()) {
                    std::uniform_int_distribution<std::size_t> idxDist(0, hairSwatches.size() - 1);
                    std::uniform_real_distribution<float> blendDist(0.15f, 0.85f);
                    auto* h1 = hairSwatches[idxDist(rng)];
                    auto* h2 = hairSwatches[idxDist(rng)];
                    return Core::MixColors(
                        ToRgbColor(h1->color), ToRgbColor(h2->color), blendDist(rng));
                }
            }
            if (faceRelated && faceRelated->defaultHairColor) {
                return ToRgbColor(faceRelated->defaultHairColor->color);
            }
            return Foundation::RgbColor{ 64, 44, 32 };
        }

        std::array<float, 18> SampleContinuousMorphNoise(std::mt19937_64& rng) {
            std::array<float, 18> out{};
            std::normal_distribution<float> dist(0.0f, 0.08f);
            for (float& v : out) {
                v = std::clamp(dist(rng), -0.20f, 0.20f);
            }
            return out;
        }

        std::optional<State::DynamicNpcSpec> AssembleDynamicNpcSpec(
            std::string slotKey,
            std::string displayName,
            Foundation::DonorMode donorMode,
            RE::TESRace* targetRace,
            Foundation::Sex targetSex,
            Foundation::Seed64 seed,
            Foundation::FaceMorphVector faceMorphs,
            Foundation::RgbColor skinColor,
            Foundation::RgbColor hairColor,
            float weight,
            float height,
            const std::optional<Core::EyeTarget>& targetIris,
            const SynthesisSettings& settings,
            RE::TESNPC* presetNpc,
            const RE::TESRace::FaceRelatedData* faceRelated,
            Foundation::TopologyPool chosenTopology,
            std::mt19937_64& rng,
            const RE::TESRace* optionalSecondRaceForNoseEyes,
            const RE::TESRace* optionalOtherParentRaceForEyes,
            bool inheritedDarkSclera,
            const ExtractedEngineDonor* optionalPrimaryDonor,
            const ExtractedEngineDonor* optionalSecondaryDonor,
            Foundation::AgeBracket requestedAgeBracket) {
            const bool smpLoaded = DpfClient::GetSingleton().IsSmpLoaded();

            State::DynamicNpcSpec spec{};
            spec.slotKey = std::move(slotKey);
            spec.displayName = std::move(displayName);
            spec.donorMode = donorMode;
            spec.topology = chosenTopology;
            spec.seed = seed;
            spec.raceRef = MakePluginFormRef(targetRace);
            spec.sex = targetSex;
            spec.templateNpcRef = MakePluginFormRef(presetNpc);
            spec.faceMorphs = faceMorphs;
            if (presetNpc && presetNpc->faceData) {
                for (const std::size_t slot : { std::size_t{ 0 }, std::size_t{ 2 }, std::size_t{ 3 } }) {
                    if (spec.faceMorphs.parts[slot] == Foundation::kUnusedFacePartPreset &&
                        presetNpc->faceData->parts[slot] >= 0) {
                        spec.faceMorphs.parts[slot] = presetNpc->faceData->parts[slot];
                    }
                }
            }
            SanitizeFaceMorphPartsForRace(
                spec.faceMorphs,
                targetRace,
                targetSex,
                faceRelated,
                optionalSecondRaceForNoseEyes);
            {
                const auto allowedEyesForTarget = CollectAllowedMorphPresets(
                    targetRace, targetSex, faceRelated, 2u, optionalSecondRaceForNoseEyes, false);
                const auto allowedMouthForTarget = CollectAllowedMorphPresets(
                    targetRace, targetSex, faceRelated, 3u, nullptr, false);
                TriProjectorCache::GetSingleton().EnforceOrbitalClearanceForTarget(
                    targetRace,
                    targetSex,
                    chosenTopology,
                    spec.faceMorphs,
                    allowedEyesForTarget,
                    allowedMouthForTarget);
            }

            const auto sexIdx = static_cast<std::size_t>(targetSex);
            if (targetRace->defaultVoiceTypes[sexIdx]) {
                spec.voiceTypeRef = MakePluginFormRef(targetRace->defaultVoiceTypes[sexIdx]);
            }
            spec.npcClassRef = State::PluginFormRef{ "Skyrim.esm", kCitizenClassFormId };
            spec.defaultOutfitRef = State::PluginFormRef{ "Skyrim.esm", kFarmClothesOutfitFormId };

            if (faceRelated->defaultFaceDetailsTextureSet) {
                spec.faceTextureSetRef = MakePluginFormRef(faceRelated->defaultFaceDetailsTextureSet);
            } else if (presetNpc && presetNpc->headRelatedData && presetNpc->headRelatedData->faceDetails) {
                spec.faceTextureSetRef = MakePluginFormRef(presetNpc->headRelatedData->faceDetails);
            } else if (faceRelated->faceDetailsTextureSets && !faceRelated->faceDetailsTextureSets->empty()) {
                for (auto* tex : *faceRelated->faceDetailsTextureSets) {
                    if (tex) {
                        spec.faceTextureSetRef = MakePluginFormRef(tex);
                        break;
                    }
                }
            }

            const auto* pool =
                HeadPartPoolIndexer::GetSingleton().GetPool(targetRace, targetSex, chosenTopology);
            auto appendChosenPart = [&](RE::BGSHeadPart* part) {
                if (!part) {
                    return;
                }
                auto ref = MakePluginFormRef(part);
                if (!ref.IsEmpty()) {
                    spec.headParts.push_back(std::move(ref));
                }
            };
            auto pickFromVec = [&](const std::vector<RE::BGSHeadPart*>& vec,
                                   RE::BGSHeadPart::HeadPartType type,
                                   bool allowFallback) -> RE::BGSHeadPart* {
                if (!vec.empty()) {
                    std::uniform_int_distribution<std::size_t> dist(0, vec.size() - 1);
                    return vec[dist(rng)];
                }
                if (allowFallback) {
                    return FindFallbackHeadPart(
                        type, chosenTopology, presetNpc, targetRace, faceRelated, targetSex, smpLoaded);
                }
                return nullptr;
            };

            RE::BGSHeadPart* facePart = (pool && pool->primaryFace)
                                            ? pool->primaryFace
                                            : FindFallbackHeadPart(
                                                  RE::BGSHeadPart::HeadPartType::kFace,
                                                  chosenTopology,
                                                  presetNpc,
                                                  targetRace,
                                                  faceRelated,
                                                  targetSex,
                                                  smpLoaded);
            appendChosenPart(facePart);

            RE::BGSHeadPart* mouthPart = (pool && pool->primaryMouth)
                                             ? pool->primaryMouth
                                             : FindFallbackHeadPart(
                                                   RE::BGSHeadPart::HeadPartType::kMisc,
                                                   chosenTopology,
                                                   presetNpc,
                                                   targetRace,
                                                   faceRelated,
                                                   targetSex,
                                                   smpLoaded);
            appendChosenPart(mouthPart);

            static const std::vector<RE::BGSHeadPart*> kEmptyPartPool{};
            appendChosenPart(Detail::SelectEyeHeadPartForRace(
                targetRace,
                faceRelated,
                targetSex,
                chosenTopology,
                presetNpc,
                targetIris,
                settings.allowSupernaturalEyes,
                inheritedDarkSclera,
                /*preferSlitPupil=*/false,
                optionalOtherParentRaceForEyes,
                rng));
            appendChosenPart(pickFromVec(
                pool ? pool->hairs : kEmptyPartPool,
                RE::BGSHeadPart::HeadPartType::kHair,
                true));
            appendChosenPart(pickFromVec(
                pool ? pool->eyebrows : kEmptyPartPool,
                RE::BGSHeadPart::HeadPartType::kEyebrows,
                true));

            if (targetSex == Foundation::Sex::Male &&
                std::bernoulli_distribution(0.50)(rng)) {
                RE::BGSHeadPart* beard = pickFromVec(
                    pool ? pool->facialHairs : kEmptyPartPool,
                    RE::BGSHeadPart::HeadPartType::kFacialHair,
                    false);
                if (!beard && presetNpc) {
                    if (auto* presetBeard =
                            presetNpc->GetHeadPartByType(RE::BGSHeadPart::HeadPartType::kFacialHair);
                        presetBeard && HeadPartPoolIndexer::IsPartCompatibleWithTopology(
                                           presetBeard, chosenTopology, smpLoaded)) {
                        beard = presetBeard;
                    }
                }
                appendChosenPart(beard);
            }

            const float scarProb = (donorMode == Foundation::DonorMode::TwoDonorOffspring)
                                       ? std::clamp(settings.offspringScarChance, 0.0f, 1.0f)
                                       : std::clamp(settings.defaultScarChance, 0.0f, 1.0f);
            if (settings.allowScars && scarProb > 0.0f &&
                std::bernoulli_distribution(static_cast<double>(scarProb))(rng) &&
                pool && !pool->scars.empty()) {
                appendChosenPart(pickFromVec(pool->scars, RE::BGSHeadPart::HeadPartType::kScar, false));
            }

            spec.skinColor = skinColor;
            spec.hairColor = hairColor;
            spec.hairColorFormRef = MatchNearestHairColorForm(faceRelated, spec.hairColor);

            spec.tintLayers.push_back(State::TintLayerSpec{
                .semanticType = State::TintSemanticType::SkinTone,
                .explicitTintIndex = State::kUnsetExplicitTintIndex,
                .color = spec.skinColor,
                .interpolationValue = 100
            });
            if (!TryAppendBeastRacialMarkings(
                    spec.tintLayers,
                    targetRace,
                    faceRelated,
                    optionalPrimaryDonor,
                    optionalSecondaryDonor,
                    seed)) {
                if (targetSex == Foundation::Sex::Female) {
                    auto addSoftTint = [&](State::TintSemanticType semType,
                                           Foundation::RgbColor fallbackColor,
                                           std::uint16_t minAlpha,
                                           std::uint16_t maxAlpha) {
                        const auto* asset =
                            FindRaceTintAsset(faceRelated, semType, State::kUnsetExplicitTintIndex);
                        if (!asset) {
                            return;
                        }
                        Foundation::RgbColor picked = fallbackColor;
                        if (!asset->presets.colors.empty()) {
                            std::vector<Foundation::RgbColor> swatches;
                            for (const auto* col : asset->presets.colors) {
                                if (col) {
                                    swatches.push_back(ToRgbColor(col->color));
                                }
                            }
                            if (!swatches.empty()) {
                                std::uniform_int_distribution<std::size_t> d(0, swatches.size() - 1);
                                picked = swatches[d(rng)];
                            }
                        }
                        std::uniform_int_distribution<int> alphaDist(minAlpha, maxAlpha);
                        spec.tintLayers.push_back(State::TintLayerSpec{
                            .semanticType = semType,
                            .explicitTintIndex = asset->texture.index,
                            .color = picked,
                            .interpolationValue = static_cast<std::uint16_t>(alphaDist(rng))
                        });
                    };
                    addSoftTint(State::TintSemanticType::LipColor, Foundation::RgbColor{ 140, 62, 68 }, 14, 22);
                    addSoftTint(State::TintSemanticType::CheekColor, Foundation::RgbColor{ 164, 82, 86 }, 10, 16);
                    addSoftTint(State::TintSemanticType::Eyeliner, Foundation::RgbColor{ 32, 26, 26 }, 12, 18);
                }
            }

            spec.weight = std::clamp(weight, 0.0f, 100.0f);
            spec.height = std::clamp(height, 0.25f, 2.5f);

            ApplyAgeProfileToSpec(spec, targetRace, faceRelated, requestedAgeBracket);

            std::string err;
            if (!State::ValidateDynamicNpcSpec(spec, &err)) {
                MORWHA_LOG_ERROR("GeneticSpecBuilder: Spec validation failed for '{}': {}", spec.slotKey, err);
                return std::nullopt;
            }
            return spec;
        }

        RE::BGSHeadPart* SelectEyeHeadPartForRace(
            RE::TESRace* targetRace,
            const RE::TESRace::FaceRelatedData* faceRelated,
            Foundation::Sex targetSex,
            Foundation::TopologyPool chosenTopology,
            RE::TESNPC* presetNpc,
            const std::optional<Core::EyeTarget>& targetIris,
            bool allowSupernaturalEyes,
            bool allowDarkScleraEyes,
            bool preferSlitPupil,
            const RE::TESRace* optionalOtherParentRaceForEyes,
            std::mt19937_64& rng) {
            static const std::vector<RE::BGSHeadPart*> kEmptyPartPool{};
            const auto* pool =
                HeadPartPoolIndexer::GetSingleton().GetPool(targetRace, targetSex, chosenTopology);
            const std::size_t nativePoolCount = pool ? pool->eyes.size() : 0u;

            std::vector<RE::BGSHeadPart*> mergedEyePool;
            const std::vector<RE::BGSHeadPart*>* eyePoolPtr = pool ? &pool->eyes : &kEmptyPartPool;
            if (pool && !pool->eyes.empty() &&
                optionalOtherParentRaceForEyes != nullptr &&
                optionalOtherParentRaceForEyes != targetRace) {
                const auto* otherPool = HeadPartPoolIndexer::GetSingleton().GetPool(
                    optionalOtherParentRaceForEyes, targetSex, chosenTopology);
                if (otherPool && !otherPool->eyes.empty()) {
                    mergedEyePool = pool->eyes;
                    const RE::BGSHeadPart* refEye = pool->eyes.front();
                    for (RE::BGSHeadPart* cand : otherPool->eyes) {
                        if (!cand ||
                            !HeadPartPoolIndexer::HaveCompatibleEyeGeometry(cand, refEye) ||
                            std::find(mergedEyePool.begin(), mergedEyePool.end(), cand) !=
                                mergedEyePool.end()) {
                            continue;
                        }
                        mergedEyePool.push_back(cand);
                    }
                    eyePoolPtr = &mergedEyePool;
                }
            }
            const bool smpLoaded = DpfClient::GetSingleton().IsSmpLoaded();
            return SelectInheritedOrPooledEye(
                *eyePoolPtr,
                nativePoolCount,
                targetIris,
                allowSupernaturalEyes,
                allowDarkScleraEyes,
                preferSlitPupil,
                chosenTopology,
                presetNpc,
                targetRace,
                faceRelated,
                targetSex,
                smpLoaded,
                rng);
        }

    }  // namespace Detail

    std::optional<State::DynamicNpcSpec> BuildZeroDonorSpec(
        std::string slotKey,
        std::string displayName,
        RE::TESRace* race,
        Foundation::Sex sex,
        Foundation::Seed64 seed,
        const SynthesisSettings& settings,
        Foundation::AgeBracket requestedAgeBracket) {
        if (slotKey.empty() || !race) {
            MORWHA_LOG_ERROR("BuildZeroDonorSpec: Invalid empty slotKey or null race");
            return std::nullopt;
        }
        auto* faceRelated = ResolveRaceFaceRelatedData(race, sex);
        if (!faceRelated) {
            MORWHA_LOG_ERROR(
                "BuildZeroDonorSpec: Race {:08X} ({}) has no FaceRelatedData for sex={}",
                race->GetFormID(),
                race->GetFormEditorID() ? race->GetFormEditorID() : "",
                static_cast<unsigned>(sex));
            return std::nullopt;
        }

        const std::uint64_t rngSeed = (seed.raw() != 0ull) ? seed.raw() : 0x9E3779B97F4A7C15ull;
        std::mt19937_64 rng(rngSeed);
        const auto chosenTopology =
            HeadPartPoolIndexer::GetSingleton().SelectPreferredTopology(race, sex);
        const auto anchorCtx =
            Detail::BuildTargetAnchorContext(race, faceRelated, sex, rngSeed, rng);

        std::array<float, Foundation::kVanillaFaceMorphCount> zeroNoise{};
        const float noiseStddev = anchorCtx.usedCatalogAnchors ? 0.16f : 0.20f;
        const float noiseLimit = anchorCtx.usedCatalogAnchors ? 0.35f : 0.45f;
        std::normal_distribution<float> morphNoiseDist(0.0f, noiseStddev);
        for (std::size_t i = 0; i + 1 < Foundation::kVanillaFaceMorphCount; ++i) {
            zeroNoise[i] = std::clamp(morphNoiseDist(rng), -noiseLimit, noiseLimit);
        }
        zeroNoise[18] = 0.0f;

        const auto synthesizedMorphs =
            Core::SynthesizeZeroDonorMorphs(sex, anchorCtx.anchorMorphs, zeroNoise);
        const auto skinColor = Detail::SampleRacialSkinColor(faceRelated, rng);
        const auto hairColor = Detail::SampleRacialHairColor(faceRelated, rng);

        std::uniform_real_distribution<float> weightDist(25.0f, 75.0f);
        std::uniform_real_distribution<float> heightDist(0.98f, 1.02f);
        const float weight = std::round(weightDist(rng));
        const float height = std::round(heightDist(rng) * 1000.0f) / 1000.0f;

        return Detail::AssembleDynamicNpcSpec(
            std::move(slotKey),
            std::move(displayName),
            Foundation::DonorMode::ZeroDonorProcedural,
            race,
            sex,
            seed,
            synthesizedMorphs,
            skinColor,
            hairColor,
            weight,
            height,
            std::nullopt,
            settings,
            anchorCtx.presetNpc,
            faceRelated,
            chosenTopology,
            rng,
            nullptr,
            nullptr,
            false,
            nullptr,
            nullptr,
            requestedAgeBracket);
    }

    std::optional<State::DynamicNpcSpec> BuildOneDonorRelativeSpec(
        std::string slotKey,
        std::string displayName,
        const ExtractedEngineDonor& donorA,
        RE::TESRace* targetRace,
        Foundation::Sex targetSex,
        Foundation::Seed64 seed,
        const SynthesisSettings& settings,
        Foundation::AgeBracket requestedAgeBracket) {
        if (slotKey.empty() || !targetRace) {
            MORWHA_LOG_ERROR("BuildOneDonorRelativeSpec: Invalid empty slotKey or null targetRace");
            return std::nullopt;
        }
        auto* faceRelated = ResolveRaceFaceRelatedData(targetRace, targetSex);
        if (!faceRelated) {
            MORWHA_LOG_ERROR(
                "BuildOneDonorRelativeSpec: Race {:08X} ({}) has no FaceRelatedData for sex={}",
                targetRace->GetFormID(),
                targetRace->GetFormEditorID() ? targetRace->GetFormEditorID() : "",
                static_cast<unsigned>(targetSex));
            return std::nullopt;
        }

        const std::uint64_t rngSeed =
            (seed.raw() != 0ull) ? seed.raw() : (0x9E3779B97F4A7C15ull ^ 0x1111ull);
        std::mt19937_64 rng(rngSeed);
        const auto chosenTopology =
            HeadPartPoolIndexer::GetSingleton().SelectPreferredTopology(targetRace, targetSex);
        const auto anchorCtx =
            Detail::BuildTargetAnchorContext(targetRace, faceRelated, targetSex, rngSeed, rng);

        const auto fallbackSkin = Detail::SampleRacialSkinColor(faceRelated, rng);
        const auto fallbackHair = Detail::SampleRacialHairColor(faceRelated, rng);
        const auto targetGroup = ResolveRaceGroup(targetRace);

        auto recombined = Core::RecombineOneDonor(
            donorA.phenotype,
            targetSex,
            targetGroup,
            anchorCtx.anchorMorphs,
            fallbackSkin,
            fallbackHair,
            seed,
            Detail::ResolveRaceSexHeight(targetRace, targetSex));

        std::optional<Core::EyeTarget> targetIris;
        bool inheritedDarkSclera = false;
        if (recombined.crossCompatibleDonors && donorA.geometryBundle.has3dGeometry) {
            const auto morphNoise = Detail::SampleContinuousMorphNoise(rng);
            const auto synth3d = TriProjectorCache::GetSingleton().SynthesizeTargetMorphsFromDonors3d(
                targetRace,
                targetSex,
                chosenTopology,
                &donorA.geometryBundle,
                nullptr,
                recombined.clusterDominanceA,
                anchorCtx.anchorMorphs,
                morphNoise,
                seed,
                /*isOneDonorRelative=*/true);
            if (synth3d.succeeded) {
                recombined.faceMorphs = synth3d.faceMorphs;
            }
        }
        if (recombined.crossCompatibleDonors) {
            const float variationRoll = std::uniform_real_distribution<float>(-0.02f, 0.02f)(rng);
            Core::DeriveInheritedEyeTarget(
                donorA.geometryBundle.irisSignature,
                nullptr,
                /*blendBothDonors=*/false,
                0.0f,
                settings.allowSupernaturalEyes,
                variationRoll,
                targetIris,
                inheritedDarkSclera);
        }

        return Detail::AssembleDynamicNpcSpec(
            std::move(slotKey),
            std::move(displayName),
            Foundation::DonorMode::OneDonorRelative,
            targetRace,
            targetSex,
            seed,
            recombined.faceMorphs,
            recombined.skinColor,
            recombined.hairColor,
            recombined.weight,
            recombined.height,
            targetIris,
            settings,
            anchorCtx.presetNpc,
            faceRelated,
            chosenTopology,
            rng,
            nullptr,
            nullptr,
            inheritedDarkSclera,
            &donorA,
            nullptr,
            requestedAgeBracket);
    }

    std::optional<State::DynamicNpcSpec> BuildTwoDonorOffspringSpec(
        std::string slotKey,
        std::string displayName,
        const ExtractedEngineDonor& donorA,
        const ExtractedEngineDonor& donorB,
        RE::TESRace* targetRace,
        Core::InheritedParentSlot forcedRaceParent,
        Foundation::Sex targetSex,
        Foundation::Seed64 seed,
        const SynthesisSettings& settings,
        Foundation::AgeBracket requestedAgeBracket) {
        if (slotKey.empty() || !targetRace) {
            MORWHA_LOG_ERROR("BuildTwoDonorOffspringSpec: Invalid empty slotKey or null targetRace");
            return std::nullopt;
        }
        auto* faceRelated = ResolveRaceFaceRelatedData(targetRace, targetSex);
        if (!faceRelated) {
            MORWHA_LOG_ERROR(
                "BuildTwoDonorOffspringSpec: Race {:08X} ({}) has no FaceRelatedData for sex={}",
                targetRace->GetFormID(),
                targetRace->GetFormEditorID() ? targetRace->GetFormEditorID() : "",
                static_cast<unsigned>(targetSex));
            return std::nullopt;
        }

        if (auto chimeraSpec = TryBuildChimeraOffspringSpec(
                slotKey, displayName, donorA, donorB, targetRace,
                forcedRaceParent, targetSex, seed, settings, requestedAgeBracket)) {
            return chimeraSpec;
        }

        const std::uint64_t rngSeed =
            (seed.raw() != 0ull) ? seed.raw() : (0x9E3779B97F4A7C15ull ^ 0x2222ull);
        std::mt19937_64 rng(rngSeed);
        const auto chosenTopology =
            HeadPartPoolIndexer::GetSingleton().SelectPreferredTopology(targetRace, targetSex);
        const auto anchorCtx =
            Detail::BuildTargetAnchorContext(targetRace, faceRelated, targetSex, rngSeed, rng);

        const auto targetGroup = ResolveRaceGroup(targetRace);
        const auto& activeDonor =
            (forcedRaceParent == Core::InheritedParentSlot::ParentA) ? donorA : donorB;

        Core::RecombinedPhenotype recombined{};
        std::optional<Core::EyeTarget> targetIris;
        bool inheritedDarkSclera = false;
        const bool allowCrossRaceMorphs = IsAllRaceMorphsUnlocked(faceRelated);
        const RE::TESRace* secondRaceForNoseEyes = nullptr;
        const RE::TESRace* otherParentRaceForEyes = nullptr;

        if (!Core::CanBlendDonorGroups(activeDonor.phenotype.raceGroup, targetGroup)) {
            const auto fallbackSkin = Detail::SampleRacialSkinColor(faceRelated, rng);
            const auto fallbackHair = Detail::SampleRacialHairColor(faceRelated, rng);
            recombined = Core::RecombineOneDonor(
                activeDonor.phenotype,
                targetSex,
                targetGroup,
                anchorCtx.anchorMorphs,
                fallbackSkin,
                fallbackHair,
                seed,
                Detail::ResolveRaceSexHeight(targetRace, targetSex));
            recombined.chosenRaceParent = forcedRaceParent;
            recombined.crossCompatibleDonors = false;
        } else {
            recombined = Core::RecombineTwoDonors(
                donorA.phenotype,
                donorB.phenotype,
                targetSex,
                forcedRaceParent,
                anchorCtx.anchorMorphs,
                seed,
                Detail::ResolveRaceSexHeight(targetRace, targetSex),
                allowCrossRaceMorphs);

            const bool blendBothDonors =
                recombined.crossCompatibleDonors &&
                Core::CanBlendDonorGroups(
                    donorA.phenotype.raceGroup, donorB.phenotype.raceGroup);
            if (blendBothDonors) {
                const auto& otherDonor =
                    (recombined.chosenRaceParent == Core::InheritedParentSlot::ParentA)
                        ? donorB
                        : donorA;
                RE::TESRace* otherBio = otherDonor.geometryBundle.unwrappedDonorRace
                                            ? otherDonor.geometryBundle.unwrappedDonorRace
                                            : UnwrapToPlayableSynthesisRace(otherDonor.race, targetSex);
                if (otherBio != nullptr && otherBio != targetRace) {
                    otherParentRaceForEyes = otherBio;
                    if (allowCrossRaceMorphs) {
                        secondRaceForNoseEyes = otherBio;
                    }
                }
            }

            const auto morphNoise = Detail::SampleContinuousMorphNoise(rng);
            if (blendBothDonors &&
                (donorA.geometryBundle.has3dGeometry || donorB.geometryBundle.has3dGeometry)) {
                const auto synth3d = TriProjectorCache::GetSingleton().SynthesizeTargetMorphsFromDonors3d(
                    targetRace,
                    targetSex,
                    chosenTopology,
                    &donorA.geometryBundle,
                    &donorB.geometryBundle,
                    recombined.clusterDominanceA,
                    anchorCtx.anchorMorphs,
                    morphNoise,
                    seed,
                    /*isOneDonorRelative=*/false,
                    allowCrossRaceMorphs);
                if (synth3d.succeeded) {
                    recombined.faceMorphs = synth3d.faceMorphs;
                }
            } else if (activeDonor.geometryBundle.has3dGeometry) {
                const auto synth3d = TriProjectorCache::GetSingleton().SynthesizeTargetMorphsFromDonors3d(
                    targetRace,
                    targetSex,
                    chosenTopology,
                    &activeDonor.geometryBundle,
                    nullptr,
                    recombined.clusterDominanceA,
                    anchorCtx.anchorMorphs,
                    morphNoise,
                    seed,
                    /*isOneDonorRelative=*/true);
                if (synth3d.succeeded) {
                    recombined.faceMorphs = synth3d.faceMorphs;
                }
            }

            if (blendBothDonors) {
                const float variationRoll = std::uniform_real_distribution<float>(-0.02f, 0.02f)(rng);
                const float eyeDomA =
                    recombined.clusterDominanceA[static_cast<std::size_t>(Foundation::FaceCluster::Eyes)];
                Core::DeriveInheritedEyeTarget(
                    donorA.geometryBundle.irisSignature,
                    &donorB.geometryBundle.irisSignature,
                    /*blendBothDonors=*/true,
                    eyeDomA,
                    settings.allowSupernaturalEyes,
                    variationRoll,
                    targetIris,
                    inheritedDarkSclera);
            } else {
                Core::DeriveInheritedEyeTarget(
                    activeDonor.geometryBundle.irisSignature,
                    nullptr,
                    /*blendBothDonors=*/false,
                    0.0f,
                    settings.allowSupernaturalEyes,
                    0.0f,
                    targetIris,
                    inheritedDarkSclera);
            }
        }

        const auto& chosenPrimaryDonor =
            (recombined.chosenRaceParent == Core::InheritedParentSlot::ParentA)
                ? donorA
                : donorB;
        const auto& otherParentDonor =
            (recombined.chosenRaceParent == Core::InheritedParentSlot::ParentA)
                ? donorB
                : donorA;
        const ExtractedEngineDonor* secondaryBeastDonor =
            (recombined.crossCompatibleDonors &&
             Core::CanBlendDonorGroups(
                 chosenPrimaryDonor.phenotype.raceGroup, otherParentDonor.phenotype.raceGroup))
                ? &otherParentDonor
                : nullptr;

        auto builtSpec = Detail::AssembleDynamicNpcSpec(
            std::move(slotKey),
            std::move(displayName),
            Foundation::DonorMode::TwoDonorOffspring,
            targetRace,
            targetSex,
            seed,
            recombined.faceMorphs,
            recombined.skinColor,
            recombined.hairColor,
            recombined.weight,
            recombined.height,
            targetIris,
            settings,
            anchorCtx.presetNpc,
            faceRelated,
            chosenTopology,
            rng,
            secondRaceForNoseEyes,
            otherParentRaceForEyes,
            inheritedDarkSclera,
            &chosenPrimaryDonor,
            secondaryBeastDonor,
            requestedAgeBracket);
        if (builtSpec) {
            RE::TESRace* bioA = donorA.geometryBundle.unwrappedDonorRace
                                    ? donorA.geometryBundle.unwrappedDonorRace
                                    : UnwrapToPlayableSynthesisRace(donorA.race, targetSex);
            RE::TESRace* bioB = donorB.geometryBundle.unwrappedDonorRace
                                    ? donorB.geometryBundle.unwrappedDonorRace
                                    : UnwrapToPlayableSynthesisRace(donorB.race, targetSex);
            RE::TESRace* secBio = nullptr;
            if (bioA != nullptr && bioB != nullptr && bioA != bioB) {
                secBio = (bioA == targetRace) ? bioB : ((bioB == targetRace) ? bioA : nullptr);
            }
            if (!secBio && otherParentDonor.race) {
                RE::TESRace* altBio = otherParentDonor.geometryBundle.unwrappedDonorRace
                                          ? otherParentDonor.geometryBundle.unwrappedDonorRace
                                          : UnwrapToPlayableSynthesisRace(otherParentDonor.race, targetSex);
                if (altBio && altBio != targetRace) {
                    secBio = altBio;
                }
            }
            if (secBio && secBio != targetRace) {
                builtSpec->secondaryRaceRef = MakePluginFormRef(secBio);
            }
        }
        return builtSpec;
    }

}  // namespace MORWHA::Engine
