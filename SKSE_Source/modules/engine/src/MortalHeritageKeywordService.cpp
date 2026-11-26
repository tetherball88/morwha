#include "morwha/engine/MortalHeritageKeywordService.h"

#include "morwha/engine/EngineLogging.h"
#include "morwha/engine/FormRefResolver.h"
#include "morwha/engine/TesNpcHydrator.h"

#include <RE/B/BGSKeyword.h>
#include <RE/I/IFormFactory.h>
#include <RE/T/TESDataHandler.h>
#include <RE/T/TESForm.h>
#include <RE/T/TESNPC.h>
#include <RE/T/TESRace.h>

#include <algorithm>
#include <cctype>
#include <vector>

namespace MORWHA::Engine {

    namespace {

        constexpr std::uint32_t kKhajiitRaceLocalId = 0x013745u;
        constexpr std::uint32_t kArgonianRaceLocalId = 0x013740u;

        [[nodiscard]] bool EqualsSkyrimEsm(std::string_view pluginName) noexcept {
            constexpr std::string_view kSkyrim = "skyrim.esm";
            if (pluginName.size() != kSkyrim.size()) {
                return false;
            }
            for (std::size_t i = 0u; i < kSkyrim.size(); ++i) {
                if (std::tolower(static_cast<unsigned char>(pluginName[i])) != kSkyrim[i]) {
                    return false;
                }
            }
            return true;
        }

        void ExtractRaceIdentity(
            const RE::TESRace* race,
            const State::PluginFormRef& fallbackRef,
            std::string& outEdid,
            std::uint32_t& outSkyrimLocalId,
            std::uint32_t& outFormId) {
            outEdid.clear();
            outSkyrimLocalId = 0u;
            outFormId = 0u;

            if (race) {
                outFormId = race->GetFormID();
                if (const char* ed = race->GetFormEditorID(); ed && ed[0] != '\0') {
                    outEdid = ed;
                }
                if (const auto* file = race->GetFile(0);
                    file && EqualsSkyrimEsm(file->GetFilename())) {
                    outSkyrimLocalId = race->GetLocalFormID();
                }
            }
            if (outFormId == 0u && !fallbackRef.IsEmpty()) {
                if (const auto* resolved = ResolvePluginForm<RE::TESRace>(fallbackRef)) {
                    outFormId = resolved->GetFormID();
                    if (outEdid.empty()) {
                        if (const char* ed = resolved->GetFormEditorID(); ed && ed[0] != '\0') {
                            outEdid = ed;
                        }
                    }
                }
                if (outSkyrimLocalId == 0u && EqualsSkyrimEsm(fallbackRef.plugin)) {
                    outSkyrimLocalId = fallbackRef.localId;
                }
            }
        }

        [[nodiscard]] const RE::TESRace* TryExtractLegacyDonorRace(
            const State::PluginFormRef& donorRef,
            Foundation::Sex targetSex) {
            if (donorRef.IsEmpty()) {
                return nullptr;
            }
            RE::TESForm* form = ResolvePluginForm(donorRef);
            if (!form || form->IsDeleted()) {
                return nullptr;
            }
            RE::TESRace* rawRace = nullptr;
            if (auto* actor = form->As<RE::Actor>()) {
                rawRace = actor->GetRace();
            } else if (auto* npc = form->As<RE::TESNPC>()) {
                rawRace = npc->race;
            }
            return rawRace ? UnwrapToPlayableSynthesisRace(rawRace, targetSex) : nullptr;
        }

    }  // namespace

    MortalHeritageKeywordService& MortalHeritageKeywordService::GetSingleton() noexcept {
        static MortalHeritageKeywordService instance;
        return instance;
    }

    RE::BGSKeyword* MortalHeritageKeywordService::GetOrCreateKeyword(std::string_view editorId) {
        if (editorId.empty()) {
            return nullptr;
        }
        const std::string key(editorId);

        std::lock_guard lock(mutex_);
        if (const auto it = keywordCache_.find(key); it != keywordCache_.end()) {
            return it->second;
        }

        if (auto* existing = RE::TESForm::LookupByEditorID<RE::BGSKeyword>(key)) {
            keywordCache_.emplace(key, existing);
            return existing;
        }

        auto* created = RE::IFormFactory::Create<RE::BGSKeyword>();
        if (!created) {
            MORWHA_LOG_ERROR(
                "MortalHeritageKeywordService: Failed allocating runtime BGSKeyword '{}'",
                key);
            return nullptr;
        }

        created->SetFormEditorID(key.c_str());
        if (RE::TESForm::LookupByEditorID(key) == nullptr) {
            const auto& [map, mapLock] = RE::TESForm::GetAllFormsByEditorID();
            const RE::BSWriteLockGuard wl{ mapLock };
            if (map) {
                map->emplace(created->formEditorID, created);
            }
        }
        if (auto* dataHandler = RE::TESDataHandler::GetSingleton()) {
            dataHandler->GetFormArray<RE::BGSKeyword>().push_back(created);
        }

        keywordCache_.emplace(key, created);
        return created;
    }

    void MortalHeritageKeywordService::InitializeAtDataLoaded() {
        const auto catalog = Core::GetStandardHeritageKeywordCatalog();
        std::size_t readyCount = 0u;
        for (const auto& kwName : catalog) {
            if (GetOrCreateKeyword(kwName) != nullptr) {
                ++readyCount;
            }
        }
        MORWHA_LOG_INFO(
            "MortalHeritageKeywordService: Pre-registered {}/{} runtime heritage BGSKeywords",
            readyCount,
            catalog.size());
    }

    ResolvedEngineHeritage MortalHeritageKeywordService::ResolveSpecHeritage(
        const State::DynamicNpcSpec& spec,
        const RE::TESRace* resolvedPrimaryRace) const {
        ResolvedEngineHeritage result{};

        std::string primaryEdid;
        std::uint32_t primarySkyrimId = 0u;
        ExtractRaceIdentity(
            resolvedPrimaryRace,
            spec.raceRef,
            primaryEdid,
            primarySkyrimId,
            result.primaryRaceFormId);

        std::string secondaryEdid;
        std::uint32_t secondarySkyrimId = 0u;
        State::PluginFormRef effectiveSecondaryRef = spec.secondaryRaceRef;
        std::uint8_t effectiveTierRaw = spec.chimeraTier;

        // Backward-compatibility inference for specs persisted prior to secondaryRaceRef/chimeraTier.
        if (effectiveSecondaryRef.IsEmpty() &&
            spec.donorMode == Foundation::DonorMode::TwoDonorOffspring) {
            if (!spec.hybridSkinId.empty() || spec.beastTailKind != State::BeastTailKind::None) {
                if (effectiveTierRaw == 0u) {
                    effectiveTierRaw = 2u;
                }
                if (const auto* coat = Core::FindHybridCoat(spec.hybridSkinId)) {
                    const bool khajSec =
                        (coat->group == Core::HybridCoatGroup::HumanoidKhajiitFur ||
                         coat->group == Core::HybridCoatGroup::ArgonianKhajiitFur);
                    effectiveSecondaryRef = State::PluginFormRef{
                        "Skyrim.esm",
                        khajSec ? kKhajiitRaceLocalId : kArgonianRaceLocalId
                    };
                } else if (spec.beastTailKind == State::BeastTailKind::Khajiit) {
                    effectiveSecondaryRef = State::PluginFormRef{ "Skyrim.esm", kKhajiitRaceLocalId };
                } else if (spec.beastTailKind == State::BeastTailKind::Argonian) {
                    effectiveSecondaryRef = State::PluginFormRef{ "Skyrim.esm", kArgonianRaceLocalId };
                }
            }
            if (effectiveSecondaryRef.IsEmpty()) {
                const auto* rA = TryExtractLegacyDonorRace(spec.donorARef, spec.sex);
                const auto* rB = TryExtractLegacyDonorRace(spec.donorBRef, spec.sex);
                if (rA && rB && rA != rB) {
                    const RE::TESRace* other =
                        (rA->GetFormID() == result.primaryRaceFormId) ? rB : rA;
                    effectiveSecondaryRef = MakePluginFormRef(other);
                }
            }
        }

        ExtractRaceIdentity(
            nullptr,
            effectiveSecondaryRef,
            secondaryEdid,
            secondarySkyrimId,
            result.secondaryRaceFormId);

        const auto tierEnum = (effectiveTierRaw >= 2u)
            ? Core::BeastChimeraTier::Tier2EquippedParts
            : ((effectiveTierRaw == 1u) ? Core::BeastChimeraTier::Tier1DominantMorph
                                        : Core::BeastChimeraTier::Disabled);

        result.summary = Core::ResolveMortalHeritage(
            primaryEdid,
            primarySkyrimId,
            secondaryEdid,
            secondarySkyrimId,
            tierEnum);
        if (!result.summary.isMixedHeritage) {
            result.secondaryRaceFormId = 0u;
        }
        return result;
    }

    void MortalHeritageKeywordService::SyncNpcHeritageKeywords(
        RE::TESNPC* targetNpc,
        const State::DynamicNpcSpec& spec,
        const RE::TESRace* resolvedPrimaryRace) {
        if (!targetNpc) {
            return;
        }

        const auto resolved = ResolveSpecHeritage(spec, resolvedPrimaryRace);

        std::vector<RE::BGSKeyword*> staleMorwhaKeywords;
        for (auto* kw : targetNpc->GetKeywords()) {
            if (!kw) {
                continue;
            }
            const char* edid = kw->GetFormEditorID();
            if (edid && std::string_view(edid).starts_with("MORWHA_")) {
                staleMorwhaKeywords.push_back(kw);
            }
        }
        if (!staleMorwhaKeywords.empty()) {
            (void)targetNpc->RemoveKeywords(staleMorwhaKeywords);
        }

        std::vector<RE::BGSKeyword*> desiredKeywords;
        desiredKeywords.reserve(resolved.summary.keywords.size());
        for (const auto& kwEdid : resolved.summary.keywords) {
            if (auto* kwForm = GetOrCreateKeyword(kwEdid)) {
                desiredKeywords.push_back(kwForm);
            }
        }
        if (!desiredKeywords.empty()) {
            (void)targetNpc->AddKeywords(desiredKeywords);
        }
    }

}  // namespace MORWHA::Engine
