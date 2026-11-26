#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace MORWHA::State {

    inline constexpr std::string_view kDefaultMorwhaIniPath = "Data/SKSE/Plugins/MORWHA/MORWHA.ini";

    enum class IniChimeraTier : std::uint8_t {
        Disabled = 0,
        Tier1DominantMorph = 1,
        Tier2EquippedParts = 2
    };

    enum class GeneratedSkinResolution : std::uint8_t {
        Auto = 0,
        Res1024 = 1,
        Res2048 = 2,
        Res4096 = 3
    };

    [[nodiscard]] constexpr std::uint32_t MaxDimensionForSkinResolution(
        GeneratedSkinResolution mode) noexcept {
        switch (mode) {
        case GeneratedSkinResolution::Res1024:
            return 1024u;
        case GeneratedSkinResolution::Res2048:
            return 2048u;
        case GeneratedSkinResolution::Auto:
        case GeneratedSkinResolution::Res4096:
            return 4096u;
        }
        return 4096u;
    }

    [[nodiscard]] constexpr std::string_view GeneratedSkinResolutionToString(
        GeneratedSkinResolution mode) noexcept {
        switch (mode) {
        case GeneratedSkinResolution::Auto:
            return "auto";
        case GeneratedSkinResolution::Res1024:
            return "1k";
        case GeneratedSkinResolution::Res2048:
            return "2k";
        case GeneratedSkinResolution::Res4096:
            return "4k";
        }
        return "auto";
    }

    struct ChimeraLineageFeatureConfig {
        bool enableEyes{ true };
        bool enableHeadFeature{ true };  // bEnableEars (Khajiit) / bEnableHorns (Argonian)
        bool enableTeeth{ true };
        bool enableTails{ true };
        bool enableHybridSkin{ true };
        bool matchTailColor{ true };
        bool matchHornColor{ true };

        [[nodiscard]] bool HasAnyEnabledFeature() const noexcept;

        bool operator==(const ChimeraLineageFeatureConfig&) const noexcept = default;
    };

    [[nodiscard]] ChimeraLineageFeatureConfig MakeDefaultKhajiitTier2Config();
    [[nodiscard]] ChimeraLineageFeatureConfig MakeDefaultArgonianTier2Config();

    struct MorwhaIniSettings {
        bool allowSupernaturalEyes{ false };
        bool allowScars{ true };
        float offspringScarChance{ 0.02f };
        float defaultScarChance{ 0.04f };
        IniChimeraTier beastChimeraTier{ IniChimeraTier::Tier2EquippedParts };
        bool enableGeneratedHybridSkins{ true };
        GeneratedSkinResolution generatedSkinResolution{ GeneratedSkinResolution::Auto };
        ChimeraLineageFeatureConfig khajiitTier2{ MakeDefaultKhajiitTier2Config() };
        ChimeraLineageFeatureConfig argonianTier2{ MakeDefaultArgonianTier2Config() };

        bool operator==(const MorwhaIniSettings&) const noexcept = default;
    };

    struct IniLoadResult {
        bool ok{ false };
        bool createdDefault{ false };
        std::size_t warningCount{ 0 };
        MorwhaIniSettings settings{};
        std::string error{};
    };

    [[nodiscard]] std::string GenerateDefaultMorwhaIniText();

    [[nodiscard]] bool ParseMorwhaIniText(
        std::string_view iniText,
        MorwhaIniSettings& outSettings,
        std::size_t* outWarningCount = nullptr);

    [[nodiscard]] IniLoadResult LoadOrCreateMorwhaIni(
        const std::filesystem::path& iniPath = kDefaultMorwhaIniPath);

}  // namespace MORWHA::State
