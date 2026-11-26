#pragma once

#include "morwha/foundation/DomainTypes.h"
#include "morwha/foundation/StrongIds.h"
#include "morwha/state/RegistryTypes.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace RE {
    class Actor;
    class BGSOutfit;
    class BGSPerk;
    class BGSTextureSet;
    class BGSVoiceType;
    class SpellItem;
    class TESClass;
    class TESCombatStyle;
    class TESFaction;
    class TESForm;
    class TESNPC;
    class TESRace;
}

namespace MORWHA::App {

    template <typename FormT>
    struct MortalFormPatch {
        bool specified{ false };
        FormT* form{ nullptr };

        constexpr void Set(FormT* f) noexcept {
            specified = true;
            form = f;
        }
    };

    struct MortalArchetypeOverrides {
        MortalFormPatch<RE::BGSVoiceType> voiceType{};
        MortalFormPatch<RE::TESClass> npcClass{};
        MortalFormPatch<RE::TESCombatStyle> combatStyle{};
        MortalFormPatch<RE::BGSOutfit> defaultOutfit{};
        MortalFormPatch<RE::BGSOutfit> sleepOutfit{};
        MortalFormPatch<RE::TESFaction> crimeFaction{};
        MortalFormPatch<RE::BGSTextureSet> faceTextureSet{};

        [[nodiscard]] constexpr bool HasAny() const noexcept {
            return voiceType.specified || npcClass.specified || combatStyle.specified ||
                   defaultOutfit.specified || sleepOutfit.specified ||
                   crimeFaction.specified || faceTextureSet.specified;
        }
    };

    struct MortalAiOverrides {
        std::optional<std::uint8_t> aggression{};
        std::optional<std::uint8_t> confidence{};
        std::optional<std::uint8_t> assistance{};
        std::optional<std::uint8_t> morality{};
        std::optional<bool> isProtected{};
        std::optional<bool> isEssential{};

        [[nodiscard]] constexpr bool HasAny() const noexcept {
            return aggression.has_value() || confidence.has_value() || assistance.has_value() ||
                   morality.has_value() || isProtected.has_value() || isEssential.has_value();
        }
    };

    struct MortalFactionRankInput {
        RE::TESFaction* faction{ nullptr };
        std::int8_t rank{ 0 };
    };

    struct MortalStatsOverrides {
        std::optional<std::uint16_t> level{};
        std::optional<std::uint16_t> calcLevelMin{};
        std::optional<std::uint16_t> calcLevelMax{};
        std::optional<bool> autoCalcStats{};
        std::optional<bool> pcLevelMult{};
        std::optional<std::uint16_t> health{};
        std::optional<std::uint16_t> magicka{};
        std::optional<std::uint16_t> stamina{};
        std::array<std::optional<std::uint8_t>, State::kNpcSkillCount> skills{};

        bool replacePerks{ false };
        std::vector<RE::BGSPerk*> perks{};
        bool replaceSpells{ false };
        std::vector<RE::SpellItem*> spells{};
        bool replaceFactions{ false };
        std::vector<MortalFactionRankInput> factions{};

        [[nodiscard]] bool HasAny() const noexcept {
            for (const auto& sk : skills) {
                if (sk.has_value()) {
                    return true;
                }
            }
            return level.has_value() || calcLevelMin.has_value() || calcLevelMax.has_value() ||
                   autoCalcStats.has_value() || pcLevelMult.has_value() ||
                   health.has_value() || magicka.has_value() || stamina.has_value() ||
                   replacePerks || !perks.empty() ||
                   replaceSpells || !spells.empty() ||
                   replaceFactions || !factions.empty();
        }
    };

    struct MortalSpawnRequest {
        RE::Actor* anchor{ nullptr };
        Foundation::DonorMode donorMode{ Foundation::DonorMode::ZeroDonorProcedural };
        RE::TESForm* donorA{ nullptr };  // RE::Actor or RE::TESNPC
        RE::TESForm* donorB{ nullptr };  // RE::Actor or RE::TESNPC
        RE::TESRace* raceOverride{ nullptr };
        std::int32_t sexInt{ -1 };       // 0=Male, 1=Female, <0=Random
        Foundation::AgeBracket ageBracket{ Foundation::AgeBracket::Auto };
        bool forcePersist{ true };
        Foundation::Seed64 seed{};
        std::string displayName{};
        std::string slotKeyPrefix{};

        MortalArchetypeOverrides archetype{};
        MortalAiOverrides ai{};
        MortalStatsOverrides stats{};
    };

    struct MortalUpdateRequest {
        std::optional<std::string> displayName{};
        std::optional<Foundation::AgeBracket> ageBracket{};
        std::optional<bool> forcePersist{};
        bool refreshOutfit{ false };

        MortalArchetypeOverrides archetype{};
        MortalAiOverrides ai{};
        MortalStatsOverrides stats{};
    };

    struct MortalRerollRequest {
        RE::TESRace* raceOverride{ nullptr };
        bool rerollRaceFromDonors{ false };
        std::int32_t sexOverride{ -1 };
        Foundation::AgeBracket ageBracketOverride{ Foundation::AgeBracket::Auto };
        std::string nameOverride{};

        MortalArchetypeOverrides archetype{};
        MortalAiOverrides ai{};
        MortalStatsOverrides stats{};
    };

    struct MortalSpawnOutcome {
        RE::Actor* actor{ nullptr };
        RE::TESNPC* baseNpc{ nullptr };
        RE::TESRace* resolvedRace{ nullptr };
        Foundation::Sex resolvedSex{ Foundation::Sex::Female };
        Foundation::AgeBracket resolvedAgeBracket{ Foundation::AgeBracket::YoungAdult };
        std::string slotKey{};
    };

}  // namespace MORWHA::App
