#include "morwha/engine/ChimeraSpecBuilder.h"

#include "SpecAssemblyInternal.h"
#include "morwha/core/MorwhaCore.h"
#include "morwha/engine/AgeProfileResolver.h"
#include "morwha/engine/ChimeraAccessoryCatalog.h"
#include "morwha/engine/ChimeraEngineSupport.h"
#include "morwha/engine/EngineLogging.h"
#include "morwha/engine/FormRefResolver.h"
#include "morwha/engine/GeneticSpecBuilder.h"
#include "morwha/engine/HeadPartPoolIndexer.h"
#include "morwha/engine/HybridSkinService.h"
#include "morwha/engine/TesNpcHydrator.h"
#include "morwha/state/DynamicNpcRegistry.h"

#include <algorithm>
#include <format>
#include <limits>
#include <random>
#include <utility>

namespace MORWHA::Engine {

    namespace {

        [[nodiscard]] std::uint64_t MixChimeraSeed(std::uint64_t raw, std::uint64_t salt) noexcept {
            std::uint64_t x = raw ^ salt;
            x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
            x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
            return x ^ (x >> 31);
        }

        [[nodiscard]] State::PluginFormRef FindNearestHairColorForm(
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

        void ReplaceHeadPartOfType(
            std::vector<State::PluginFormRef>& headParts,
            RE::BGSHeadPart::HeadPartType targetType,
            const RE::BGSHeadPart* replacement) {
            if (!replacement) {
                return;
            }
            auto newRef = MakePluginFormRef(replacement);
            if (newRef.IsEmpty()) {
                return;
            }
            for (auto& existingRef : headParts) {
                if (const auto* existing = ResolvePluginForm<RE::BGSHeadPart>(existingRef);
                    existing && existing->type.get() == targetType) {
                    existingRef = std::move(newRef);
                    return;
                }
            }
            headParts.push_back(std::move(newRef));
        }

        void UpsertTintLayer(
            std::vector<State::TintLayerSpec>& tintLayers,
            const RE::TESRace::FaceRelatedData* faceRelated,
            State::TintSemanticType semanticType,
            Foundation::RgbColor color,
            std::uint16_t alpha) {
            if (!faceRelated || alpha == 0u) {
                return;
            }
            const auto* asset =
                FindRaceTintAsset(faceRelated, semanticType, State::kUnsetExplicitTintIndex);
            if (!asset) {
                return;
            }
            const std::uint16_t clampedAlpha = std::min<std::uint16_t>(100u, alpha);
            for (auto& layer : tintLayers) {
                if (layer.semanticType == semanticType) {
                    layer.explicitTintIndex = asset->texture.index;
                    layer.color = color;
                    layer.interpolationValue = clampedAlpha;
                    return;
                }
            }
            tintLayers.push_back(State::TintLayerSpec{
                .semanticType = semanticType,
                .explicitTintIndex = asset->texture.index,
                .color = color,
                .interpolationValue = clampedAlpha
            });
        }

        [[nodiscard]] bool AttachRandomArgonianHorn(
            State::DynamicNpcSpec& spec,
            const ChimeraAccessoryCatalog& catalog,
            Core::ChimeraLineageKind lineage,
            const RE::TESRace* targetRace,
            Foundation::Sex targetSex,
            Foundation::Seed64 seed,
            std::uint64_t salt) {
            const auto hornHeadParts = catalog.GetValidHornHeadParts(lineage, targetRace, targetSex);
            const auto hornAddons = catalog.GetValidSkinHornAddons(lineage, targetRace, targetSex);
            const std::size_t totalHorns = hornHeadParts.size() + hornAddons.size();
            if (totalHorns == 0u) {
                return false;
            }
            std::mt19937_64 hornRng(MixChimeraSeed(seed.raw(), salt));
            std::uniform_int_distribution<std::size_t> dist(0u, totalHorns - 1u);
            const std::size_t idx = dist(hornRng);
            if (idx < hornHeadParts.size()) {
                if (auto hornRef = MakePluginFormRef(hornHeadParts[idx]); !hornRef.IsEmpty()) {
                    spec.headParts.push_back(std::move(hornRef));
                    return true;
                }
            } else {
                if (auto armaRef = MakePluginFormRef(hornAddons[idx - hornHeadParts.size()]);
                    !armaRef.IsEmpty()) {
                    spec.skinAddonRefs.push_back(std::move(armaRef));
                    return true;
                }
            }
            return false;
        }

        [[nodiscard]] bool TryAssignGeneratedHybridSkin(
            State::DynamicNpcSpec& spec,
            Core::HybridCoatGroup group,
            Foundation::Sex targetSex,
            Foundation::Seed64 seed,
            const SynthesisSettings& settings,
            const State::ChimeraLineageFeatureConfig& featCfg,
            Foundation::RgbColor targetBeastColor,
            Foundation::RgbColor humanInheritedHair,
            const RE::TESRace::FaceRelatedData* faceRelated) {
            if (!settings.enableGeneratedHybridSkins || !featCfg.enableHybridSkin) {
                return false;
            }
            if (!HybridSkinService::GetSingleton().IsCoatGroupReadyForSettings(
                    group, targetSex, settings.enableGeneratedHybridSkins)) {
                return false;
            }

            const auto plan = Core::PlanChimeraHybridCoat(
                group,
                targetBeastColor,
                humanInheritedHair,
                spec.hairColor,
                spec.skinColor,
                spec.beastTailKind != State::BeastTailKind::None,
                featCfg.matchTailColor,
                seed);
            if (plan.coatId.empty()) {
                return false;
            }
            if (plan.adjustHairColor) {
                spec.hairColor = plan.adjustedHairColor;
                if (faceRelated) {
                    spec.hairColorFormRef = FindNearestHairColorForm(faceRelated, spec.hairColor);
                }
            }
            if (plan.clearTailColor) {
                spec.tailColor.reset();
            } else if (plan.adjustedTailColor.has_value()) {
                spec.tailColor = plan.adjustedTailColor;
            }
            spec.hybridSkinId = plan.coatId;
            return true;
        }

        [[nodiscard]] std::optional<State::DynamicNpcSpec> BuildChimeraOffspringSpecInternal(
            std::string slotKey,
            std::string displayName,
            const Core::ChimeraResolutionPlan& plan,
            const ExtractedEngineDonor& donorA,
            const ExtractedEngineDonor& donorB,
            RE::TESRace* targetRace,
            Foundation::Sex targetSex,
            Foundation::Seed64 seed,
            const SynthesisSettings& settings,
            Foundation::AgeBracket requestedAgeBracket) {
            const auto& humanoidDonor =
                (plan.humanoidParent == Core::InheritedParentSlot::ParentA)
                    ? donorA
                    : donorB;
            const auto& beastDonor =
                (plan.beastParent == Core::InheritedParentSlot::ParentA)
                    ? donorA
                    : donorB;

            if (!Core::CanBlendDonorGroups(
                    humanoidDonor.phenotype.raceGroup, ResolveRaceGroup(targetRace))) {
                return std::nullopt;
            }

            auto specOpt = BuildOneDonorRelativeSpec(
                std::move(slotKey),
                std::move(displayName),
                humanoidDonor,
                targetRace,
                targetSex,
                seed,
                settings,
                Foundation::AgeBracket::YoungAdult);
            if (!specOpt) {
                return std::nullopt;
            }
            auto& spec = *specOpt;
            spec.donorMode = Foundation::DonorMode::TwoDonorOffspring;
            const Foundation::RgbColor humanInheritedHair =
                Core::SanitizeYouthfulHumanHairTint(spec.hairColor, requestedAgeBracket, seed);

            const auto* faceRelated = ResolveRaceFaceRelatedData(targetRace, targetSex);
            if (!faceRelated) {
                return std::nullopt;
            }

            const float raceSexHeight = Detail::ResolveRaceSexHeight(targetRace, targetSex);
            const auto traits = Core::SynthesizeChimeraPigment(
                plan.lineage,
                humanoidDonor.phenotype,
                beastDonor.phenotype,
                humanoidDonor.geometryBundle.irisSignature,
                beastDonor.geometryBundle.irisSignature,
                spec.skinColor,
                spec.hairColor,
                targetSex,
                raceSexHeight,
                settings.allowSupernaturalEyes,
                seed);

            spec.skinColor = traits.skinColor;
            spec.hairColor = traits.hairColor;
            spec.hairColorFormRef = FindNearestHairColorForm(faceRelated, spec.hairColor);
            spec.weight = traits.weight;
            spec.height = traits.height;

            const auto targetGroup = ResolveRaceGroup(targetRace);
            UpsertTintLayer(
                spec.tintLayers,
                faceRelated,
                State::TintSemanticType::SkinTone,
                spec.skinColor,
                100u);
            if (targetGroup == Core::RaceGroup::Humanoid) {
                UpsertTintLayer(
                    spec.tintLayers,
                    faceRelated,
                    State::TintSemanticType::Eyeliner,
                    traits.eyelinerColor,
                    traits.eyelinerAlpha);
                UpsertTintLayer(
                    spec.tintLayers,
                    faceRelated,
                    State::TintSemanticType::Nose,
                    traits.noseTintColor,
                    traits.noseTintAlpha);
            }

            const bool graftsKhajiit =
                (plan.lineage == Core::ChimeraLineageKind::HumanoidKhajiit) ||
                (plan.lineage == Core::ChimeraLineageKind::KhajiitArgonian &&
                 beastDonor.phenotype.raceGroup == Core::RaceGroup::Khajiit);
            const bool enableEyes =
                graftsKhajiit ? settings.khajiitTier2.enableEyes : settings.argonianTier2.enableEyes;

            if (enableEyes) {
                auto* templateNpc = ResolvePluginForm<RE::TESNPC>(spec.templateNpcRef);
                std::mt19937_64 eyeRng(MixChimeraSeed(seed.raw(), 0xE7E20001E7E20001ull));
                const RE::TESRace* otherEyeRace = beastDonor.geometryBundle.unwrappedDonorRace
                                                      ? beastDonor.geometryBundle.unwrappedDonorRace
                                                      : UnwrapToPlayableSynthesisRace(beastDonor.race, targetSex);
                if (auto* slitEye = Detail::SelectEyeHeadPartForRace(
                        targetRace,
                        faceRelated,
                        targetSex,
                        spec.topology,
                        templateNpc,
                        traits.targetEye,
                        settings.allowSupernaturalEyes,
                        traits.inheritedDarkEyes,
                        /*preferSlitPupil=*/true,
                        (otherEyeRace != targetRace) ? otherEyeRace : nullptr,
                        eyeRng)) {
                    ReplaceHeadPartOfType(spec.headParts, RE::BGSHeadPart::HeadPartType::kEyes, slitEye);
                }
            }

            auto& catalog = ChimeraAccessoryCatalog::GetSingleton();
            spec.tailColor.reset();
            spec.hornColor.reset();

            if (plan.lineage == Core::ChimeraLineageKind::KhajiitArgonian) {
                // Native beast base keeps its racial tail and beast fangs; graft cross-species hybrid skin (+ optional horns on Khajiit).
                spec.beastTailKind = State::BeastTailKind::None;
                spec.beastTeethKind = State::BeastTeethKind::None;

                if (targetGroup == Core::RaceGroup::Khajiit) {
                    const auto& argCfg = settings.argonianTier2;
                    const bool attachedHorn =
                        argCfg.enableHeadFeature &&
                        AttachRandomArgonianHorn(
                            spec, catalog, plan.lineage, targetRace, targetSex, seed, 0x40A4002840A40028ull);
                    if (attachedHorn && argCfg.matchHornColor) {
                        spec.hornColor = traits.hornColor;
                    }
                    (void)TryAssignGeneratedHybridSkin(
                        spec,
                        Core::HybridCoatGroup::KhajiitArgonianLizard,
                        targetSex,
                        seed,
                        settings,
                        argCfg,
                        traits.coatTargetColor,
                        humanInheritedHair,
                        faceRelated);
                } else {
                    (void)TryAssignGeneratedHybridSkin(
                        spec,
                        Core::HybridCoatGroup::ArgonianKhajiitFur,
                        targetSex,
                        seed,
                        settings,
                        settings.khajiitTier2,
                        traits.coatTargetColor,
                        humanInheritedHair,
                        faceRelated);
                }
            } else {
                const State::ChimeraLineageFeatureConfig& featCfg =
                    (plan.lineage == Core::ChimeraLineageKind::HumanoidKhajiit)
                        ? settings.khajiitTier2
                        : settings.argonianTier2;

                const bool enableSharpTeethForRace =
                    featCfg.enableTeeth &&
                    catalog.RaceUsesCanonicalHumanMouth(targetRace, targetSex);
                spec.beastTeethKind =
                    enableSharpTeethForRace ? State::BeastTeethKind::Sharp : State::BeastTeethKind::None;

                if (plan.lineage == Core::ChimeraLineageKind::HumanoidKhajiit) {
                    spec.beastTailKind =
                        featCfg.enableTails ? State::BeastTailKind::Khajiit : State::BeastTailKind::None;
                    if (spec.beastTailKind != State::BeastTailKind::None && featCfg.matchTailColor) {
                        spec.tailColor = traits.tailColor;
                    }
                    if (featCfg.enableHeadFeature) {
                        if (auto* earHp = catalog.PickEarHeadPart(plan.lineage, targetRace, targetSex, seed)) {
                            if (auto earRef = MakePluginFormRef(earHp); !earRef.IsEmpty()) {
                                spec.headParts.push_back(std::move(earRef));
                            }
                        }
                    }
                } else if (plan.lineage == Core::ChimeraLineageKind::HumanoidArgonian) {
                    spec.beastTailKind =
                        featCfg.enableTails ? State::BeastTailKind::Argonian : State::BeastTailKind::None;
                    if (spec.beastTailKind != State::BeastTailKind::None && featCfg.matchTailColor) {
                        spec.tailColor = traits.tailColor;
                    }
                    const bool attachedHorn =
                        featCfg.enableHeadFeature &&
                        AttachRandomArgonianHorn(
                            spec, catalog, plan.lineage, targetRace, targetSex, seed, 0x40A4000240A40002ull);
                    if (attachedHorn && featCfg.matchHornColor) {
                        spec.hornColor = traits.hornColor;
                    }
                }

                if (enableSharpTeethForRace) {
                    const bool hasRsvSharp = catalog.HasBeastMouthTextureSet(State::BeastTeethKind::Sharp);
                    auto* catalogMouth =
                        catalog.PickCatalogedMouthHeadPart(plan.lineage, targetRace, targetSex, seed);
                    if (catalogMouth != nullptr) {
                        const bool chooseCatalogMouth =
                            !hasRsvSharp ||
                            ((MixChimeraSeed(seed.raw(), 0x7EE740027EE74002ull) & 1ull) != 0ull);
                        if (chooseCatalogMouth) {
                            ReplaceHeadPartOfType(
                                spec.headParts, RE::BGSHeadPart::HeadPartType::kMisc, catalogMouth);
                        }
                    }
                }

                const auto coatGroup =
                    (plan.lineage == Core::ChimeraLineageKind::HumanoidKhajiit)
                        ? Core::HybridCoatGroup::HumanoidKhajiitFur
                        : Core::HybridCoatGroup::HumanoidArgonianLizard;
                const Foundation::RgbColor rawBeastCoatTarget =
                    (plan.lineage == Core::ChimeraLineageKind::HumanoidKhajiit)
                        ? Core::NormalizeKhajiitPelageSrgb(
                              beastDonor.phenotype.skinColor, beastDonor.phenotype.secondaryColor)
                        : traits.coatTargetColor;
                (void)TryAssignGeneratedHybridSkin(
                    spec,
                    coatGroup,
                    targetSex,
                    seed,
                    settings,
                    featCfg,
                    rawBeastCoatTarget,
                    humanInheritedHair,
                    faceRelated);
            }

            ApplyAgeProfileToSpec(spec, targetRace, faceRelated, requestedAgeBracket);

            RE::TESRace* secBioRace = beastDonor.geometryBundle.unwrappedDonorRace
                                          ? beastDonor.geometryBundle.unwrappedDonorRace
                                          : UnwrapToPlayableSynthesisRace(beastDonor.race, targetSex);
            if (secBioRace && secBioRace != targetRace) {
                spec.secondaryRaceRef = MakePluginFormRef(secBioRace);
            }
            spec.chimeraTier = 2u;

            std::string err;
            if (!State::ValidateDynamicNpcSpec(spec, &err)) {
                MORWHA_LOG_ERROR(
                    "ChimeraSpecBuilder: Tier-2 validation failed for '{}': {}",
                    spec.slotKey,
                    err);
                return std::nullopt;
            }

            MORWHA_LOG_INFO(
                "ChimeraSpecBuilder: Tier-2 chimera '{}' lineage={} targetRace={:08X} sex={} tail={} teeth={} headParts={} skinAddons={} hybridSkin='{}' skin=({},{},{}) hair=({},{},{})",
                spec.slotKey,
                static_cast<unsigned>(plan.lineage),
                targetRace->GetFormID(),
                static_cast<unsigned>(targetSex),
                static_cast<unsigned>(spec.beastTailKind),
                static_cast<unsigned>(spec.beastTeethKind),
                spec.headParts.size(),
                spec.skinAddonRefs.size(),
                spec.hybridSkinId,
                spec.skinColor.r,
                spec.skinColor.g,
                spec.skinColor.b,
                spec.hairColor.r,
                spec.hairColor.g,
                spec.hairColor.b);
            return spec;
        }

    }  // namespace

    std::optional<State::DynamicNpcSpec> TryBuildChimeraOffspringSpec(
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
            return std::nullopt;
        }

        const auto avail = QueryActiveChimeraTierAvailability(settings, targetSex);
        const auto chimeraPlan = Core::ResolveChimeraPlan(
            donorA.phenotype.raceGroup,
            donorB.phenotype.raceGroup,
            settings.beastChimeraTier,
            avail,
            seed,
            forcedRaceParent);
        if (chimeraPlan.lineage == Core::ChimeraLineageKind::None) {
            return std::nullopt;
        }

        const auto targetGroup = ResolveRaceGroup(targetRace);
        if (chimeraPlan.lineage == Core::ChimeraLineageKind::KhajiitArgonian) {
            const bool beastTargetOk =
                (targetGroup == Core::RaceGroup::Khajiit || targetGroup == Core::RaceGroup::Argonian);
            const bool beastCrossReady =
                (targetGroup == Core::RaceGroup::Khajiit)
                    ? avail.tier2ArgonianOnKhajiitReady
                    : avail.tier2KhajiitOnArgonianReady;
            if (chimeraPlan.effectiveTier == Core::BeastChimeraTier::Tier2EquippedParts &&
                beastTargetOk && beastCrossReady) {
                return BuildChimeraOffspringSpecInternal(
                    std::move(slotKey),
                    std::move(displayName),
                    chimeraPlan,
                    donorA,
                    donorB,
                    targetRace,
                    targetSex,
                    seed,
                    settings,
                    requestedAgeBracket);
            }
        }

        if (chimeraPlan.effectiveTier == Core::BeastChimeraTier::Tier2EquippedParts &&
            chimeraPlan.lineage != Core::ChimeraLineageKind::KhajiitArgonian &&
            targetGroup == Core::RaceGroup::Humanoid) {
            return BuildChimeraOffspringSpecInternal(
                std::move(slotKey),
                std::move(displayName),
                chimeraPlan,
                donorA,
                donorB,
                targetRace,
                targetSex,
                seed,
                settings,
                requestedAgeBracket);
        }

        if (chimeraPlan.effectiveTier != Core::BeastChimeraTier::Tier1DominantMorph &&
            chimeraPlan.effectiveTier != Core::BeastChimeraTier::Disabled &&
            chimeraPlan.lineage != Core::ChimeraLineageKind::KhajiitArgonian &&
            targetGroup != Core::RaceGroup::Humanoid) {
            return std::nullopt;
        }

        const auto& preferredDonor =
            (forcedRaceParent == Core::InheritedParentSlot::ParentA) ? donorA : donorB;
        const auto& alternateDonor =
            (forcedRaceParent == Core::InheritedParentSlot::ParentA) ? donorB : donorA;

        const ExtractedEngineDonor* matchingDonor = nullptr;
        if (Core::CanBlendDonorGroups(preferredDonor.phenotype.raceGroup, targetGroup)) {
            matchingDonor = &preferredDonor;
        } else if (Core::CanBlendDonorGroups(alternateDonor.phenotype.raceGroup, targetGroup)) {
            matchingDonor = &alternateDonor;
        }
        if (!matchingDonor) {
            return std::nullopt;
        }

        if (chimeraPlan.usedFallbackTier) {
            MORWHA_LOG_INFO(
                "ChimeraSpecBuilder: Requested chimera tier={} fell back to Tier 1 for '{}'",
                static_cast<unsigned>(chimeraPlan.requestedTier),
                slotKey);
        }

        auto specOpt = BuildOneDonorRelativeSpec(
            std::move(slotKey),
            std::move(displayName),
            *matchingDonor,
            targetRace,
            targetSex,
            seed,
            settings,
            Foundation::AgeBracket::YoungAdult);
        if (!specOpt) {
            return std::nullopt;
        }

        // Preserve two-donor offspring provenance so two-parent reroll ("mspawn reroll anyrace") works.
        specOpt->donorMode = Foundation::DonorMode::TwoDonorOffspring;
        {
            const ExtractedEngineDonor& nonMatchingDonor =
                (matchingDonor == &donorA) ? donorB : donorA;
            RE::TESRace* secBioRace = nonMatchingDonor.geometryBundle.unwrappedDonorRace
                                          ? nonMatchingDonor.geometryBundle.unwrappedDonorRace
                                          : UnwrapToPlayableSynthesisRace(nonMatchingDonor.race, targetSex);
            if (secBioRace && secBioRace != targetRace) {
                specOpt->secondaryRaceRef = MakePluginFormRef(secBioRace);
            }
            specOpt->chimeraTier =
                (chimeraPlan.effectiveTier == Core::BeastChimeraTier::Disabled) ? 0u : 1u;
        }
        if (const auto* faceRel = ResolveRaceFaceRelatedData(targetRace, targetSex)) {
            ApplyAgeProfileToSpec(*specOpt, targetRace, faceRel, requestedAgeBracket);
        }

        MORWHA_LOG_INFO(
            "ChimeraSpecBuilder: Tier-1 dominant-parent inheritance '{}' targetRace={:08X} sex={} matchingParentRace={:08X}",
            specOpt->slotKey,
            targetRace->GetFormID(),
            static_cast<unsigned>(targetSex),
            matchingDonor->race ? matchingDonor->race->GetFormID() : 0u);

        return specOpt;
    }

}  // namespace MORWHA::Engine
