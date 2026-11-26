#include "morwha/app/SynthesisCoordinator.h"

#include "morwha/engine/DpfClient.h"
#include "morwha/engine/EngineLogging.h"
#include "morwha/engine/Mortal3dSyncService.h"
#include "morwha/engine/SkseSerializationCallbacks.h"
#include "morwha/state/DynamicNpcRegistry.h"
#include "morwha/state/RegistryGarbageCollector.h"

#include <algorithm>
#include <unordered_set>

namespace MORWHA::App {

    std::size_t SynthesisCoordinator::GetUncommittedSpecCount() const {
        std::lock_guard lock(registryMutex_);
        return static_cast<std::size_t>(std::ranges::count_if(specs_, [](const State::DynamicNpcSpec& spec) {
            return !spec.committed;
        }));
    }

    void SynthesisCoordinator::OnGameSaved(std::span<const State::SpawnedMortalLink> savedLinks) noexcept {
        if (registryCorrupt_.load(std::memory_order_acquire) || savedLinks.empty()) {
            return;
        }

        std::size_t committedNow = 0;
        {
            std::lock_guard lock(registryMutex_);
            const std::size_t uncommittedBefore = static_cast<std::size_t>(
                std::ranges::count_if(specs_, [](const State::DynamicNpcSpec& s) { return !s.committed; }));
            if (uncommittedBefore == 0) {
                return;
            }
            if (!State::MarkCommittedSpecsForLinks(specs_, savedLinks)) {
                return;
            }
            const std::size_t uncommittedAfter = static_cast<std::size_t>(
                std::ranges::count_if(specs_, [](const State::DynamicNpcSpec& s) { return !s.committed; }));
            committedNow = uncommittedBefore - uncommittedAfter;

            std::string saveErr;
            if (!State::SaveRegistryAtomic(State::kDefaultRegistryPath, specs_, &saveErr)) {
                MORWHA_LOG_ERROR(
                    "OnGameSaved: Failed to persist committed dynamic NPC state to '{}': {}",
                    State::kDefaultRegistryPath,
                    saveErr);
                return;
            }
        }

        MORWHA_LOG_INFO(
            "OnGameSaved: Promoted {} staged dynamic NPC spec(s) to committed",
            committedNow);
    }

    std::size_t SynthesisCoordinator::PurgeUncommittedSpecs() noexcept {
        if (registryCorrupt_.load(std::memory_order_acquire)) {
            return 0u;
        }

        std::vector<std::string> prunedKeys;
        std::size_t releasedCount = 0;
        {
            std::lock_guard lock(registryMutex_);
            const bool hasUncommitted = std::ranges::any_of(specs_, [](const State::DynamicNpcSpec& s) {
                return !s.committed;
            });
            if (!hasUncommitted) {
                return 0u;
            }

            const std::unordered_set<std::string> emptyKeys;
            auto plan = State::PlanRegistryGarbageCollection(
                specs_,
                emptyKeys,
                emptyKeys,
                /*pruneUncommitted=*/true,
                /*pruneUnreferencedCommitted=*/false);
            if (plan.prunedUncommittedKeys.empty()) {
                return 0u;
            }

            std::string saveErr;
            if (!State::SaveRegistryAtomic(State::kDefaultRegistryPath, plan.survivingSpecs, &saveErr)) {
                MORWHA_LOG_ERROR(
                    "PurgeUncommittedSpecs: Failed saving '{}' ({}); aborting uncommitted purge",
                    State::kDefaultRegistryPath,
                    saveErr);
                return 0u;
            }

            specs_ = std::move(plan.survivingSpecs);
            if (specs_.empty()) {
                nextSlotIndex_ = 1;
            }
            prunedKeys = std::move(plan.prunedUncommittedKeys);

            auto& dpf = Engine::DpfClient::GetSingleton();
            for (const auto& key : prunedKeys) {
                if (dpf.ReleaseNpcSlot(key)) {
                    ++releasedCount;
                }
            }
        }

        const std::unordered_set<std::string> prunedSet(prunedKeys.begin(), prunedKeys.end());
        Engine::Mortal3dSyncService::GetSingleton().UnregisterHydratedNpcsBySlotKeys(prunedSet);

        MORWHA_LOG_INFO(
            "PurgeUncommittedSpecs: Purged {} unsaved dynamic NPC spec(s) (released {} DPF slot(s))",
            prunedKeys.size(),
            releasedCount);
        return prunedKeys.size();
    }

    GarbageCollectionReport SynthesisCoordinator::RunGarbageCollection(bool includeCrossSaveScan) {
        GarbageCollectionReport report{};
        if (registryCorrupt_.load(std::memory_order_acquire)) {
            report.error = "DynamicNPCRegistry.json failed boot validation; GC disabled";
            return report;
        }

        const auto activeLinks = Serialization::GetActiveLinksSnapshot();
        std::unordered_set<std::string> activeSessionKeys;
        activeSessionKeys.reserve(activeLinks.size());
        for (const auto& link : activeLinks) {
            if (!link.slotKey.empty()) {
                activeSessionKeys.insert(link.slotKey);
            }
        }

        const auto candidateSaveDirs = includeCrossSaveScan
                                           ? Serialization::ResolveActiveSaveDirectories()
                                           : std::vector<std::filesystem::path>{};

        std::unordered_set<std::string> unregisteredSyncKeys;
        {
            std::lock_guard lock(registryMutex_);
            State::SaveDirectoryScanResult scan{};
            if (includeCrossSaveScan) {
                scan = State::ScanSaveDirectoriesForSlotKeys(candidateSaveDirs, /*requireCompanionEss=*/true);
                report.saveScanPerformed = true;
                report.scannedCoSaveCount = scan.scannedCoSaveCount;
                report.skippedOrphanCoSaveCount = scan.skippedOrphanCoSaveCount;
                report.matchedMorwhaSaveCount = scan.matchedMorwhaSaveCount;
                if (!scan.ok) {
                    MORWHA_LOG_WARN(
                        "RunGarbageCollection: Co-save scan incomplete ({}); skipping committed slot pruning",
                        scan.error);
                    report.error = scan.error;
                }
            }

            const bool pruneCommitted = includeCrossSaveScan && scan.canPruneCommitted;
            auto plan = State::PlanRegistryGarbageCollection(
                specs_,
                activeSessionKeys,
                scan.referencedSlotKeys,
                /*pruneUncommitted=*/true,
                pruneCommitted);

            if (plan.TotalPrunedCount() > 0) {
                std::string saveErr;
                if (!State::SaveRegistryAtomic(State::kDefaultRegistryPath, plan.survivingSpecs, &saveErr)) {
                    report.error = saveErr;
                    report.remainingSpecCount = specs_.size();
                    MORWHA_LOG_ERROR(
                        "RunGarbageCollection: Failed saving '{}' ({}); aborting slot release",
                        State::kDefaultRegistryPath,
                        saveErr);
                    return report;
                }
                specs_ = std::move(plan.survivingSpecs);
                if (specs_.empty()) {
                    nextSlotIndex_ = 1;
                }
            }

            report.prunedUncommittedCount = plan.prunedUncommittedKeys.size();
            report.prunedCommittedCount = plan.prunedCommittedKeys.size();
            report.remainingSpecCount = specs_.size();

            auto& dpf = Engine::DpfClient::GetSingleton();
            if (dpf.IsDpfAvailable()) {
                const bool zeroLiveAcrossAllSaves =
                    specs_.empty() &&
                    activeSessionKeys.empty() &&
                    pruneCommitted &&
                    scan.referencedSlotKeys.empty();
                if (zeroLiveAcrossAllSaves) {
                    report.releasedDpfSlotCount = dpf.ReleaseAllMorwhaSlots();
                } else {
                    for (const auto& key : plan.prunedUncommittedKeys) {
                        if (dpf.ReleaseNpcSlot(key)) {
                            ++report.releasedDpfSlotCount;
                        }
                    }
                    for (const auto& key : plan.prunedCommittedKeys) {
                        if (dpf.ReleaseNpcSlot(key)) {
                            ++report.releasedDpfSlotCount;
                        }
                    }
                }
            }

            unregisteredSyncKeys.insert(
                plan.prunedUncommittedKeys.begin(), plan.prunedUncommittedKeys.end());
            unregisteredSyncKeys.insert(
                plan.prunedCommittedKeys.begin(), plan.prunedCommittedKeys.end());
            report.ok = report.error.empty();
        }

        if (!unregisteredSyncKeys.empty()) {
            Engine::Mortal3dSyncService::GetSingleton().UnregisterHydratedNpcsBySlotKeys(
                unregisteredSyncKeys);
        }

        if (report.prunedUncommittedCount > 0 ||
            report.prunedCommittedCount > 0 ||
            report.releasedDpfSlotCount > 0) {
            MORWHA_LOG_INFO(
                "RunGarbageCollection: scannedCoSaves={} (orphanSkseSkipped={}, morwhaSaves={}), prunedUncommitted={}, prunedOrphaned={}, releasedDpfSlots={}, remainingSpecs={}",
                report.scannedCoSaveCount,
                report.skippedOrphanCoSaveCount,
                report.matchedMorwhaSaveCount,
                report.prunedUncommittedCount,
                report.prunedCommittedCount,
                report.releasedDpfSlotCount,
                report.remainingSpecCount);
        }

        return report;
    }

}  // namespace MORWHA::App
