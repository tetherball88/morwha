#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace MORWHA::State {

    inline constexpr std::uint32_t kAccessoryManifestSchemaVersion = 1u;
    inline constexpr std::size_t kMaxAccessoryManifestEntries = 256u;
    inline constexpr std::string_view kDefaultChimeraAccessoriesDir =
        "Data/SKSE/Plugins/MORWHA/ChimeraAccessories";

    enum class ChimeraManifestLineage : std::uint8_t {
        HumanoidKhajiit = 1,
        HumanoidArgonian = 2,
        AnyChimera = 3
    };

    enum class ChimeraAccessorySlotKind : std::uint8_t {
        HeadPart = 1,
        SkinArmorAddon = 2
    };

    enum class ChimeraAccessorySexFilter : std::uint8_t {
        Any = 0,
        Male = 1,
        Female = 2
    };

    struct ChimeraAccessoryEntrySpec {
        ChimeraManifestLineage lineage{ ChimeraManifestLineage::AnyChimera };
        ChimeraAccessorySlotKind slotKind{ ChimeraAccessorySlotKind::HeadPart };
        ChimeraAccessorySexFilter sex{ ChimeraAccessorySexFilter::Any };
        std::uint32_t localId{ 0 };

        bool operator==(const ChimeraAccessoryEntrySpec&) const noexcept = default;
    };

    struct ChimeraAccessoryManifest {
        std::string plugin{};
        bool requiresSkee64{ false };
        std::vector<ChimeraAccessoryEntrySpec> entries{};

        bool operator==(const ChimeraAccessoryManifest&) const noexcept = default;
    };

    [[nodiscard]] bool ParseChimeraAccessoryManifestJson(
        std::string_view jsonText,
        ChimeraAccessoryManifest& outManifest,
        std::string* outError = nullptr);

    [[nodiscard]] std::vector<ChimeraAccessoryManifest> LoadChimeraAccessoryManifestsFromDirectory(
        const std::filesystem::path& dirPath,
        std::size_t* outFilesScanned = nullptr,
        std::size_t* outFilesFailed = nullptr);

}  // namespace MORWHA::State
