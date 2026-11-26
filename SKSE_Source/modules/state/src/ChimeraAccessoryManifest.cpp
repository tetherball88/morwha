#include "morwha/state/ChimeraAccessoryManifest.h"

#include "JsonDom.h"

#include <algorithm>
#include <cctype>
#include <format>
#include <fstream>
#include <system_error>

namespace MORWHA::State {

    namespace {

        constexpr std::size_t kMaxManifestFileBytes = 1024ull * 1024ull;
        constexpr std::size_t kMaxPluginNameBytes = 260u;

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

        [[nodiscard]] bool HasControlChars(std::string_view text) noexcept {
            for (const unsigned char ch : text) {
                if (ch < 0x20u || ch == 0x7Fu) {
                    return true;
                }
            }
            return false;
        }

        [[nodiscard]] bool ParseLineage(
            std::string_view token,
            ChimeraManifestLineage& outLineage) noexcept {
            if (EqualsIgnoreCase(token, "HumanoidKhajiit")) {
                outLineage = ChimeraManifestLineage::HumanoidKhajiit;
                return true;
            }
            if (EqualsIgnoreCase(token, "HumanoidArgonian")) {
                outLineage = ChimeraManifestLineage::HumanoidArgonian;
                return true;
            }
            if (EqualsIgnoreCase(token, "AnyChimera")) {
                outLineage = ChimeraManifestLineage::AnyChimera;
                return true;
            }
            return false;
        }

        [[nodiscard]] bool ParseSlotKind(
            std::string_view token,
            ChimeraAccessorySlotKind& outSlotKind) noexcept {
            if (EqualsIgnoreCase(token, "HeadPart")) {
                outSlotKind = ChimeraAccessorySlotKind::HeadPart;
                return true;
            }
            if (EqualsIgnoreCase(token, "SkinArmorAddon")) {
                outSlotKind = ChimeraAccessorySlotKind::SkinArmorAddon;
                return true;
            }
            return false;
        }

        [[nodiscard]] bool ParseSexFilter(
            std::string_view token,
            ChimeraAccessorySexFilter& outSex) noexcept {
            if (EqualsIgnoreCase(token, "Any")) {
                outSex = ChimeraAccessorySexFilter::Any;
                return true;
            }
            if (EqualsIgnoreCase(token, "Male")) {
                outSex = ChimeraAccessorySexFilter::Male;
                return true;
            }
            if (EqualsIgnoreCase(token, "Female")) {
                outSex = ChimeraAccessorySexFilter::Female;
                return true;
            }
            return false;
        }

    }  // namespace

    bool ParseChimeraAccessoryManifestJson(
        std::string_view jsonText,
        ChimeraAccessoryManifest& outManifest,
        std::string* outError) {
        outManifest = ChimeraAccessoryManifest{};

        Detail::JsonValue rootVal;
        if (!Detail::ParseJsonDocument(jsonText, rootVal, outError, /*allowDuplicateKeys=*/false)) {
            return false;
        }
        const auto* rootObj = rootVal.AsObject();
        if (!rootObj) {
            return Detail::SetJsonError(outError, "Accessory manifest root must be JSON object");
        }

        const auto verIt = rootObj->find("schemaVersion");
        if (verIt == rootObj->end()) {
            return Detail::SetJsonError(outError, "Accessory manifest missing 'schemaVersion'");
        }
        std::uint32_t schemaVer = 0;
        if (!Detail::ParseUIntFromJson(verIt->second, schemaVer, "schemaVersion", outError) ||
            schemaVer != kAccessoryManifestSchemaVersion) {
            return Detail::SetJsonError(
                outError, std::format("Unsupported manifest schemaVersion {}", schemaVer));
        }

        const auto pluginIt = rootObj->find("plugin");
        if (pluginIt == rootObj->end()) {
            return Detail::SetJsonError(outError, "Accessory manifest missing 'plugin'");
        }
        const auto* pluginStr = pluginIt->second.AsString();
        if (!pluginStr || pluginStr->empty() || pluginStr->size() > kMaxPluginNameBytes ||
            HasControlChars(*pluginStr)) {
            return Detail::SetJsonError(outError, "Accessory manifest has invalid 'plugin'");
        }
        outManifest.plugin = *pluginStr;

        if (const auto skeeIt = rootObj->find("requiresSkee64"); skeeIt != rootObj->end()) {
            const auto* b = std::get_if<bool>(&skeeIt->second.data);
            if (!b) {
                return Detail::SetJsonError(outError, "Accessory manifest 'requiresSkee64' must be boolean");
            }
            outManifest.requiresSkee64 = *b;
        }

        const auto entriesIt = rootObj->find("entries");
        if (entriesIt == rootObj->end()) {
            return Detail::SetJsonError(outError, "Accessory manifest missing 'entries'");
        }

        const auto* entriesArr = entriesIt->second.AsArray();
        if (!entriesArr || entriesArr->size() > kMaxAccessoryManifestEntries) {
            return Detail::SetJsonError(outError, "Accessory manifest 'entries' exceeds bounds");
        }

        outManifest.entries.reserve(entriesArr->size());
        for (const auto& entryVal : *entriesArr) {
            const auto* entryObj = entryVal.AsObject();
            if (!entryObj) {
                return Detail::SetJsonError(outError, "Accessory manifest 'entries[]' must be object");
            }
            const auto linIt = entryObj->find("lineage");
            const auto slotIt = entryObj->find("slotKind");
            const auto idIt = entryObj->find("localId");
            if (linIt == entryObj->end() || slotIt == entryObj->end() || idIt == entryObj->end()) {
                return Detail::SetJsonError(
                    outError, "Accessory entry missing 'lineage', 'slotKind', or 'localId'");
            }
            const auto* linStr = linIt->second.AsString();
            const auto* slotStr = slotIt->second.AsString();
            if (!linStr || !slotStr) {
                return Detail::SetJsonError(
                    outError, "Accessory entry 'lineage' and 'slotKind' must be strings");
            }

            ChimeraAccessoryEntrySpec entry{};
            if (!ParseLineage(*linStr, entry.lineage)) {
                return Detail::SetJsonError(outError, std::format("Unknown lineage '{}'", *linStr));
            }
            if (!ParseSlotKind(*slotStr, entry.slotKind)) {
                return Detail::SetJsonError(outError, std::format("Unknown slotKind '{}'", *slotStr));
            }
            if (const auto sexIt = entryObj->find("sex"); sexIt != entryObj->end()) {
                const auto* sexStr = sexIt->second.AsString();
                if (!sexStr || !ParseSexFilter(*sexStr, entry.sex)) {
                    return Detail::SetJsonError(outError, "Accessory entry 'sex' must be Any|Male|Female");
                }
            }
            if (!Detail::ParseUIntFromJson(idIt->second, entry.localId, "entries[].localId", outError)) {
                return false;
            }
            if (entry.localId == 0u || entry.localId > 0x00FFFFFFu) {
                return Detail::SetJsonError(
                    outError, std::format("Accessory entry localId out of range: 0x{:X}", entry.localId));
            }
            outManifest.entries.push_back(entry);
        }
        return true;
    }

    std::vector<ChimeraAccessoryManifest> LoadChimeraAccessoryManifestsFromDirectory(
        const std::filesystem::path& dirPath,
        std::size_t* outFilesScanned,
        std::size_t* outFilesFailed) {
        std::size_t scanned = 0u;
        std::size_t failed = 0u;
        std::vector<ChimeraAccessoryManifest> result;

        std::error_code ec;
        if (!std::filesystem::exists(dirPath, ec) || ec || !std::filesystem::is_directory(dirPath, ec) || ec) {
            if (outFilesScanned) {
                *outFilesScanned = scanned;
            }
            if (outFilesFailed) {
                *outFilesFailed = failed;
            }
            return result;
        }

        std::vector<std::filesystem::path> files;
        for (const auto& entry : std::filesystem::directory_iterator(dirPath, ec)) {
            if (ec || !entry.is_regular_file(ec)) {
                continue;
            }
            if (EqualsIgnoreCase(entry.path().extension().string(), ".json")) {
                files.push_back(entry.path());
            }
        }
        std::ranges::sort(files);

        for (const auto& filePath : files) {
            ++scanned;
            const auto fileSize = std::filesystem::file_size(filePath, ec);
            if (ec || fileSize == 0u || fileSize > kMaxManifestFileBytes) {
                ++failed;
                continue;
            }
            std::ifstream in(filePath, std::ios::binary);
            if (!in.is_open()) {
                ++failed;
                continue;
            }
            std::string content(static_cast<std::size_t>(fileSize), '\0');
            in.read(content.data(), static_cast<std::streamsize>(fileSize));
            if (!in || static_cast<std::size_t>(in.gcount()) != fileSize) {
                ++failed;
                continue;
            }

            ChimeraAccessoryManifest manifest;
            if (!ParseChimeraAccessoryManifestJson(content, manifest, nullptr)) {
                ++failed;
                continue;
            }
            result.push_back(std::move(manifest));
        }

        if (outFilesScanned) {
            *outFilesScanned = scanned;
        }
        if (outFilesFailed) {
            *outFilesFailed = failed;
        }
        return result;
    }

}  // namespace MORWHA::State
