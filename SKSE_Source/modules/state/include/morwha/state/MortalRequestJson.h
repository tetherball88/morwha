#pragma once

#include "morwha/foundation/DomainTypes.h"
#include "morwha/foundation/StrongIds.h"
#include "morwha/state/RegistryTypes.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace MORWHA::State {

    struct JsonFormSelector {
        bool specified{ false };
        bool clear{ false };
        PluginFormRef pluginRef{};
        Foundation::FormRawId runtimeId{};

        bool operator==(const JsonFormSelector&) const noexcept = default;
    };

    struct JsonFactionRankSelector {
        JsonFormSelector faction{};
        std::int8_t rank{ 0 };

        bool operator==(const JsonFactionRankSelector&) const noexcept = default;
    };

    struct JsonArchetypePatch {
        JsonFormSelector voiceType{};
        JsonFormSelector npcClass{};
        JsonFormSelector combatStyle{};
        JsonFormSelector defaultOutfit{};
        JsonFormSelector sleepOutfit{};
        JsonFormSelector crimeFaction{};
        JsonFormSelector faceTextureSet{};

        bool operator==(const JsonArchetypePatch&) const noexcept = default;
    };

    struct JsonAiPatch {
        std::optional<std::uint8_t> aggression{};
        std::optional<std::uint8_t> confidence{};
        std::optional<std::uint8_t> assistance{};
        std::optional<std::uint8_t> morality{};
        std::optional<bool> isProtected{};
        std::optional<bool> isEssential{};

        bool operator==(const JsonAiPatch&) const noexcept = default;
    };

    struct JsonStatsPatch {
        std::optional<std::uint16_t> level{};
        std::optional<std::uint16_t> calcLevelMin{};
        std::optional<std::uint16_t> calcLevelMax{};
        std::optional<bool> autoCalcStats{};
        std::optional<bool> pcLevelMult{};
        std::optional<std::uint16_t> health{};
        std::optional<std::uint16_t> magicka{};
        std::optional<std::uint16_t> stamina{};
        std::array<std::optional<std::uint8_t>, kNpcSkillCount> skills{};

        bool replacePerks{ false };
        std::vector<JsonFormSelector> perks{};
        bool replaceSpells{ false };
        std::vector<JsonFormSelector> spells{};
        bool replaceFactions{ false };
        std::vector<JsonFactionRankSelector> factions{};

        bool operator==(const JsonStatsPatch&) const noexcept = default;
    };

    struct JsonSpawnRequest {
        Foundation::DonorMode donorMode{ Foundation::DonorMode::ZeroDonorProcedural };
        bool donorModeExplicit{ false };
        std::int32_t sex{ -1 };
        Foundation::AgeBracket ageBracket{ Foundation::AgeBracket::Auto };
        bool forcePersist{ true };
        Foundation::Seed64 seed{};
        std::string displayName{};
        std::string slotKeyPrefix{};

        JsonFormSelector race{};
        JsonFormSelector donorA{};
        JsonFormSelector donorB{};

        JsonArchetypePatch archetype{};
        JsonAiPatch ai{};
        JsonStatsPatch stats{};

        bool operator==(const JsonSpawnRequest&) const noexcept = default;
    };

    struct JsonUpdateRequest {
        std::optional<std::string> displayName{};
        std::optional<Foundation::AgeBracket> ageBracket{};
        std::optional<bool> forcePersist{};
        bool refreshOutfit{ false };

        JsonArchetypePatch archetype{};
        JsonAiPatch ai{};
        JsonStatsPatch stats{};

        bool operator==(const JsonUpdateRequest&) const noexcept = default;
    };

    [[nodiscard]] bool ParseJsonSpawnRequest(
        std::string_view jsonText,
        JsonSpawnRequest& outRequest,
        std::string* outError = nullptr);

    [[nodiscard]] bool ParseJsonUpdateRequest(
        std::string_view jsonText,
        JsonUpdateRequest& outRequest,
        std::string* outError = nullptr);

}  // namespace MORWHA::State
