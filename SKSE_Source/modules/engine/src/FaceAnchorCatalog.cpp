#include "morwha/engine/FaceAnchorCatalog.h"

#include "morwha/core/MorwhaCore.h"
#include "morwha/engine/EngineLogging.h"
#include "morwha/engine/TesNpcHydrator.h"

#include <string>
#include <string_view>
#include <vector>

namespace MORWHA::Engine {

    FaceAnchorCatalog& FaceAnchorCatalog::GetSingleton() noexcept {
        static FaceAnchorCatalog instance;
        return instance;
    }

    void FaceAnchorCatalog::LoadCatalog() {
        LoadCatalog(std::filesystem::path(State::kDefaultFaceAnchorsDir));
    }

    void FaceAnchorCatalog::LoadCatalog(const std::filesystem::path& rootDir) {
        lastStats_ = State::FaceAnchorDirLoadStats{};
        auto rawPresets = State::LoadFaceAnchorsFromDirectory(rootDir, &lastStats_, /*rejectSculpted=*/true);
        presets_.clear();
        presets_.reserve(rawPresets.size());
        for (auto& p : rawPresets) {
            p.raceKey = Core::NormalizeRaceKey(p.raceKey);
            if (!p.raceKey.empty()) {
                presets_.push_back(std::move(p));
            }
        }
        MORWHA_LOG_INFO(
            "FaceAnchorCatalog: Loaded {} slider-only .jslot anchors from '{}' (scanned={}, skippedSculpted={}, failed={})",
            presets_.size(),
            rootDir.string(),
            lastStats_.filesScanned,
            lastStats_.skippedSculpted,
            lastStats_.failedFiles);
    }

    std::vector<Foundation::FaceMorphVector> FaceAnchorCatalog::GatherMorphPoolForRace(
        const RE::TESRace* race,
        Foundation::Sex sex) const {
        if (!race || presets_.empty()) {
            return {};
        }

        const std::string_view rawEdid = race->GetFormEditorID() ? race->GetFormEditorID() : "";
        const std::string raceKey = Core::NormalizeRaceKey(rawEdid);
        const std::string morphKey =
            (race->morphRace && race->morphRace->GetFormEditorID())
                ? Core::NormalizeRaceKey(race->morphRace->GetFormEditorID())
                : std::string{};
        const bool isHumanoidGroup = (ResolveRaceGroup(race) == Core::RaceGroup::Humanoid);

        std::vector<Core::AnchorMorphCandidate> cands;
        cands.reserve(presets_.size());
        for (const auto& p : presets_) {
            cands.push_back(Core::AnchorMorphCandidate{
                .raceKey = p.raceKey,
                .sex = p.sex,
                .faceMorphs = p.faceMorphs
            });
        }
        return Core::SelectAnchorMorphPool(cands, raceKey, morphKey, sex, isHumanoidGroup);
    }

}  // namespace MORWHA::Engine
