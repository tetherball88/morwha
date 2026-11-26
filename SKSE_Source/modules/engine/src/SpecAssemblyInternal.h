#pragma once

#include "morwha/core/MorwhaCore.h"
#include "morwha/engine/SynthesisSettings.h"
#include "morwha/foundation/DomainTypes.h"
#include "morwha/foundation/StrongIds.h"
#include "morwha/state/RegistryTypes.h"

#include <RE/B/BGSHeadPart.h>
#include <RE/T/TESNPC.h>
#include <RE/T/TESRace.h>

#include <array>
#include <optional>
#include <random>
#include <string>
#include <vector>

namespace MORWHA::Engine {
    struct ExtractedEngineDonor;
}

namespace MORWHA::Engine::Detail {

    struct RacialAnchorContext {
        RE::TESNPC* presetNpc{ nullptr };
        Foundation::FaceMorphVector anchorMorphs{};
        bool usedCatalogAnchors{ false };
    };

    [[nodiscard]] float ResolveRaceSexHeight(
        const RE::TESRace* race,
        Foundation::Sex sex) noexcept;

    [[nodiscard]] RacialAnchorContext BuildTargetAnchorContext(
        RE::TESRace* race,
        const RE::TESRace::FaceRelatedData* faceRelated,
        Foundation::Sex sex,
        std::uint64_t rngSeed,
        std::mt19937_64& rng);

    [[nodiscard]] Foundation::RgbColor SampleRacialSkinColor(
        const RE::TESRace::FaceRelatedData* faceRelated,
        std::mt19937_64& rng);

    [[nodiscard]] Foundation::RgbColor SampleRacialHairColor(
        const RE::TESRace::FaceRelatedData* faceRelated,
        std::mt19937_64& rng);

    [[nodiscard]] std::array<float, 18> SampleContinuousMorphNoise(std::mt19937_64& rng);

    [[nodiscard]] RE::BGSHeadPart* SelectEyeHeadPartForRace(
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
        std::mt19937_64& rng,
        bool preferOtherParentEyes = false);

    [[nodiscard]] std::optional<State::DynamicNpcSpec> AssembleDynamicNpcSpec(
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
        const RE::TESRace* optionalSecondRaceForNoseEyes = nullptr,
        const RE::TESRace* optionalOtherParentRaceForEyes = nullptr,
        bool inheritedDarkSclera = false,
        const ExtractedEngineDonor* optionalPrimaryDonor = nullptr,
        const ExtractedEngineDonor* optionalSecondaryDonor = nullptr,
        Foundation::AgeBracket requestedAgeBracket = Foundation::AgeBracket::Auto,
        bool preferOtherParentEyes = false,
        bool preferOtherParentBrows = false);

}  // namespace MORWHA::Engine::Detail
