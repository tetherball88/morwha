#include "morwha/state/DynamicNpcRegistry.h"

#include "JsonDom.h"
#include "morwha/state/CoSaveCodec.h"

#include <array>
#include <cmath>
#include <filesystem>
#include <format>
#include <fstream>
#include <limits>
#include <string>
#include <unordered_set>

namespace MORWHA::State {

    namespace {

        constexpr std::size_t kMaxDisplayNameBytes = 256u;
        constexpr std::size_t kMaxPluginNameBytes = 260u;
        constexpr std::uintmax_t kMaxRegistryFileBytes = 128ull * 1024ull * 1024ull;

        bool SetError(std::string* outError, std::string msg) {
            return Detail::SetJsonError(outError, std::move(msg));
        }

        [[nodiscard]] bool HasControlChars(std::string_view text) noexcept {
            for (const unsigned char ch : text) {
                if (ch < 0x20u || ch == 0x7Fu) {
                    return true;
                }
            }
            return false;
        }

        [[nodiscard]] bool ValidatePluginFormRef(
            const PluginFormRef& ref,
            bool required,
            std::string_view fieldName,
            std::string* outError) {
            if (ref.IsEmpty()) {
                return required
                           ? SetError(outError, std::format("{} must not be empty", fieldName))
                           : true;
            }
            if (ref.plugin.empty() || ref.plugin.size() > kMaxPluginNameBytes || HasControlChars(ref.plugin)) {
                return SetError(outError, std::format("{} has invalid plugin name", fieldName));
            }
            if (ref.localId == 0 || ref.localId > 0x00FFFFFFu) {
                return SetError(outError, std::format("{} has out-of-range localId 0x{:X}", fieldName, ref.localId));
            }
            return true;
        }

        void AppendEscapedJsonString(std::string& out, std::string_view text) {
            out.push_back('"');
            for (const unsigned char ch : text) {
                switch (ch) {
                    case '"':
                        out.append("\\\"");
                        break;
                    case '\\':
                        out.append("\\\\");
                        break;
                    case '\b':
                        out.append("\\b");
                        break;
                    case '\f':
                        out.append("\\f");
                        break;
                    case '\n':
                        out.append("\\n");
                        break;
                    case '\r':
                        out.append("\\r");
                        break;
                    case '\t':
                        out.append("\\t");
                        break;
                    default:
                        if (ch < 0x20u) {
                            out.append(std::format("\\u{:04X}", static_cast<unsigned int>(ch)));
                        } else {
                            out.push_back(static_cast<char>(ch));
                        }
                        break;
                }
            }
            out.push_back('"');
        }

        void AppendJsonFloat(std::string& out, float value) {
            if (!std::isfinite(value)) {
                out.push_back('0');
                return;
            }
            if (value == 0.0f) {
                out.push_back('0');
                return;
            }
            std::array<char, 64> buf{};
            const auto [ptr, ec] = std::to_chars(buf.data(), buf.data() + buf.size(), value);
            if (ec == std::errc{}) {
                out.append(buf.data(), ptr);
            } else {
                out.push_back('0');
            }
        }

        void AppendPluginFormRefJson(std::string& out, const PluginFormRef& ref) {
            if (ref.IsEmpty()) {
                out.append("null");
                return;
            }
            out.append("{\"plugin\":");
            AppendEscapedJsonString(out, ref.plugin);
            out.append(",\"localId\":\"");
            out.append(std::format("0x{:06X}", ref.localId));
            out.append("\"}");
        }

        void AppendRgbJson(std::string& out, Foundation::RgbColor color) {
            out.append(std::format("[{},{},{}]", color.r, color.g, color.b));
        }

        bool ParseRgbColor(const Detail::JsonValue& val, Foundation::RgbColor& out, std::string_view field, std::string* outError) {
            const auto* arr = val.AsArray();
            if (!arr || arr->size() != 3) {
                return SetError(outError, std::format("Field '{}' must be [r,g,b]", field));
            }
            std::uint8_t r = 0, g = 0, b = 0;
            if (!Detail::ParseUIntFromJson((*arr)[0], r, field, outError) ||
                !Detail::ParseUIntFromJson((*arr)[1], g, field, outError) ||
                !Detail::ParseUIntFromJson((*arr)[2], b, field, outError)) {
                return false;
            }
            out = Foundation::RgbColor{ r, g, b };
            return true;
        }

        bool ParsePluginFormRef(
            const Detail::JsonValue& val,
            PluginFormRef& out,
            bool allowNull,
            std::string_view field,
            std::string* outError) {
            if (val.IsNull()) {
                if (!allowNull) {
                    return SetError(outError, std::format("Field '{}' cannot be null", field));
                }
                out = PluginFormRef{};
                return true;
            }
            const auto* obj = val.AsObject();
            if (!obj) {
                return SetError(outError, std::format("Field '{}' must be form-ref object", field));
            }
            const auto pluginIt = obj->find("plugin");
            const auto localIt = obj->find("localId");
            if (pluginIt == obj->end() || localIt == obj->end()) {
                return SetError(outError, std::format("Field '{}' missing 'plugin' or 'localId'", field));
            }
            const auto* pluginStr = pluginIt->second.AsString();
            if (!pluginStr) {
                return SetError(outError, std::format("Field '{}.plugin' must be string", field));
            }
            std::uint32_t localId = 0;
            if (!Detail::ParseUIntFromJson(localIt->second, localId, field, outError)) {
                return false;
            }
            out.plugin = *pluginStr;
            out.localId = localId;
            return true;
        }

        bool ParseOptionalFormRef(
            const Detail::JsonObject& obj,
            const char* key,
            PluginFormRef& out,
            std::string* outError) {
            const auto it = obj.find(key);
            if (it == obj.end()) {
                out = PluginFormRef{};
                return true;
            }
            return ParsePluginFormRef(it->second, out, true, key, outError);
        }

        bool ParseNpcObject(const Detail::JsonValue& val, DynamicNpcSpec& out, std::string* outError) {
            const auto* obj = val.AsObject();
            if (!obj) {
                return SetError(outError, "NPC entry must be JSON object");
            }

            auto requireField = [&](const char* key) -> const Detail::JsonValue* {
                const auto it = obj->find(key);
                if (it == obj->end()) {
                    SetError(outError, std::format("NPC missing required field '{}'", key));
                    return nullptr;
                }
                return &it->second;
            };

            const auto* slotKeyVal = requireField("slotKey");
            const auto* raceVal = requireField("raceRef");
            const auto* morphsVal = requireField("faceMorphs");
            const auto* skinVal = requireField("skinColor");
            const auto* hairVal = requireField("hairColor");
            if (!slotKeyVal || !raceVal || !morphsVal || !skinVal || !hairVal) {
                return false;
            }
            const auto* slotKeyStr = slotKeyVal->AsString();
            if (!slotKeyStr) {
                return SetError(outError, "'slotKey' must be string");
            }
            out.slotKey = *slotKeyStr;

            if (const auto it = obj->find("displayName"); it != obj->end()) {
                const auto* s = it->second.AsString();
                if (!s) {
                    return SetError(outError, "'displayName' must be string");
                }
                out.displayName = *s;
            }
            if (const auto it = obj->find("donorMode"); it != obj->end()) {
                std::uint8_t raw = 0;
                if (!Detail::ParseUIntFromJson(it->second, raw, "donorMode", outError)) {
                    return false;
                }
                out.donorMode = static_cast<Foundation::DonorMode>(raw);
            }
            if (const auto it = obj->find("topology"); it != obj->end()) {
                std::uint8_t raw = 0;
                if (!Detail::ParseUIntFromJson(it->second, raw, "topology", outError)) {
                    return false;
                }
                out.topology = static_cast<Foundation::TopologyPool>(raw);
            }
            if (const auto it = obj->find("seed"); it != obj->end()) {
                std::uint64_t seedRaw = 0;
                if (!Detail::ParseUIntFromJson(it->second, seedRaw, "seed", outError)) {
                    return false;
                }
                out.seed = Foundation::Seed64{ seedRaw };
            }
            if (const auto it = obj->find("committed"); it != obj->end()) {
                const auto* b = it->second.AsBool();
                if (!b) {
                    return SetError(outError, "'committed' must be boolean");
                }
                out.committed = *b;
            }
            if (const auto it = obj->find("ageBracket"); it != obj->end()) {
                std::uint8_t raw = 0;
                if (!Detail::ParseUIntFromJson(it->second, raw, "ageBracket", outError)) {
                    return false;
                }
                out.ageBracket = static_cast<Foundation::AgeBracket>(raw);
            }
            if (const auto it = obj->find("forcePersist"); it != obj->end()) {
                const auto* b = it->second.AsBool();
                if (!b) {
                    return SetError(outError, "'forcePersist' must be boolean");
                }
                out.forcePersist = *b;
            }
            if (!ParsePluginFormRef(*raceVal, out.raceRef, false, "raceRef", outError)) {
                return false;
            }
            if (const auto it = obj->find("sex"); it != obj->end()) {
                std::uint8_t raw = 0;
                if (!Detail::ParseUIntFromJson(it->second, raw, "sex", outError)) {
                    return false;
                }
                out.sex = static_cast<Foundation::Sex>(raw);
            }
            if (!ParseOptionalFormRef(*obj, "templateNpcRef", out.templateNpcRef, outError) ||
                !ParseOptionalFormRef(*obj, "voiceTypeRef", out.voiceTypeRef, outError) ||
                !ParseOptionalFormRef(*obj, "npcClassRef", out.npcClassRef, outError) ||
                !ParseOptionalFormRef(*obj, "combatStyleRef", out.combatStyleRef, outError) ||
                !ParseOptionalFormRef(*obj, "defaultOutfitRef", out.defaultOutfitRef, outError) ||
                !ParseOptionalFormRef(*obj, "sleepOutfitRef", out.sleepOutfitRef, outError) ||
                !ParseOptionalFormRef(*obj, "crimeFactionRef", out.crimeFactionRef, outError) ||
                !ParseOptionalFormRef(*obj, "faceTextureSetRef", out.faceTextureSetRef, outError) ||
                !ParseOptionalFormRef(*obj, "hairColorFormRef", out.hairColorFormRef, outError) ||
                !ParseOptionalFormRef(*obj, "donorARef", out.donorARef, outError) ||
                !ParseOptionalFormRef(*obj, "donorBRef", out.donorBRef, outError) ||
                !ParseOptionalFormRef(*obj, "secondaryRaceRef", out.secondaryRaceRef, outError)) {
                return false;
            }
            if (const auto it = obj->find("chimeraTier"); it != obj->end() && !it->second.IsNull()) {
                if (!Detail::ParseUIntFromJson(it->second, out.chimeraTier, "chimeraTier", outError)) {
                    return false;
                }
            }
            auto parseOptionalString = [&](const char* key, std::string& outStr) -> bool {
                const auto it = obj->find(key);
                if (it == obj->end() || it->second.IsNull()) {
                    outStr.clear();
                    return true;
                }
                const auto* s = it->second.AsString();
                if (!s) {
                    return SetError(outError, std::format("'{}' must be string", key));
                }
                outStr = *s;
                return true;
            };
            if (!parseOptionalString("donorASlotKey", out.donorASlotKey) ||
                !parseOptionalString("donorBSlotKey", out.donorBSlotKey) ||
                !parseOptionalString("hybridSkinId", out.hybridSkinId)) {
                return false;
            }

            if (const auto it = obj->find("headParts"); it != obj->end()) {
                const auto* arr = it->second.AsArray();
                if (!arr || arr->size() > kMaxHeadPartsPerNpc) {
                    return SetError(outError, "'headParts' must be an array within bounds");
                }
                out.headParts.reserve(arr->size());
                for (const auto& hpVal : *arr) {
                    PluginFormRef hpRef;
                    if (!ParsePluginFormRef(hpVal, hpRef, false, "headParts[]", outError)) {
                        return false;
                    }
                    out.headParts.push_back(std::move(hpRef));
                }
            }

            if (const auto it = obj->find("height"); it != obj->end()) {
                if (!Detail::ParseFloatFromJson(it->second, out.height, "height", outError)) {
                    return false;
                }
            }
            if (const auto it = obj->find("weight"); it != obj->end()) {
                if (!Detail::ParseFloatFromJson(it->second, out.weight, "weight", outError)) {
                    return false;
                }
            }

            const auto* morphObj = morphsVal->AsObject();
            if (!morphObj) {
                return SetError(outError, "'faceMorphs' must be JSON object");
            }
            const auto morphArrIt = morphObj->find("morphs");
            const auto partArrIt = morphObj->find("parts");
            if (morphArrIt == morphObj->end() || partArrIt == morphObj->end()) {
                return SetError(outError, "'faceMorphs' missing 'morphs' or 'parts'");
            }
            const auto* morphArr = morphArrIt->second.AsArray();
            const auto* partArr = partArrIt->second.AsArray();
            if (!morphArr || morphArr->size() != Foundation::kVanillaFaceMorphCount ||
                !partArr || partArr->size() != static_cast<std::size_t>(Foundation::FacePartSlot::kCount)) {
                return SetError(outError, "'faceMorphs' arrays have wrong size");
            }
            for (std::size_t i = 0; i < Foundation::kVanillaFaceMorphCount; ++i) {
                if (!Detail::ParseFloatFromJson((*morphArr)[i], out.faceMorphs.morphs[i], "faceMorphs.morphs", outError)) {
                    return false;
                }
            }
            for (std::size_t i = 0; i < partArr->size(); ++i) {
                if (!Detail::ParseSignedIntFromJson((*partArr)[i], out.faceMorphs.parts[i], "faceMorphs.parts", outError)) {
                    return false;
                }
            }

            if (!ParseRgbColor(*skinVal, out.skinColor, "skinColor", outError) ||
                !ParseRgbColor(*hairVal, out.hairColor, "hairColor", outError)) {
                return false;
            }

            if (const auto it = obj->find("tintLayers"); it != obj->end()) {
                const auto* arr = it->second.AsArray();
                if (!arr || arr->size() > kMaxTintLayersPerNpc) {
                    return SetError(outError, "'tintLayers' must be an array within bounds");
                }
                out.tintLayers.reserve(arr->size());
                for (const auto& layerVal : *arr) {
                    const auto* layerObj = layerVal.AsObject();
                    if (!layerObj) {
                        return SetError(outError, "'tintLayers[]' must be object");
                    }
                    TintLayerSpec layer{};
                    const auto semIt = layerObj->find("semanticType");
                    const auto colIt = layerObj->find("color");
                    const auto interpIt = layerObj->find("interpolationValue");
                    if (semIt == layerObj->end() || colIt == layerObj->end() || interpIt == layerObj->end()) {
                        return SetError(outError, "'tintLayers[]' missing required fields");
                    }
                    std::uint8_t semRaw = 0;
                    if (!Detail::ParseUIntFromJson(semIt->second, semRaw, "tintLayers.semanticType", outError)) {
                        return false;
                    }
                    layer.semanticType = static_cast<TintSemanticType>(semRaw);
                    if (const auto idxIt = layerObj->find("explicitTintIndex"); idxIt != layerObj->end()) {
                        if (!Detail::ParseUIntFromJson(idxIt->second, layer.explicitTintIndex, "tintLayers.explicitTintIndex", outError)) {
                            return false;
                        }
                    }
                    if (!ParseRgbColor(colIt->second, layer.color, "tintLayers.color", outError) ||
                        !Detail::ParseUIntFromJson(interpIt->second, layer.interpolationValue, "tintLayers.interpolationValue", outError)) {
                        return false;
                    }
                    out.tintLayers.push_back(layer);
                }
            }

            if (const auto it = obj->find("beastTailKind"); it != obj->end()) {
                std::uint8_t raw = 0;
                if (!Detail::ParseUIntFromJson(it->second, raw, "beastTailKind", outError)) {
                    return false;
                }
                out.beastTailKind = static_cast<BeastTailKind>(raw);
            }
            if (const auto it = obj->find("beastTeethKind"); it != obj->end()) {
                std::uint8_t raw = 0;
                if (!Detail::ParseUIntFromJson(it->second, raw, "beastTeethKind", outError)) {
                    return false;
                }
                out.beastTeethKind = static_cast<BeastTeethKind>(raw);
            }
            if (const auto it = obj->find("skinAddonRefs"); it != obj->end()) {
                const auto* arr = it->second.AsArray();
                if (!arr || arr->size() > kMaxSkinAddonsPerNpc) {
                    return SetError(outError, "'skinAddonRefs' must be an array within bounds");
                }
                out.skinAddonRefs.reserve(arr->size());
                for (const auto& saVal : *arr) {
                    PluginFormRef saRef;
                    if (!ParsePluginFormRef(saVal, saRef, false, "skinAddonRefs[]", outError)) {
                        return false;
                    }
                    out.skinAddonRefs.push_back(std::move(saRef));
                }
            }
            if (const auto it = obj->find("tailColor"); it != obj->end() && !it->second.IsNull()) {
                Foundation::RgbColor c{};
                if (!ParseRgbColor(it->second, c, "tailColor", outError)) {
                    return false;
                }
                out.tailColor = c;
            }
            if (const auto it = obj->find("hornColor"); it != obj->end() && !it->second.IsNull()) {
                Foundation::RgbColor c{};
                if (!ParseRgbColor(it->second, c, "hornColor", outError)) {
                    return false;
                }
                out.hornColor = c;
            }
            if (const auto it = obj->find("ai"); it != obj->end() && !it->second.IsNull()) {
                const auto* aiObj = it->second.AsObject();
                if (!aiObj) {
                    return SetError(outError, "'ai' must be object");
                }
                if (const auto f = aiObj->find("aggression"); f != aiObj->end() &&
                    !Detail::ParseUIntFromJson(f->second, out.ai.aggression, "ai.aggression", outError)) {
                    return false;
                }
                if (const auto f = aiObj->find("confidence"); f != aiObj->end() &&
                    !Detail::ParseUIntFromJson(f->second, out.ai.confidence, "ai.confidence", outError)) {
                    return false;
                }
                if (const auto f = aiObj->find("assistance"); f != aiObj->end() &&
                    !Detail::ParseUIntFromJson(f->second, out.ai.assistance, "ai.assistance", outError)) {
                    return false;
                }
                if (const auto f = aiObj->find("morality"); f != aiObj->end() &&
                    !Detail::ParseUIntFromJson(f->second, out.ai.morality, "ai.morality", outError)) {
                    return false;
                }
                if (const auto f = aiObj->find("isProtected"); f != aiObj->end()) {
                    const auto* b = f->second.AsBool();
                    if (!b) {
                        return SetError(outError, "'ai.isProtected' must be boolean");
                    }
                    out.ai.isProtected = *b;
                }
                if (const auto f = aiObj->find("isEssential"); f != aiObj->end()) {
                    const auto* b = f->second.AsBool();
                    if (!b) {
                        return SetError(outError, "'ai.isEssential' must be boolean");
                    }
                    out.ai.isEssential = *b;
                }
            }
            if (const auto it = obj->find("stats"); it != obj->end() && !it->second.IsNull()) {
                const auto* stObj = it->second.AsObject();
                if (!stObj) {
                    return SetError(outError, "'stats' must be object");
                }
                if (const auto f = stObj->find("level"); f != stObj->end() &&
                    !Detail::ParseUIntFromJson(f->second, out.stats.level, "stats.level", outError)) {
                    return false;
                }
                if (const auto f = stObj->find("calcLevelMin"); f != stObj->end() &&
                    !Detail::ParseUIntFromJson(f->second, out.stats.calcLevelMin, "stats.calcLevelMin", outError)) {
                    return false;
                }
                if (const auto f = stObj->find("calcLevelMax"); f != stObj->end() &&
                    !Detail::ParseUIntFromJson(f->second, out.stats.calcLevelMax, "stats.calcLevelMax", outError)) {
                    return false;
                }
                if (const auto f = stObj->find("autoCalcStats"); f != stObj->end()) {
                    const auto* b = f->second.AsBool();
                    if (!b) {
                        return SetError(outError, "'stats.autoCalcStats' must be boolean");
                    }
                    out.stats.autoCalcStats = *b;
                }
                if (const auto f = stObj->find("pcLevelMult"); f != stObj->end()) {
                    const auto* b = f->second.AsBool();
                    if (!b) {
                        return SetError(outError, "'stats.pcLevelMult' must be boolean");
                    }
                    out.stats.pcLevelMult = *b;
                }
                if (const auto f = stObj->find("health"); f != stObj->end() &&
                    !Detail::ParseUIntFromJson(f->second, out.stats.health, "stats.health", outError)) {
                    return false;
                }
                if (const auto f = stObj->find("magicka"); f != stObj->end() &&
                    !Detail::ParseUIntFromJson(f->second, out.stats.magicka, "stats.magicka", outError)) {
                    return false;
                }
                if (const auto f = stObj->find("stamina"); f != stObj->end() &&
                    !Detail::ParseUIntFromJson(f->second, out.stats.stamina, "stats.stamina", outError)) {
                    return false;
                }
                if (const auto f = stObj->find("skills"); f != stObj->end()) {
                    const auto* skArr = f->second.AsArray();
                    if (!skArr || skArr->size() != kNpcSkillCount) {
                        return SetError(outError, "'stats.skills' must be 18-element array");
                    }
                    for (std::size_t sIdx = 0; sIdx < kNpcSkillCount; ++sIdx) {
                        if (!Detail::ParseUIntFromJson((*skArr)[sIdx], out.stats.skills[sIdx], "stats.skills", outError)) {
                            return false;
                        }
                    }
                }
            }
            auto parseFormRefArray = [&](const char* key,
                                         std::size_t maxCount,
                                         std::vector<PluginFormRef>& outVec) -> bool {
                const auto it = obj->find(key);
                if (it == obj->end() || it->second.IsNull()) {
                    return true;
                }
                const auto* arr = it->second.AsArray();
                if (!arr || arr->size() > maxCount) {
                    return SetError(outError, std::format("'{}' must be an array within bounds", key));
                }
                outVec.reserve(arr->size());
                for (const auto& elem : *arr) {
                    PluginFormRef ref;
                    if (!ParsePluginFormRef(elem, ref, false, key, outError)) {
                        return false;
                    }
                    outVec.push_back(std::move(ref));
                }
                return true;
            };
            if (!parseFormRefArray("perkRefs", kMaxPerksPerNpc, out.perkRefs) ||
                !parseFormRefArray("spellRefs", kMaxSpellsPerNpc, out.spellRefs)) {
                return false;
            }
            if (const auto it = obj->find("factions"); it != obj->end() && !it->second.IsNull()) {
                const auto* arr = it->second.AsArray();
                if (!arr || arr->size() > kMaxFactionsPerNpc) {
                    return SetError(outError, "'factions' must be an array within bounds");
                }
                out.factions.reserve(arr->size());
                for (const auto& fVal : *arr) {
                    const auto* fObj = fVal.AsObject();
                    if (!fObj) {
                        return SetError(outError, "'factions[]' must be object");
                    }
                    const auto refIt = fObj->find("factionRef");
                    const auto rankIt = fObj->find("rank");
                    if (refIt == fObj->end() || rankIt == fObj->end()) {
                        return SetError(outError, "'factions[]' missing 'factionRef' or 'rank'");
                    }
                    FactionRankSpec fr{};
                    int rankSigned = 0;
                    if (!ParsePluginFormRef(refIt->second, fr.factionRef, false, "factions.factionRef", outError) ||
                        !Detail::ParseSignedIntFromJson(rankIt->second, rankSigned, "factions.rank", outError)) {
                        return false;
                    }
                    if (rankSigned < -1 || rankSigned > 127) {
                        return SetError(outError, "'factions.rank' must be in [-1, 127]");
                    }
                    fr.rank = static_cast<std::int8_t>(rankSigned);
                    out.factions.push_back(std::move(fr));
                }
            }

            return ValidateDynamicNpcSpec(out, outError);
        }

        bool ReadAndParseRegistryFile(
            const std::filesystem::path& filePath,
            std::vector<DynamicNpcSpec>& outSpecs,
            std::string* outError) {
            std::error_code ec;
            if (!std::filesystem::is_regular_file(filePath, ec) || ec) {
                return SetError(outError, "Registry path is not a regular file");
            }
            const auto fileSize = std::filesystem::file_size(filePath, ec);
            if (ec || fileSize == 0 || fileSize > kMaxRegistryFileBytes) {
                return SetError(outError, "Registry file is empty or exceeds size limit");
            }
            std::ifstream in(filePath, std::ios::binary);
            if (!in.is_open()) {
                return SetError(outError, "Failed to open registry file for reading");
            }
            std::string content(static_cast<std::size_t>(fileSize), '\0');
            in.read(content.data(), static_cast<std::streamsize>(fileSize));
            if (!in || static_cast<std::size_t>(in.gcount()) != fileSize) {
                return SetError(outError, "Failed to read complete registry file");
            }
            return ParseRegistryJson(content, outSpecs, outError);
        }

    }  // namespace

    bool ValidateDynamicNpcSpec(const DynamicNpcSpec& spec, std::string* outError) {
        if (spec.slotKey.empty() || spec.slotKey.size() > kMaxSlotKeyBytes || HasControlChars(spec.slotKey)) {
            return SetError(outError, "slotKey must be 1..256 non-control bytes");
        }
        if (spec.displayName.size() > kMaxDisplayNameBytes) {
            return SetError(outError, "displayName exceeds 256 bytes");
        }
        if (static_cast<std::uint8_t>(spec.sex) > static_cast<std::uint8_t>(Foundation::Sex::Female)) {
            return SetError(outError, "Invalid sex enum value");
        }
        if (static_cast<std::uint8_t>(spec.donorMode) >
            static_cast<std::uint8_t>(Foundation::DonorMode::TwoDonorOffspring)) {
            return SetError(outError, "Invalid donorMode enum value");
        }
        if (static_cast<std::uint8_t>(spec.topology) >
            static_cast<std::uint8_t>(Foundation::TopologyPool::CustomFallback)) {
            return SetError(outError, "Invalid topology enum value");
        }
        if (static_cast<std::uint8_t>(spec.ageBracket) >
            static_cast<std::uint8_t>(Foundation::AgeBracket::Elder)) {
            return SetError(outError, "Invalid ageBracket enum value");
        }
        if (!ValidatePluginFormRef(spec.raceRef, true, "raceRef", outError) ||
            !ValidatePluginFormRef(spec.templateNpcRef, false, "templateNpcRef", outError) ||
            !ValidatePluginFormRef(spec.voiceTypeRef, false, "voiceTypeRef", outError) ||
            !ValidatePluginFormRef(spec.npcClassRef, false, "npcClassRef", outError) ||
            !ValidatePluginFormRef(spec.combatStyleRef, false, "combatStyleRef", outError) ||
            !ValidatePluginFormRef(spec.defaultOutfitRef, false, "defaultOutfitRef", outError) ||
            !ValidatePluginFormRef(spec.sleepOutfitRef, false, "sleepOutfitRef", outError) ||
            !ValidatePluginFormRef(spec.crimeFactionRef, false, "crimeFactionRef", outError) ||
            !ValidatePluginFormRef(spec.faceTextureSetRef, false, "faceTextureSetRef", outError) ||
            !ValidatePluginFormRef(spec.hairColorFormRef, false, "hairColorFormRef", outError) ||
            !ValidatePluginFormRef(spec.donorARef, false, "donorARef", outError) ||
            !ValidatePluginFormRef(spec.donorBRef, false, "donorBRef", outError) ||
            !ValidatePluginFormRef(spec.secondaryRaceRef, false, "secondaryRaceRef", outError)) {
            return false;
        }
        if (spec.chimeraTier > 2u) {
            return SetError(outError, "chimeraTier must be 0, 1, or 2");
        }
        if (spec.ai.aggression > 3u || spec.ai.confidence > 4u ||
            spec.ai.assistance > 2u || spec.ai.morality > 3u) {
            return SetError(outError, "Invalid ai enum bounds");
        }
        if (spec.stats.level < 1u || spec.stats.level > 2000u ||
            spec.stats.calcLevelMax > 2000u ||
            spec.stats.health < 1u || spec.stats.health > 20000u ||
            spec.stats.magicka > 20000u || spec.stats.stamina > 20000u) {
            return SetError(outError, "Invalid stats level or attribute bounds");
        }
        if (spec.perkRefs.size() > kMaxPerksPerNpc) {
            return SetError(outError, "perkRefs exceeds max limit");
        }
        for (const auto& pk : spec.perkRefs) {
            if (!ValidatePluginFormRef(pk, true, "perkRefs[]", outError)) {
                return false;
            }
        }
        if (spec.spellRefs.size() > kMaxSpellsPerNpc) {
            return SetError(outError, "spellRefs exceeds max limit");
        }
        for (const auto& sp : spec.spellRefs) {
            if (!ValidatePluginFormRef(sp, true, "spellRefs[]", outError)) {
                return false;
            }
        }
        if (spec.factions.size() > kMaxFactionsPerNpc) {
            return SetError(outError, "factions exceeds max limit");
        }
        for (const auto& fac : spec.factions) {
            if (!ValidatePluginFormRef(fac.factionRef, true, "factions[].factionRef", outError)) {
                return false;
            }
            if (fac.rank < -1) {
                return SetError(outError, "factions[].rank must be >= -1");
            }
        }
        if (spec.donorASlotKey.size() > kMaxSlotKeyBytes || HasControlChars(spec.donorASlotKey)) {
            return SetError(outError, "donorASlotKey must be <= 256 non-control bytes");
        }
        if (spec.donorBSlotKey.size() > kMaxSlotKeyBytes || HasControlChars(spec.donorBSlotKey)) {
            return SetError(outError, "donorBSlotKey must be <= 256 non-control bytes");
        }
        if (spec.hybridSkinId.size() > 64u || HasControlChars(spec.hybridSkinId) ||
            spec.hybridSkinId.find("..") != std::string::npos ||
            spec.hybridSkinId.find('/') != std::string::npos ||
            spec.hybridSkinId.find('\\') != std::string::npos) {
            return SetError(outError, "hybridSkinId must be <= 64 safe identifier bytes");
        }
        if (spec.headParts.size() > kMaxHeadPartsPerNpc) {
            return SetError(outError, "headParts exceeds max limit");
        }
        for (const auto& hp : spec.headParts) {
            if (!ValidatePluginFormRef(hp, true, "headParts[]", outError)) {
                return false;
            }
        }
        if (!std::isfinite(spec.height) || spec.height < 0.25f || spec.height > 2.5f) {
            return SetError(outError, "height must be finite in [0.25, 2.5]");
        }
        if (!std::isfinite(spec.weight) || spec.weight < 0.0f || spec.weight > 100.0f) {
            return SetError(outError, "weight must be finite in [0, 100]");
        }
        for (std::size_t i = 0; i < Foundation::kVanillaFaceMorphCount; ++i) {
            const float m = spec.faceMorphs.morphs[i];
            if (!std::isfinite(m) || m < -1.0f || m > 1.0f) {
                return SetError(outError, std::format("faceMorphs.morphs[{}] out of bounds", i));
            }
        }
        if (spec.tintLayers.size() > kMaxTintLayersPerNpc) {
            return SetError(outError, "tintLayers exceeds max limit");
        }
        for (const auto& layer : spec.tintLayers) {
            if (static_cast<std::uint8_t>(layer.semanticType) >
                static_cast<std::uint8_t>(TintSemanticType::Dirt)) {
                return SetError(outError, "Invalid tint layer semanticType");
            }
            if (layer.interpolationValue > 100u) {
                return SetError(outError, "Tint layer interpolationValue must be <= 100");
            }
        }
        if (static_cast<std::uint8_t>(spec.beastTailKind) >
            static_cast<std::uint8_t>(BeastTailKind::Argonian)) {
            return SetError(outError, "Invalid beastTailKind enum value");
        }
        if (static_cast<std::uint8_t>(spec.beastTeethKind) >
            static_cast<std::uint8_t>(BeastTeethKind::Sharp)) {
            return SetError(outError, "Invalid beastTeethKind enum value");
        }
        if (spec.skinAddonRefs.size() > kMaxSkinAddonsPerNpc) {
            return SetError(outError, "skinAddonRefs exceeds max limit");
        }
        for (const auto& sa : spec.skinAddonRefs) {
            if (!ValidatePluginFormRef(sa, true, "skinAddonRefs[]", outError)) {
                return false;
            }
        }
        return true;
    }

    std::string SerializeRegistryJson(std::span<const DynamicNpcSpec> specs) {
        std::string out;
        out.reserve(specs.size() * 768u + 128u);
        out.append("{\n  \"schemaVersion\": ");
        out.append(std::to_string(kRegistrySchemaVersion));
        out.append(",\n  \"owner\": \"MORWHA\",\n  \"npcs\": [");

        for (std::size_t i = 0; i < specs.size(); ++i) {
            const auto& spec = specs[i];
            out.append(i == 0 ? "\n    {\n" : ",\n    {\n");

            out.append("      \"slotKey\": ");
            AppendEscapedJsonString(out, spec.slotKey);
            out.append(",\n      \"displayName\": ");
            AppendEscapedJsonString(out, spec.displayName);
            out.append(std::format(",\n      \"donorMode\": {}", static_cast<unsigned>(spec.donorMode)));
            out.append(std::format(",\n      \"topology\": {}", static_cast<unsigned>(spec.topology)));
            out.append(std::format(",\n      \"seed\": \"0x{:016X}\"", spec.seed.raw()));
            out.append(std::format(",\n      \"committed\": {}", spec.committed ? "true" : "false"));
            out.append(std::format(",\n      \"ageBracket\": {}", static_cast<unsigned>(spec.ageBracket)));
            if (!spec.forcePersist) {
                out.append(",\n      \"forcePersist\": false");
            }

            out.append(",\n      \"raceRef\": ");
            AppendPluginFormRefJson(out, spec.raceRef);
            out.append(std::format(",\n      \"sex\": {}", static_cast<unsigned>(spec.sex)));
            out.append(",\n      \"templateNpcRef\": ");
            AppendPluginFormRefJson(out, spec.templateNpcRef);
            out.append(",\n      \"voiceTypeRef\": ");
            AppendPluginFormRefJson(out, spec.voiceTypeRef);
            out.append(",\n      \"npcClassRef\": ");
            AppendPluginFormRefJson(out, spec.npcClassRef);
            if (!spec.combatStyleRef.IsEmpty()) {
                out.append(",\n      \"combatStyleRef\": ");
                AppendPluginFormRefJson(out, spec.combatStyleRef);
            }
            out.append(",\n      \"defaultOutfitRef\": ");
            AppendPluginFormRefJson(out, spec.defaultOutfitRef);
            if (!spec.sleepOutfitRef.IsEmpty()) {
                out.append(",\n      \"sleepOutfitRef\": ");
                AppendPluginFormRefJson(out, spec.sleepOutfitRef);
            }
            if (!spec.crimeFactionRef.IsEmpty()) {
                out.append(",\n      \"crimeFactionRef\": ");
                AppendPluginFormRefJson(out, spec.crimeFactionRef);
            }
            out.append(",\n      \"faceTextureSetRef\": ");
            AppendPluginFormRefJson(out, spec.faceTextureSetRef);
            out.append(",\n      \"hairColorFormRef\": ");
            AppendPluginFormRefJson(out, spec.hairColorFormRef);

            out.append(",\n      \"headParts\": [");
            for (std::size_t hpIdx = 0; hpIdx < spec.headParts.size(); ++hpIdx) {
                if (hpIdx > 0) {
                    out.push_back(',');
                }
                AppendPluginFormRefJson(out, spec.headParts[hpIdx]);
            }
            out.append("],\n      \"height\": ");
            AppendJsonFloat(out, spec.height);
            out.append(",\n      \"weight\": ");
            AppendJsonFloat(out, spec.weight);

            out.append(",\n      \"faceMorphs\": {\n        \"morphs\": [");
            for (std::size_t mIdx = 0; mIdx < Foundation::kVanillaFaceMorphCount; ++mIdx) {
                if (mIdx > 0) {
                    out.push_back(',');
                }
                AppendJsonFloat(out, spec.faceMorphs.morphs[mIdx]);
            }
            out.append("],\n        \"parts\": [");
            for (std::size_t pIdx = 0; pIdx < spec.faceMorphs.parts.size(); ++pIdx) {
                if (pIdx > 0) {
                    out.push_back(',');
                }
                out.append(std::to_string(spec.faceMorphs.parts[pIdx]));
            }
            out.append("]\n      },\n      \"skinColor\": ");
            AppendRgbJson(out, spec.skinColor);
            out.append(",\n      \"hairColor\": ");
            AppendRgbJson(out, spec.hairColor);

            out.append(",\n      \"tintLayers\": [");
            for (std::size_t tIdx = 0; tIdx < spec.tintLayers.size(); ++tIdx) {
                const auto& layer = spec.tintLayers[tIdx];
                if (tIdx > 0) {
                    out.push_back(',');
                }
                out.append(std::format(
                    "{{\"semanticType\":{},\"explicitTintIndex\":{},\"color\":",
                    static_cast<unsigned>(layer.semanticType),
                    layer.explicitTintIndex));
                AppendRgbJson(out, layer.color);
                out.append(std::format(",\"interpolationValue\":{}}}", layer.interpolationValue));
            }
            out.append("]");
            if (spec.beastTailKind != BeastTailKind::None) {
                out.append(std::format(",\n      \"beastTailKind\": {}", static_cast<unsigned>(spec.beastTailKind)));
            }
            if (spec.beastTeethKind != BeastTeethKind::None) {
                out.append(std::format(",\n      \"beastTeethKind\": {}", static_cast<unsigned>(spec.beastTeethKind)));
            }
            if (!spec.skinAddonRefs.empty()) {
                out.append(",\n      \"skinAddonRefs\": [");
                for (std::size_t saIdx = 0; saIdx < spec.skinAddonRefs.size(); ++saIdx) {
                    if (saIdx > 0) {
                        out.push_back(',');
                    }
                    AppendPluginFormRefJson(out, spec.skinAddonRefs[saIdx]);
                }
                out.push_back(']');
            }
            if (spec.tailColor.has_value()) {
                out.append(",\n      \"tailColor\": ");
                AppendRgbJson(out, *spec.tailColor);
            }
            if (!spec.hybridSkinId.empty()) {
                out.append(",\n      \"hybridSkinId\": ");
                AppendEscapedJsonString(out, spec.hybridSkinId);
            }
            if (spec.hornColor.has_value()) {
                out.append(",\n      \"hornColor\": ");
                AppendRgbJson(out, *spec.hornColor);
            }
            if (!spec.donorARef.IsEmpty()) {
                out.append(",\n      \"donorARef\": ");
                AppendPluginFormRefJson(out, spec.donorARef);
            }
            if (!spec.donorBRef.IsEmpty()) {
                out.append(",\n      \"donorBRef\": ");
                AppendPluginFormRefJson(out, spec.donorBRef);
            }
            if (!spec.donorASlotKey.empty()) {
                out.append(",\n      \"donorASlotKey\": ");
                AppendEscapedJsonString(out, spec.donorASlotKey);
            }
            if (!spec.donorBSlotKey.empty()) {
                out.append(",\n      \"donorBSlotKey\": ");
                AppendEscapedJsonString(out, spec.donorBSlotKey);
            }
            if (!spec.secondaryRaceRef.IsEmpty()) {
                out.append(",\n      \"secondaryRaceRef\": ");
                AppendPluginFormRefJson(out, spec.secondaryRaceRef);
            }
            if (spec.chimeraTier != 0u) {
                out.append(std::format(",\n      \"chimeraTier\": {}", static_cast<unsigned>(spec.chimeraTier)));
            }
            if (!spec.ai.IsDefaultCitizen()) {
                out.append(std::format(
                    ",\n      \"ai\": {{\"aggression\":{},\"confidence\":{},\"assistance\":{},\"morality\":{},\"isProtected\":{},\"isEssential\":{}}}",
                    spec.ai.aggression,
                    spec.ai.confidence,
                    spec.ai.assistance,
                    spec.ai.morality,
                    spec.ai.isProtected ? "true" : "false",
                    spec.ai.isEssential ? "true" : "false"));
            }
            if (!spec.stats.IsDefaultCitizen()) {
                out.append(std::format(
                    ",\n      \"stats\": {{\"level\":{},\"calcLevelMin\":{},\"calcLevelMax\":{},\"autoCalcStats\":{},\"pcLevelMult\":{},\"health\":{},\"magicka\":{},\"stamina\":{},\"skills\":[",
                    spec.stats.level,
                    spec.stats.calcLevelMin,
                    spec.stats.calcLevelMax,
                    spec.stats.autoCalcStats ? "true" : "false",
                    spec.stats.pcLevelMult ? "true" : "false",
                    spec.stats.health,
                    spec.stats.magicka,
                    spec.stats.stamina));
                for (std::size_t sIdx = 0; sIdx < kNpcSkillCount; ++sIdx) {
                    if (sIdx > 0u) {
                        out.push_back(',');
                    }
                    out.append(std::to_string(spec.stats.skills[sIdx]));
                }
                out.append("]}");
            }
            if (!spec.perkRefs.empty()) {
                out.append(",\n      \"perkRefs\": [");
                for (std::size_t pIdx = 0; pIdx < spec.perkRefs.size(); ++pIdx) {
                    if (pIdx > 0u) {
                        out.push_back(',');
                    }
                    AppendPluginFormRefJson(out, spec.perkRefs[pIdx]);
                }
                out.push_back(']');
            }
            if (!spec.spellRefs.empty()) {
                out.append(",\n      \"spellRefs\": [");
                for (std::size_t sIdx = 0; sIdx < spec.spellRefs.size(); ++sIdx) {
                    if (sIdx > 0u) {
                        out.push_back(',');
                    }
                    AppendPluginFormRefJson(out, spec.spellRefs[sIdx]);
                }
                out.push_back(']');
            }
            if (!spec.factions.empty()) {
                out.append(",\n      \"factions\": [");
                for (std::size_t fIdx = 0; fIdx < spec.factions.size(); ++fIdx) {
                    if (fIdx > 0u) {
                        out.push_back(',');
                    }
                    out.append("{\"factionRef\":");
                    AppendPluginFormRefJson(out, spec.factions[fIdx].factionRef);
                    out.append(std::format(",\"rank\":{}}}", static_cast<int>(spec.factions[fIdx].rank)));
                }
                out.push_back(']');
            }
            out.append("\n    }");
        }

        if (!specs.empty()) {
            out.append("\n  ");
        }
        out.append("]\n}\n");
        return out;
    }

    bool ParseRegistryJson(
        std::string_view jsonText,
        std::vector<DynamicNpcSpec>& outSpecs,
        std::string* outError) {
        outSpecs.clear();

        Detail::JsonValue rootVal;
        if (!Detail::ParseJsonDocument(jsonText, rootVal, outError, /*allowDuplicateKeys=*/false)) {
            return false;
        }
        const auto* rootObj = rootVal.AsObject();
        if (!rootObj) {
            return SetError(outError, "Registry root must be JSON object");
        }

        const auto verIt = rootObj->find("schemaVersion");
        if (verIt == rootObj->end()) {
            return SetError(outError, "Missing 'schemaVersion'");
        }
        std::uint32_t schemaVer = 0;
        if (!Detail::ParseUIntFromJson(verIt->second, schemaVer, "schemaVersion", outError) ||
            schemaVer != kRegistrySchemaVersion) {
            return SetError(outError, std::format("Unsupported schemaVersion {}", schemaVer));
        }

        if (const auto ownerIt = rootObj->find("owner"); ownerIt != rootObj->end()) {
            const auto* ownerStr = ownerIt->second.AsString();
            if (!ownerStr || *ownerStr != "MORWHA") {
                return SetError(outError, "Registry 'owner' mismatch");
            }
        }

        const auto npcsIt = rootObj->find("npcs");
        if (npcsIt == rootObj->end()) {
            return SetError(outError, "Missing 'npcs' array");
        }
        const auto* npcsArr = npcsIt->second.AsArray();
        if (!npcsArr || npcsArr->size() > kMaxRegistryNpcs) {
            return SetError(outError, "'npcs' must be an array within bounds");
        }

        std::vector<DynamicNpcSpec> parsed;
        parsed.reserve(npcsArr->size());
        std::unordered_set<std::string> seenSlotKeys;
        seenSlotKeys.reserve(npcsArr->size());

        for (const auto& entry : *npcsArr) {
            DynamicNpcSpec spec;
            if (!ParseNpcObject(entry, spec, outError)) {
                return false;
            }
            if (!seenSlotKeys.insert(spec.slotKey).second) {
                return SetError(outError, std::format("Duplicate slotKey '{}'", spec.slotKey));
            }
            parsed.push_back(std::move(spec));
        }

        outSpecs = std::move(parsed);
        return true;
    }

    bool SaveRegistryAtomic(
        const std::filesystem::path& targetPath,
        std::span<const DynamicNpcSpec> specs,
        std::string* outError) {
        if (targetPath.empty()) {
            return SetError(outError, "Registry target path is empty");
        }
        if (specs.size() > kMaxRegistryNpcs) {
            return SetError(outError, "Registry NPC count exceeds maximum");
        }

        std::unordered_set<std::string_view> seenKeys;
        seenKeys.reserve(specs.size());
        for (const auto& spec : specs) {
            if (!ValidateDynamicNpcSpec(spec, outError)) {
                return false;
            }
            if (!seenKeys.insert(spec.slotKey).second) {
                return SetError(outError, std::format("Duplicate slotKey '{}'", spec.slotKey));
            }
        }

        std::error_code ec;
        const auto parent = targetPath.parent_path();
        if (!parent.empty()) {
            std::filesystem::create_directories(parent, ec);
            if (ec) {
                return SetError(outError, std::format("Failed to create registry directory: {}", ec.message()));
            }
        }
        if (std::filesystem::is_directory(targetPath, ec)) {
            return SetError(outError, "Registry target path is a directory");
        }

        const std::string json = SerializeRegistryJson(specs);
        if (json.empty()) {
            return SetError(outError, "Serialized registry JSON is empty");
        }

        std::filesystem::path tmpPath = targetPath;
        tmpPath += ".tmp";
        std::filesystem::path bakPath = targetPath;
        bakPath += ".bak";

        {
            std::ofstream out(tmpPath, std::ios::binary | std::ios::trunc);
            if (!out.is_open()) {
                return SetError(outError, "Failed to open temporary registry file for writing");
            }
            out.write(json.data(), static_cast<std::streamsize>(json.size()));
            out.flush();
            if (!out.good()) {
                out.close();
                std::filesystem::remove(tmpPath, ec);
                return SetError(outError, "Failed to write/flush temporary registry file");
            }
        }

        const auto writtenSize = std::filesystem::file_size(tmpPath, ec);
        if (ec || writtenSize != json.size() || writtenSize == 0) {
            std::filesystem::remove(tmpPath, ec);
            return SetError(outError, "Temporary registry file size verification failed");
        }

        if (std::filesystem::exists(targetPath, ec) && !ec) {
            std::filesystem::copy_file(
                targetPath,
                bakPath,
                std::filesystem::copy_options::overwrite_existing,
                ec);
            if (ec) {
                std::filesystem::remove(tmpPath, ec);
                return SetError(outError, std::format("Failed to write backup registry: {}", ec.message()));
            }
        }

        std::filesystem::rename(tmpPath, targetPath, ec);
        if (ec) {
            std::error_code rmEc;
            std::filesystem::remove(tmpPath, rmEc);
            return SetError(outError, std::format("Atomic registry rename failed: {}", ec.message()));
        }
        return true;
    }

    RegistryLoadResult LoadRegistrySafe(const std::filesystem::path& targetPath) {
        RegistryLoadResult result{};
        if (targetPath.empty()) {
            result.error = "Registry target path is empty";
            return result;
        }

        std::filesystem::path bakPath = targetPath;
        bakPath += ".bak";

        std::error_code ecPrimary;
        const bool hasPrimary = std::filesystem::exists(targetPath, ecPrimary);
        std::error_code ecBak;
        const bool hasBak = std::filesystem::exists(bakPath, ecBak);

        if (ecPrimary || ecBak) {
            result.fileExisted = true;
            result.error = "Failed to query registry file existence";
            return result;
        }

        if (!hasPrimary && !hasBak) {
            result.ok = true;
            result.fileExisted = false;
            return result;
        }

        result.fileExisted = true;
        std::string primaryErr;
        if (hasPrimary && ReadAndParseRegistryFile(targetPath, result.specs, &primaryErr)) {
            result.ok = true;
            result.recoveredFromBackup = false;
            return result;
        }

        std::string bakErr;
        if (hasBak && ReadAndParseRegistryFile(bakPath, result.specs, &bakErr)) {
            result.ok = true;
            result.recoveredFromBackup = true;
            result.error = primaryErr.empty() ? "Primary missing; recovered from backup" : primaryErr;
            return result;
        }

        result.specs.clear();
        result.error = std::format(
            "Primary error: [{}]; Backup error: [{}]",
            primaryErr.empty() ? "missing" : primaryErr,
            bakErr.empty() ? "missing" : bakErr);
        return result;
    }

}  // namespace MORWHA::State
