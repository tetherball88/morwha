#pragma once

#include "morwha/foundation/DomainTypes.h"
#include "morwha/state/RegistryTypes.h"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace MORWHA::State {

    inline constexpr std::string_view kDefaultFaceAnchorsDir = "Data/SKSE/Plugins/MORWHA/FaceAnchors";

    struct FaceAnchorPreset {
        std::string anchorId{};
        std::string raceKey{};
        Foundation::Sex sex{ Foundation::Sex::Female };
        Foundation::TopologyPool authoredTopology{ Foundation::TopologyPool::Vanilla };
        Foundation::FaceMorphVector faceMorphs{};
        std::optional<float> weight{};
        PluginFormRef headTextureRef{};
        std::optional<Foundation::RgbColor> hairColor{};
        std::optional<Foundation::RgbColor> skinColor{};
        std::vector<PluginFormRef> headParts{};
        bool hasSculptData{ false };
    };

    struct FaceAnchorDirLoadStats {
        std::size_t filesScanned{ 0 };
        std::size_t presetsLoaded{ 0 };
        std::size_t skippedSculpted{ 0 };
        std::size_t failedFiles{ 0 };
    };

    /// Parses RaceMenu "Plugin.esm|03B522" or "Plugin.esp|0x000A06" strings into PluginFormRef.
    [[nodiscard]] std::optional<PluginFormRef> ParsePipeFormIdentifier(std::string_view identifier);

    /// Parses a RaceMenu .jslot preset JSON payload into FaceAnchorPreset (clamping sliders and
    /// sanitizing RaceMenu's 3.4e38 FLT_MAX sentinels). Rejects sculpted presets when rejectSculpted=true.
    [[nodiscard]] bool ParseJslotAnchorJson(
        std::string_view jsonText,
        FaceAnchorPreset& outPreset,
        std::string* outError = nullptr,
        bool rejectSculpted = true);

    /// Scans <rootDir>/<raceDir>/<sexDir>/*.jslot deterministically in lexicographical order.
    [[nodiscard]] std::vector<FaceAnchorPreset> LoadFaceAnchorsFromDirectory(
        const std::filesystem::path& rootDir,
        FaceAnchorDirLoadStats* outStats = nullptr,
        bool rejectSculpted = true);

}  // namespace MORWHA::State
