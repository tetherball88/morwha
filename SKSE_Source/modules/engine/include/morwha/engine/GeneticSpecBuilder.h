#pragma once

#include "morwha/core/MorwhaCore.h"
#include "morwha/engine/DonorPhenotypeExtractor.h"
#include "morwha/engine/SynthesisSettings.h"
#include "morwha/foundation/DomainTypes.h"
#include "morwha/foundation/StrongIds.h"
#include "morwha/state/RegistryTypes.h"

#include <optional>
#include <string>

namespace RE {
    class TESRace;
}

namespace MORWHA::Engine {

    /// Builds a validated 0-donor procedural DynamicNpcSpec grounded in the target race/sex's FaceRelatedData.
    [[nodiscard]] std::optional<State::DynamicNpcSpec> BuildZeroDonorSpec(
        std::string slotKey,
        std::string displayName,
        RE::TESRace* race,
        Foundation::Sex sex,
        Foundation::Seed64 seed,
        const SynthesisSettings& settings = {},
        Foundation::AgeBracket requestedAgeBracket = Foundation::AgeBracket::Auto);

    /// Builds a validated 1-donor relative DynamicNpcSpec inheriting face clusters, colors, and build from donorA.
    [[nodiscard]] std::optional<State::DynamicNpcSpec> BuildOneDonorRelativeSpec(
        std::string slotKey,
        std::string displayName,
        const ExtractedEngineDonor& donorA,
        RE::TESRace* targetRace,
        Foundation::Sex targetSex,
        Foundation::Seed64 seed,
        const SynthesisSettings& settings = {},
        Foundation::AgeBracket requestedAgeBracket = Foundation::AgeBracket::Auto);

    /// Builds a validated 2-donor offspring DynamicNpcSpec combining two parents via MORWHA-Core.
    [[nodiscard]] std::optional<State::DynamicNpcSpec> BuildTwoDonorOffspringSpec(
        std::string slotKey,
        std::string displayName,
        const ExtractedEngineDonor& donorA,
        const ExtractedEngineDonor& donorB,
        RE::TESRace* targetRace,
        Core::InheritedParentSlot forcedRaceParent,
        Foundation::Sex targetSex,
        Foundation::Seed64 seed,
        const SynthesisSettings& settings = {},
        Foundation::AgeBracket requestedAgeBracket = Foundation::AgeBracket::Auto);

}  // namespace MORWHA::Engine
