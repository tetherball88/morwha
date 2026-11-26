#pragma once

#include "morwha/foundation/DomainTypes.h"
#include "morwha/state/FaceAnchorPreset.h"

#include <cstddef>
#include <filesystem>
#include <vector>

namespace RE {
    class TESRace;
}

namespace MORWHA::Engine {

    /// Runtime catalog of slider-only .jslot face anchors loaded from Data/SKSE/Plugins/MORWHA/FaceAnchors.
    class FaceAnchorCatalog {
    public:
        static FaceAnchorCatalog& GetSingleton() noexcept;

        void LoadCatalog();
        void LoadCatalog(const std::filesystem::path& rootDir);

        [[nodiscard]] std::size_t GetPresetCount() const noexcept { return presets_.size(); }
        [[nodiscard]] const State::FaceAnchorDirLoadStats& GetLastLoadStats() const noexcept {
            return lastStats_;
        }

        /// Returns matching anchors for race/sex (exact normalized raceKey first, falling back to
        /// "humanoid" anchors only when fewer than 3 exact anchors exist and the race is humanoid).
        [[nodiscard]] std::vector<Foundation::FaceMorphVector> GatherMorphPoolForRace(
            const RE::TESRace* race,
            Foundation::Sex sex) const;

    private:
        FaceAnchorCatalog() = default;

        std::vector<State::FaceAnchorPreset> presets_{};
        State::FaceAnchorDirLoadStats lastStats_{};
    };

}  // namespace MORWHA::Engine
