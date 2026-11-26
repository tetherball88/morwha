#include "morwha/state/MortalRequestJson.h"

#include "JsonDom.h"
#include "morwha/state/DynamicNpcRegistry.h"

#include <cctype>
#include <charconv>
#include <format>

namespace MORWHA::State {

    namespace {

        constexpr std::size_t kMaxDisplayNameBytes = 256u;

        using Detail::JsonArray;
        using Detail::JsonObject;
        using Detail::JsonValue;
        using Detail::ParseJsonDocument;
        using Detail::ParseSignedIntFromJson;
        using Detail::ParseUIntFromJson;
        using Detail::SetJsonError;

        [[nodiscard]] std::string_view TrimAscii(std::string_view sv) noexcept {
            while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.front()))) {
                sv.remove_prefix(1);
            }
            while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.back()))) {
                sv.remove_suffix(1);
            }
            return sv;
        }

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

        [[nodiscard]] bool ParseHexOrDecU32(std::string_view sv, std::uint32_t& out) noexcept {
            sv = TrimAscii(sv);
            int base = 10;
            if (sv.size() >= 2 && sv[0] == '0' && (sv[1] == 'x' || sv[1] == 'X')) {
                base = 16;
                sv.remove_prefix(2);
            } else {
                bool hasHexAlpha = false;
                for (const char ch : sv) {
                    if ((ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F')) {
                        hasHexAlpha = true;
                        break;
                    }
                }
                if (hasHexAlpha) {
                    base = 16;
                }
            }
            if (sv.empty()) {
                return false;
            }
            const auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), out, base);
            return ec == std::errc{} && ptr == sv.data() + sv.size();
        }

        [[nodiscard]] bool ParsePipePluginRef(
            std::string_view raw,
            PluginFormRef& outRef,
            std::string_view field,
            std::string* outError) {
            const auto pipePos = raw.find('|');
            if (pipePos == std::string_view::npos) {
                return false;
            }
            const std::string_view pluginPart = TrimAscii(raw.substr(0, pipePos));
            std::string_view idPart = TrimAscii(raw.substr(pipePos + 1));
            if (pluginPart.empty() || idPart.empty()) {
                return SetJsonError(
                    outError,
                    std::format("Field '{}' has malformed 'Plugin|FormID' string '{}'", field, raw));
            }
            if (idPart.size() >= 2 && idPart[0] == '0' && (idPart[1] == 'x' || idPart[1] == 'X')) {
                idPart.remove_prefix(2);
            }
            std::uint32_t localId = 0;
            const auto [ptr, ec] = std::from_chars(idPart.data(), idPart.data() + idPart.size(), localId, 16);
            if (ec != std::errc{} || ptr != idPart.data() + idPart.size() || localId == 0 || localId > 0x00FFFFFFu) {
                return SetJsonError(
                    outError,
                    std::format("Field '{}' has invalid hex localId in '{}'", field, raw));
            }
            outRef.plugin = std::string(pluginPart);
            outRef.localId = localId;
            return true;
        }

        [[nodiscard]] bool ParseFormSelectorValue(
            const JsonValue& val,
            JsonFormSelector& outSel,
            std::string_view field,
            std::string* outError) {
            outSel = JsonFormSelector{};
            outSel.specified = true;

            if (val.IsNull()) {
                outSel.clear = true;
                return true;
            }

            if (val.AsNumber()) {
                std::uint32_t rawId = 0;
                if (!ParseUIntFromJson(val, rawId, field, outError)) {
                    return false;
                }
                if (rawId == 0u || rawId == 0xFFFFFFFFu) {
                    outSel.clear = true;
                } else {
                    outSel.runtimeId = Foundation::FormRawId{ rawId };
                }
                return true;
            }

            if (const auto* str = val.AsString()) {
                const std::string_view trimmed = TrimAscii(*str);
                if (trimmed.empty() || EqualsIgnoreCase(trimmed, "none") || EqualsIgnoreCase(trimmed, "null")) {
                    outSel.clear = true;
                    return true;
                }
                if (trimmed.find('|') != std::string_view::npos) {
                    return ParsePipePluginRef(trimmed, outSel.pluginRef, field, outError);
                }
                std::uint32_t rawId = 0;
                if (!ParseHexOrDecU32(trimmed, rawId)) {
                    return SetJsonError(
                        outError,
                        std::format("Field '{}' expected FormID or 'Plugin.esp|0x1234', got '{}'", field, *str));
                }
                if (rawId == 0u || rawId == 0xFFFFFFFFu) {
                    outSel.clear = true;
                } else {
                    outSel.runtimeId = Foundation::FormRawId{ rawId };
                }
                return true;
            }

            if (const auto* obj = val.AsObject()) {
                if (obj->empty()) {
                    outSel.clear = true;
                    return true;
                }
                const auto pIt = obj->find("plugin");
                const auto lIt = obj->find("localId");
                if (pIt == obj->end() || lIt == obj->end() || !pIt->second.AsString()) {
                    return SetJsonError(
                        outError,
                        std::format("Field '{}' object requires 'plugin' and 'localId'", field));
                }
                std::uint32_t localId = 0;
                if (!ParseUIntFromJson(lIt->second, localId, field, outError)) {
                    return false;
                }
                if (pIt->second.AsString()->empty() || localId == 0u || localId > 0x00FFFFFFu) {
                    return SetJsonError(
                        outError,
                        std::format("Field '{}' has invalid plugin/localId range", field));
                }
                outSel.pluginRef.plugin = *pIt->second.AsString();
                outSel.pluginRef.localId = localId;
                return true;
            }

            return SetJsonError(outError, std::format("Field '{}' must be FormID, 'Plugin|Hex', object, or null", field));
        }

        [[nodiscard]] bool ParseAgeBracketJson(
            const JsonValue& val,
            Foundation::AgeBracket& outBracket,
            std::string_view field,
            std::string* outError) {
            if (val.AsNumber()) {
                std::uint8_t raw = 0;
                if (!ParseUIntFromJson(val, raw, field, outError) || raw > 4u) {
                    return SetJsonError(outError, std::format("Field '{}' ageBracket out of range [0..4]", field));
                }
                outBracket = static_cast<Foundation::AgeBracket>(raw);
                return true;
            }
            if (const auto* s = val.AsString()) {
                const std::string_view sv = TrimAscii(*s);
                if (EqualsIgnoreCase(sv, "Auto")) {
                    outBracket = Foundation::AgeBracket::Auto;
                    return true;
                }
                if (EqualsIgnoreCase(sv, "YoungAdult") || EqualsIgnoreCase(sv, "young_adult") || EqualsIgnoreCase(sv, "young")) {
                    outBracket = Foundation::AgeBracket::YoungAdult;
                    return true;
                }
                if (EqualsIgnoreCase(sv, "Mature") || EqualsIgnoreCase(sv, "adult")) {
                    outBracket = Foundation::AgeBracket::Mature;
                    return true;
                }
                if (EqualsIgnoreCase(sv, "MiddleAged") || EqualsIgnoreCase(sv, "middle_aged") || EqualsIgnoreCase(sv, "middle")) {
                    outBracket = Foundation::AgeBracket::MiddleAged;
                    return true;
                }
                if (EqualsIgnoreCase(sv, "Elder") || EqualsIgnoreCase(sv, "old")) {
                    outBracket = Foundation::AgeBracket::Elder;
                    return true;
                }
            }
            return SetJsonError(outError, std::format("Field '{}' has unrecognized ageBracket", field));
        }

        [[nodiscard]] bool ParseDonorModeJson(
            const JsonValue& val,
            Foundation::DonorMode& outMode,
            std::string* outError) {
            if (val.AsNumber()) {
                std::uint8_t raw = 0;
                if (!ParseUIntFromJson(val, raw, "donorMode", outError) || raw > 2u) {
                    return SetJsonError(outError, "Field 'donorMode' integer out of range [0..2]");
                }
                outMode = static_cast<Foundation::DonorMode>(raw);
                return true;
            }
            if (const auto* s = val.AsString()) {
                const std::string_view sv = TrimAscii(*s);
                if (EqualsIgnoreCase(sv, "ZeroDonorProcedural") || EqualsIgnoreCase(sv, "zero") || EqualsIgnoreCase(sv, "procedural")) {
                    outMode = Foundation::DonorMode::ZeroDonorProcedural;
                    return true;
                }
                if (EqualsIgnoreCase(sv, "OneDonorRelative") || EqualsIgnoreCase(sv, "relative") || EqualsIgnoreCase(sv, "one")) {
                    outMode = Foundation::DonorMode::OneDonorRelative;
                    return true;
                }
                if (EqualsIgnoreCase(sv, "TwoDonorOffspring") || EqualsIgnoreCase(sv, "offspring") || EqualsIgnoreCase(sv, "child") || EqualsIgnoreCase(sv, "two")) {
                    outMode = Foundation::DonorMode::TwoDonorOffspring;
                    return true;
                }
            }
            return SetJsonError(outError, "Field 'donorMode' has unrecognized value");
        }

        [[nodiscard]] bool ParseSexJson(
            const JsonValue& val,
            std::int32_t& outSex,
            std::string* outError) {
            if (val.AsNumber()) {
                std::int32_t raw = -1;
                if (!ParseSignedIntFromJson(val, raw, "sex", outError) || raw < -1 || raw > 1) {
                    return SetJsonError(outError, "Field 'sex' integer must be -1, 0, or 1");
                }
                outSex = raw;
                return true;
            }
            if (const auto* s = val.AsString()) {
                const std::string_view sv = TrimAscii(*s);
                if (EqualsIgnoreCase(sv, "Male") || EqualsIgnoreCase(sv, "m") || sv == "0") {
                    outSex = 0;
                    return true;
                }
                if (EqualsIgnoreCase(sv, "Female") || EqualsIgnoreCase(sv, "f") || sv == "1") {
                    outSex = 1;
                    return true;
                }
                if (EqualsIgnoreCase(sv, "Random") || EqualsIgnoreCase(sv, "Auto") || EqualsIgnoreCase(sv, "Any") || sv == "-1") {
                    outSex = -1;
                    return true;
                }
            }
            return SetJsonError(outError, "Field 'sex' must be 'Male', 'Female', 'Random', -1, 0, or 1");
        }

        [[nodiscard]] std::optional<std::size_t> LookupSkillIndexByName(std::string_view key) noexcept {
            static constexpr std::array<std::pair<std::string_view, std::size_t>, 20> kSkillNames = { {
                { "oneHanded", 0u },
                { "twoHanded", 1u },
                { "marksman", 2u },
                { "archery", 2u },
                { "block", 3u },
                { "smithing", 4u },
                { "heavyArmor", 5u },
                { "lightArmor", 6u },
                { "pickpocket", 7u },
                { "lockpicking", 8u },
                { "sneak", 9u },
                { "alchemy", 10u },
                { "speechcraft", 11u },
                { "speech", 11u },
                { "alteration", 12u },
                { "conjuration", 13u },
                { "destruction", 14u },
                { "illusion", 15u },
                { "restoration", 16u },
                { "enchanting", 17u },
            } };
            for (const auto& [name, idx] : kSkillNames) {
                if (EqualsIgnoreCase(key, name)) {
                    return idx;
                }
            }
            return std::nullopt;
        }

        [[nodiscard]] bool ParseArchetypeFieldsFromObject(
            const JsonObject& obj,
            JsonArchetypePatch& inoutPatch,
            std::string* outError) {
            auto tryField = [&](const char* key, JsonFormSelector& target) -> bool {
                const auto it = obj.find(key);
                if (it == obj.end()) {
                    return true;
                }
                return ParseFormSelectorValue(it->second, target, key, outError);
            };
            return tryField("voiceType", inoutPatch.voiceType) &&
                   tryField("npcClass", inoutPatch.npcClass) &&
                   tryField("combatStyle", inoutPatch.combatStyle) &&
                   tryField("defaultOutfit", inoutPatch.defaultOutfit) &&
                   tryField("sleepOutfit", inoutPatch.sleepOutfit) &&
                   tryField("crimeFaction", inoutPatch.crimeFaction) &&
                   tryField("faceTextureSet", inoutPatch.faceTextureSet);
        }

        [[nodiscard]] bool ParseAiObject(
            const JsonObject& obj,
            JsonAiPatch& outAi,
            std::string* outError) {
            auto parseBoundedU8 = [&](const char* key, std::uint8_t maxVal, std::optional<std::uint8_t>& dest) -> bool {
                const auto it = obj.find(key);
                if (it == obj.end() || it->second.IsNull()) {
                    return true;
                }
                std::uint8_t v = 0;
                if (!ParseUIntFromJson(it->second, v, key, outError) || v > maxVal) {
                    return SetJsonError(outError, std::format("Field 'ai.{}' out of range [0..{}]", key, maxVal));
                }
                dest = v;
                return true;
            };
            auto parseOptBool = [&](const char* key, std::optional<bool>& dest) -> bool {
                const auto it = obj.find(key);
                if (it == obj.end() || it->second.IsNull()) {
                    return true;
                }
                const auto* b = it->second.AsBool();
                if (!b) {
                    return SetJsonError(outError, std::format("Field 'ai.{}' requires boolean", key));
                }
                dest = *b;
                return true;
            };
            return parseBoundedU8("aggression", 3u, outAi.aggression) &&
                   parseBoundedU8("confidence", 4u, outAi.confidence) &&
                   parseBoundedU8("assistance", 2u, outAi.assistance) &&
                   parseBoundedU8("morality", 3u, outAi.morality) &&
                   parseOptBool("isProtected", outAi.isProtected) &&
                   parseOptBool("isEssential", outAi.isEssential);
        }

        [[nodiscard]] bool ParseSelectorArray(
            const JsonValue& val,
            std::size_t maxCount,
            std::vector<JsonFormSelector>& outVec,
            std::string_view field,
            std::string* outError) {
            const auto* arr = val.AsArray();
            if (!arr) {
                return SetJsonError(outError, std::format("Field '{}' requires array", field));
            }
            if (arr->size() > maxCount) {
                return SetJsonError(outError, std::format("Field '{}' exceeds max entries ({})", field, maxCount));
            }
            outVec.clear();
            outVec.reserve(arr->size());
            for (const auto& elem : *arr) {
                JsonFormSelector sel{};
                if (!ParseFormSelectorValue(elem, sel, field, outError)) {
                    return false;
                }
                if (!sel.clear) {
                    outVec.push_back(std::move(sel));
                }
            }
            return true;
        }

        [[nodiscard]] bool ParseFactionsArray(
            const JsonValue& val,
            std::vector<JsonFactionRankSelector>& outVec,
            std::string* outError) {
            const auto* arr = val.AsArray();
            if (!arr) {
                return SetJsonError(outError, "Field 'factions' requires array");
            }
            if (arr->size() > kMaxFactionsPerNpc) {
                return SetJsonError(
                    outError,
                    std::format("Field 'factions' exceeds max entries ({})", kMaxFactionsPerNpc));
            }
            outVec.clear();
            outVec.reserve(arr->size());
            for (const auto& elem : *arr) {
                const auto* fObj = elem.AsObject();
                if (!fObj) {
                    return SetJsonError(outError, "Each entry in 'factions' must be an object");
                }
                const auto facIt = fObj->find("faction");
                if (facIt == fObj->end()) {
                    return SetJsonError(outError, "Faction entry requires 'faction'");
                }
                JsonFactionRankSelector item{};
                if (!ParseFormSelectorValue(facIt->second, item.faction, "factions[].faction", outError) ||
                    item.faction.clear) {
                    return SetJsonError(outError, "Faction entry has empty/invalid 'faction'");
                }
                if (const auto rankIt = fObj->find("rank"); rankIt != fObj->end() && !rankIt->second.IsNull()) {
                    if (!ParseSignedIntFromJson(rankIt->second, item.rank, "factions[].rank", outError) ||
                        item.rank < -1) {
                        return SetJsonError(outError, "Field 'factions[].rank' must be in [-1..127]");
                    }
                }
                outVec.push_back(std::move(item));
            }
            return true;
        }

        [[nodiscard]] bool ParseStatsObject(
            const JsonObject& obj,
            JsonStatsPatch& inoutStats,
            std::string* outError) {
            auto parseNonZeroU16 = [&](const char* key, std::optional<std::uint16_t>& dest, bool allowZero) -> bool {
                const auto it = obj.find(key);
                if (it == obj.end() || it->second.IsNull()) {
                    return true;
                }
                std::uint16_t v = 0;
                if (!ParseUIntFromJson(it->second, v, key, outError) || (!allowZero && v == 0u)) {
                    return SetJsonError(outError, std::format("Field 'stats.{}' has invalid uint16 value", key));
                }
                dest = v;
                return true;
            };
            auto parseOptBool = [&](const char* key, std::optional<bool>& dest) -> bool {
                const auto it = obj.find(key);
                if (it == obj.end() || it->second.IsNull()) {
                    return true;
                }
                const auto* b = it->second.AsBool();
                if (!b) {
                    return SetJsonError(outError, std::format("Field 'stats.{}' requires boolean", key));
                }
                dest = *b;
                return true;
            };

            if (!parseNonZeroU16("level", inoutStats.level, false) ||
                !parseNonZeroU16("calcLevelMin", inoutStats.calcLevelMin, true) ||
                !parseNonZeroU16("calcLevelMax", inoutStats.calcLevelMax, true) ||
                !parseOptBool("autoCalcStats", inoutStats.autoCalcStats) ||
                !parseOptBool("pcLevelMult", inoutStats.pcLevelMult) ||
                !parseNonZeroU16("health", inoutStats.health, false) ||
                !parseNonZeroU16("magicka", inoutStats.magicka, false) ||
                !parseNonZeroU16("stamina", inoutStats.stamina, false)) {
                return false;
            }

            if (const auto skIt = obj.find("skills"); skIt != obj.end() && !skIt->second.IsNull()) {
                if (const auto* skArr = skIt->second.AsArray()) {
                    if (skArr->size() != kNpcSkillCount) {
                        return SetJsonError(outError, "Field 'stats.skills' array must contain 18 values");
                    }
                    for (std::size_t i = 0; i < kNpcSkillCount; ++i) {
                        std::uint8_t sv = 15u;
                        if (!ParseUIntFromJson((*skArr)[i], sv, "stats.skills[]", outError)) {
                            return false;
                        }
                        inoutStats.skills[i] = sv;
                    }
                } else if (const auto* skObj = skIt->second.AsObject()) {
                    for (const auto& [k, v] : *skObj) {
                        const auto idxOpt = LookupSkillIndexByName(k);
                        if (!idxOpt) {
                            return SetJsonError(outError, std::format("Unknown skill name '{}' in 'stats.skills'", k));
                        }
                        std::uint8_t sv = 15u;
                        if (!ParseUIntFromJson(v, sv, k, outError)) {
                            return false;
                        }
                        inoutStats.skills[*idxOpt] = sv;
                    }
                } else {
                    return SetJsonError(outError, "Field 'stats.skills' must be an 18-element array or named skill object");
                }
            }

            if (const auto pkIt = obj.find("perks"); pkIt != obj.end() && !pkIt->second.IsNull()) {
                inoutStats.replacePerks = true;
                if (!ParseSelectorArray(pkIt->second, kMaxPerksPerNpc, inoutStats.perks, "perks", outError)) {
                    return false;
                }
            }
            if (const auto spIt = obj.find("spells"); spIt != obj.end() && !spIt->second.IsNull()) {
                inoutStats.replaceSpells = true;
                if (!ParseSelectorArray(spIt->second, kMaxSpellsPerNpc, inoutStats.spells, "spells", outError)) {
                    return false;
                }
            }
            if (const auto facIt = obj.find("factions"); facIt != obj.end() && !facIt->second.IsNull()) {
                inoutStats.replaceFactions = true;
                if (!ParseFactionsArray(facIt->second, inoutStats.factions, outError)) {
                    return false;
                }
            }

            return true;
        }

        [[nodiscard]] bool ParseSharedPatchesFromRoot(
            const JsonObject& root,
            JsonArchetypePatch& archetype,
            JsonAiPatch& ai,
            JsonStatsPatch& stats,
            std::string* outError) {
            if (!ParseArchetypeFieldsFromObject(root, archetype, outError)) {
                return false;
            }
            if (const auto archIt = root.find("archetype"); archIt != root.end() && !archIt->second.IsNull()) {
                const auto* archObj = archIt->second.AsObject();
                if (!archObj || !ParseArchetypeFieldsFromObject(*archObj, archetype, outError)) {
                    return archObj ? false : SetJsonError(outError, "Field 'archetype' must be an object");
                }
            }
            if (const auto aiIt = root.find("ai"); aiIt != root.end() && !aiIt->second.IsNull()) {
                const auto* aiObj = aiIt->second.AsObject();
                if (!aiObj || !ParseAiObject(*aiObj, ai, outError)) {
                    return aiObj ? false : SetJsonError(outError, "Field 'ai' must be an object");
                }
            }
            if (const auto stIt = root.find("stats"); stIt != root.end() && !stIt->second.IsNull()) {
                const auto* stObj = stIt->second.AsObject();
                if (!stObj || !ParseStatsObject(*stObj, stats, outError)) {
                    return stObj ? false : SetJsonError(outError, "Field 'stats' must be an object");
                }
            }
            if (const auto pkIt = root.find("perks"); pkIt != root.end() && !pkIt->second.IsNull()) {
                stats.replacePerks = true;
                if (!ParseSelectorArray(pkIt->second, kMaxPerksPerNpc, stats.perks, "perks", outError)) {
                    return false;
                }
            }
            if (const auto spIt = root.find("spells"); spIt != root.end() && !spIt->second.IsNull()) {
                stats.replaceSpells = true;
                if (!ParseSelectorArray(spIt->second, kMaxSpellsPerNpc, stats.spells, "spells", outError)) {
                    return false;
                }
            }
            if (const auto facIt = root.find("factions"); facIt != root.end() && !facIt->second.IsNull()) {
                stats.replaceFactions = true;
                if (!ParseFactionsArray(facIt->second, stats.factions, outError)) {
                    return false;
                }
            }
            return true;
        }

    }  // namespace

    bool ParseJsonSpawnRequest(
        std::string_view jsonText,
        JsonSpawnRequest& outRequest,
        std::string* outError) {
        outRequest = JsonSpawnRequest{};
        JsonValue doc{};
        if (!ParseJsonDocument(jsonText, doc, outError)) {
            return false;
        }
        const auto* root = doc.AsObject();
        if (!root) {
            return SetJsonError(outError, "Spawn JSON root must be an object");
        }

        if (const auto dmIt = root->find("donorMode"); dmIt != root->end() && !dmIt->second.IsNull()) {
            if (!ParseDonorModeJson(dmIt->second, outRequest.donorMode, outError)) {
                return false;
            }
            outRequest.donorModeExplicit = true;
        }
        if (const auto sexIt = root->find("sex"); sexIt != root->end() && !sexIt->second.IsNull()) {
            if (!ParseSexJson(sexIt->second, outRequest.sex, outError)) {
                return false;
            }
        }
        if (const auto ageIt = root->find("ageBracket"); ageIt != root->end() && !ageIt->second.IsNull()) {
            if (!ParseAgeBracketJson(ageIt->second, outRequest.ageBracket, "ageBracket", outError)) {
                return false;
            }
        }
        if (const auto fpIt = root->find("forcePersist"); fpIt != root->end() && !fpIt->second.IsNull()) {
            const auto* b = fpIt->second.AsBool();
            if (!b) {
                return SetJsonError(outError, "Field 'forcePersist' requires boolean");
            }
            outRequest.forcePersist = *b;
        }
        if (const auto seedIt = root->find("seed"); seedIt != root->end() && !seedIt->second.IsNull()) {
            std::uint64_t rawSeed = 0;
            if (!ParseUIntFromJson(seedIt->second, rawSeed, "seed", outError)) {
                return false;
            }
            outRequest.seed = Foundation::Seed64{ rawSeed };
        }

        auto parseStringAlias = [&](const char* k1, const char* k2, std::string& dest, std::size_t maxLen) -> bool {
            const auto it = root->contains(k1) ? root->find(k1) : root->find(k2);
            if (it == root->end() || it->second.IsNull()) {
                return true;
            }
            const auto* s = it->second.AsString();
            if (!s || s->size() > maxLen) {
                return SetJsonError(outError, std::format("Field '{}' must be string (<= {} bytes)", it->first, maxLen));
            }
            dest = *s;
            return true;
        };
        if (!parseStringAlias("displayName", "name", outRequest.displayName, kMaxDisplayNameBytes) ||
            !parseStringAlias("slotKeyPrefix", "slotKey", outRequest.slotKeyPrefix, 128u)) {
            return false;
        }

        if (const auto rIt = root->find("race"); rIt != root->end() && !rIt->second.IsNull()) {
            if (!ParseFormSelectorValue(rIt->second, outRequest.race, "race", outError)) {
                return false;
            }
        }
        const auto dAIt = root->contains("donorA") ? root->find("donorA") : root->find("parentA");
        if (dAIt != root->end() && !dAIt->second.IsNull()) {
            if (!ParseFormSelectorValue(dAIt->second, outRequest.donorA, dAIt->first, outError)) {
                return false;
            }
        }
        const auto dBIt = root->contains("donorB") ? root->find("donorB") : root->find("parentB");
        if (dBIt != root->end() && !dBIt->second.IsNull()) {
            if (!ParseFormSelectorValue(dBIt->second, outRequest.donorB, dBIt->first, outError)) {
                return false;
            }
        }

        if (!outRequest.donorModeExplicit) {
            const bool hasA = outRequest.donorA.specified && !outRequest.donorA.clear;
            const bool hasB = outRequest.donorB.specified && !outRequest.donorB.clear;
            if (hasA && hasB) {
                outRequest.donorMode = Foundation::DonorMode::TwoDonorOffspring;
            } else if (hasA) {
                outRequest.donorMode = Foundation::DonorMode::OneDonorRelative;
            }
        }

        return ParseSharedPatchesFromRoot(
            *root, outRequest.archetype, outRequest.ai, outRequest.stats, outError);
    }

    bool ParseJsonUpdateRequest(
        std::string_view jsonText,
        JsonUpdateRequest& outRequest,
        std::string* outError) {
        outRequest = JsonUpdateRequest{};
        JsonValue doc{};
        if (!ParseJsonDocument(jsonText, doc, outError)) {
            return false;
        }
        const auto* root = doc.AsObject();
        if (!root) {
            return SetJsonError(outError, "Update JSON root must be an object");
        }

        const auto nameIt = root->contains("displayName") ? root->find("displayName") : root->find("name");
        if (nameIt != root->end() && !nameIt->second.IsNull()) {
            const auto* s = nameIt->second.AsString();
            if (!s || s->empty() || s->size() > kMaxDisplayNameBytes) {
                return SetJsonError(outError, "Field 'displayName' must be a non-empty string (<= 256 bytes)");
            }
            outRequest.displayName = *s;
        }

        if (const auto ageIt = root->find("ageBracket"); ageIt != root->end() && !ageIt->second.IsNull()) {
            Foundation::AgeBracket br = Foundation::AgeBracket::Auto;
            if (!ParseAgeBracketJson(ageIt->second, br, "ageBracket", outError)) {
                return false;
            }
            outRequest.ageBracket = br;
        }

        if (const auto fpIt = root->find("forcePersist"); fpIt != root->end() && !fpIt->second.IsNull()) {
            const auto* b = fpIt->second.AsBool();
            if (!b) {
                return SetJsonError(outError, "Field 'forcePersist' requires boolean");
            }
            outRequest.forcePersist = *b;
        }

        if (const auto roIt = root->find("refreshOutfit"); roIt != root->end() && !roIt->second.IsNull()) {
            const auto* b = roIt->second.AsBool();
            if (!b) {
                return SetJsonError(outError, "Field 'refreshOutfit' requires boolean");
            }
            outRequest.refreshOutfit = *b;
        }

        return ParseSharedPatchesFromRoot(
            *root, outRequest.archetype, outRequest.ai, outRequest.stats, outError);
    }

}  // namespace MORWHA::State
