#pragma once

#include "morwha/state/RegistryTypes.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <unordered_set>
#include <vector>

namespace MORWHA::State {

    inline constexpr std::uint32_t kSkseCoSaveSignature = 0x45534B53u;  // 'SKSE'
    inline constexpr std::uint32_t kSkseCoSaveFormatVersion = 1u;
    inline constexpr std::uintmax_t kMaxSkseCoSaveFileBytes = 256ull * 1024ull * 1024ull;

    struct SkseCoSaveParseResult {
        bool ok{ false };
        bool hasMorwhaPlugin{ false };
        std::unordered_set<std::string> slotKeys{};
        std::string error{};
    };

    struct SaveDirectoryScanResult {
        bool ok{ false };
        bool canPruneCommitted{ false };
        std::size_t existingDirectoryCount{ 0 };
        std::size_t scannedCoSaveCount{ 0 };
        std::size_t skippedOrphanCoSaveCount{ 0 };
        std::size_t matchedMorwhaSaveCount{ 0 };
        std::unordered_set<std::string> referencedSlotKeys{};
        std::string error{};
    };

    struct GarbageCollectionPlan {
        std::vector<DynamicNpcSpec> survivingSpecs{};
        std::vector<std::string> prunedUncommittedKeys{};
        std::vector<std::string> prunedCommittedKeys{};

        [[nodiscard]] std::size_t TotalPrunedCount() const noexcept {
            return prunedUncommittedKeys.size() + prunedCommittedKeys.size();
        }
    };

    /// Parses an in-memory SKSE64 .skse co-save buffer and extracts all MORWHA 'MRWH'/'LNKS' slotKeys.
    [[nodiscard]] SkseCoSaveParseResult ExtractSlotKeysFromSkseBytes(
        std::span<const std::uint8_t> bytes);

    /// Streams an SKSE64 .skse file from disk, seeking past non-'MRWH' plugins and decoding 'LNKS' slotKeys.
    [[nodiscard]] SkseCoSaveParseResult ExtractSlotKeysFromSkseFile(
        const std::filesystem::path& sksePath);

    /// Scans candidate save directories for .skse files (requiring companion .ess when enabled).
    /// Fails closed (ok = false, canPruneCommitted = false) if any paired .skse file cannot be read or parsed.
    [[nodiscard]] SaveDirectoryScanResult ScanSaveDirectoriesForSlotKeys(
        std::span<const std::filesystem::path> candidateDirs,
        bool requireCompanionEss = true);

    /// Expands root slotKeys transitively across DynamicNpcSpec::donorASlotKey and donorBSlotKey.
    [[nodiscard]] std::unordered_set<std::string> ExpandTransitiveDonorSlotKeys(
        std::span<const DynamicNpcSpec> specs,
        const std::unordered_set<std::string>& rootSlotKeys);

    /// Marks specs reachable from savedLinks (including transitive dynamic donors) as committed = true.
    /// Returns true iff at least one spec transitioned from uncommitted to committed.
    [[nodiscard]] bool MarkCommittedSpecsForLinks(
        std::vector<DynamicNpcSpec>& specs,
        std::span<const SpawnedMortalLink> savedLinks);

    /// Partitions registry specs into surviving vs pruned uncommitted/committed sets.
    [[nodiscard]] GarbageCollectionPlan PlanRegistryGarbageCollection(
        std::span<const DynamicNpcSpec> specs,
        const std::unordered_set<std::string>& activeSessionKeys,
        const std::unordered_set<std::string>& savedCoSaveKeys,
        bool pruneUncommitted,
        bool pruneUnreferencedCommitted);

}  // namespace MORWHA::State
