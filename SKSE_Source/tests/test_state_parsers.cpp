#include "TestHarness.h"

#include "morwha/state/FaceAnchorPreset.h"

#include <cmath>
#include <filesystem>
#include <string>

namespace {

    using MORWHA::Foundation::FacePartSlot;
    using MORWHA::Foundation::kUnusedFacePartPreset;
    using MORWHA::Foundation::TopologyPool;
    using MORWHA::State::FaceAnchorDirLoadStats;
    using MORWHA::State::FaceAnchorPreset;
    using MORWHA::State::LoadFaceAnchorsFromDirectory;
    using MORWHA::State::ParseJslotAnchorJson;
    using MORWHA::State::ParsePipeFormIdentifier;

    [[nodiscard]] bool ApproxEqual(float a, float b, float eps = 1.0e-5f) noexcept {
        return std::abs(a - b) <= eps;
    }

    [[nodiscard]] std::filesystem::path FindStarterAnchorsDir() {
        std::error_code ec;
        std::filesystem::path cursor = std::filesystem::current_path(ec);
        for (int depth = 0; depth < 8 && !cursor.empty(); ++depth) {
            const auto candidate = cursor / "SKSE" / "Plugins" / "MORWHA" / "FaceAnchors";
            if (std::filesystem::exists(candidate, ec) && std::filesystem::is_directory(candidate, ec)) {
                return candidate;
            }
            cursor = cursor.parent_path();
        }
        return {};
    }

}  // namespace

MORWHA_TEST(FaceAnchorPreset, ParseRealRaceMenuJslotWithSentinels) {
    const std::string jsonWithBom =
        "\xEF\xBB\xBF{"
        "\"actor\":{\"weight\":62.5,\"headTexture\":\"Skyrim.esm|051500\",\"hairColor\":4282921250},"
        "\"tintInfo\":["
        "{\"texture\":\"Actors\\\\Character\\\\Character Assets\\\\TintMasks\\\\LipColor.dds\",\"color\":4289000000},"
        "{\"texture\":\"Actors\\\\Character\\\\Character Assets\\\\TintMasks\\\\SkinTone.dds\",\"color\":4291340424}"
        "],"
        "\"headParts\":["
        "{\"formIdentifier\":\"High Poly Head.esm|000800\"},"
        "{\"formIdentifier\":\"KS Hairdos.esm|012ABC\"}"
        "],"
        "\"morphs\":{\"sculpt\":[],\"default\":{"
        "\"morphs\":[-0.25,3.402823466385289E+38,1.45,0.1,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.42],"
        "\"presets\":[4,4294967295,7,2]"
        "}}"
        "}";

    FaceAnchorPreset preset{};
    std::string err;
    MORWHA_REQUIRE(ParseJslotAnchorJson(jsonWithBom, preset, &err, true));
    MORWHA_CHECK(!preset.hasSculptData);
    MORWHA_CHECK(preset.authoredTopology == TopologyPool::HighPolyHead);
    MORWHA_CHECK(ApproxEqual(preset.faceMorphs.morphs[0], -0.25f));
    MORWHA_CHECK(ApproxEqual(preset.faceMorphs.morphs[1], 0.0f));
    MORWHA_CHECK(ApproxEqual(preset.faceMorphs.morphs[2], 0.75f));
    MORWHA_CHECK(ApproxEqual(preset.faceMorphs.morphs[18], 0.0f));
    MORWHA_CHECK_EQ(preset.faceMorphs.parts[static_cast<std::size_t>(FacePartSlot::Nose)], 4);
    MORWHA_CHECK_EQ(preset.faceMorphs.parts[static_cast<std::size_t>(FacePartSlot::Unknown)], kUnusedFacePartPreset);
    MORWHA_CHECK_EQ(preset.faceMorphs.parts[static_cast<std::size_t>(FacePartSlot::Eyes)], 7);
    MORWHA_CHECK_EQ(preset.faceMorphs.parts[static_cast<std::size_t>(FacePartSlot::Mouth)], 2);
}

MORWHA_TEST(FaceAnchorPreset, RejectSculptedMalformedAndLoadStarterAnchors) {
    const std::string sculpted =
        "{\"morphs\":{\"sculpt\":[{\"vertices\":[1,2,3]}],\"default\":{\"morphs\":[0.1,0.2,0.0]}}}";
    FaceAnchorPreset preset{};
    std::string err;
    MORWHA_CHECK(!ParseJslotAnchorJson(sculpted, preset, &err, true));
    MORWHA_CHECK(ParseJslotAnchorJson(sculpted, preset, &err, false));
    MORWHA_CHECK(!ParsePipeFormIdentifier("NoPipeHere").has_value());

    const auto anchorsDir = FindStarterAnchorsDir();
    MORWHA_REQUIRE(!anchorsDir.empty());
    FaceAnchorDirLoadStats stats{};
    const auto presets = LoadFaceAnchorsFromDirectory(anchorsDir, &stats, true);
    MORWHA_CHECK_EQ(stats.presetsLoaded, 8u);
    MORWHA_CHECK_EQ(presets.size(), 8u);
}
