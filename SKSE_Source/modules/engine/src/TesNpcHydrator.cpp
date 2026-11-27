#include "morwha/engine/TesNpcHydrator.h"

#include "morwha/engine/AgeProfileResolver.h"
#include "morwha/engine/ChimeraAccessoryCatalog.h"
#include "morwha/engine/DpfClient.h"
#include "morwha/engine/EngineLogging.h"
#include "morwha/engine/FormRefResolver.h"
#include "morwha/engine/GeneticSpecBuilder.h"
#include "morwha/engine/HeadPartPoolIndexer.h"
#include "morwha/engine/HybridSkinService.h"
#include "morwha/engine/MortalHeritageKeywordService.h"
#include "morwha/engine/TriProjectorCache.h"

#include <RE/B/BGSPerk.h>
#include <RE/E/ExtraCombatStyle.h>
#include <RE/S/SpellItem.h>
#include <RE/T/TESFaction.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <limits>
#include <string_view>
#include <utility>
#include <vector>

namespace MORWHA::Engine {

    namespace {

        constexpr std::uint32_t kDefaultRaceFormId = 0x000019u;
        constexpr std::uint32_t kCitizenClassFormId = 0x01326Bu;
        constexpr std::uint32_t kDefaultCombatStyleFormId = 0x00003Du;
        constexpr std::uint32_t kFarmClothesOutfitFormId = 0x01DC10u;
        constexpr std::uint32_t kActorTypeNpcKeywordFormId = 0x013794u;

        [[nodiscard]] bool IsHeadPartAllowedForHydration(
            const RE::BGSHeadPart* part,
            const RE::TESRace* race,
            Foundation::Sex sex,
            Foundation::TopologyPool topology,
            bool smpLoaded,
            bool allowCrossRaceCompatibleEye = false) noexcept {
            if (!part) {
                return false;
            }
            const auto& catalog = ChimeraAccessoryCatalog::GetSingleton();
            if (catalog.IsApprovedAccessoryHeadPart(part, race, sex) ||
                catalog.IsRuntimeBeastMouthPart(part) ||
                catalog.IsRuntimePromotedHornPart(part) ||
                catalog.IsRuntimeAccessoryHeadPartClone(part) ||
                HybridSkinService::GetSingleton().IsRuntimeHybridFacePart(part)) {
                return true;
            }
            const auto& indexer = HeadPartPoolIndexer::GetSingleton();
            if (indexer.IsBuilt()) {
                if (indexer.IsPartInIndexedPool(part, race, sex, topology)) {
                    return true;
                }
                if (allowCrossRaceCompatibleEye) {
                    if (part->type.get() == RE::BGSHeadPart::HeadPartType::kEyes) {
                        return indexer.IsCompatibleIndexedEyeForRace(part, race, sex, topology);
                    }
                    if (part->type.get() == RE::BGSHeadPart::HeadPartType::kEyebrows) {
                        return indexer.IsCompatibleIndexedBrowForRace(part, race, sex, topology);
                    }
                }
                return false;
            }
            return HeadPartPoolIndexer::IsPartCompatibleWithTopology(part, topology, smpLoaded);
        }

    }  // namespace

    Foundation::RgbColor CompositeTintOverMidGrey(RE::Color c, float alpha) noexcept {
        return Core::CompositeSkinTintOverMidGrey(
            Foundation::RgbColor{ c.red, c.green, c.blue }, alpha);
    }

    Core::RaceGroup ResolveRaceGroup(const RE::TESRace* race) {
        if (!race) {
            return Core::RaceGroup::Other;
        }
        const std::string_view rawEdid = race->GetFormEditorID() ? race->GetFormEditorID() : "";
        auto group = Core::ClassifyRaceKey(Core::NormalizeRaceKey(rawEdid));
        if (group == Core::RaceGroup::Other && race->morphRace) {
            const std::string_view morphEdid =
                race->morphRace->GetFormEditorID() ? race->morphRace->GetFormEditorID() : "";
            group = Core::ClassifyRaceKey(Core::NormalizeRaceKey(morphEdid));
        }
        if (group == Core::RaceGroup::Other &&
            !race->IsChildRace() &&
            HeadPartPoolIndexer::GetSingleton().SharesHumanoidChargenTri(race)) {
            return Core::RaceGroup::Humanoid;
        }
        return group;
    }

    bool IsMerRace(const RE::TESRace* race) {
        if (!race) {
            return false;
        }
        const std::string_view rawEdid = race->GetFormEditorID() ? race->GetFormEditorID() : "";
        if (Core::IsMerRaceKey(Core::NormalizeRaceKey(rawEdid))) {
            return true;
        }
        if (race->morphRace) {
            const std::string_view morphEdid =
                race->morphRace->GetFormEditorID() ? race->morphRace->GetFormEditorID() : "";
            return Core::IsMerRaceKey(Core::NormalizeRaceKey(morphEdid));
        }
        return false;
    }

    RE::TESRace::FaceRelatedData* ResolveRaceFaceRelatedData(
        RE::TESRace* race,
        Foundation::Sex sex) noexcept {
        if (!race) {
            return nullptr;
        }
        const auto idx = static_cast<std::size_t>(sex);
        if (race->faceRelatedData[idx]) {
            return race->faceRelatedData[idx];
        }
        if (race->morphRace && race->morphRace->faceRelatedData[idx]) {
            return race->morphRace->faceRelatedData[idx];
        }
        return nullptr;
    }

    RE::TESRace* UnwrapToPlayableSynthesisRace(
        RE::TESRace* candidate,
        Foundation::Sex sex) noexcept {
        if (!candidate || candidate->data.flags.all(RE::RACE_DATA::Flag::kChild)) {
            return nullptr;
        }

        const std::string_view rawEdid =
            candidate->GetFormEditorID() ? candidate->GetFormEditorID() : "";
        if (HeadPartPoolIndexer::ContainsCaseInsensitive(rawEdid, "Vampire") &&
            candidate->morphRace &&
            !candidate->morphRace->data.flags.all(RE::RACE_DATA::Flag::kChild)) {
            candidate = candidate->morphRace;
        }

        const std::string_view unwrappedEdid =
            candidate->GetFormEditorID() ? candidate->GetFormEditorID() : "";
        const bool isCustomHeadRace =
            HeadPartPoolIndexer::EqualsCaseInsensitive(unwrappedEdid, "ElderRace") ||
            HeadPartPoolIndexer::ContainsCaseInsensitive(unwrappedEdid, "UBE") ||
            HeadPartPoolIndexer::ContainsCaseInsensitive(unwrappedEdid, "CotR") ||
            HeadPartPoolIndexer::ContainsCaseInsensitive(unwrappedEdid, "CharmersOfTheReach") ||
            HeadPartPoolIndexer::ContainsCaseInsensitive(unwrappedEdid, "CITRUS");

        auto hasIndexedPrimaryFace = [&](RE::TESRace* r) noexcept -> bool {
            if (!r) {
                return false;
            }
            const auto& indexer = HeadPartPoolIndexer::GetSingleton();
            if (!indexer.IsBuilt()) {
                return ResolveRaceFaceRelatedData(r, sex) != nullptr;
            }
            const auto topo = indexer.SelectPreferredTopology(r, sex);
            const auto* pool = indexer.GetPool(r, sex, topo);
            return pool && pool->primaryFace != nullptr;
        };

        if (!isCustomHeadRace &&
            candidate->data.flags.all(RE::RACE_DATA::Flag::kFaceGenHead) &&
            ResolveRaceFaceRelatedData(candidate, sex) != nullptr &&
            hasIndexedPrimaryFace(candidate)) {
            return candidate;
        }

        std::string norm = Core::NormalizeRaceKey(
            HeadPartPoolIndexer::EqualsCaseInsensitive(unwrappedEdid, "ElderRace") ? "Imperial" : unwrappedEdid);
        if ((norm.empty() || norm == "humanoid") && candidate->morphRace && candidate->morphRace->GetFormEditorID()) {
            norm = Core::NormalizeRaceKey(candidate->morphRace->GetFormEditorID());
        }

        std::uint32_t targetBaseFormId = 0x013746u;  // NordRace default
        if (norm == "argonian") {
            targetBaseFormId = 0x013740u;
        } else if (norm == "breton") {
            targetBaseFormId = 0x013741u;
        } else if (norm == "darkelf") {
            targetBaseFormId = 0x013742u;
        } else if (norm == "highelf") {
            targetBaseFormId = 0x013743u;
        } else if (norm == "imperial") {
            targetBaseFormId = 0x013744u;
        } else if (norm == "khajiit") {
            targetBaseFormId = 0x013745u;
        } else if (norm == "nord") {
            targetBaseFormId = 0x013746u;
        } else if (norm == "orc") {
            targetBaseFormId = 0x013747u;
        } else if (norm == "redguard") {
            targetBaseFormId = 0x013748u;
        } else if (norm == "woodelf") {
            targetBaseFormId = 0x013749u;
        }

        if (auto* dataHandler = RE::TESDataHandler::GetSingleton()) {
            if (auto* baseRace = dataHandler->LookupForm<RE::TESRace>(targetBaseFormId, "Skyrim.esm")) {
                if (ResolveRaceFaceRelatedData(baseRace, sex) != nullptr) {
                    return baseRace;
                }
            }
        }
        return (ResolveRaceFaceRelatedData(candidate, sex) != nullptr) ? candidate : nullptr;
    }

    const RE::TESRace::FaceRelatedData::TintAsset* FindRaceTintAsset(
        const RE::TESRace::FaceRelatedData* faceRelated,
        State::TintSemanticType semanticType,
        std::uint16_t explicitIndex) noexcept {
        if (!faceRelated || !faceRelated->tintMasks) {
            return nullptr;
        }
        if (explicitIndex != State::kUnsetExplicitTintIndex) {
            for (const auto* asset : *faceRelated->tintMasks) {
                if (asset && asset->texture.index == explicitIndex) {
                    return asset;
                }
            }
        }
        const auto targetType = static_cast<std::uint8_t>(semanticType);
        for (const auto* asset : *faceRelated->tintMasks) {
            if (asset && asset->texture.skinTone.underlying() == targetType) {
                return asset;
            }
        }
        return nullptr;
    }

    std::vector<std::int32_t> ExtractAllowedMorphPresetIndices(
        const RE::TESRace* race,
        Foundation::Sex sex,
        const RE::TESRace::FaceRelatedData* faceRelated,
        std::size_t slot) {
        auto nativePresets =
            HeadPartPoolIndexer::GetSingleton().GetNativeMorphPresets(race, sex, slot);
        if (!faceRelated || slot >= 4u) {
            return nativePresets;
        }
        const auto& morphMask = faceRelated->availableMorphs[slot];
        return Core::DecodeMpavMorphPresetMask(
            morphMask.morphFlags, morphMask.unk04, nativePresets);
    }

    bool IsAllRaceMorphsUnlocked(
        const RE::TESRace::FaceRelatedData* faceRelated) noexcept {
        return faceRelated != nullptr &&
               Core::IsMpavAllMorphsUnlocked(
                   faceRelated->availableMorphs[0].morphFlags,
                   faceRelated->availableMorphs[2].morphFlags);
    }

    std::vector<std::int32_t> CollectAllowedMorphPresets(
        const RE::TESRace* race,
        Foundation::Sex sex,
        const RE::TESRace::FaceRelatedData* faceRelated,
        std::size_t slot,
        const RE::TESRace* optionalSecondRace,
        bool allowSharedTri) {
        auto validBits = ExtractAllowedMorphPresetIndices(race, sex, faceRelated, slot);
        if (slot == 0u || slot == 2u) {
            if (optionalSecondRace != nullptr && optionalSecondRace != race) {
                const auto& indexer = HeadPartPoolIndexer::GetSingleton();
                if (IsAllRaceMorphsUnlocked(faceRelated) ||
                    (indexer.SharesHumanoidChargenTri(race) &&
                     indexer.SharesHumanoidChargenTri(optionalSecondRace))) {
                    const auto* secondFr = ResolveRaceFaceRelatedData(
                        const_cast<RE::TESRace*>(optionalSecondRace), sex);
                    const auto extra = ExtractAllowedMorphPresetIndices(
                        optionalSecondRace, sex, secondFr, slot);
                    validBits.insert(validBits.end(), extra.begin(), extra.end());
                }
            } else if (allowSharedTri) {
                const auto extra =
                    HeadPartPoolIndexer::GetSingleton().GetSharedTriMorphPresets(race, sex, slot);
                validBits.insert(validBits.end(), extra.begin(), extra.end());
            }
            std::sort(validBits.begin(), validBits.end());
            validBits.erase(std::unique(validBits.begin(), validBits.end()), validBits.end());
        }
        return validBits;
    }

    void SanitizeFaceMorphPartsForRace(
        Foundation::FaceMorphVector& faceMorphs,
        const RE::TESRace* race,
        Foundation::Sex sex,
        const RE::TESRace::FaceRelatedData* faceRelated,
        const RE::TESRace* optionalSecondRaceForNoseEyes,
        bool allowSharedTriNoseEyes) noexcept {
        faceMorphs.parts[static_cast<std::size_t>(Foundation::FacePartSlot::Unknown)] =
            Foundation::kUnusedFacePartPreset;
        if (!faceRelated && !race) {
            return;
        }

        for (const std::size_t slot : { std::size_t{ 0 }, std::size_t{ 2 }, std::size_t{ 3 } }) {
            const std::int32_t val = faceMorphs.parts[slot];
            if (val == Foundation::kUnusedFacePartPreset || val < 0) {
                continue;
            }

            const std::vector<std::int32_t> validBits = CollectAllowedMorphPresets(
                race, sex, faceRelated, slot, optionalSecondRaceForNoseEyes, allowSharedTriNoseEyes);
            if (validBits.empty()) {
                continue;
            }
            if (!std::binary_search(validBits.begin(), validBits.end(), val)) {
                faceMorphs.parts[slot] =
                    validBits[static_cast<std::size_t>(val) % validBits.size()];
            }
        }
    }

    RE::BGSHeadPart* FindFallbackHeadPart(
        RE::BGSHeadPart::HeadPartType type,
        Foundation::TopologyPool topology,
        RE::TESNPC* baselineNpc,
        RE::TESRace* race,
        const RE::TESRace::FaceRelatedData* faceRelated,
        Foundation::Sex sex,
        bool smpLoaded) {
        if (const auto* pool = HeadPartPoolIndexer::GetSingleton().GetPool(race, sex, topology)) {
            switch (type) {
            case RE::BGSHeadPart::HeadPartType::kFace:
                if (pool->primaryFace) {
                    return pool->primaryFace;
                }
                break;
            case RE::BGSHeadPart::HeadPartType::kMisc:
                if (pool->primaryMouth) {
                    return pool->primaryMouth;
                }
                if (!pool->mouths.empty()) {
                    return pool->mouths.front();
                }
                break;
            case RE::BGSHeadPart::HeadPartType::kEyes:
                if (!pool->eyes.empty()) {
                    return pool->eyes.front();
                }
                break;
            case RE::BGSHeadPart::HeadPartType::kHair:
                if (!pool->hairs.empty()) {
                    return pool->hairs.front();
                }
                break;
            case RE::BGSHeadPart::HeadPartType::kEyebrows:
                if (!pool->eyebrows.empty()) {
                    return pool->eyebrows.front();
                }
                break;
            case RE::BGSHeadPart::HeadPartType::kFacialHair:
                if (!pool->facialHairs.empty()) {
                    return pool->facialHairs.front();
                }
                break;
            case RE::BGSHeadPart::HeadPartType::kScar:
                if (!pool->scars.empty()) {
                    return pool->scars.front();
                }
                break;
            default:
                break;
            }
        }
        if (baselineNpc) {
            if (auto* part = baselineNpc->GetHeadPartByType(type);
                IsHeadPartAllowedForHydration(part, race, sex, topology, smpLoaded)) {
                return part;
            }
        }
        if (faceRelated && faceRelated->headParts) {
            for (auto* part : *faceRelated->headParts) {
                if (part && part->type.get() == type &&
                    IsHeadPartAllowedForHydration(part, race, sex, topology, smpLoaded)) {
                    return part;
                }
            }
        }
        const RE::SEX reSex = (sex == Foundation::Sex::Female) ? RE::SEX::kFemale : RE::SEX::kMale;
        if (race) {
            if (auto* part = race->GetHeadPartByType(type, reSex);
                IsHeadPartAllowedForHydration(part, race, sex, topology, smpLoaded)) {
                return part;
            }
        }
        if (auto* dataHandler = RE::TESDataHandler::GetSingleton()) {
            for (auto* part : dataHandler->GetFormArray<RE::BGSHeadPart>()) {
                if (!part || part->type.get() != type) {
                    continue;
                }
                if (!IsHeadPartAllowedForHydration(part, race, sex, topology, smpLoaded)) {
                    continue;
                }
                if (!part->flags.all(RE::BGSHeadPart::Flag::kPlayable) ||
                    (part->GetFormFlags() & RE::BGSHeadPart::RecordFlags::kNonPlayable) != 0) {
                    continue;
                }
                if (part->flags.any(RE::BGSHeadPart::Flag::kMale, RE::BGSHeadPart::Flag::kFemale)) {
                    const bool wantFemale = (sex == Foundation::Sex::Female);
                    if (wantFemale ? !part->flags.all(RE::BGSHeadPart::Flag::kFemale)
                                   : !part->flags.all(RE::BGSHeadPart::Flag::kMale)) {
                        continue;
                    }
                }
                if (race && part->validRaces &&
                    (part->validRaces->HasForm(race) ||
                     (race->morphRace && part->validRaces->HasForm(race->morphRace)))) {
                    return part;
                }
            }
        }
        return nullptr;
    }

    bool HydrateTesNpc(
        RE::TESNPC* targetNpc,
        const State::DynamicNpcSpec& spec,
        bool isMidSession) {
        if (!targetNpc) {
            MORWHA_LOG_ERROR("HydrateTesNpc: targetNpc is null for slotKey '{}'", spec.slotKey);
            return false;
        }

        auto* resolvedRace = ResolvePluginForm<RE::TESRace>(spec.raceRef);
        if (!resolvedRace) {
            MORWHA_LOG_ERROR(
                "HydrateTesNpc: Failed to resolve race '{}:{:06X}' for slotKey '{}'",
                spec.raceRef.plugin,
                spec.raceRef.localId,
                spec.slotKey);
            return false;
        }

        const auto* faceRelated = ResolveRaceFaceRelatedData(resolvedRace, spec.sex);
        const auto sexIdx = static_cast<std::size_t>(spec.sex);
        auto* dataHandler = RE::TESDataHandler::GetSingleton();
        const bool smpLoaded = DpfClient::GetSingleton().IsSmpLoaded();

        Foundation::TopologyPool activeTopology =
            (spec.topology == Foundation::TopologyPool::HighPolyHead)
                ? Foundation::TopologyPool::HighPolyHead
                : Foundation::TopologyPool::Vanilla;
        if (activeTopology == Foundation::TopologyPool::HighPolyHead &&
            HeadPartPoolIndexer::GetSingleton().SelectPreferredTopology(resolvedRace, spec.sex) !=
                Foundation::TopologyPool::HighPolyHead) {
            MORWHA_LOG_WARN(
                "HydrateTesNpc: High Poly Head requested for {:08X} ('{}') but HPH pool unavailable; downgrading to Vanilla topology",
                targetNpc->GetFormID(),
                spec.slotKey);
            activeTopology = Foundation::TopologyPool::Vanilla;
        }

        // Resolve optional baseline adult preset without touching factions/packages/templates.
        RE::TESNPC* baselineNpc = ResolvePluginForm<RE::TESNPC>(spec.templateNpcRef);
        if (!baselineNpc && faceRelated && faceRelated->presetNPCs) {
            for (auto* cand : *faceRelated->presetNPCs) {
                if (cand) {
                    baselineNpc = cand;
                    break;
                }
            }
        }

        if (baselineNpc &&
            (baselineNpc->boundData.boundMin.x != 0 || baselineNpc->boundData.boundMax.z != 0)) {
            targetNpc->boundData = baselineNpc->boundData;
        } else {
            targetNpc->boundData.boundMin = RE::NiNPShortPoint3{ -22, -14, 0 };
            targetNpc->boundData.boundMax = RE::NiNPShortPoint3{ 22, 14, 128 };
        }

        // Snapshot any externally distributed (e.g. SPID / RSV / SkyPatcher / OBody) WNAM skin on
        // same-race & same-sex re-hydration BEFORE mutating actorBaseFlags and race so RerollMortal
        // and UpdateMortal do not wipe body skins back to race default while NiOverride head spells remain.
        auto& accessoryCatalog = ChimeraAccessoryCatalog::GetSingleton();
        auto& hybridSkinService = HybridSkinService::GetSingleton();
        const RE::TESRace* const prevRace = targetNpc->race;
        const bool prevIsFemale = targetNpc->IsFemale();
        const bool wantFemale = (spec.sex == Foundation::Sex::Female);
        RE::TESObjectARMO* preservedExternalSkin = nullptr;
        if (prevRace == resolvedRace && prevIsFemale == wantFemale && targetNpc->skin != nullptr &&
            !accessoryCatalog.IsRuntimeChimeraSkin(targetNpc->skin) &&
            !hybridSkinService.IsRuntimeHybridSkinArmor(targetNpc->skin)) {
            preservedExternalSkin = targetNpc->skin;
        }

        // Core TESActorBaseData: explicitly clear kRespawn and template flags.
        targetNpc->actorData.actorBaseFlags = RE::ACTOR_BASE_DATA::Flag::kUnique;
        if (spec.stats.autoCalcStats) {
            targetNpc->actorData.actorBaseFlags.set(RE::ACTOR_BASE_DATA::Flag::kAutoCalcStats);
        }
        if (spec.stats.pcLevelMult) {
            targetNpc->actorData.actorBaseFlags.set(RE::ACTOR_BASE_DATA::Flag::kPCLevelMult);
        }
        if (spec.ai.isProtected) {
            targetNpc->actorData.actorBaseFlags.set(RE::ACTOR_BASE_DATA::Flag::kProtected);
        }
        if (spec.ai.isEssential) {
            targetNpc->actorData.actorBaseFlags.set(RE::ACTOR_BASE_DATA::Flag::kEssential);
        }
        if (spec.sex == Foundation::Sex::Female) {
            targetNpc->actorData.actorBaseFlags.set(RE::ACTOR_BASE_DATA::Flag::kFemale);
        }
        targetNpc->actorData.actorBaseFlags.reset(
            RE::ACTOR_BASE_DATA::Flag::kRespawn,
            RE::ACTOR_BASE_DATA::Flag::kIsChargenFacePreset,
            RE::ACTOR_BASE_DATA::Flag::kUsesTemplate,
            RE::ACTOR_BASE_DATA::Flag::kCalcForAllTemplates,
            RE::ACTOR_BASE_DATA::Flag::kSimpleActor);
        targetNpc->actorData.magickaOffset = 0;
        targetNpc->actorData.staminaOffset = 0;
        targetNpc->actorData.level = spec.stats.level;
        targetNpc->actorData.calcLevelMin = spec.stats.calcLevelMin;
        targetNpc->actorData.calcLevelMax = spec.stats.calcLevelMax;
        targetNpc->actorData.speedMult = 100;
        targetNpc->actorData.baseDisposition = 35;
        targetNpc->actorData.templateUseFlags = RE::ACTOR_BASE_DATA::TEMPLATE_USE_FLAG::kNone;
        targetNpc->actorData.healthOffset = 0;
        targetNpc->actorData.bleedoutOverride = 0;
        targetNpc->baseTemplateForm = nullptr;

        // Race, voice, class, combat style, outfit, skin, keywords.
        targetNpc->race = resolvedRace;
        targetNpc->originalRace = resolvedRace;
        targetNpc->faceNPC = nullptr;

        RE::NiPointer<RE::BGSAttackDataMap> attackMap = resolvedRace->attackDataMap;
        if (!attackMap && resolvedRace->morphRace) {
            attackMap = resolvedRace->morphRace->attackDataMap;
        }
        if (!attackMap && resolvedRace->armorParentRace) {
            attackMap = resolvedRace->armorParentRace->attackDataMap;
        }
        if (!attackMap && baselineNpc) {
            attackMap = baselineNpc->attackDataMap;
        }
        if (!attackMap && dataHandler) {
            if (auto* defaultRace = dataHandler->LookupForm<RE::TESRace>(kDefaultRaceFormId, "Skyrim.esm")) {
                attackMap = defaultRace->attackDataMap;
            }
        }
        if (!attackMap) {
            MORWHA_LOG_ERROR(
                "HydrateTesNpc: Failed to resolve BGSAttackDataMap for {:08X} ('{}', race={:08X})",
                targetNpc->GetFormID(),
                spec.slotKey,
                resolvedRace->GetFormID());
            return false;
        }
        targetNpc->attackDataMap = attackMap;

        RE::BGSVoiceType* voice = ResolvePluginForm<RE::BGSVoiceType>(spec.voiceTypeRef);
        if (!voice) {
            voice = ResolveVoiceTypeForAgeAndRace(resolvedRace, spec.sex, spec.ageBracket, spec.seed);
        }
        if (!voice && IsVoiceTypeSexCompatible(resolvedRace->defaultVoiceTypes[sexIdx], spec.sex)) {
            voice = resolvedRace->defaultVoiceTypes[sexIdx];
        }
        if (!voice && baselineNpc && IsVoiceTypeSexCompatible(baselineNpc->voiceType, spec.sex)) {
            voice = baselineNpc->voiceType;
        }
        targetNpc->voiceType = voice;

        RE::TESClass* npcClass = ResolvePluginForm<RE::TESClass>(spec.npcClassRef);
        if (!npcClass && dataHandler) {
            npcClass = dataHandler->LookupForm<RE::TESClass>(kCitizenClassFormId, "Skyrim.esm");
        }
        if (!npcClass && baselineNpc) {
            npcClass = baselineNpc->npcClass;
        }
        targetNpc->npcClass = npcClass;

        RE::TESCombatStyle* combatStyle = ResolvePluginForm<RE::TESCombatStyle>(spec.combatStyleRef);
        if (!combatStyle && dataHandler) {
            combatStyle = dataHandler->LookupForm<RE::TESCombatStyle>(kDefaultCombatStyleFormId, "Skyrim.esm");
        }
        if (!combatStyle && baselineNpc) {
            combatStyle = baselineNpc->combatStyle;
        }
        targetNpc->combatStyle = combatStyle;

        RE::BGSOutfit* outfit = ResolvePluginForm<RE::BGSOutfit>(spec.defaultOutfitRef);
        if (!outfit && dataHandler) {
            outfit = dataHandler->LookupForm<RE::BGSOutfit>(kFarmClothesOutfitFormId, "Skyrim.esm");
        }
        if (!outfit && baselineNpc) {
            outfit = baselineNpc->defaultOutfit;
        }
        targetNpc->defaultOutfit = outfit;
        targetNpc->sleepOutfit = ResolvePluginForm<RE::BGSOutfit>(spec.sleepOutfitRef);
        targetNpc->crimeFaction = ResolvePluginForm<RE::TESFaction>(spec.crimeFactionRef);
        accessoryCatalog.RecordNpcChimeraState(
            targetNpc->GetFormID(), resolvedRace, spec.sex, spec.beastTailKind, spec.hybridSkinId);

        RE::TESObjectARMO* const effectiveBaseSkin =
            preservedExternalSkin ? preservedExternalSkin : resolvedRace->skin;
        if (spec.hybridSkinId.empty()) {
            if (auto* customSkin = accessoryCatalog.GetOrCreateChimeraSkinArmor(
                    effectiveBaseSkin, resolvedRace, spec.beastTailKind)) {
                targetNpc->skin = customSkin;
                targetNpc->farSkin = nullptr;
            } else if (preservedExternalSkin != nullptr) {
                targetNpc->skin = preservedExternalSkin;
                targetNpc->farSkin = nullptr;
            } else if (resolvedRace->skin != nullptr) {
                targetNpc->skin = nullptr;
                targetNpc->farSkin = nullptr;
            } else if (baselineNpc) {
                targetNpc->skin = baselineNpc->skin;
                targetNpc->farSkin = baselineNpc->farSkin;
            } else {
                targetNpc->skin = nullptr;
                targetNpc->farSkin = nullptr;
            }
        } else {
            targetNpc->skin = nullptr;
            targetNpc->farSkin = nullptr;
        }

        if (dataHandler) {
            if (auto* actorTypeKw = dataHandler->LookupForm<RE::BGSKeyword>(
                    kActorTypeNpcKeywordFormId, "Skyrim.esm")) {
                if (!targetNpc->HasKeyword(actorTypeKw)) {
                    targetNpc->AddKeyword(actorTypeKw);
                }
            }
        }
        // Opt custom hybrid-skinned / beast-toothed chimeras out of Racial Skin Variance's SPID
        // head-texture & teeth override spells (-RSVignore) so RSV does not replace MORWHA_HybFace_*
        // or sharp beast mouth textures via NiOverride 0.1-1.0s after spawn.
        if (auto* rsvIgnoreKw = RE::TESForm::LookupByEditorID<RE::BGSKeyword>("RSVignore")) {
            const bool wantRsvIgnore =
                !spec.hybridSkinId.empty() || spec.beastTeethKind != State::BeastTeethKind::None;
            if (wantRsvIgnore) {
                if (!targetNpc->HasKeyword(rsvIgnoreKw)) {
                    targetNpc->AddKeyword(rsvIgnoreKw);
                }
            } else if (targetNpc->HasKeyword(rsvIgnoreKw)) {
                targetNpc->RemoveKeyword(rsvIgnoreKw);
            }
        }
        SyncNpcAgeCompatibilityKeywords(targetNpc, spec);
        MortalHeritageKeywordService::GetSingleton().SyncNpcHeritageKeywords(
            targetNpc, spec, resolvedRace);

        // AI disposition and skill/attribute values.
        std::memset(&targetNpc->aiData, 0, sizeof(targetNpc->aiData));
        targetNpc->SetAggressionLevel(
            static_cast<RE::ACTOR_AGGRESSION>(std::min<std::uint8_t>(3u, spec.ai.aggression)));
        targetNpc->SetConfidenceLevel(
            static_cast<RE::ACTOR_CONFIDENCE>(std::min<std::uint8_t>(4u, spec.ai.confidence)));
        targetNpc->SetAssistanceLevel(
            static_cast<RE::ACTOR_ASSISTANCE>(std::min<std::uint8_t>(2u, spec.ai.assistance)));
        const std::uint8_t moral = std::min<std::uint8_t>(3u, spec.ai.morality);
        targetNpc->aiData.morality1 = (moral & 1u) != 0u;
        targetNpc->aiData.morality2 = (moral & 2u) != 0u;
        targetNpc->aiData.energyLevel2 = true;
        targetNpc->aiData.energyLevel5 = true;
        targetNpc->aiData.energyLevel6 = true;

        for (std::size_t sIdx = 0; sIdx < State::kNpcSkillCount; ++sIdx) {
            targetNpc->playerSkills.values[sIdx] = spec.stats.skills[sIdx];
            targetNpc->playerSkills.offsets[sIdx] = 0;
        }
        targetNpc->playerSkills.health = spec.stats.health;
        targetNpc->playerSkills.magicka = spec.stats.magicka;
        targetNpc->playerSkills.stamina = spec.stats.stamina;
        targetNpc->playerSkills.farawayModelDistance =
            baselineNpc ? baselineNpc->playerSkills.farawayModelDistance : 0.0f;

        // Perks, base spells, and factions.
        if (targetNpc->perks) {
            RE::free(targetNpc->perks);
            targetNpc->perks = nullptr;
            targetNpc->perkCount = 0;
        }
        if (!spec.perkRefs.empty()) {
            std::vector<RE::BGSPerk*> resolvedPerks;
            resolvedPerks.reserve(spec.perkRefs.size());
            for (const auto& pkRef : spec.perkRefs) {
                if (auto* pk = ResolvePluginForm<RE::BGSPerk>(pkRef);
                    pk && !pk->IsDeleted() &&
                    std::ranges::find(resolvedPerks, pk) == resolvedPerks.end()) {
                    resolvedPerks.push_back(pk);
                }
            }
            if (!resolvedPerks.empty()) {
                auto* perkArr = RE::calloc<RE::PerkRankData>(resolvedPerks.size());
                if (perkArr) {
                    for (std::size_t i = 0; i < resolvedPerks.size(); ++i) {
                        perkArr[i].perk = resolvedPerks[i];
                        perkArr[i].currentRank = 1;
                    }
                    targetNpc->perks = perkArr;
                    targetNpc->perkCount = static_cast<std::uint32_t>(resolvedPerks.size());
                }
            }
        }

        if (targetNpc->actorEffects && targetNpc->actorEffects->spells) {
            RE::free(targetNpc->actorEffects->spells);
            targetNpc->actorEffects->spells = nullptr;
            targetNpc->actorEffects->numSpells = 0;
        }
        if (!spec.spellRefs.empty()) {
            std::vector<RE::SpellItem*> resolvedSpells;
            resolvedSpells.reserve(spec.spellRefs.size());
            for (const auto& spRef : spec.spellRefs) {
                if (auto* sp = ResolvePluginForm<RE::SpellItem>(spRef);
                    sp && !sp->IsDeleted() &&
                    std::ranges::find(resolvedSpells, sp) == resolvedSpells.end()) {
                    resolvedSpells.push_back(sp);
                }
            }
            if (!resolvedSpells.empty()) {
                if (!targetNpc->actorEffects) {
                    targetNpc->actorEffects = new RE::TESSpellList::SpellData();
                }
                if (targetNpc->actorEffects) {
                    (void)targetNpc->actorEffects->AddSpells(resolvedSpells);
                }
            }
        }

        targetNpc->factions.clear();
        for (const auto& facSpec : spec.factions) {
            if (auto* fac = ResolvePluginForm<RE::TESFaction>(facSpec.factionRef);
                fac && !fac->IsDeleted()) {
                targetNpc->factions.push_back(RE::FACTION_RANK{ fac, facSpec.rank });
            }
        }

        targetNpc->SetFullName(spec.displayName.empty() ? "Mortal" : spec.displayName.c_str());
        targetNpc->shortName = "";
        targetNpc->height = spec.height;
        targetNpc->weight = spec.weight;
        targetNpc->soundLevel = RE::SOUND_LEVEL::kNormal;

        // Allocate FaceData on Skyrim's heap (no TES_HEAP_REDEFINE_NEW on FaceData).
        if (!targetNpc->faceData) {
            targetNpc->faceData = RE::calloc<RE::TESNPC::FaceData>(1);
        }
        if (!targetNpc->faceData) {
            MORWHA_LOG_ERROR("HydrateTesNpc: Failed to allocate FaceData for {:08X}", targetNpc->GetFormID());
            return false;
        }
        Foundation::FaceMorphVector sanitizedMorphs = spec.faceMorphs;
        SanitizeFaceMorphPartsForRace(
            sanitizedMorphs,
            resolvedRace,
            spec.sex,
            faceRelated,
            nullptr,
            spec.donorMode == Foundation::DonorMode::TwoDonorOffspring);
        {
            const auto allowedEyes = CollectAllowedMorphPresets(
                resolvedRace,
                spec.sex,
                faceRelated,
                2u,
                nullptr,
                spec.donorMode == Foundation::DonorMode::TwoDonorOffspring);
            const auto allowedMouth = CollectAllowedMorphPresets(
                resolvedRace, spec.sex, faceRelated, 3u, nullptr, false);
            TriProjectorCache::GetSingleton().EnforceOrbitalClearanceForTarget(
                resolvedRace,
                spec.sex,
                activeTopology,
                sanitizedMorphs,
                allowedEyes,
                allowedMouth);
        }
        for (std::size_t i = 0; i < Foundation::kVanillaFaceMorphCount; ++i) {
            targetNpc->faceData->morphs[i] = sanitizedMorphs.morphs[i];
        }
        for (std::size_t slot = 0; slot < RE::TESNPC::FaceData::Parts::kTotal; ++slot) {
            const std::int32_t presetVal = sanitizedMorphs.parts[slot];
            if (slot == RE::TESNPC::FaceData::Parts::kUnknown ||
                presetVal == Foundation::kUnusedFacePartPreset || presetVal < 0) {
                targetNpc->faceData->parts[slot] = RE::TESNPC::FaceData::kDefault;
            } else {
                targetNpc->faceData->parts[slot] = presetVal;
            }
        }

        // Rebuild headParts cleanly with one part per HeadPartType + topology-matched fallbacks.
        if (targetNpc->headParts) {
            RE::free(targetNpc->headParts);
            targetNpc->headParts = nullptr;
            targetNpc->numHeadParts = 0;
        }
        std::array<RE::BGSHeadPart*, static_cast<std::size_t>(RE::BGSHeadPart::HeadPartType::kTotal)> selectedParts{};
        std::vector<RE::BGSHeadPart*> extendedParts;
        for (const auto& hpRef : spec.headParts) {
            auto* part = ResolvePluginForm<RE::BGSHeadPart>(hpRef);
            if (!part) {
                continue;
            }
            if (!IsHeadPartAllowedForHydration(
                    part,
                    resolvedRace,
                    spec.sex,
                    activeTopology,
                    smpLoaded,
                    spec.donorMode == Foundation::DonorMode::TwoDonorOffspring)) {
                MORWHA_LOG_DEBUG(
                    "HydrateTesNpc: Dropped out-of-pool persisted headPart {:08X} ('{}') on {:08X}",
                    part->GetFormID(),
                    part->GetFormEditorID() ? part->GetFormEditorID() : "",
                    targetNpc->GetFormID());
                continue;
            }
            const auto typeIdx = static_cast<std::size_t>(part->type.get());
            if (typeIdx < selectedParts.size()) {
                if (selectedParts[typeIdx] == nullptr) {
                    selectedParts[typeIdx] = part;
                }
            } else if (std::ranges::find(extendedParts, part) == extendedParts.end()) {
                extendedParts.push_back(part);
            }
        }
        const std::array<RE::BGSHeadPart::HeadPartType, 5> requiredTypes = {
            RE::BGSHeadPart::HeadPartType::kFace,
            RE::BGSHeadPart::HeadPartType::kMisc,
            RE::BGSHeadPart::HeadPartType::kEyes,
            RE::BGSHeadPart::HeadPartType::kHair,
            RE::BGSHeadPart::HeadPartType::kEyebrows
        };
        for (const auto reqType : requiredTypes) {
            const auto idx = static_cast<std::size_t>(reqType);
            if (!selectedParts[idx]) {
                selectedParts[idx] = FindFallbackHeadPart(
                    reqType, activeTopology, baselineNpc, resolvedRace, faceRelated, spec.sex, smpLoaded);
            }
        }
        const auto miscIdx = static_cast<std::size_t>(RE::BGSHeadPart::HeadPartType::kMisc);
        if (selectedParts[miscIdx]) {
            if (accessoryCatalog.IsCatalogedAccessoryMouth(selectedParts[miscIdx])) {
                if (auto* resolvedMouth = accessoryCatalog.ResolveHydratedCatalogMouthPart(selectedParts[miscIdx])) {
                    selectedParts[miscIdx] = resolvedMouth;
                }
            } else if (spec.beastTeethKind != State::BeastTeethKind::None) {
                if (auto* beastMouth = accessoryCatalog.ResolveRuntimeBeastMouthPart(
                        selectedParts[miscIdx], spec.beastTeethKind)) {
                    selectedParts[miscIdx] = beastMouth;
                }
            }
            selectedParts[miscIdx] =
                accessoryCatalog.ResolveRaceCompatibleAccessoryHeadPart(selectedParts[miscIdx], resolvedRace);
        }
        for (auto*& extPart : extendedParts) {
            extPart = accessoryCatalog.ResolveRaceCompatibleAccessoryHeadPart(extPart, resolvedRace);
        }
        if (!spec.skinAddonRefs.empty()) {
            const auto promotedHorns = accessoryCatalog.ResolvePromotedHornHeadParts(
                resolvedRace, spec.sex, spec.skinAddonRefs);
            for (auto* hornPart : promotedHorns) {
                if (!hornPart) {
                    continue;
                }
                const auto existingIt = std::ranges::find_if(
                    extendedParts, [&](const RE::BGSHeadPart* existing) {
                        return existing && existing->type == hornPart->type;
                    });
                if (existingIt == extendedParts.end()) {
                    extendedParts.push_back(hornPart);
                }
            }
        }
        const auto faceIdx = static_cast<std::size_t>(RE::BGSHeadPart::HeadPartType::kFace);

        HybridSkinService::ResolvedHybridForms resolvedHybridForms{};
        if (!spec.hybridSkinId.empty()) {
            resolvedHybridForms = hybridSkinService.ResolveHybridNpcForms(
                spec.hybridSkinId,
                effectiveBaseSkin,
                resolvedRace,
                spec.sex,
                spec.beastTailKind,
                selectedParts[faceIdx]);
            if (resolvedHybridForms.skinArmor) {
                targetNpc->skin = resolvedHybridForms.skinArmor;
                targetNpc->farSkin = nullptr;
            } else if (auto* fallbackTailSkin = accessoryCatalog.GetOrCreateChimeraSkinArmor(
                           effectiveBaseSkin, resolvedRace, spec.beastTailKind)) {
                targetNpc->skin = fallbackTailSkin;
                targetNpc->farSkin = nullptr;
            } else if (preservedExternalSkin != nullptr) {
                targetNpc->skin = preservedExternalSkin;
                targetNpc->farSkin = nullptr;
            }
            if (resolvedHybridForms.faceHeadPartClone) {
                selectedParts[faceIdx] = resolvedHybridForms.faceHeadPartClone;
            }
        }

        for (auto* part : selectedParts) {
            if (part) {
                targetNpc->ChangeHeadPart(part);
            }
        }
        for (auto* extPart : extendedParts) {
            if (extPart) {
                targetNpc->ChangeHeadPart(extPart);
            }
        }

        // HeadRelatedData: complexion texture set + BGSColorForm hair color.
        if (!targetNpc->headRelatedData) {
            targetNpc->headRelatedData = new RE::TESNPC::HeadRelatedData();
        }
        RE::BGSTextureSet* faceTex = resolvedHybridForms.faceTextureSet
            ? resolvedHybridForms.faceTextureSet
            : ResolvePluginForm<RE::BGSTextureSet>(spec.faceTextureSetRef);
        if (!faceTex && faceRelated) {
            faceTex = faceRelated->defaultFaceDetailsTextureSet;
        }
        if (!faceTex && baselineNpc && baselineNpc->headRelatedData) {
            faceTex = baselineNpc->headRelatedData->faceDetails;
        }
        targetNpc->SetFaceTexture(faceTex);

        RE::BGSColorForm* hairColorForm = ResolvePluginForm<RE::BGSColorForm>(spec.hairColorFormRef);
        if (!hairColorForm && faceRelated && faceRelated->availableHairColors && !faceRelated->availableHairColors->empty()) {
            std::uint32_t bestDist = (std::numeric_limits<std::uint32_t>::max)();
            for (auto* cand : *faceRelated->availableHairColors) {
                if (!cand) {
                    continue;
                }
                const auto dist = ColorDistanceSq(spec.hairColor, ToRgbColor(cand->color));
                if (dist < bestDist) {
                    bestDist = dist;
                    hairColorForm = cand;
                }
            }
        }
        if (!hairColorForm && faceRelated) {
            hairColorForm = faceRelated->defaultHairColor;
        }
        if (!hairColorForm && baselineNpc && baselineNpc->headRelatedData) {
            hairColorForm = baselineNpc->headRelatedData->hairColor;
        }
        targetNpc->SetHairColor(hairColorForm);

        // Rebuild tintLayers on Skyrim's heap using race-resolved tintIndex and 0..100 interpolationValue.
        if (!targetNpc->tintLayers) {
            targetNpc->tintLayers = new RE::BSTArray<RE::TESNPC::Layer*>();
        } else {
            for (auto* oldLayer : *targetNpc->tintLayers) {
                if (oldLayer) {
                    RE::free(oldLayer);
                }
            }
            targetNpc->tintLayers->clear();
        }

        bool emittedSkinTone = false;
        auto pushTintLayer = [&](std::uint16_t tintIndex, Foundation::RgbColor col, std::uint16_t interpVal) {
            auto* layer = RE::calloc<RE::TESNPC::Layer>(1);
            if (!layer) {
                return;
            }
            layer->tintColor = RE::Color(col.r, col.g, col.b, 255);
            layer->tintIndex = tintIndex;
            layer->preset = 0xFFFFu;
            layer->interpolationValue = std::min<std::uint16_t>(100u, interpVal);
            targetNpc->tintLayers->push_back(layer);
        };

        Foundation::RgbColor appliedSkinColor = spec.skinColor;
        std::uint16_t appliedSkinInterp = 100;
        const RE::TESRace::FaceRelatedData::TintAsset* appliedSkinAsset = nullptr;

        for (const auto& layerSpec : spec.tintLayers) {
            const auto* asset =
                FindRaceTintAsset(faceRelated, layerSpec.semanticType, layerSpec.explicitTintIndex);
            if (!asset) {
                continue;
            }
            if (layerSpec.semanticType == State::TintSemanticType::SkinTone) {
                appliedSkinColor = layerSpec.color;
                appliedSkinInterp = std::min<std::uint16_t>(100u, layerSpec.interpolationValue);
                appliedSkinAsset = asset;
                pushTintLayer(asset->texture.index, appliedSkinColor, appliedSkinInterp);
                emittedSkinTone = true;
            } else {
                pushTintLayer(asset->texture.index, layerSpec.color, layerSpec.interpolationValue);
            }
        }
        if (!emittedSkinTone) {
            if (const auto* skinAsset = FindRaceTintAsset(
                    faceRelated, State::TintSemanticType::SkinTone, State::kUnsetExplicitTintIndex)) {
                appliedSkinAsset = skinAsset;
                appliedSkinColor = spec.skinColor;
                appliedSkinInterp = 100;
                pushTintLayer(skinAsset->texture.index, appliedSkinColor, appliedSkinInterp);
            }
        }

        RE::TintMask skinTintMask{};
        skinTintMask.texture = appliedSkinAsset ? const_cast<RE::TESTexture*>(&appliedSkinAsset->texture.file) : nullptr;
        skinTintMask.color = RE::Color(appliedSkinColor.r, appliedSkinColor.g, appliedSkinColor.b, 0);
        skinTintMask.alpha = static_cast<float>(appliedSkinInterp) / 100.0f;
        skinTintMask.type = RE::TintMask::Type::kSkinTone;

        RE::NiColorA computedSkin{ -1.0f, -1.0f, -1.0f, 1.0f };
        targetNpc->bodyTintColor = RE::Color(0, 0, 0, 0);
        targetNpc->SetSkinFromTint(&computedSkin, &skinTintMask, true);

        if (targetNpc->bodyTintColor.red == 0 &&
            targetNpc->bodyTintColor.green == 0 &&
            targetNpc->bodyTintColor.blue == 0) {
            if (std::isfinite(computedSkin.red) &&
                std::isfinite(computedSkin.green) &&
                std::isfinite(computedSkin.blue) &&
                computedSkin.red >= 0.0f &&
                (computedSkin.red > 0.001f || computedSkin.green > 0.001f || computedSkin.blue > 0.001f)) {
                auto toByte = [](float c) noexcept -> std::uint8_t {
                    return static_cast<std::uint8_t>(std::clamp<long>(std::lround(c * 255.0f), 0l, 255l));
                };
                targetNpc->bodyTintColor = RE::Color(
                    toByte(computedSkin.red),
                    toByte(computedSkin.green),
                    toByte(computedSkin.blue),
                    255);
            } else {
                auto fallbackChan = [&](std::uint8_t c) noexcept -> std::uint8_t {
                    const float blended =
                        128.0f * (1.0f - skinTintMask.alpha) + static_cast<float>(c) * skinTintMask.alpha;
                    return static_cast<std::uint8_t>(std::clamp<long>(std::lround(blended), 0l, 255l));
                };
                targetNpc->bodyTintColor = RE::Color(
                    fallbackChan(appliedSkinColor.r),
                    fallbackChan(appliedSkinColor.g),
                    fallbackChan(appliedSkinColor.b),
                    255);
            }
        }
        targetNpc->bodyTintColor.alpha = 255;

        if (!targetNpc->IsInitialized()) {
            targetNpc->InitItem();
        }

        // Enforce critical post-InitItem invariants.
        targetNpc->faceNPC = nullptr;
        if (!targetNpc->attackDataMap) {
            targetNpc->attackDataMap = attackMap;
        }
        targetNpc->actorData.actorBaseFlags.reset(RE::ACTOR_BASE_DATA::Flag::kRespawn);

        MORWHA_LOG_INFO(
            "HydrateTesNpc: Hydrated {:08X} ('{}', key='{}', race={:08X}, sex={}, topology={}, headParts={}, tints={}, bodyTint=({},{},{}), midSession={})",
            targetNpc->GetFormID(),
            spec.displayName,
            spec.slotKey,
            resolvedRace->GetFormID(),
            static_cast<unsigned>(spec.sex),
            static_cast<unsigned>(activeTopology),
            static_cast<int>(targetNpc->numHeadParts),
            targetNpc->tintLayers ? targetNpc->tintLayers->size() : 0u,
            targetNpc->bodyTintColor.red,
            targetNpc->bodyTintColor.green,
            targetNpc->bodyTintColor.blue,
            isMidSession);

        return true;
    }

    void SyncLiveActorFromSpec(
        RE::Actor* actor,
        RE::TESNPC* baseNpc,
        const State::DynamicNpcSpec& spec,
        bool refreshOutfit) {
        if (!actor || !baseNpc) {
            return;
        }

        if (auto* cs = baseNpc->combatStyle) {
            auto* xCs = actor->extraList.GetByType<RE::ExtraCombatStyle>();
            if (!xCs && !spec.combatStyleRef.IsEmpty()) {
                xCs = RE::BSExtraData::Create<RE::ExtraCombatStyle>();
                if (xCs) {
                    xCs->combatStyle = cs;
                    actor->extraList.Add(xCs);
                }
            } else if (xCs) {
                xCs->combatStyle = cs;
            }
        }

        if (auto* avOwner = actor->AsActorValueOwner()) {
            if (!spec.stats.autoCalcStats) {
                avOwner->SetBaseActorValue(
                    RE::ActorValue::kHealth, static_cast<float>(baseNpc->playerSkills.health));
                avOwner->SetBaseActorValue(
                    RE::ActorValue::kMagicka, static_cast<float>(baseNpc->playerSkills.magicka));
                avOwner->SetBaseActorValue(
                    RE::ActorValue::kStamina, static_cast<float>(baseNpc->playerSkills.stamina));
                for (std::size_t sIdx = 0; sIdx < State::kNpcSkillCount; ++sIdx) {
                    const auto av = static_cast<RE::ActorValue>(
                        static_cast<int>(RE::ActorValue::kOneHanded) + static_cast<int>(sIdx));
                    avOwner->SetBaseActorValue(
                        av, static_cast<float>(baseNpc->playerSkills.values[sIdx]));
                }
            }
            avOwner->SetBaseActorValue(
                RE::ActorValue::kAggression, static_cast<float>(spec.ai.aggression));
            avOwner->SetBaseActorValue(
                RE::ActorValue::kConfidence, static_cast<float>(spec.ai.confidence));
            avOwner->SetBaseActorValue(
                RE::ActorValue::kAssistance, static_cast<float>(spec.ai.assistance));
            avOwner->SetBaseActorValue(
                RE::ActorValue::kMorality, static_cast<float>(spec.ai.morality));
        }

        if (!spec.perkRefs.empty()) {
            actor->ApplyPerksFromBase();
        }

        for (const auto& spRef : spec.spellRefs) {
            if (auto* sp = ResolvePluginForm<RE::SpellItem>(spRef);
                sp && !sp->IsDeleted() && !actor->HasSpell(sp)) {
                actor->AddSpell(sp);
            }
        }

        for (const auto& facSpec : spec.factions) {
            if (auto* fac = ResolvePluginForm<RE::TESFaction>(facSpec.factionRef);
                fac && !fac->IsDeleted() && facSpec.rank >= 0 && !actor->IsInFaction(fac)) {
                actor->AddToFaction(fac, facSpec.rank);
            }
        }

        if (refreshOutfit && baseNpc->defaultOutfit && !actor->IsDisabled()) {
            actor->InitInventoryIfRequired();
            actor->AddWornOutfit(baseNpc->defaultOutfit, true);
        }
    }

}  // namespace MORWHA::Engine
