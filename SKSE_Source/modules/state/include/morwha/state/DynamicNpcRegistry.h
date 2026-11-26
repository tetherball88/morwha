#pragma once

#include "morwha/state/RegistryTypes.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace MORWHA::State {

    inline constexpr std::uint32_t kRegistrySchemaVersion = 1u;
    inline constexpr std::size_t kMaxRegistryNpcs = 65536u;
    inline constexpr std::size_t kMaxHeadPartsPerNpc = 32u;
    inline constexpr std::size_t kMaxTintLayersPerNpc = 64u;
    inline constexpr std::size_t kMaxSkinAddonsPerNpc = 8u;
    inline constexpr std::size_t kMaxPerksPerNpc = 64u;
    inline constexpr std::size_t kMaxSpellsPerNpc = 64u;
    inline constexpr std::size_t kMaxFactionsPerNpc = 32u;
    inline constexpr std::string_view kDefaultRegistryPath =
        "Data/SKSE/Plugins/MORWHA/DynamicNPCRegistry.json";

    struct RegistryLoadResult {
        bool ok{ false };
        bool fileExisted{ false };
        bool recoveredFromBackup{ false };
        std::vector<DynamicNpcSpec> specs{};
        std::string error{};
    };

    /// Validates all structural, bounds, finiteness, and PluginFormRef invariants of a DynamicNpcSpec.
    [[nodiscard]] bool ValidateDynamicNpcSpec(
        const DynamicNpcSpec& spec,
        std::string* outError = nullptr);

    /// Deterministically serializes registry entries into schema v1 JSON using locale-free formatting.
    [[nodiscard]] std::string SerializeRegistryJson(std::span<const DynamicNpcSpec> specs);

    /// Parses and validates schema v1 JSON into outSpecs; clears outSpecs and fails on malformed input or duplicates.
    [[nodiscard]] bool ParseRegistryJson(
        std::string_view jsonText,
        std::vector<DynamicNpcSpec>& outSpecs,
        std::string* outError = nullptr);

    /// Writes specs to <target>.tmp, backs up existing <target> to <target>.bak, and atomically swaps via rename.
    [[nodiscard]] bool SaveRegistryAtomic(
        const std::filesystem::path& targetPath,
        std::span<const DynamicNpcSpec> specs,
        std::string* outError = nullptr);

    /// Loads <target>, automatically falling back to <target>.bak if primary is missing or corrupt.
    [[nodiscard]] RegistryLoadResult LoadRegistrySafe(const std::filesystem::path& targetPath);

}  // namespace MORWHA::State
