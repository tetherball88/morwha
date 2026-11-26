#include "TestHarness.h"

#include "morwha/state/ChimeraAccessoryManifest.h"
#include "morwha/state/MorwhaIniConfig.h"

#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

    using MORWHA::State::ChimeraAccessoryManifest;
    using MORWHA::State::GeneratedSkinResolution;
    using MORWHA::State::GenerateDefaultMorwhaIniText;
    using MORWHA::State::IniChimeraTier;
    using MORWHA::State::LoadChimeraAccessoryManifestsFromDirectory;
    using MORWHA::State::LoadOrCreateMorwhaIni;
    using MORWHA::State::MorwhaIniSettings;
    using MORWHA::State::ParseChimeraAccessoryManifestJson;
    using MORWHA::State::ParseMorwhaIniText;

}  // namespace

MORWHA_TEST(MorwhaIniConfig, DefaultGenerationAndRoundtrip) {
    const std::string iniText = GenerateDefaultMorwhaIniText();
    MorwhaIniSettings parsed{};
    std::size_t warnings = 99u;
    MORWHA_REQUIRE(ParseMorwhaIniText(iniText, parsed, &warnings));
    MORWHA_CHECK_EQ(warnings, std::size_t{ 0u });
    MORWHA_CHECK(parsed == MorwhaIniSettings{});
    MORWHA_CHECK(parsed.beastChimeraTier == IniChimeraTier::Tier2EquippedParts);
    MORWHA_CHECK(parsed.enableGeneratedHybridSkins);
    MORWHA_CHECK(parsed.generatedSkinResolution == GeneratedSkinResolution::Auto);
    MORWHA_CHECK(parsed.khajiitTier2.enableHybridSkin);
    MORWHA_CHECK(parsed.argonianTier2.enableHybridSkin);
    MORWHA_CHECK(parsed.khajiitTier2.matchTailColor);
    MORWHA_CHECK(parsed.argonianTier2.matchTailColor);
    MORWHA_CHECK(parsed.argonianTier2.matchHornColor);
}

MORWHA_TEST(MorwhaIniConfig, CustomTogglesCommentsAndBom) {
    constexpr std::string_view kCustomIni =
        "\xEF\xBB\xBF"
        "[General]\n"
        "bAllowSupernaturalEyes = true ; comment\n"
        "bAllowScars = false\n"
        "fOffspringScarChance = 0.25\n"
        "fDefaultScarChance = 2.50\n"
        "[Chimeras]\n"
        "sBeastChimeraTier = tier2 # trailing hash comment\n"
        "bEnableGeneratedHybridSkins = false\n"
        "sGeneratedSkinResolution = 2048\n"
        "[ChimeraTier2.Khajiit]\n"
        "bEnableEyes = false\n"
        "bEnableEars = false\n"
        "bEnableTeeth = false\n"
        "bEnableTails = true\n"
        "bMatchTailColor = false\n"
        "bEnableHybridSkin = true\n"
        "[ChimeraTier2.Argonian]\n"
        "bEnableHorns = false\n"
        "bMatchHornColor = false\n"
        "bMatchTailColor = false\n"
        "bEnableHybridSkin = false\n";

    MorwhaIniSettings parsed{};
    std::size_t warnings = 99u;
    MORWHA_REQUIRE(ParseMorwhaIniText(kCustomIni, parsed, &warnings));
    MORWHA_CHECK_EQ(warnings, std::size_t{ 0u });
    MORWHA_CHECK(parsed.allowSupernaturalEyes);
    MORWHA_CHECK(!parsed.allowScars);
    MORWHA_CHECK(std::abs(parsed.offspringScarChance - 0.25f) < 1e-4f);
    MORWHA_CHECK(std::abs(parsed.defaultScarChance - 1.0f) < 1e-4f);
    MORWHA_CHECK(parsed.beastChimeraTier == IniChimeraTier::Tier2EquippedParts);
    MORWHA_CHECK(!parsed.enableGeneratedHybridSkins);
    MORWHA_CHECK(parsed.generatedSkinResolution == GeneratedSkinResolution::Res2048);

    MorwhaIniSettings resCfg{};
    std::size_t resWarn = 99u;
    MORWHA_REQUIRE(ParseMorwhaIniText("[Chimeras]\nsGeneratedSkinResolution = 4K\n", resCfg, &resWarn));
    MORWHA_CHECK_EQ(resWarn, std::size_t{ 0u });
    MORWHA_CHECK(resCfg.generatedSkinResolution == GeneratedSkinResolution::Res4096);
    MORWHA_REQUIRE(ParseMorwhaIniText("[Chimeras]\nsGeneratedSkinResolution = 1k\n", resCfg, &resWarn));
    MORWHA_CHECK(resCfg.generatedSkinResolution == GeneratedSkinResolution::Res1024);
    MORWHA_REQUIRE(ParseMorwhaIniText("[Chimeras]\nsGeneratedSkinResolution = auto\n", resCfg, &resWarn));
    MORWHA_CHECK(resCfg.generatedSkinResolution == GeneratedSkinResolution::Auto);

    MORWHA_CHECK(!parsed.khajiitTier2.enableEyes);
    MORWHA_CHECK(!parsed.khajiitTier2.enableHeadFeature);
    MORWHA_CHECK(!parsed.khajiitTier2.enableTeeth);
    MORWHA_CHECK(parsed.khajiitTier2.enableTails);
    MORWHA_CHECK(!parsed.khajiitTier2.matchTailColor);
    MORWHA_CHECK(parsed.khajiitTier2.enableHybridSkin);
    MORWHA_CHECK(parsed.khajiitTier2.HasAnyEnabledFeature());

    MORWHA_CHECK(!parsed.argonianTier2.enableHeadFeature);
    MORWHA_CHECK(!parsed.argonianTier2.matchHornColor);
    MORWHA_CHECK(!parsed.argonianTier2.matchTailColor);
    MORWHA_CHECK(!parsed.argonianTier2.enableHybridSkin);

    MorwhaIniSettings allOffCfg{};
    MORWHA_REQUIRE(ParseMorwhaIniText(
        "[ChimeraTier2.Khajiit]\n"
        "bEnableEyes=0\nbEnableEars=0\nbEnableTeeth=0\nbEnableTails=0\nbEnableHybridSkin=0\n",
        allOffCfg));
    MORWHA_CHECK(!allOffCfg.khajiitTier2.HasAnyEnabledFeature());
}

MORWHA_TEST(MorwhaIniConfig, AutoCreateAndPreserveExistingFile) {
    const auto stamp = static_cast<unsigned long long>(
        std::chrono::steady_clock::now().time_since_epoch().count());
    const auto tempRoot = std::filesystem::temp_directory_path() /
                          ("morwha_ini_test_" + std::to_string(stamp));
    const auto iniPath = tempRoot / "SKSE/Plugins/MORWHA/MORWHA.ini";
    const auto cleanup = [&]() {
        std::error_code ec;
        std::filesystem::remove_all(tempRoot, ec);
    };

    const auto firstLoad = LoadOrCreateMorwhaIni(iniPath);
    MORWHA_REQUIRE(firstLoad.ok);
    MORWHA_CHECK(firstLoad.createdDefault);
    MORWHA_CHECK(std::filesystem::exists(iniPath));

    {
        std::ofstream edit(iniPath, std::ios::binary | std::ios::app);
        edit << "\n[ChimeraTier2.Khajiit]\nbEnableTeeth = false\nbEnableHybridSkin = false\n";
    }

    const auto secondLoad = LoadOrCreateMorwhaIni(iniPath);
    MORWHA_REQUIRE(secondLoad.ok);
    MORWHA_CHECK(!secondLoad.createdDefault);
    MORWHA_CHECK(!secondLoad.settings.khajiitTier2.enableTeeth);
    MORWHA_CHECK(!secondLoad.settings.khajiitTier2.enableHybridSkin);

    cleanup();
}

MORWHA_TEST(MorwhaIniConfig, ShippedAccessoryManifestsValid) {
    ChimeraAccessoryManifest rejected{};
    std::string err;
    MORWHA_CHECK(!ParseChimeraAccessoryManifestJson(
        R"({"schemaVersion":1,"plugin":"x.esp"})",
        rejected,
        &err));

    const auto shippedDir =
        std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() /
        "SKSE/Plugins/MORWHA/ChimeraAccessories";
    if (std::filesystem::exists(shippedDir)) {
        std::size_t scanned = 0u;
        std::size_t failed = 0u;
        const auto manifests =
            LoadChimeraAccessoryManifestsFromDirectory(shippedDir, &scanned, &failed);
        MORWHA_CHECK_EQ(scanned, std::size_t{ 4u });
        MORWHA_CHECK_EQ(failed, std::size_t{ 0u });
        MORWHA_CHECK_EQ(manifests.size(), std::size_t{ 4u });
        for (const auto& m : manifests) {
            MORWHA_CHECK(!m.entries.empty());
        }
    }
}
