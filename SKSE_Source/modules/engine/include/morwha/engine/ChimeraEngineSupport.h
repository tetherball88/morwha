#pragma once

#include "morwha/core/MorwhaCore.h"
#include "morwha/engine/SynthesisSettings.h"
#include "morwha/foundation/DomainTypes.h"

namespace MORWHA::Engine {

    struct InstalledChimeraModsReport {
        bool wearableBeastPartsLoaded{ false };
    };

    /// Soft-detects loaded third-party Tier-2 accessory plugins strictly via loaded plugin names
    /// (never for visual guessing).
    [[nodiscard]] InstalledChimeraModsReport InspectInstalledChimeraMods() noexcept;

    /// Returns effective runtime readiness for Tier-2 chimera modes; unsupported lineages
    /// cascade cleanly down toward Tier 1.
    [[nodiscard]] Core::TierAvailability QueryActiveChimeraTierAvailability() noexcept;
    [[nodiscard]] Core::TierAvailability QueryActiveChimeraTierAvailability(
        const SynthesisSettings& settings) noexcept;
    [[nodiscard]] Core::TierAvailability QueryActiveChimeraTierAvailability(
        const SynthesisSettings& settings,
        Foundation::Sex targetSex) noexcept;

}  // namespace MORWHA::Engine
