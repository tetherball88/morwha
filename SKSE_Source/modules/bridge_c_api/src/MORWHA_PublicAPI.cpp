#include "MORWHA_PublicAPI.h"

#include "morwha/app/SynthesisCoordinator.h"

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
#include <cstddef>
#include <cstring>
#include <string_view>

namespace {

    template <typename StructT>
    [[nodiscard]] bool CopyValidatedStruct(
        const StructT* src,
        StructT& outLocal) noexcept {
        if (!src || src->structSize != sizeof(StructT)) {
            return false;
        }
        std::memcpy(&outLocal, src, sizeof(StructT));
        return true;
    }

    [[nodiscard]] MORWHA::Foundation::DonorMode ToInternalDonorMode(MORWHA_DonorMode m) noexcept {
        switch (m) {
            case MORWHA_DonorMode::OneDonorRelative:
                return MORWHA::Foundation::DonorMode::OneDonorRelative;
            case MORWHA_DonorMode::TwoDonorOffspring:
                return MORWHA::Foundation::DonorMode::TwoDonorOffspring;
            case MORWHA_DonorMode::ZeroDonorProcedural:
            default:
                return MORWHA::Foundation::DonorMode::ZeroDonorProcedural;
        }
    }

    [[nodiscard]] MORWHA::Foundation::AgeBracket ToInternalAgeBracket(MORWHA_AgeBracket b) noexcept {
        const auto raw = static_cast<std::uint8_t>(b);
        return (raw <= 4u)
                   ? static_cast<MORWHA::Foundation::AgeBracket>(raw)
                   : MORWHA::Foundation::AgeBracket::Auto;
    }

    template <typename FormT>
    void TranslateFormOverride(
        std::uint32_t rawFormId,
        MORWHA::App::MortalFormPatch<FormT>& outPatch) {
        if (rawFormId == 0u) {
            return;
        }
        if (rawFormId == kMORWHA_ClearFormId) {
            outPatch.Set(nullptr);
            return;
        }
        if (auto* form = RE::TESForm::LookupByID<FormT>(rawFormId); form && !form->IsDeleted()) {
            outPatch.Set(form);
        }
    }

    void UnpackExtensionBlocks(
        const MORWHA_ArchetypeParams* rawArch,
        const MORWHA_AiParams* rawAi,
        const MORWHA_StatsParams* rawStats,
        MORWHA::App::MortalArchetypeOverrides& outArch,
        MORWHA::App::MortalAiOverrides& outAi,
        MORWHA::App::MortalStatsOverrides& outStats) {
        MORWHA_ArchetypeParams arch{};
        if (CopyValidatedStruct(rawArch, arch)) {
            TranslateFormOverride(arch.voiceTypeFormId, outArch.voiceType);
            TranslateFormOverride(arch.npcClassFormId, outArch.npcClass);
            TranslateFormOverride(arch.combatStyleFormId, outArch.combatStyle);
            TranslateFormOverride(arch.defaultOutfitFormId, outArch.defaultOutfit);
            TranslateFormOverride(arch.sleepOutfitFormId, outArch.sleepOutfit);
            TranslateFormOverride(arch.crimeFactionFormId, outArch.crimeFaction);
            TranslateFormOverride(arch.faceTextureSetFormId, outArch.faceTextureSet);
        }

        MORWHA_AiParams ai{};
        if (CopyValidatedStruct(rawAi, ai)) {
            if (ai.aggression >= 0) {
                outAi.aggression = static_cast<std::uint8_t>(ai.aggression);
            }
            if (ai.confidence >= 0) {
                outAi.confidence = static_cast<std::uint8_t>(ai.confidence);
            }
            if (ai.assistance >= 0) {
                outAi.assistance = static_cast<std::uint8_t>(ai.assistance);
            }
            if (ai.morality >= 0) {
                outAi.morality = static_cast<std::uint8_t>(ai.morality);
            }
            if (ai.isProtected >= 0) {
                outAi.isProtected = (ai.isProtected != 0);
            }
            if (ai.isEssential >= 0) {
                outAi.isEssential = (ai.isEssential != 0);
            }
        }

        MORWHA_StatsParams st{};
        if (CopyValidatedStruct(rawStats, st)) {
            if (st.level > 0u) {
                outStats.level = st.level;
            }
            if (st.calcLevelMin > 0u) {
                outStats.calcLevelMin = st.calcLevelMin;
            }
            if (st.calcLevelMax > 0u) {
                outStats.calcLevelMax = st.calcLevelMax;
            }
            if (st.autoCalcStats >= 0) {
                outStats.autoCalcStats = (st.autoCalcStats != 0);
            }
            if (st.pcLevelMult >= 0) {
                outStats.pcLevelMult = (st.pcLevelMult != 0);
            }
            if (st.health > 0u) {
                outStats.health = st.health;
            }
            if (st.magicka > 0u) {
                outStats.magicka = st.magicka;
            }
            if (st.stamina > 0u) {
                outStats.stamina = st.stamina;
            }
            for (std::size_t i = 0; i < MORWHA::State::kNpcSkillCount; ++i) {
                if (st.skills[i] > 0u) {
                    outStats.skills[i] = st.skills[i];
                }
            }

            outStats.replacePerks = (st.replacePerks != 0u) || (st.perkFormIds && st.perkCount > 0u);
            if (st.perkFormIds && st.perkCount > 0u) {
                const std::uint32_t count = std::min<std::uint32_t>(st.perkCount, 64u);
                outStats.perks.reserve(count);
                for (std::uint32_t i = 0; i < count; ++i) {
                    if (auto* pk = RE::TESForm::LookupByID<RE::BGSPerk>(st.perkFormIds[i]);
                        pk && !pk->IsDeleted()) {
                        outStats.perks.push_back(pk);
                    }
                }
            }

            outStats.replaceSpells = (st.replaceSpells != 0u) || (st.spellFormIds && st.spellCount > 0u);
            if (st.spellFormIds && st.spellCount > 0u) {
                const std::uint32_t count = std::min<std::uint32_t>(st.spellCount, 64u);
                outStats.spells.reserve(count);
                for (std::uint32_t i = 0; i < count; ++i) {
                    if (auto* sp = RE::TESForm::LookupByID<RE::SpellItem>(st.spellFormIds[i]);
                        sp && !sp->IsDeleted()) {
                        outStats.spells.push_back(sp);
                    }
                }
            }

            outStats.replaceFactions = (st.replaceFactions != 0u) || (st.factions && st.factionCount > 0u);
            if (st.factions && st.factionCount > 0u) {
                const std::uint32_t count = std::min<std::uint32_t>(st.factionCount, 32u);
                outStats.factions.reserve(count);
                for (std::uint32_t i = 0; i < count; ++i) {
                    if (auto* fac = RE::TESForm::LookupByID<RE::TESFaction>(st.factions[i].factionFormId);
                        fac && !fac->IsDeleted()) {
                        outStats.factions.push_back(MORWHA::App::MortalFactionRankInput{
                            .faction = fac,
                            .rank = st.factions[i].rank
                        });
                    }
                }
            }
        }
    }

}  // namespace

extern "C" {

std::uint32_t MORWHA_GetApiVersion() {
    return kMORWHA_ApiVersion;
}

bool MORWHA_IsReady() {
    return MORWHA::App::SynthesisCoordinator::GetSingleton().IsReady();
}

bool MORWHA_SpawnMortal(const MORWHA_SpawnParams* params, MORWHA_SpawnResult* outResult) {
    MORWHA_SpawnParams local{};
    if (!CopyValidatedStruct(params, local) || local.anchorRefFormId == 0u) {
        return false;
    }

    auto* anchor = RE::TESForm::LookupByID<RE::Actor>(local.anchorRefFormId);
    if (!anchor) {
        return false;
    }

    MORWHA::App::MortalSpawnRequest req{};
    req.anchor = anchor;
    req.donorMode = ToInternalDonorMode(local.donorMode);
    req.donorA = (local.donorAFormId != 0u) ? RE::TESForm::LookupByID(local.donorAFormId) : nullptr;
    req.donorB = (local.donorBFormId != 0u) ? RE::TESForm::LookupByID(local.donorBFormId) : nullptr;
    req.raceOverride =
        (local.raceFormId != 0u) ? RE::TESForm::LookupByID<RE::TESRace>(local.raceFormId) : nullptr;
    req.sexInt = static_cast<std::int32_t>(local.targetSex);
    req.ageBracket = ToInternalAgeBracket(local.ageBracket);
    req.forcePersist = (local.forcePersist != 0u);
    req.seed = MORWHA::Foundation::Seed64{ local.seed };
    if (local.displayName) {
        req.displayName = local.displayName;
    }
    if (local.slotKeyPrefix) {
        req.slotKeyPrefix = local.slotKeyPrefix;
    }

    UnpackExtensionBlocks(local.archetype, local.ai, local.stats, req.archetype, req.ai, req.stats);

    const auto outcome = MORWHA::App::SynthesisCoordinator::GetSingleton().SpawnMortal(req);
    if (!outcome.actor) {
        return false;
    }

    if (outResult && outResult->structSize == sizeof(MORWHA_SpawnResult)) {
        MORWHA_SpawnResult filled{};
        filled.actorFormId = outcome.actor->GetFormID();
        filled.baseNpcFormId = outcome.baseNpc ? outcome.baseNpc->GetFormID() : 0u;
        filled.resolvedRaceFormId = outcome.resolvedRace ? outcome.resolvedRace->GetFormID() : 0u;
        filled.resolvedSex = static_cast<std::uint8_t>(outcome.resolvedSex);
        filled.resolvedAgeBracket = static_cast<MORWHA_AgeBracket>(outcome.resolvedAgeBracket);
        const std::size_t copyLen =
            std::min<std::size_t>(outcome.slotKey.size(), sizeof(filled.slotKey) - 1u);
        std::memcpy(filled.slotKey, outcome.slotKey.data(), copyLen);
        filled.slotKey[copyLen] = '\0';

        std::memcpy(outResult, &filled, sizeof(MORWHA_SpawnResult));
    }

    return true;
}

bool MORWHA_UpdateMortal(const MORWHA_UpdateParams* params) {
    MORWHA_UpdateParams local{};
    if (!CopyValidatedStruct(params, local) || local.targetActorFormId == 0u) {
        return false;
    }

    auto* targetActor = RE::TESForm::LookupByID<RE::Actor>(local.targetActorFormId);
    if (!targetActor) {
        return false;
    }

    MORWHA::App::MortalUpdateRequest req{};
    if (local.displayName && local.displayName[0] != '\0') {
        req.displayName = std::string(local.displayName);
    }
    const auto bracket = ToInternalAgeBracket(local.ageBracket);
    if (bracket != MORWHA::Foundation::AgeBracket::Auto) {
        req.ageBracket = bracket;
    }
    if (local.forcePersist >= 0) {
        req.forcePersist = (local.forcePersist != 0);
    }
    req.refreshOutfit = (local.refreshOutfit != 0u);
    UnpackExtensionBlocks(local.archetype, local.ai, local.stats, req.archetype, req.ai, req.stats);

    return MORWHA::App::SynthesisCoordinator::GetSingleton().UpdateMortal(targetActor, req);
}

bool MORWHA_RerollMortal(const MORWHA_RerollParams* params) {
    MORWHA_RerollParams local{};
    if (!CopyValidatedStruct(params, local) || local.targetActorFormId == 0u) {
        return false;
    }
    auto* targetActor = RE::TESForm::LookupByID<RE::Actor>(local.targetActorFormId);
    if (!targetActor) {
        return false;
    }

    MORWHA::App::MortalRerollRequest req{};
    req.raceOverride =
        (local.raceFormId != 0u) ? RE::TESForm::LookupByID<RE::TESRace>(local.raceFormId) : nullptr;
    req.rerollRaceFromDonors = (local.rerollRaceFromDonors != 0u);
    req.sexOverride = static_cast<std::int32_t>(local.sexOverride);
    req.ageBracketOverride = ToInternalAgeBracket(local.ageBracketOverride);
    if (local.nameOverride) {
        req.nameOverride = local.nameOverride;
    }
    UnpackExtensionBlocks(local.archetype, local.ai, local.stats, req.archetype, req.ai, req.stats);

    return MORWHA::App::SynthesisCoordinator::GetSingleton().RerollMortal(targetActor, req);
}

bool MORWHA_GetMortalHeritage(std::uint32_t targetFormId, MORWHA_HeritageInfo* outInfo) {
    if (targetFormId == 0u || !outInfo || outInfo->structSize != sizeof(MORWHA_HeritageInfo)) {
        return false;
    }
    auto* form = RE::TESForm::LookupByID(targetFormId);
    if (!form) {
        return false;
    }

    const auto resolvedOpt =
        MORWHA::App::SynthesisCoordinator::GetSingleton().QueryMortalHeritage(form);
    if (!resolvedOpt) {
        return false;
    }

    auto copyBounded = [](auto& dstArr, std::string_view sv) noexcept {
        const std::size_t n = std::min<std::size_t>(sv.size(), sizeof(dstArr) - 1u);
        if (n > 0u) {
            std::memcpy(dstArr, sv.data(), n);
        }
        dstArr[n] = '\0';
    };

    MORWHA_HeritageInfo filled{};
    filled.isMorwhaMortal = 1u;
    filled.isMixedHeritage = resolvedOpt->summary.isMixedHeritage ? 1u : 0u;
    filled.chimeraTier = static_cast<std::uint8_t>(resolvedOpt->summary.chimeraTier);
    filled.primaryRaceFormId = resolvedOpt->primaryRaceFormId;
    filled.secondaryRaceFormId = resolvedOpt->secondaryRaceFormId;
    copyBounded(filled.primaryRace, resolvedOpt->summary.primaryRace);
    copyBounded(filled.secondaryRace, resolvedOpt->summary.secondaryRace);
    copyBounded(filled.groupPair, resolvedOpt->summary.groupPair);
    copyBounded(filled.racePair, resolvedOpt->summary.racePair);
    copyBounded(filled.lineageLabel, resolvedOpt->summary.lineageLabel);
    copyBounded(filled.loreName, resolvedOpt->summary.loreName);

    std::memcpy(outInfo, &filled, sizeof(MORWHA_HeritageInfo));
    return true;
}

}  // extern "C"
