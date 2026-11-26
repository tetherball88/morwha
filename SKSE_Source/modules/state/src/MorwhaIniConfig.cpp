#include "morwha/state/MorwhaIniConfig.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <format>
#include <fstream>
#include <system_error>

namespace MORWHA::State {

    namespace {

        constexpr std::size_t kMaxIniFileBytes = 1024ull * 1024ull;

        [[nodiscard]] bool EqualsIgnoreCase(std::string_view a, std::string_view b) noexcept {
            if (a.size() != b.size()) {
                return false;
            }
            for (std::size_t i = 0; i < a.size(); ++i) {
                if (std::tolower(static_cast<unsigned char>(a[i])) !=
                    std::tolower(static_cast<unsigned char>(b[i]))) {
                    return false;
                }
            }
            return true;
        }

        [[nodiscard]] std::string_view TrimAscii(std::string_view sv) noexcept {
            while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.front())) != 0) {
                sv.remove_prefix(1u);
            }
            while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.back())) != 0) {
                sv.remove_suffix(1u);
            }
            return sv;
        }

        [[nodiscard]] std::string_view UnquoteTrimmed(std::string_view sv) noexcept {
            sv = TrimAscii(sv);
            if (sv.size() >= 2u &&
                ((sv.front() == '"' && sv.back() == '"') ||
                 (sv.front() == '\'' && sv.back() == '\''))) {
                sv.remove_prefix(1u);
                sv.remove_suffix(1u);
                sv = TrimAscii(sv);
            }
            return sv;
        }

        [[nodiscard]] std::string_view StripInlineCommentRespectingQuotes(
            std::string_view rawVal) noexcept {
            char activeQuote = '\0';
            for (std::size_t i = 0; i < rawVal.size(); ++i) {
                const char ch = rawVal[i];
                if (activeQuote != '\0') {
                    if (ch == activeQuote) {
                        activeQuote = '\0';
                    }
                    continue;
                }
                if (ch == '"' || ch == '\'') {
                    activeQuote = ch;
                    continue;
                }
                if (ch == ';' || ch == '#') {
                    return rawVal.substr(0u, i);
                }
            }
            return rawVal;
        }

        [[nodiscard]] bool TryParseBool(std::string_view token, bool& outVal) noexcept {
            const std::string_view clean = UnquoteTrimmed(token);
            if (EqualsIgnoreCase(clean, "true") || EqualsIgnoreCase(clean, "1") ||
                EqualsIgnoreCase(clean, "on") || EqualsIgnoreCase(clean, "yes") ||
                EqualsIgnoreCase(clean, "enable") || EqualsIgnoreCase(clean, "enabled")) {
                outVal = true;
                return true;
            }
            if (EqualsIgnoreCase(clean, "false") || EqualsIgnoreCase(clean, "0") ||
                EqualsIgnoreCase(clean, "off") || EqualsIgnoreCase(clean, "no") ||
                EqualsIgnoreCase(clean, "disable") || EqualsIgnoreCase(clean, "disabled")) {
                outVal = false;
                return true;
            }
            return false;
        }

        [[nodiscard]] bool TryParseUnitFloat(std::string_view token, float& outVal) noexcept {
            const std::string_view clean = UnquoteTrimmed(token);
            if (clean.empty()) {
                return false;
            }
            float parsed = 0.0f;
            const auto [ptr, ec] =
                std::from_chars(clean.data(), clean.data() + clean.size(), parsed);
            if (ec != std::errc{} || ptr != clean.data() + clean.size() ||
                !std::isfinite(parsed)) {
                return false;
            }
            outVal = std::clamp(parsed, 0.0f, 1.0f);
            return true;
        }

        [[nodiscard]] bool TryParseChimeraTier(
            std::string_view token,
            IniChimeraTier& outTier) noexcept {
            const std::string_view clean = UnquoteTrimmed(token);
            if (EqualsIgnoreCase(clean, "0") || EqualsIgnoreCase(clean, "off") ||
                EqualsIgnoreCase(clean, "disabled") || EqualsIgnoreCase(clean, "disable") ||
                EqualsIgnoreCase(clean, "none") || EqualsIgnoreCase(clean, "false")) {
                outTier = IniChimeraTier::Disabled;
                return true;
            }
            if (EqualsIgnoreCase(clean, "1") || EqualsIgnoreCase(clean, "tier1") ||
                EqualsIgnoreCase(clean, "t1") || EqualsIgnoreCase(clean, "on") ||
                EqualsIgnoreCase(clean, "true")) {
                outTier = IniChimeraTier::Tier1DominantMorph;
                return true;
            }
            if (EqualsIgnoreCase(clean, "2") || EqualsIgnoreCase(clean, "tier2") ||
                EqualsIgnoreCase(clean, "t2")) {
                outTier = IniChimeraTier::Tier2EquippedParts;
                return true;
            }
            return false;
        }

        [[nodiscard]] bool TryParseGeneratedSkinResolution(
            std::string_view token,
            GeneratedSkinResolution& outMode) noexcept {
            const std::string_view clean = UnquoteTrimmed(token);
            if (EqualsIgnoreCase(clean, "auto") || EqualsIgnoreCase(clean, "native") ||
                EqualsIgnoreCase(clean, "0")) {
                outMode = GeneratedSkinResolution::Auto;
                return true;
            }
            if (EqualsIgnoreCase(clean, "1k") || EqualsIgnoreCase(clean, "1024")) {
                outMode = GeneratedSkinResolution::Res1024;
                return true;
            }
            if (EqualsIgnoreCase(clean, "2k") || EqualsIgnoreCase(clean, "2048")) {
                outMode = GeneratedSkinResolution::Res2048;
                return true;
            }
            if (EqualsIgnoreCase(clean, "4k") || EqualsIgnoreCase(clean, "4096")) {
                outMode = GeneratedSkinResolution::Res4096;
                return true;
            }
            return false;
        }

        [[nodiscard]] bool ApplyLineageSetting(
            std::string_view key,
            std::string_view value,
            bool isKhajiitSection,
            ChimeraLineageFeatureConfig& cfg) {
            if (EqualsIgnoreCase(key, "bEnableEyes")) {
                return TryParseBool(value, cfg.enableEyes);
            }
            if ((isKhajiitSection && EqualsIgnoreCase(key, "bEnableEars")) ||
                (!isKhajiitSection && EqualsIgnoreCase(key, "bEnableHorns"))) {
                return TryParseBool(value, cfg.enableHeadFeature);
            }
            if (EqualsIgnoreCase(key, "bEnableTeeth")) {
                return TryParseBool(value, cfg.enableTeeth);
            }
            if (EqualsIgnoreCase(key, "bEnableTails")) {
                return TryParseBool(value, cfg.enableTails);
            }
            if (EqualsIgnoreCase(key, "bMatchTailColor")) {
                return TryParseBool(value, cfg.matchTailColor);
            }
            if (!isKhajiitSection && EqualsIgnoreCase(key, "bMatchHornColor")) {
                return TryParseBool(value, cfg.matchHornColor);
            }
            if (EqualsIgnoreCase(key, "bEnableHybridSkin")) {
                return TryParseBool(value, cfg.enableHybridSkin);
            }
            return false;
        }

    }  // namespace

    bool ChimeraLineageFeatureConfig::HasAnyEnabledFeature() const noexcept {
        return enableEyes || enableHeadFeature || enableTeeth || enableTails || enableHybridSkin;
    }

    ChimeraLineageFeatureConfig MakeDefaultKhajiitTier2Config() {
        return ChimeraLineageFeatureConfig{};
    }

    ChimeraLineageFeatureConfig MakeDefaultArgonianTier2Config() {
        return ChimeraLineageFeatureConfig{};
    }

    std::string GenerateDefaultMorwhaIniText() {
        return
            "; ============================================================================\n"
            "; M.O.R.W.H.A Configuration (Data/SKSE/Plugins/MORWHA/MORWHA.ini)\n"
            ";\n"
            "; Auto-generated in MO2 Overwrite when no mod provides MORWHA.ini.\n"
            "; Reload edits live in Skyrim's console with: mspawn reload\n"
            "; ============================================================================\n"
            "\n"
            "[General]\n"
            "; Allow emissive / glowing / supernatural eyes during synthesis (default: false)\n"
            "bAllowSupernaturalEyes = false\n"
            "; Allow facial scar overlays during synthesis (default: true)\n"
            "bAllowScars = true\n"
            "; Scar roll probability for 2-donor biological offspring [0.0..1.0]\n"
            "fOffspringScarChance = 0.02\n"
            "; Scar roll probability for 0-donor procedural / 1-donor relatives [0.0..1.0]\n"
            "fDefaultScarChance = 0.04\n"
            "\n"
            "[Chimeras]\n"
            "; Humanoid/Mer x Beast & Beast-Cross chimera mode: off | tier1 | tier2\n"
            "; tier2 automatically cascades tier2 -> tier1 per lineage if hybrid skins/accessories are disabled.\n"
            "sBeastChimeraTier = tier2\n"
            "; Generate & cache 4-map (diffuse + _msn 3D normals + _s + _sk) hybrid skin sets over your installed skin mod\n"
            "bEnableGeneratedHybridSkins = true\n"
            "; Output resolution cap for generated hybrid skins: auto | 1k | 2k | 4k\n"
            "; Never upscales: 'auto' matches each installed skin map's native resolution (1K / 2K / 4K);\n"
            "; '1k'/'2k'/'4k' downscales higher-res skin maps to the cap while keeping lower-res maps as-is.\n"
            "sGeneratedSkinResolution = auto\n"
            "\n"
            "[ChimeraTier2.Khajiit]\n"
            "; Vertical-slit beast eyes on Humanoid x Khajiit chimeras (false = normal humanoid eyes)\n"
            "bEnableEyes = true\n"
            "; Cat ears headpart (ChooeyDintEarsEdit.esp); disabling keeps Tier 2 active without requiring ears\n"
            "bEnableEars = true\n"
            "; Sharp beast teeth (BnP RSV Sharp / Sharpened Teeth)\n"
            "bEnableTeeth = true\n"
            "; Cloned Khajiit tail on TESNPC::skin\n"
            "bEnableTails = true\n"
            "; Dynamically tint tail mesh to match hairColor (false = default skin tint)\n"
            "bMatchTailColor = true\n"
            "; Apply 4-map .mrst hybrid fur skin sets (when bEnableGeneratedHybridSkins = true)\n"
            "bEnableHybridSkin = true\n"
            "\n"
            "[ChimeraTier2.Argonian]\n"
            "; Vertical-slit beast eyes on Humanoid x Argonian chimeras (false = normal humanoid eyes)\n"
            "bEnableEyes = true\n"
            "; Reptilian/draconic horns (NK_HornSlider.esp / PraedysHorns.esp); disabling keeps Tier 2 active without requiring horns\n"
            "bEnableHorns = true\n"
            "; Dynamically tint horns to match Argonian parent's horn or secondary color\n"
            "bMatchHornColor = true\n"
            "; Sharp beast teeth (BnP RSV Sharp / Sharpened Teeth)\n"
            "bEnableTeeth = true\n"
            "; Cloned Argonian tail on TESNPC::skin\n"
            "bEnableTails = true\n"
            "; Dynamically tint tail mesh to match Argonian parent's main scale color (false = default skin tint)\n"
            "bMatchTailColor = true\n"
            "; Apply 4-map .mrst hybrid lizard/osteoderm skin sets (when bEnableGeneratedHybridSkins = true)\n"
            "bEnableHybridSkin = true\n";
    }

    bool ParseMorwhaIniText(
        std::string_view iniText,
        MorwhaIniSettings& outSettings,
        std::size_t* outWarningCount) {
        outSettings = MorwhaIniSettings{};
        std::size_t warnings = 0u;

        // Strip UTF-8 BOM if present.
        if (iniText.size() >= 3u &&
            static_cast<unsigned char>(iniText[0]) == 0xEFu &&
            static_cast<unsigned char>(iniText[1]) == 0xBBu &&
            static_cast<unsigned char>(iniText[2]) == 0xBFu) {
            iniText.remove_prefix(3u);
        }

        enum class IniSection : std::uint8_t {
            None = 0,
            General,
            Chimeras,
            ChimeraTier2Khajiit,
            ChimeraTier2Argonian,
            Unknown
        };

        IniSection currentSection = IniSection::None;

        std::size_t cursor = 0u;
        while (cursor <= iniText.size()) {
            const std::size_t nlPos = iniText.find('\n', cursor);
            const std::size_t len =
                (nlPos == std::string_view::npos) ? (iniText.size() - cursor) : (nlPos - cursor);
            std::string_view line = iniText.substr(cursor, len);
            if (!line.empty() && line.back() == '\r') {
                line.remove_suffix(1u);
            }
            line = TrimAscii(line);

            const bool atEnd = (nlPos == std::string_view::npos);
            cursor = atEnd ? iniText.size() : (nlPos + 1u);

            if (line.empty() || line.front() == ';' || line.front() == '#') {
                if (atEnd) {
                    break;
                }
                continue;
            }

            if (line.front() == '[') {
                const std::size_t closePos = line.find(']');
                if (closePos == std::string_view::npos) {
                    ++warnings;
                    currentSection = IniSection::Unknown;
                } else {
                    const std::string_view secName = TrimAscii(line.substr(1u, closePos - 1u));
                    const std::string_view trailing = TrimAscii(line.substr(closePos + 1u));
                    if (!trailing.empty() && trailing.front() != ';' && trailing.front() != '#') {
                        ++warnings;
                    }
                    if (EqualsIgnoreCase(secName, "General")) {
                        currentSection = IniSection::General;
                    } else if (EqualsIgnoreCase(secName, "Chimeras")) {
                        currentSection = IniSection::Chimeras;
                    } else if (EqualsIgnoreCase(secName, "ChimeraTier2.Khajiit")) {
                        currentSection = IniSection::ChimeraTier2Khajiit;
                    } else if (EqualsIgnoreCase(secName, "ChimeraTier2.Argonian")) {
                        currentSection = IniSection::ChimeraTier2Argonian;
                    } else {
                        ++warnings;
                        currentSection = IniSection::Unknown;
                    }
                }
                if (atEnd) {
                    break;
                }
                continue;
            }

            const std::size_t eqPos = line.find('=');
            if (eqPos == std::string_view::npos) {
                ++warnings;
                if (atEnd) {
                    break;
                }
                continue;
            }

            const std::string_view key = TrimAscii(line.substr(0u, eqPos));
            const std::string_view val =
                TrimAscii(StripInlineCommentRespectingQuotes(line.substr(eqPos + 1u)));

            bool handled = false;
            switch (currentSection) {
            case IniSection::General:
                if (EqualsIgnoreCase(key, "bAllowSupernaturalEyes")) {
                    handled = TryParseBool(val, outSettings.allowSupernaturalEyes);
                } else if (EqualsIgnoreCase(key, "bAllowScars")) {
                    handled = TryParseBool(val, outSettings.allowScars);
                } else if (EqualsIgnoreCase(key, "fOffspringScarChance")) {
                    handled = TryParseUnitFloat(val, outSettings.offspringScarChance);
                } else if (EqualsIgnoreCase(key, "fDefaultScarChance")) {
                    handled = TryParseUnitFloat(val, outSettings.defaultScarChance);
                }
                break;
            case IniSection::Chimeras:
                if (EqualsIgnoreCase(key, "sBeastChimeraTier")) {
                    handled = TryParseChimeraTier(val, outSettings.beastChimeraTier);
                } else if (EqualsIgnoreCase(key, "bEnableGeneratedHybridSkins")) {
                    handled = TryParseBool(val, outSettings.enableGeneratedHybridSkins);
                } else if (EqualsIgnoreCase(key, "sGeneratedSkinResolution")) {
                    handled = TryParseGeneratedSkinResolution(val, outSettings.generatedSkinResolution);
                }
                break;
            case IniSection::ChimeraTier2Khajiit:
                handled = ApplyLineageSetting(key, val, /*isKhajiitSection=*/true, outSettings.khajiitTier2);
                break;
            case IniSection::ChimeraTier2Argonian:
                handled = ApplyLineageSetting(key, val, /*isKhajiitSection=*/false, outSettings.argonianTier2);
                break;
            case IniSection::None:
            case IniSection::Unknown:
                handled = false;
                break;
            }

            if (!handled) {
                ++warnings;
            }
            if (atEnd) {
                break;
            }
        }

        if (outWarningCount) {
            *outWarningCount = warnings;
        }
        return true;
    }

    IniLoadResult LoadOrCreateMorwhaIni(const std::filesystem::path& iniPath) {
        IniLoadResult res{};
        res.settings = MorwhaIniSettings{};

        std::error_code ec;
        const bool exists = std::filesystem::exists(iniPath, ec);
        if (ec) {
            res.error = std::format("Failed checking INI '{}': {}", iniPath.string(), ec.message());
            return res;
        }

        if (!exists) {
            const auto parent = iniPath.parent_path();
            if (!parent.empty()) {
                std::filesystem::create_directories(parent, ec);
                if (ec) {
                    res.error = std::format(
                        "Failed creating INI directory '{}': {}",
                        parent.string(),
                        ec.message());
                    return res;
                }
            }

            const std::string defaultText = GenerateDefaultMorwhaIniText();
            std::ofstream out(iniPath, std::ios::binary | std::ios::trunc);
            if (!out.is_open()) {
                res.error = std::format("Failed opening '{}' for write", iniPath.string());
                return res;
            }
            out.write(defaultText.data(), static_cast<std::streamsize>(defaultText.size()));
            out.flush();
            if (!out) {
                res.error = std::format("Failed writing default INI '{}'", iniPath.string());
                return res;
            }

            res.ok = true;
            res.createdDefault = true;
            return res;
        }

        const auto fileSize = std::filesystem::file_size(iniPath, ec);
        if (ec || fileSize > kMaxIniFileBytes) {
            res.error = std::format(
                "INI '{}' missing size or exceeds max size ({} bytes)",
                iniPath.string(),
                fileSize);
            return res;
        }

        std::ifstream in(iniPath, std::ios::binary);
        if (!in.is_open()) {
            res.error = std::format("Failed opening INI '{}' for read", iniPath.string());
            return res;
        }

        std::string content(static_cast<std::size_t>(fileSize), '\0');
        if (fileSize > 0u) {
            in.read(content.data(), static_cast<std::streamsize>(fileSize));
            if (!in || static_cast<std::size_t>(in.gcount()) != fileSize) {
                res.error = std::format("Failed reading INI '{}'", iniPath.string());
                return res;
            }
        }

        std::size_t warnings = 0u;
        (void)ParseMorwhaIniText(content, res.settings, &warnings);
        res.ok = true;
        res.createdDefault = false;
        res.warningCount = warnings;
        return res;
    }

}  // namespace MORWHA::State
