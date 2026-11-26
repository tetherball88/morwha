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

    /// Builds a validated Chimera offspring DynamicNpcSpec through MORWHA-Core,
    /// or returns std::nullopt when the pairing is non-chimeric or disabled.
    [[nodiscard]] std::optional<State::DynamicNpcSpec> TryBuildChimeraOffspringSpec(
        std::string slotKey,
        std::string displayName,
        const ExtractedEngineDonor& donorA,
        const ExtractedEngineDonor& donorB,
        RE::TESRace* targetRace,
        Core::InheritedParentSlot forcedRaceParent,
        Foundation::Sex targetSex,
        Foundation::Seed64 seed,
        const SynthesisSettings& settings,
        Foundation::AgeBracket requestedAgeBracket = Foundation::AgeBracket::Auto);

}  // namespace MORWHA::Engine
