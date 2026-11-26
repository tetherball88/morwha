#include "morwha/app/MortalOverridesApplier.h"

#include "morwha/engine/FormRefResolver.h"
#include "morwha/state/DynamicNpcRegistry.h"

#include <RE/B/BGSOutfit.h>
#include <RE/B/BGSPerk.h>
#include <RE/B/BGSTextureSet.h>
#include <RE/B/BGSVoiceType.h>
#include <RE/S/SpellItem.h>
#include <RE/T/TESClass.h>
#include <RE/T/TESCombatStyle.h>
#include <RE/T/TESFaction.h>
#include <RE/T/TESForm.h>
#include <RE/T/TESNPC.h>
#include <RE/T/TESRace.h>

#include <algorithm>
#include <format>

namespace MORWHA::App {

    namespace {

        template <typename FormT>
        void ApplyFormPatch(const MortalFormPatch<FormT>& patch, State::PluginFormRef& outRef) {
            if (!patch.specified) {
                return;
            }
            outRef = patch.form ? Engine::MakePluginFormRef(patch.form) : State::PluginFormRef{};
        }

        [[nodiscard]] RE::TESForm* ResolveSelectorRawForm(
            const State::JsonFormSelector& sel,
            std::string_view field,
            std::string* outError) {
            if (!sel.specified || sel.clear) {
                return nullptr;
            }
            RE::TESForm* found = nullptr;
            if (!sel.pluginRef.IsEmpty()) {
                found = Engine::ResolvePluginForm(sel.pluginRef);
            } else if (!sel.runtimeId.IsNull()) {
                found = RE::TESForm::LookupByID(sel.runtimeId.raw());
            }
            if (!found || found->IsDeleted()) {
                if (outError) {
                    *outError = std::format("Failed to resolve form for '{}'", field);
                }
                return nullptr;
            }
            return found;
        }

        template <typename FormT>
        [[nodiscard]] bool ResolveTypedFormPatch(
            const State::JsonFormSelector& sel,
            MortalFormPatch<FormT>& outPatch,
            std::string_view field,
            std::string* outError) {
            if (!sel.specified) {
                return true;
            }
            if (sel.clear) {
                outPatch.Set(nullptr);
                return true;
            }
            RE::TESForm* raw = ResolveSelectorRawForm(sel, field, outError);
            if (!raw) {
                return false;
            }
            auto* typed = raw->As<FormT>();
            if (!typed) {
                if (outError) {
                    *outError = std::format(
                        "Form {:08X} for '{}' has incompatible TESForm type",
                        raw->GetFormID(),
                        field);
                }
                return false;
            }
            outPatch.Set(typed);
            return true;
        }

        [[nodiscard]] bool ConvertJsonPatches(
            const State::JsonArchetypePatch& jsonArch,
            const State::JsonAiPatch& jsonAi,
            const State::JsonStatsPatch& jsonStats,
            MortalArchetypeOverrides& outArch,
            MortalAiOverrides& outAi,
            MortalStatsOverrides& outStats,
            std::string* outError) {
            if (!ResolveTypedFormPatch(jsonArch.voiceType, outArch.voiceType, "voiceType", outError) ||
                !ResolveTypedFormPatch(jsonArch.npcClass, outArch.npcClass, "npcClass", outError) ||
                !ResolveTypedFormPatch(jsonArch.combatStyle, outArch.combatStyle, "combatStyle", outError) ||
                !ResolveTypedFormPatch(jsonArch.defaultOutfit, outArch.defaultOutfit, "defaultOutfit", outError) ||
                !ResolveTypedFormPatch(jsonArch.sleepOutfit, outArch.sleepOutfit, "sleepOutfit", outError) ||
                !ResolveTypedFormPatch(jsonArch.crimeFaction, outArch.crimeFaction, "crimeFaction", outError) ||
                !ResolveTypedFormPatch(jsonArch.faceTextureSet, outArch.faceTextureSet, "faceTextureSet", outError)) {
                return false;
            }

            outAi.aggression = jsonAi.aggression;
            outAi.confidence = jsonAi.confidence;
            outAi.assistance = jsonAi.assistance;
            outAi.morality = jsonAi.morality;
            outAi.isProtected = jsonAi.isProtected;
            outAi.isEssential = jsonAi.isEssential;

            outStats.level = jsonStats.level;
            outStats.calcLevelMin = jsonStats.calcLevelMin;
            outStats.calcLevelMax = jsonStats.calcLevelMax;
            outStats.autoCalcStats = jsonStats.autoCalcStats;
            outStats.pcLevelMult = jsonStats.pcLevelMult;
            outStats.health = jsonStats.health;
            outStats.magicka = jsonStats.magicka;
            outStats.stamina = jsonStats.stamina;
            outStats.skills = jsonStats.skills;

            outStats.replacePerks = jsonStats.replacePerks;
            outStats.perks.reserve(jsonStats.perks.size());
            for (const auto& pkSel : jsonStats.perks) {
                MortalFormPatch<RE::BGSPerk> pkPatch{};
                if (!ResolveTypedFormPatch(pkSel, pkPatch, "perks[]", outError)) {
                    return false;
                }
                if (pkPatch.form) {
                    outStats.perks.push_back(pkPatch.form);
                }
            }

            outStats.replaceSpells = jsonStats.replaceSpells;
            outStats.spells.reserve(jsonStats.spells.size());
            for (const auto& spSel : jsonStats.spells) {
                MortalFormPatch<RE::SpellItem> spPatch{};
                if (!ResolveTypedFormPatch(spSel, spPatch, "spells[]", outError)) {
                    return false;
                }
                if (spPatch.form) {
                    outStats.spells.push_back(spPatch.form);
                }
            }

            outStats.replaceFactions = jsonStats.replaceFactions;
            outStats.factions.reserve(jsonStats.factions.size());
            for (const auto& facSel : jsonStats.factions) {
                MortalFormPatch<RE::TESFaction> facPatch{};
                if (!ResolveTypedFormPatch(facSel.faction, facPatch, "factions[].faction", outError)) {
                    return false;
                }
                if (facPatch.form) {
                    outStats.factions.push_back(MortalFactionRankInput{
                        .faction = facPatch.form,
                        .rank = facSel.rank
                    });
                }
            }

            return true;
        }

    }  // namespace

    void ApplyMortalOverridesToSpec(
        State::DynamicNpcSpec& spec,
        const MortalArchetypeOverrides& archetype,
        const MortalAiOverrides& ai,
        const MortalStatsOverrides& stats) {
        ApplyFormPatch(archetype.voiceType, spec.voiceTypeRef);
        ApplyFormPatch(archetype.npcClass, spec.npcClassRef);
        ApplyFormPatch(archetype.combatStyle, spec.combatStyleRef);
        ApplyFormPatch(archetype.defaultOutfit, spec.defaultOutfitRef);
        ApplyFormPatch(archetype.sleepOutfit, spec.sleepOutfitRef);
        ApplyFormPatch(archetype.crimeFaction, spec.crimeFactionRef);
        ApplyFormPatch(archetype.faceTextureSet, spec.faceTextureSetRef);

        if (ai.aggression) {
            spec.ai.aggression = std::min<std::uint8_t>(3u, *ai.aggression);
        }
        if (ai.confidence) {
            spec.ai.confidence = std::min<std::uint8_t>(4u, *ai.confidence);
        }
        if (ai.assistance) {
            spec.ai.assistance = std::min<std::uint8_t>(2u, *ai.assistance);
        }
        if (ai.morality) {
            spec.ai.morality = std::min<std::uint8_t>(3u, *ai.morality);
        }
        if (ai.isProtected) {
            spec.ai.isProtected = *ai.isProtected;
        }
        if (ai.isEssential) {
            spec.ai.isEssential = *ai.isEssential;
        }

        if (stats.level && *stats.level > 0u) {
            spec.stats.level = *stats.level;
        }
        if (stats.calcLevelMin) {
            spec.stats.calcLevelMin = *stats.calcLevelMin;
        }
        if (stats.calcLevelMax) {
            spec.stats.calcLevelMax = *stats.calcLevelMax;
        }
        if (stats.autoCalcStats) {
            spec.stats.autoCalcStats = *stats.autoCalcStats;
        }
        if (stats.pcLevelMult) {
            spec.stats.pcLevelMult = *stats.pcLevelMult;
        }
        if (stats.health && *stats.health > 0u) {
            spec.stats.health = *stats.health;
        }
        if (stats.magicka && *stats.magicka > 0u) {
            spec.stats.magicka = *stats.magicka;
        }
        if (stats.stamina && *stats.stamina > 0u) {
            spec.stats.stamina = *stats.stamina;
        }
        for (std::size_t i = 0; i < State::kNpcSkillCount; ++i) {
            if (stats.skills[i]) {
                spec.stats.skills[i] = *stats.skills[i];
            }
        }

        if (stats.replacePerks || !stats.perks.empty()) {
            spec.perkRefs.clear();
            for (const auto* pk : stats.perks) {
                if (!pk) {
                    continue;
                }
                const auto ref = Engine::MakePluginFormRef(pk);
                if (!ref.IsEmpty() &&
                    spec.perkRefs.size() < State::kMaxPerksPerNpc &&
                    std::ranges::find(spec.perkRefs, ref) == spec.perkRefs.end()) {
                    spec.perkRefs.push_back(ref);
                }
            }
        }

        if (stats.replaceSpells || !stats.spells.empty()) {
            spec.spellRefs.clear();
            for (const auto* sp : stats.spells) {
                if (!sp) {
                    continue;
                }
                const auto ref = Engine::MakePluginFormRef(sp);
                if (!ref.IsEmpty() &&
                    spec.spellRefs.size() < State::kMaxSpellsPerNpc &&
                    std::ranges::find(spec.spellRefs, ref) == spec.spellRefs.end()) {
                    spec.spellRefs.push_back(ref);
                }
            }
        }

        if (stats.replaceFactions || !stats.factions.empty()) {
            spec.factions.clear();
            for (const auto& entry : stats.factions) {
                if (!entry.faction || entry.rank < -1) {
                    continue;
                }
                const auto ref = Engine::MakePluginFormRef(entry.faction);
                if (ref.IsEmpty() || spec.factions.size() >= State::kMaxFactionsPerNpc) {
                    continue;
                }
                const auto existingIt = std::ranges::find_if(spec.factions, [&](const auto& f) {
                    return f.factionRef == ref;
                });
                if (existingIt != spec.factions.end()) {
                    existingIt->rank = entry.rank;
                } else {
                    spec.factions.push_back(State::FactionRankSpec{
                        .factionRef = ref,
                        .rank = entry.rank
                    });
                }
            }
        }
    }

    void CopyPreservedArchetypeAndStats(
        const State::DynamicNpcSpec& previousSpec,
        State::DynamicNpcSpec& newSpec) {
        newSpec.forcePersist = previousSpec.forcePersist;
        newSpec.npcClassRef = previousSpec.npcClassRef;
        newSpec.combatStyleRef = previousSpec.combatStyleRef;
        newSpec.defaultOutfitRef = previousSpec.defaultOutfitRef;
        newSpec.sleepOutfitRef = previousSpec.sleepOutfitRef;
        newSpec.crimeFactionRef = previousSpec.crimeFactionRef;
        newSpec.ai = previousSpec.ai;
        newSpec.stats = previousSpec.stats;
        newSpec.perkRefs = previousSpec.perkRefs;
        newSpec.spellRefs = previousSpec.spellRefs;
        newSpec.factions = previousSpec.factions;
    }

    bool BuildMortalSpawnRequestFromJson(
        RE::Actor* anchor,
        const State::JsonSpawnRequest& parsed,
        MortalSpawnRequest& outRequest,
        std::string* outError) {
        outRequest = MortalSpawnRequest{};
        outRequest.anchor = anchor;
        outRequest.donorMode = parsed.donorMode;
        outRequest.sexInt = parsed.sex;
        outRequest.ageBracket = parsed.ageBracket;
        outRequest.forcePersist = parsed.forcePersist;
        outRequest.seed = parsed.seed;
        outRequest.displayName = parsed.displayName;
        outRequest.slotKeyPrefix = parsed.slotKeyPrefix;

        MortalFormPatch<RE::TESRace> racePatch{};
        if (!ResolveTypedFormPatch(parsed.race, racePatch, "race", outError)) {
            return false;
        }
        outRequest.raceOverride = racePatch.form;

        auto resolveDonor = [&](const State::JsonFormSelector& sel, RE::TESForm*& outDonor, std::string_view label) -> bool {
            if (!sel.specified || sel.clear) {
                outDonor = nullptr;
                return true;
            }
            RE::TESForm* raw = ResolveSelectorRawForm(sel, label, outError);
            if (!raw) {
                return false;
            }
            if (!raw->As<RE::Actor>() && !raw->As<RE::TESNPC>()) {
                if (outError) {
                    *outError = std::format(
                        "Donor form {:08X} for '{}' must be an Actor or base TESNPC",
                        raw->GetFormID(),
                        label);
                }
                return false;
            }
            outDonor = raw;
            return true;
        };

        if (!resolveDonor(parsed.donorA, outRequest.donorA, "donorA") ||
            !resolveDonor(parsed.donorB, outRequest.donorB, "donorB")) {
            return false;
        }

        return ConvertJsonPatches(
            parsed.archetype,
            parsed.ai,
            parsed.stats,
            outRequest.archetype,
            outRequest.ai,
            outRequest.stats,
            outError);
    }

    bool BuildMortalUpdateRequestFromJson(
        const State::JsonUpdateRequest& parsed,
        MortalUpdateRequest& outRequest,
        std::string* outError) {
        outRequest = MortalUpdateRequest{};
        outRequest.displayName = parsed.displayName;
        outRequest.ageBracket = parsed.ageBracket;
        outRequest.forcePersist = parsed.forcePersist;
        outRequest.refreshOutfit = parsed.refreshOutfit;

        return ConvertJsonPatches(
            parsed.archetype,
            parsed.ai,
            parsed.stats,
            outRequest.archetype,
            outRequest.ai,
            outRequest.stats,
            outError);
    }

}  // namespace MORWHA::App
