#include "morwha/state/FaceAnchorPreset.h"

#include "JsonDom.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <format>
#include <fstream>
#include <system_error>

namespace MORWHA::State {

    namespace {

        constexpr std::size_t kMaxJslotFileBytes = 16ull * 1024ull * 1024ull;
        constexpr std::size_t kMaxHeadPartsInJslot = 32u;

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

        [[nodiscard]] bool EndsWithIgnoreCase(std::string_view haystack, std::string_view suffix) noexcept {
            if (haystack.size() < suffix.size()) {
                return false;
            }
            return EqualsIgnoreCase(haystack.substr(haystack.size() - suffix.size()), suffix);
        }

        [[nodiscard]] bool IsHighPolyHeadPluginName(std::string_view pluginName) noexcept {
            return EqualsIgnoreCase(pluginName, "High Poly Head.esm") ||
                   EqualsIgnoreCase(pluginName, "KLH_HighPolyHead.esp");
        }

        [[nodiscard]] bool HasControlChars(std::string_view text) noexcept {
            for (const unsigned char ch : text) {
                if (ch < 0x20u || ch == 0x7Fu) {
                    return true;
                }
            }
            return false;
        }

        [[nodiscard]] std::optional<Foundation::Sex> ParseSexFolderName(std::string_view dirName) noexcept {
            if (EqualsIgnoreCase(dirName, "female") || EqualsIgnoreCase(dirName, "f")) {
                return Foundation::Sex::Female;
            }
            if (EqualsIgnoreCase(dirName, "male") || EqualsIgnoreCase(dirName, "m")) {
                return Foundation::Sex::Male;
            }
            return std::nullopt;
        }

    }  // namespace

    std::optional<PluginFormRef> ParsePipeFormIdentifier(std::string_view identifier) {
        const std::size_t sep = identifier.find('|');
        if (sep == std::string_view::npos || sep == 0 || (sep + 1) >= identifier.size()) {
            return std::nullopt;
        }
        const std::string_view plugin = identifier.substr(0, sep);
        if (plugin.size() > 260u || HasControlChars(plugin)) {
            return std::nullopt;
        }

        std::string_view idStr = identifier.substr(sep + 1);
        if (idStr.size() >= 2 && idStr[0] == '0' && (idStr[1] == 'x' || idStr[1] == 'X')) {
            idStr.remove_prefix(2);
        }
        if (idStr.empty() || idStr.size() > 8) {
            return std::nullopt;
        }
        std::uint32_t rawId = 0;
        const auto [ptr, ec] = std::from_chars(idStr.data(), idStr.data() + idStr.size(), rawId, 16);
        if (ec != std::errc{} || ptr != idStr.data() + idStr.size()) {
            return std::nullopt;
        }
        if (rawId == 0 || rawId > 0x00FFFFFFu) {
            return std::nullopt;
        }
        return PluginFormRef{
            .plugin = std::string(plugin),
            .localId = rawId
        };
    }

    bool ParseJslotAnchorJson(
        std::string_view jsonText,
        FaceAnchorPreset& outPreset,
        std::string* outError,
        bool rejectSculpted) {
        outPreset = FaceAnchorPreset{};

        Detail::JsonValue rootVal;
        if (!Detail::ParseJsonDocument(jsonText, rootVal, outError, /*allowDuplicateKeys=*/true)) {
            return false;
        }
        const auto* rootObj = rootVal.AsObject();
        if (!rootObj) {
            return Detail::SetJsonError(outError, "RaceMenu .jslot root must be a JSON object");
        }

        const auto morphsIt = rootObj->find("morphs");
        if (morphsIt == rootObj->end()) {
            return Detail::SetJsonError(outError, "RaceMenu .jslot missing 'morphs' object");
        }
        const auto* morphsObj = morphsIt->second.AsObject();
        if (!morphsObj) {
            return Detail::SetJsonError(outError, "RaceMenu .jslot 'morphs' must be an object");
        }

        if (const auto sculptIt = morphsObj->find("sculpt"); sculptIt != morphsObj->end()) {
            const auto* sculptArr = sculptIt->second.AsArray();
            if (sculptArr) {
                if (!sculptArr->empty()) {
                    outPreset.hasSculptData = true;
                }
            } else if (!sculptIt->second.IsNull()) {
                outPreset.hasSculptData = true;
            }
            if (outPreset.hasSculptData && rejectSculpted) {
                return Detail::SetJsonError(
                    outError,
                    "RaceMenu .jslot contains sculpt vertex data; slider-only presets required");
            }
        }

        const auto defaultIt = morphsObj->find("default");
        if (defaultIt == morphsObj->end()) {
            return Detail::SetJsonError(outError, "RaceMenu .jslot missing 'morphs.default'");
        }
        const auto* defaultObj = defaultIt->second.AsObject();
        if (!defaultObj) {
            return Detail::SetJsonError(outError, "RaceMenu .jslot 'morphs.default' must be an object");
        }

        const auto sliderArrIt = defaultObj->find("morphs");
        if (sliderArrIt == defaultObj->end()) {
            return Detail::SetJsonError(outError, "RaceMenu .jslot missing 'morphs.default.morphs'");
        }
        const auto* sliderArr = sliderArrIt->second.AsArray();
        if (!sliderArr || sliderArr->empty() || sliderArr->size() > Foundation::kVanillaFaceMorphCount) {
            return Detail::SetJsonError(
                outError,
                std::format("'morphs.default.morphs' must have 1..{} numbers", Foundation::kVanillaFaceMorphCount));
        }

        for (std::size_t i = 0; i < sliderArr->size(); ++i) {
            double rawVal = 0.0;
            if (!Detail::ParseDoubleFromJson((*sliderArr)[i], rawVal, "morphs.default.morphs[]", outError)) {
                return false;
            }
            if (i + 1 >= Foundation::kVanillaFaceMorphCount) {
                outPreset.faceMorphs.morphs[i] = 0.0f;
            } else if (!std::isfinite(rawVal) || std::abs(rawVal) > 4.0) {
                // RaceMenu writes FLT_MAX (3.402823466385289E+38) for unused/default morph sentinels.
                outPreset.faceMorphs.morphs[i] = 0.0f;
            } else {
                outPreset.faceMorphs.morphs[i] = std::clamp(static_cast<float>(rawVal), -0.75f, 0.75f);
            }
        }
        outPreset.faceMorphs.morphs[Foundation::kVanillaFaceMorphCount - 1] = 0.0f;

        if (const auto presetsIt = defaultObj->find("presets"); presetsIt != defaultObj->end()) {
            const auto* presetsArr = presetsIt->second.AsArray();
            if (!presetsArr || presetsArr->size() > outPreset.faceMorphs.parts.size()) {
                return Detail::SetJsonError(outError, "'morphs.default.presets' must be array of 0..4 integers");
            }
            for (std::size_t i = 0; i < presetsArr->size(); ++i) {
                std::int64_t rawPart = -1;
                if (!Detail::ParseSignedIntFromJson((*presetsArr)[i], rawPart, "morphs.default.presets[]", outError)) {
                    return false;
                }
                if (i == static_cast<std::size_t>(Foundation::FacePartSlot::Unknown) ||
                    rawPart < 0 || rawPart >= static_cast<std::int64_t>(Foundation::kUnusedFacePartPreset)) {
                    outPreset.faceMorphs.parts[i] = Foundation::kUnusedFacePartPreset;
                } else {
                    outPreset.faceMorphs.parts[i] = static_cast<std::int32_t>(rawPart);
                }
            }
        }

        if (const auto actorIt = rootObj->find("actor"); actorIt != rootObj->end()) {
            if (const auto* actorObj = actorIt->second.AsObject()) {
                if (const auto weightIt = actorObj->find("weight"); weightIt != actorObj->end()) {
                    float w = 50.0f;
                    if (Detail::ParseFloatFromJson(weightIt->second, w, "actor.weight", outError)) {
                        outPreset.weight = std::clamp(w, 0.0f, 100.0f);
                    }
                }
                if (const auto texIt = actorObj->find("headTexture"); texIt != actorObj->end()) {
                    if (const auto* texStr = texIt->second.AsString()) {
                        if (auto ref = ParsePipeFormIdentifier(*texStr)) {
                            outPreset.headTextureRef = std::move(*ref);
                        }
                    }
                }
                if (const auto hairIt = actorObj->find("hairColor"); hairIt != actorObj->end()) {
                    std::uint32_t packedHair = 0;
                    if (Detail::ParseUIntFromJson(hairIt->second, packedHair, "actor.hairColor", outError)) {
                        outPreset.hairColor = Foundation::RgbColor{
                            static_cast<std::uint8_t>((packedHair >> 16) & 0xFFu),
                            static_cast<std::uint8_t>((packedHair >> 8) & 0xFFu),
                            static_cast<std::uint8_t>(packedHair & 0xFFu)
                        };
                    }
                }
            }
        }

        if (const auto tintIt = rootObj->find("tintInfo"); tintIt != rootObj->end()) {
            if (const auto* tintArr = tintIt->second.AsArray()) {
                for (const auto& entryVal : *tintArr) {
                    const auto* entryObj = entryVal.AsObject();
                    if (!entryObj) {
                        continue;
                    }
                    const auto texIt = entryObj->find("texture");
                    const auto colIt = entryObj->find("color");
                    if (texIt == entryObj->end() || colIt == entryObj->end()) {
                        continue;
                    }
                    const auto* texStr = texIt->second.AsString();
                    if (!texStr || !EndsWithIgnoreCase(*texStr, "skintone.dds")) {
                        continue;
                    }
                    std::uint32_t packed = 0;
                    if (Detail::ParseUIntFromJson(colIt->second, packed, "tintInfo[].color", nullptr) &&
                        ((packed >> 24) & 0xFFu) > 0u) {
                        outPreset.skinColor = Foundation::RgbColor{
                            static_cast<std::uint8_t>((packed >> 16) & 0xFFu),
                            static_cast<std::uint8_t>((packed >> 8) & 0xFFu),
                            static_cast<std::uint8_t>(packed & 0xFFu)
                        };
                        break;
                    }
                }
            }
        }

        if (const auto modsIt = rootObj->find("modNames"); modsIt != rootObj->end()) {
            if (const auto* modsArr = modsIt->second.AsArray()) {
                for (const auto& modVal : *modsArr) {
                    if (const auto* modStr = modVal.AsString(); modStr && IsHighPolyHeadPluginName(*modStr)) {
                        outPreset.authoredTopology = Foundation::TopologyPool::HighPolyHead;
                        break;
                    }
                }
            }
        }

        if (const auto partsIt = rootObj->find("headParts"); partsIt != rootObj->end()) {
            if (const auto* partsArr = partsIt->second.AsArray()) {
                outPreset.headParts.reserve((std::min)(partsArr->size(), kMaxHeadPartsInJslot));
                for (const auto& partVal : *partsArr) {
                    if (outPreset.headParts.size() >= kMaxHeadPartsInJslot) {
                        break;
                    }
                    const auto* partObj = partVal.AsObject();
                    if (!partObj) {
                        continue;
                    }
                    if (const auto idIt = partObj->find("formIdentifier"); idIt != partObj->end()) {
                        if (const auto* idStr = idIt->second.AsString()) {
                            if (auto ref = ParsePipeFormIdentifier(*idStr)) {
                                if (IsHighPolyHeadPluginName(ref->plugin)) {
                                    outPreset.authoredTopology = Foundation::TopologyPool::HighPolyHead;
                                }
                                outPreset.headParts.push_back(std::move(*ref));
                            }
                        }
                    }
                }
            }
        }

        return true;
    }

    std::vector<FaceAnchorPreset> LoadFaceAnchorsFromDirectory(
        const std::filesystem::path& rootDir,
        FaceAnchorDirLoadStats* outStats,
        bool rejectSculpted) {
        FaceAnchorDirLoadStats stats{};
        std::vector<FaceAnchorPreset> presets;

        std::error_code ec;
        if (!std::filesystem::exists(rootDir, ec) || ec || !std::filesystem::is_directory(rootDir, ec) || ec) {
            if (outStats) {
                *outStats = stats;
            }
            return presets;
        }

        std::vector<std::filesystem::path> raceDirs;
        for (const auto& raceEntry : std::filesystem::directory_iterator(rootDir, ec)) {
            if (!ec && raceEntry.is_directory(ec)) {
                raceDirs.push_back(raceEntry.path());
            }
        }
        std::ranges::sort(raceDirs);

        for (const auto& raceDir : raceDirs) {
            const std::string raceKey = raceDir.filename().string();
            if (raceKey.empty()) {
                continue;
            }

            std::vector<std::filesystem::path> sexDirs;
            for (const auto& sexEntry : std::filesystem::directory_iterator(raceDir, ec)) {
                if (!ec && sexEntry.is_directory(ec)) {
                    sexDirs.push_back(sexEntry.path());
                }
            }
            std::ranges::sort(sexDirs);

            for (const auto& sexDir : sexDirs) {
                const auto sexOpt = ParseSexFolderName(sexDir.filename().string());
                if (!sexOpt) {
                    continue;
                }

                std::vector<std::filesystem::path> jslotFiles;
                for (const auto& fileEntry : std::filesystem::directory_iterator(sexDir, ec)) {
                    if (ec || !fileEntry.is_regular_file(ec)) {
                        continue;
                    }
                    const std::string ext = fileEntry.path().extension().string();
                    if (EqualsIgnoreCase(ext, ".jslot")) {
                        jslotFiles.push_back(fileEntry.path());
                    }
                }
                std::ranges::sort(jslotFiles);

                for (const auto& filePath : jslotFiles) {
                    ++stats.filesScanned;
                    const auto fileSize = std::filesystem::file_size(filePath, ec);
                    if (ec || fileSize == 0 || fileSize > kMaxJslotFileBytes) {
                        ++stats.failedFiles;
                        continue;
                    }

                    std::ifstream in(filePath, std::ios::binary);
                    if (!in.is_open()) {
                        ++stats.failedFiles;
                        continue;
                    }
                    std::string content(static_cast<std::size_t>(fileSize), '\0');
                    in.read(content.data(), static_cast<std::streamsize>(fileSize));
                    if (!in || static_cast<std::size_t>(in.gcount()) != fileSize) {
                        ++stats.failedFiles;
                        continue;
                    }

                    FaceAnchorPreset preset;
                    if (!ParseJslotAnchorJson(content, preset, nullptr, rejectSculpted)) {
                        if (preset.hasSculptData && rejectSculpted) {
                            ++stats.skippedSculpted;
                        } else {
                            ++stats.failedFiles;
                        }
                        continue;
                    }

                    preset.anchorId = filePath.stem().string();
                    preset.raceKey = raceKey;
                    preset.sex = *sexOpt;
                    presets.push_back(std::move(preset));
                    ++stats.presetsLoaded;
                }
            }
        }

        if (outStats) {
            *outStats = stats;
        }
        return presets;
    }

}  // namespace MORWHA::State
