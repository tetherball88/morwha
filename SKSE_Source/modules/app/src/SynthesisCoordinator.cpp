#include "morwha/app/SynthesisCoordinator.h"

#include "morwha/app/MortalOverridesApplier.h"
#include "morwha/core/MorwhaCore.h"
#include "morwha/engine/AgeProfileResolver.h"
#include "morwha/engine/ChimeraAccessoryCatalog.h"
#include "morwha/engine/ChimeraEngineSupport.h"
#include "morwha/engine/DonorPhenotypeExtractor.h"
#include "morwha/engine/DpfClient.h"
#include "morwha/engine/EngineLogging.h"
#include "morwha/engine/FaceAnchorCatalog.h"
#include "morwha/engine/FormRefResolver.h"
#include "morwha/engine/GeneticSpecBuilder.h"
#include "morwha/engine/HeadPartPoolIndexer.h"
#include "morwha/engine/HybridSkinService.h"
#include "morwha/engine/MortalHeritageKeywordService.h"
#include "morwha/engine/Mortal3dSyncService.h"
#include "morwha/engine/MortalSpawner.h"
#include "morwha/engine/SkseSerializationCallbacks.h"
#include "morwha/engine/TesNpcHydrator.h"
#include "morwha/engine/TriProjectorCache.h"
#include "morwha/state/DynamicNpcRegistry.h"
#include "morwha/state/MortalRequestJson.h"
#include "morwha/state/MorwhaIniConfig.h"
#include "morwha/state/SessionEpoch.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <format>

namespace MORWHA::App {

    namespace {

        constexpr std::uint32_t kNordRaceFormId = 0x013746u;

        [[nodiscard]] std::uint64_t SplitMix64(std::uint64_t x) noexcept {
            x += 0x9E3779B97F4A7C15ull;
            x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
            x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
            return x ^ (x >> 31);
        }

        [[nodiscard]] RE::TESRace* UnwrapMortalSynthesisRace(
            RE::TESRace* candidate,
            Foundation::Sex sex) noexcept {
            return Engine::UnwrapToPlayableSynthesisRace(candidate, sex);
        }

        [[nodiscard]] RE::TESRace* ResolveTargetRace(RE::TESRace* requested, Foundation::Sex sex) noexcept {
            if (auto* unwrapped = UnwrapMortalSynthesisRace(requested, sex)) {
                return unwrapped;
            }
            if (auto* dataHandler = RE::TESDataHandler::GetSingleton()) {
                if (auto* nord = dataHandler->LookupForm<RE::TESRace>(kNordRaceFormId, "Skyrim.esm")) {
                    return UnwrapMortalSynthesisRace(nord, sex);
                }
            }
            return nullptr;
        }

        [[nodiscard]] Foundation::Sex ResolveTargetSex(
            std::int32_t sexInt,
            Foundation::Seed64 seed,
            bool allowRandom) noexcept {
            if (allowRandom && sexInt < 0) {
                return Core::ChooseRandomSex(seed);
            }
            return (sexInt == 0) ? Foundation::Sex::Male : Foundation::Sex::Female;
        }

        [[nodiscard]] std::string SanitizeSlotKeyPrefix(
            std::string_view rawPrefix,
            std::string_view fallbackPrefix) {
            std::string out;
            out.reserve(std::min<std::size_t>(rawPrefix.size(), 40u));
            for (const char ch : rawPrefix) {
                if (out.size() >= 40u) {
                    break;
                }
                const auto uch = static_cast<unsigned char>(ch);
                if (std::isalnum(uch) || ch == '_' || ch == '-') {
                    out.push_back(ch);
                } else if (ch == ' ') {
                    out.push_back('_');
                }
            }
            while (!out.empty() && (out.back() == '_' || out.back() == '-')) {
                out.pop_back();
            }
            return out.empty() ? std::string(fallbackPrefix) : out;
        }

        [[nodiscard]] bool ValidateSpawnPreconditions(
            std::string_view callerName,
            bool ready,
            bool registryCorrupt,
            RE::Actor* anchor) noexcept {
            if (!ready) {
                MORWHA_LOG_ERROR("{}: MORWHA is not ready yet", callerName);
                return false;
            }
            if (!Engine::DpfClient::GetSingleton().IsDpfAvailable()) {
                MORWHA_LOG_ERROR("{}: DPF RE is unavailable", callerName);
                return false;
            }
            if (registryCorrupt) {
                MORWHA_LOG_ERROR("{}: Aborted because DynamicNPCRegistry.json failed validation on boot", callerName);
                return false;
            }
            if (!anchor) {
                MORWHA_LOG_ERROR("{}: akAnchor is null", callerName);
                return false;
            }
            return true;
        }

        [[nodiscard]] std::optional<Engine::ExtractedEngineDonor> ExtractDonorFromForm(
            RE::TESForm* donorForm) {
            if (!donorForm || donorForm->IsDeleted()) {
                return std::nullopt;
            }
            if (auto* actor = donorForm->As<RE::Actor>()) {
                return Engine::ExtractDonorPhenotype(actor);
            }
            if (auto* baseNpc = donorForm->As<RE::TESNPC>()) {
                return Engine::ExtractDonorPhenotypeFromBase(baseNpc);
            }
            return std::nullopt;
        }

        void PopulateDonorProvenanceFromForm(
            RE::TESForm* donorForm,
            const Engine::ExtractedEngineDonor& extracted,
            State::PluginFormRef& outRef,
            std::string& outSlotKey,
            Foundation::FormRawId& outRefId) {
            outRef = {};
            outSlotKey.clear();
            outRefId = {};
            if (!donorForm) {
                return;
            }

            if (auto* donorActor = donorForm->As<RE::Actor>()) {
                outRefId = Foundation::FormRawId{ donorActor->GetFormID() };
                if (const auto donorLink = Serialization::FindActiveLinkByActor(outRefId)) {
                    outSlotKey = donorLink->slotKey;
                }
                if ((donorActor->GetFormID() & 0xFF000000u) != 0xFF000000u) {
                    outRef = Engine::MakePluginFormRef(donorActor);
                } else if (extracted.baseNpc &&
                           (extracted.baseNpc->GetFormID() & 0xFF000000u) != 0xFF000000u) {
                    outRef = Engine::MakePluginFormRef(extracted.baseNpc);
                }
                return;
            }

            if (auto* baseNpc = donorForm->As<RE::TESNPC>()) {
                outRefId = Foundation::FormRawId{ baseNpc->GetFormID() };
                if ((baseNpc->GetFormID() & 0xFF000000u) != 0xFF000000u) {
                    outRef = Engine::MakePluginFormRef(baseNpc);
                }
            }
        }

        [[nodiscard]] std::optional<Engine::ExtractedEngineDonor> ResolveStoredDonor(
            Foundation::FormRawId linkDonorId,
            const State::PluginFormRef& jsonDonorRef,
            std::string_view jsonSlotKey,
            Foundation::FormRawId& outResolvedRefId) {
            outResolvedRefId = linkDonorId;
            if (!linkDonorId.IsNull()) {
                if (auto* actor = RE::TESForm::LookupByID<RE::Actor>(linkDonorId.raw());
                    actor && !actor->IsDeleted()) {
                    if (auto extracted = Engine::ExtractDonorPhenotype(actor)) {
                        return extracted;
                    }
                }
                if (auto* baseNpc = RE::TESForm::LookupByID<RE::TESNPC>(linkDonorId.raw());
                    baseNpc && !baseNpc->IsDeleted()) {
                    if (auto extracted = Engine::ExtractDonorPhenotypeFromBase(baseNpc)) {
                        return extracted;
                    }
                }
            }

            if (!jsonDonorRef.IsEmpty()) {
                if (auto* form = Engine::ResolvePluginForm(jsonDonorRef)) {
                    if (auto* actor = form->As<RE::Actor>(); actor && !actor->IsDeleted()) {
                        if (auto extracted = Engine::ExtractDonorPhenotype(actor)) {
                            outResolvedRefId = Foundation::FormRawId{ actor->GetFormID() };
                            return extracted;
                        }
                    } else if (auto* baseNpc = form->As<RE::TESNPC>()) {
                        if (auto extracted = Engine::ExtractDonorPhenotypeFromBase(baseNpc)) {
                            return extracted;
                        }
                    }
                }
            }

            if (!jsonSlotKey.empty()) {
                if (const auto donorLink = Serialization::FindActiveLinkBySlotKey(jsonSlotKey);
                    donorLink && !donorLink->actorRefId.IsNull()) {
                    if (auto* actor = RE::TESForm::LookupByID<RE::Actor>(donorLink->actorRefId.raw());
                        actor && !actor->IsDeleted()) {
                        if (auto extracted = Engine::ExtractDonorPhenotype(actor)) {
                            outResolvedRefId = donorLink->actorRefId;
                            return extracted;
                        }
                    }
                }
                if (const auto slot = Engine::DpfClient::GetSingleton().AcquireOrRecoverNpcSlot(jsonSlotKey);
                    slot && slot->npc) {
                    if (auto extracted = Engine::ExtractDonorPhenotypeFromBase(slot->npc)) {
                        return extracted;
                    }
                }
            }

            return std::nullopt;
        }

        void AssignTwoDonorTargetRace(
            RE::TESRace* requestedRace,
            const Engine::ExtractedEngineDonor& donorA,
            const Engine::ExtractedEngineDonor& donorB,
            Foundation::Sex targetSex,
            Foundation::Seed64 seed,
            const Engine::SynthesisSettings& settings,
            RE::TESRace*& outTargetRace,
            Core::InheritedParentSlot& outForcedRaceParent) {
            outForcedRaceParent = Core::ChooseOffspringRaceParent(seed);
            outTargetRace = nullptr;

            auto matchParentFromUnwrapped = [&](RE::TESRace* unwrapped) {
                const Foundation::FormRawId unwrappedId{ unwrapped->GetFormID() };
                if (unwrappedId == donorA.phenotype.raceId &&
                    unwrappedId != donorB.phenotype.raceId) {
                    outForcedRaceParent = Core::InheritedParentSlot::ParentA;
                } else if (unwrappedId == donorB.phenotype.raceId &&
                           unwrappedId != donorA.phenotype.raceId) {
                    outForcedRaceParent = Core::InheritedParentSlot::ParentB;
                } else {
                    const auto overrideGroup = Engine::ResolveRaceGroup(unwrapped);
                    if (overrideGroup == donorA.phenotype.raceGroup &&
                        overrideGroup != donorB.phenotype.raceGroup) {
                        outForcedRaceParent = Core::InheritedParentSlot::ParentA;
                    } else if (overrideGroup == donorB.phenotype.raceGroup &&
                               overrideGroup != donorA.phenotype.raceGroup) {
                        outForcedRaceParent = Core::InheritedParentSlot::ParentB;
                    }
                }
            };

            if (requestedRace) {
                if (auto* unwrapped = UnwrapMortalSynthesisRace(requestedRace, targetSex)) {
                    outTargetRace = unwrapped;
                    matchParentFromUnwrapped(unwrapped);
                    return;
                }
            }

            const auto chimeraPlan = Core::ResolveChimeraPlan(
                donorA.phenotype.raceGroup,
                donorB.phenotype.raceGroup,
                settings.beastChimeraTier,
                Engine::QueryActiveChimeraTierAvailability(settings, targetSex),
                seed);
            if (chimeraPlan.lineage != Core::ChimeraLineageKind::None) {
                outForcedRaceParent = chimeraPlan.chosenRaceParent;
            }

            RE::TESRace* chosenRaw =
                (outForcedRaceParent == Core::InheritedParentSlot::ParentA)
                    ? donorA.race
                    : donorB.race;
            outTargetRace = UnwrapMortalSynthesisRace(chosenRaw, targetSex);
            if (!outTargetRace) {
                outForcedRaceParent =
                    (outForcedRaceParent == Core::InheritedParentSlot::ParentA)
                        ? Core::InheritedParentSlot::ParentB
                        : Core::InheritedParentSlot::ParentA;
                RE::TESRace* altRaw =
                    (outForcedRaceParent == Core::InheritedParentSlot::ParentA)
                        ? donorA.race
                        : donorB.race;
                outTargetRace = UnwrapMortalSynthesisRace(altRaw, targetSex);
            }
            if (!outTargetRace) {
                outTargetRace = ResolveTargetRace(nullptr, targetSex);
            }
        }

        void StripAgeTintLayers(std::vector<State::TintLayerSpec>& tintLayers) {
            std::erase_if(tintLayers, [](const State::TintLayerSpec& l) {
                return l.semanticType == State::TintSemanticType::LaughLines ||
                       l.semanticType == State::TintSemanticType::EyeSocketLower ||
                       l.semanticType == State::TintSemanticType::Forehead;
            });
        }

        [[nodiscard]] std::optional<Foundation::RgbColor> ResolveSpecEarHairTint(
            const State::DynamicNpcSpec& spec) noexcept {
            return Core::ResolveChimeraEarHairTint(
                spec.hybridSkinId,
                spec.skinColor,
                spec.beastTailKind == State::BeastTailKind::Khajiit,
                spec.tailColor);
        }

        [[nodiscard]] std::shared_ptr<const std::vector<float>> ResolveSpecEarMorphDeltas(
            const State::DynamicNpcSpec& spec) {
            if (spec.secondaryRaceRef.IsEmpty()) {
                return nullptr;
            }
            auto* primaryRace = Engine::ResolvePluginForm<RE::TESRace>(spec.raceRef);
            auto* secondaryRace = Engine::ResolvePluginForm<RE::TESRace>(spec.secondaryRaceRef);
            if (!primaryRace || !secondaryRace || primaryRace == secondaryRace) {
                return nullptr;
            }
            Foundation::TopologyPool activeTopo =
                (spec.topology == Foundation::TopologyPool::HighPolyHead)
                    ? Foundation::TopologyPool::HighPolyHead
                    : Foundation::TopologyPool::Vanilla;
            if (activeTopo == Foundation::TopologyPool::HighPolyHead &&
                Engine::HeadPartPoolIndexer::GetSingleton().SelectPreferredTopology(
                    primaryRace, spec.sex) != Foundation::TopologyPool::HighPolyHead) {
                activeTopo = Foundation::TopologyPool::Vanilla;
            }
            auto deltas = Engine::TriProjectorCache::GetSingleton().ComputeSpecCrossRaceEarDeltas(
                primaryRace, secondaryRace, spec.sex, activeTopo, spec.seed);
            if (deltas.empty()) {
                return nullptr;
            }
            return std::make_shared<const std::vector<float>>(std::move(deltas));
        }

    }  // namespace

    SynthesisCoordinator& SynthesisCoordinator::GetSingleton() noexcept {
        static SynthesisCoordinator instance;
        return instance;
    }

    void SynthesisCoordinator::OnDataLoaded() noexcept {
        (void)ReloadSettingsFromIni();
        auto& dpf = Engine::DpfClient::GetSingleton();
        dpf.InitializeAtDataLoaded();
        Engine::Mortal3dSyncService::GetSingleton().RegisterEventSink();
        Engine::HeadPartPoolIndexer::GetSingleton().BuildIndex(dpf.IsSmpLoaded());
        Engine::ChimeraAccessoryCatalog::GetSingleton().InitializeAtDataLoaded();
        Engine::MortalHeritageKeywordService::GetSingleton().InitializeAtDataLoaded();
        Engine::HybridSkinService::GetSingleton().InitializeAtDataLoaded(GetSettings());
        Engine::FaceAnchorCatalog::GetSingleton().LoadCatalog();

        const std::filesystem::path registryPath(State::kDefaultRegistryPath);
        auto loadRes = State::LoadRegistrySafe(registryPath);
        if (!loadRes.ok) {
            registryCorrupt_.store(true, std::memory_order_release);
            MORWHA_LOG_ERROR(
                "SynthesisCoordinator: Failed to load '{}' ({}); writes disabled to protect registry",
                registryPath.string(),
                loadRes.error);
        } else {
            registryCorrupt_.store(false, std::memory_order_release);
            if (loadRes.recoveredFromBackup) {
                MORWHA_LOG_WARN(
                    "SynthesisCoordinator: Recovered {} NPC spec(s) from backup ({}); repairing primary '{}'",
                    loadRes.specs.size(),
                    loadRes.error,
                    registryPath.string());
                std::string repairErr;
                if (!State::SaveRegistryAtomic(registryPath, loadRes.specs, &repairErr)) {
                    MORWHA_LOG_ERROR(
                        "SynthesisCoordinator: Failed to repair primary registry '{}': {}",
                        registryPath.string(),
                        repairErr);
                }
            }
        }

        {
            std::lock_guard lock(registryMutex_);
            specs_ = std::move(loadRes.specs);
            hydratedNpcToSlotKey_.clear();
        }

        if (!registryCorrupt_.load(std::memory_order_acquire)) {
            (void)RunGarbageCollection(/*includeCrossSaveScan=*/true);
        }

        std::vector<State::DynamicNpcSpec> specsToHydrate;
        {
            std::lock_guard lock(registryMutex_);
            specsToHydrate = specs_;
        }

        if (dpf.IsDpfAvailable() && !registryCorrupt_.load(std::memory_order_acquire)) {
            std::size_t hydratedCount = 0;
            for (const auto& spec : specsToHydrate) {
                const auto slot = dpf.AcquireOrRecoverNpcSlot(spec.slotKey);
                if (!slot || !slot->npc) {
                    MORWHA_LOG_ERROR(
                        "SynthesisCoordinator: Could not recover DPF slot for '{}'",
                        spec.slotKey);
                    continue;
                }
                if (!Engine::HydrateTesNpc(slot->npc, spec, /*isMidSession=*/false)) {
                    MORWHA_LOG_ERROR(
                        "SynthesisCoordinator: Failed to hydrate recovered TESNPC {:08X} ('{}')",
                        slot->npc->GetFormID(),
                        spec.slotKey);
                    continue;
                }
                {
                    std::lock_guard lock(registryMutex_);
                    hydratedNpcToSlotKey_[slot->npc->GetFormID()] = spec.slotKey;
                }
                Engine::Mortal3dSyncService::GetSingleton().RegisterHydratedNpc(
                    slot->npc->GetFormID(),
                    spec.slotKey,
                    spec.hairColor,
                    Foundation::RgbColor{
                        slot->npc->bodyTintColor.red,
                        slot->npc->bodyTintColor.green,
                        slot->npc->bodyTintColor.blue
                    },
                    spec.tailColor,
                    spec.hornColor,
                    ResolveSpecEarHairTint(spec),
                    ResolveSpecEarMorphDeltas(spec));
                ++hydratedCount;
            }
            MORWHA_LOG_INFO(
                "SynthesisCoordinator: Recovered and hydrated {}/{} persisted dynamic NPC(s) at kDataLoaded",
                hydratedCount,
                specsToHydrate.size());
        }

        ready_.store(true, std::memory_order_release);
        MORWHA_LOG_INFO("SynthesisCoordinator: Ready (version={})", Engine::GetInstalledModVersion());
    }

    void SynthesisCoordinator::OnSessionRevert() noexcept {
        State::BumpSessionEpoch("SessionRevert");
        Engine::Mortal3dSyncService::GetSingleton().ClearPendingOnRevert();
        if (IsReady() && !registryCorrupt_.load(std::memory_order_acquire)) {
            (void)PurgeUncommittedSpecs();
        }
    }

    void SynthesisCoordinator::OnPostLoadGame() noexcept {
        const auto links = Serialization::ReconcileLinksOnPostLoad();
        auto& syncService = Engine::Mortal3dSyncService::GetSingleton();
        std::size_t queuedCount = 0;
        for (const auto& link : links) {
            auto* actor = RE::TESForm::LookupByID<RE::Actor>(link.actorRefId.raw());
            if (actor && !actor->IsDeleted() && !actor->IsDisabled()) {
                syncService.QueueActor3dSync(actor);
                ++queuedCount;
            }
        }
        if (!links.empty()) {
            MORWHA_LOG_INFO(
                "SynthesisCoordinator: PostLoad reconciled {} mortal link(s), queued {} 3D sync(s)",
                links.size(),
                queuedCount);
        }
    }

    bool SynthesisCoordinator::IsReady() const noexcept {
        return ready_.load(std::memory_order_acquire);
    }

    std::string SynthesisCoordinator::GetVersion() const {
        return std::string(Engine::GetInstalledModVersion());
    }

    std::size_t SynthesisCoordinator::GetRegisteredSpecCount() const {
        std::lock_guard lock(registryMutex_);
        return specs_.size();
    }

    Engine::SynthesisSettings SynthesisCoordinator::GetSettings() const {
        std::lock_guard lock(settingsMutex_);
        return settings_;
    }

    void SynthesisCoordinator::SetSettings(const Engine::SynthesisSettings& settings) {
        std::lock_guard lock(settingsMutex_);
        settings_ = settings;
    }

    bool SynthesisCoordinator::ReloadSettingsFromIni() {
        const auto iniRes = State::LoadOrCreateMorwhaIni(State::kDefaultMorwhaIniPath);
        if (!iniRes.ok) {
            MORWHA_LOG_ERROR(
                "SynthesisCoordinator: Failed to load/create '{}': {}",
                State::kDefaultMorwhaIniPath,
                iniRes.error);
            return false;
        }

        const Engine::SynthesisSettings parsed = Engine::SynthesisSettingsFromIni(iniRes.settings);
        {
            std::lock_guard lock(settingsMutex_);
            settings_ = parsed;
        }
        Engine::HybridSkinService::GetSingleton().UpdateSettings(parsed);

        MORWHA_LOG_INFO(
            "SynthesisCoordinator: {} '{}' (warnings={}, chimeraTier={}, hybridSkins={}({}), khajiit=[eyes={},ears={},teeth={},tails={},hyb={}], argonian=[eyes={},horns={},teeth={},tails={},hyb={}])",
            iniRes.createdDefault ? "Created default INI" : "Loaded INI",
            State::kDefaultMorwhaIniPath,
            iniRes.warningCount,
            static_cast<unsigned>(parsed.beastChimeraTier),
            parsed.enableGeneratedHybridSkins,
            State::GeneratedSkinResolutionToString(parsed.generatedSkinResolution),
            parsed.khajiitTier2.enableEyes,
            parsed.khajiitTier2.enableHeadFeature,
            parsed.khajiitTier2.enableTeeth,
            parsed.khajiitTier2.enableTails,
            parsed.khajiitTier2.enableHybridSkin,
            parsed.argonianTier2.enableEyes,
            parsed.argonianTier2.enableHeadFeature,
            parsed.argonianTier2.enableTeeth,
            parsed.argonianTier2.enableTails,
            parsed.argonianTier2.enableHybridSkin);
        return true;
    }

    Foundation::Seed64 SynthesisCoordinator::NextSeed() noexcept {
        std::lock_guard lock(registryMutex_);
        const auto nowTicks = static_cast<std::uint64_t>(
            std::chrono::steady_clock::now().time_since_epoch().count());
        std::uint64_t rawSeed = SplitMix64(nowTicks ^ (++seedCounter_));
        if (rawSeed == 0) {
            rawSeed = 0x123456789ABCDEFull;
        }
        return Foundation::Seed64{ rawSeed };
    }

    bool SynthesisCoordinator::ReserveSlotKeyAndSeed(
        std::string_view keyPrefix,
        Foundation::Seed64 explicitSeed,
        std::string& outSlotKey,
        Foundation::Seed64& outSeed) {
        {
            std::lock_guard lock(registryMutex_);
            auto keyExists = [&](std::string_view candidate) {
                return std::ranges::any_of(specs_, [&](const auto& existing) {
                    return existing.slotKey == candidate;
                });
            };
            do {
                outSlotKey = std::format("{}_{:04}", keyPrefix, nextSlotIndex_++);
            } while (keyExists(outSlotKey));
        }

        outSeed = explicitSeed.IsNull() ? NextSeed() : explicitSeed;
        return true;
    }

    MortalSpawnOutcome SynthesisCoordinator::PersistHydrateAndSpawn(
        const State::DynamicNpcSpec& spec,
        RE::TESRace* resolvedRace,
        RE::Actor* anchor,
        Foundation::FormRawId donorARefId,
        Foundation::FormRawId donorBRefId) {
        MortalSpawnOutcome outcome{};
        State::DynamicNpcSpec stagedSpec = spec;
        stagedSpec.committed = false;

        std::string valErr;
        if (!State::ValidateDynamicNpcSpec(stagedSpec, &valErr)) {
            MORWHA_LOG_ERROR(
                "PersistHydrateAndSpawn: Spec '{}' failed validation: {}",
                stagedSpec.slotKey,
                valErr);
            return outcome;
        }

        auto& dpf = Engine::DpfClient::GetSingleton();
        const auto slot = dpf.AcquireOrRecoverNpcSlot(stagedSpec.slotKey);
        if (!slot || !slot->npc) {
            MORWHA_LOG_ERROR("PersistHydrateAndSpawn: Failed to reserve DPF slot for '{}'", stagedSpec.slotKey);
            return outcome;
        }

        {
            std::lock_guard lock(registryMutex_);
            const bool duplicate = std::ranges::any_of(specs_, [&](const auto& existing) {
                return existing.slotKey == stagedSpec.slotKey;
            });
            if (duplicate) {
                MORWHA_LOG_ERROR("PersistHydrateAndSpawn: SlotKey collision for '{}'", stagedSpec.slotKey);
                return outcome;
            }

            specs_.push_back(stagedSpec);
            std::string saveErr;
            if (!State::SaveRegistryAtomic(State::kDefaultRegistryPath, specs_, &saveErr)) {
                specs_.pop_back();
                (void)dpf.ReleaseNpcSlot(stagedSpec.slotKey);
                MORWHA_LOG_ERROR(
                    "PersistHydrateAndSpawn: Failed to persist '{}' atomically: {}",
                    stagedSpec.slotKey,
                    saveErr);
                return outcome;
            }
        }

        auto rollbackStagedSpec = [&]() {
            {
                std::lock_guard lock(registryMutex_);
                std::erase_if(specs_, [&](const auto& existing) {
                    return existing.slotKey == stagedSpec.slotKey;
                });
                std::string saveErr;
                (void)State::SaveRegistryAtomic(State::kDefaultRegistryPath, specs_, &saveErr);
                (void)dpf.ReleaseNpcSlot(stagedSpec.slotKey);
            }
            Engine::Mortal3dSyncService::GetSingleton().UnregisterHydratedNpcsBySlotKeys(
                { stagedSpec.slotKey });
        };

        if (!Engine::HydrateTesNpc(slot->npc, stagedSpec, /*isMidSession=*/true)) {
            MORWHA_LOG_ERROR(
                "PersistHydrateAndSpawn: Failed to hydrate new TESNPC {:08X} ('{}')",
                slot->npc->GetFormID(),
                stagedSpec.slotKey);
            rollbackStagedSpec();
            return outcome;
        }
        {
            std::lock_guard lock(registryMutex_);
            hydratedNpcToSlotKey_[slot->npc->GetFormID()] = stagedSpec.slotKey;
        }

        Engine::Mortal3dSyncService::GetSingleton().RegisterHydratedNpc(
            slot->npc->GetFormID(),
            stagedSpec.slotKey,
            stagedSpec.hairColor,
            Foundation::RgbColor{
                slot->npc->bodyTintColor.red,
                slot->npc->bodyTintColor.green,
                slot->npc->bodyTintColor.blue
            },
            stagedSpec.tailColor,
            stagedSpec.hornColor,
            ResolveSpecEarHairTint(stagedSpec),
            ResolveSpecEarMorphDeltas(stagedSpec));

        RE::Actor* spawned = Engine::SpawnHydratedMortalAtAnchor(
            slot->npc, *slot, stagedSpec, anchor, stagedSpec.forcePersist, donorARefId, donorBRefId);
        if (!spawned) {
            rollbackStagedSpec();
            return outcome;
        }

        Engine::SyncLiveActorFromSpec(spawned, slot->npc, stagedSpec, /*refreshOutfit=*/false);

        if (auto* console = RE::ConsoleLog::GetSingleton()) {
            console->Print(
                "MORWHA: Spawned '%s' (Actor %08X, Base %08X, key '%s')",
                stagedSpec.displayName.c_str(),
                spawned->GetFormID(),
                slot->npc->GetFormID(),
                stagedSpec.slotKey.c_str());
        }

        outcome.actor = spawned;
        outcome.baseNpc = slot->npc;
        outcome.resolvedRace = resolvedRace;
        outcome.resolvedSex = stagedSpec.sex;
        outcome.resolvedAgeBracket = stagedSpec.ageBracket;
        outcome.slotKey = stagedSpec.slotKey;
        return outcome;
    }

    MortalSpawnOutcome SynthesisCoordinator::SpawnMortal(const MortalSpawnRequest& request) {
        MortalSpawnOutcome empty{};
        if (!ValidateSpawnPreconditions(
                "SpawnMortal",
                IsReady(),
                registryCorrupt_.load(std::memory_order_acquire),
                request.anchor)) {
            return empty;
        }

        const auto settings = GetSettings();
        std::optional<State::DynamicNpcSpec> specOpt;
        RE::TESRace* targetRace = nullptr;
        Foundation::FormRawId donorARefId{};
        Foundation::FormRawId donorBRefId{};

        if (request.donorMode == Foundation::DonorMode::ZeroDonorProcedural) {
            const std::string prefix = SanitizeSlotKeyPrefix(request.slotKeyPrefix, "test_mortal");
            std::string slotKey;
            Foundation::Seed64 seed{};
            (void)ReserveSlotKeyAndSeed(prefix, request.seed, slotKey, seed);

            const Foundation::Sex targetSex = ResolveTargetSex(request.sexInt, seed, /*allowRandom=*/true);
            targetRace = ResolveTargetRace(
                request.raceOverride ? request.raceOverride : request.anchor->GetRace(), targetSex);
            if (!targetRace) {
                MORWHA_LOG_ERROR("SpawnMortal(ZeroDonor): Failed to resolve any playable FaceGen race");
                return empty;
            }

            const std::string resolvedName =
                request.displayName.empty() ? "Test Mortal" : request.displayName;
            specOpt = Engine::BuildZeroDonorSpec(
                slotKey, resolvedName, targetRace, targetSex, seed, settings, request.ageBracket);
        } else if (request.donorMode == Foundation::DonorMode::OneDonorRelative) {
            if (!request.donorA) {
                MORWHA_LOG_ERROR("SpawnMortal(OneDonor): donorA is null");
                return empty;
            }
            const auto donorOpt = ExtractDonorFromForm(request.donorA);
            if (!donorOpt) {
                MORWHA_LOG_ERROR(
                    "SpawnMortal(OneDonor): Failed to extract phenotype from donor {:08X}",
                    request.donorA->GetFormID());
                return empty;
            }

            const std::string prefix = SanitizeSlotKeyPrefix(request.slotKeyPrefix, "relative_mortal");
            std::string slotKey;
            Foundation::Seed64 seed{};
            (void)ReserveSlotKeyAndSeed(prefix, request.seed, slotKey, seed);

            const Foundation::Sex targetSex = ResolveTargetSex(request.sexInt, seed, /*allowRandom=*/true);
            targetRace = request.raceOverride
                             ? UnwrapMortalSynthesisRace(request.raceOverride, targetSex)
                             : nullptr;
            if (!targetRace) {
                targetRace = UnwrapMortalSynthesisRace(donorOpt->race, targetSex);
            }
            if (!targetRace) {
                targetRace = ResolveTargetRace(nullptr, targetSex);
            }
            if (!targetRace) {
                MORWHA_LOG_ERROR("SpawnMortal(OneDonor): Failed to resolve any playable FaceGen race");
                return empty;
            }

            const std::string resolvedName =
                request.displayName.empty() ? "Relative Mortal" : request.displayName;
            specOpt = Engine::BuildOneDonorRelativeSpec(
                slotKey,
                resolvedName,
                *donorOpt,
                targetRace,
                targetSex,
                seed,
                settings,
                request.ageBracket);
            if (specOpt) {
                PopulateDonorProvenanceFromForm(
                    request.donorA, *donorOpt, specOpt->donorARef, specOpt->donorASlotKey, donorARefId);
                MORWHA_LOG_INFO(
                    "SpawnRelativeMortal: '{}' donor={:08X} -> targetRace={:08X}, sex={}, age={}, height={:.3f}, donorH={:.3f}, skin=({},{},{}), hair=({},{},{}), live3dHair={}, donorTier={}",
                    specOpt->slotKey,
                    request.donorA->GetFormID(),
                    targetRace->GetFormID(),
                    static_cast<unsigned>(targetSex),
                    static_cast<unsigned>(specOpt->ageBracket),
                    specOpt->height,
                    donorOpt->phenotype.height,
                    specOpt->skinColor.r,
                    specOpt->skinColor.g,
                    specOpt->skinColor.b,
                    specOpt->hairColor.r,
                    specOpt->hairColor.g,
                    specOpt->hairColor.b,
                    donorOpt->extractedLive3dHairShader,
                    static_cast<unsigned>(donorOpt->extractionTier));
            }
        } else {
            if (!request.donorA || !request.donorB || request.donorA == request.donorB) {
                MORWHA_LOG_ERROR("SpawnMortal(TwoDonor): Requires two distinct non-null parent forms");
                return empty;
            }
            const auto donorAOpt = ExtractDonorFromForm(request.donorA);
            const auto donorBOpt = ExtractDonorFromForm(request.donorB);
            if (!donorAOpt || !donorBOpt) {
                MORWHA_LOG_ERROR(
                    "SpawnMortal(TwoDonor): Failed to extract donor phenotypes (donorA={}, donorB={})",
                    donorAOpt.has_value(),
                    donorBOpt.has_value());
                return empty;
            }

            const std::string prefix = SanitizeSlotKeyPrefix(request.slotKeyPrefix, "offspring_mortal");
            std::string slotKey;
            Foundation::Seed64 seed{};
            (void)ReserveSlotKeyAndSeed(prefix, request.seed, slotKey, seed);

            const Foundation::Sex targetSex = ResolveTargetSex(request.sexInt, seed, /*allowRandom=*/true);
            Core::InheritedParentSlot forcedRaceParent{};
            AssignTwoDonorTargetRace(
                request.raceOverride,
                *donorAOpt,
                *donorBOpt,
                targetSex,
                seed,
                settings,
                targetRace,
                forcedRaceParent);
            if (!targetRace) {
                MORWHA_LOG_ERROR("SpawnMortal(TwoDonor): Failed to resolve any playable FaceGen race");
                return empty;
            }

            const std::string resolvedName =
                request.displayName.empty() ? "Offspring Mortal" : request.displayName;
            specOpt = Engine::BuildTwoDonorOffspringSpec(
                slotKey,
                resolvedName,
                *donorAOpt,
                *donorBOpt,
                targetRace,
                forcedRaceParent,
                targetSex,
                seed,
                settings,
                request.ageBracket);
            if (specOpt) {
                PopulateDonorProvenanceFromForm(
                    request.donorA, *donorAOpt, specOpt->donorARef, specOpt->donorASlotKey, donorARefId);
                PopulateDonorProvenanceFromForm(
                    request.donorB, *donorBOpt, specOpt->donorBRef, specOpt->donorBSlotKey, donorBRefId);
                MORWHA_LOG_INFO(
                    "SpawnOffspringMortal: '{}' parents=({:08X},{:08X}) -> targetRace={:08X}, sex={}, age={}, height={:.3f}, donorH=({:.3f}/{:.3f}), skin=({},{},{}), hair=({},{},{}), live3dHair=({}/{}), donorTiers=({}/{})",
                    specOpt->slotKey,
                    request.donorA->GetFormID(),
                    request.donorB->GetFormID(),
                    targetRace->GetFormID(),
                    static_cast<unsigned>(targetSex),
                    static_cast<unsigned>(specOpt->ageBracket),
                    specOpt->height,
                    donorAOpt->phenotype.height,
                    donorBOpt->phenotype.height,
                    specOpt->skinColor.r,
                    specOpt->skinColor.g,
                    specOpt->skinColor.b,
                    specOpt->hairColor.r,
                    specOpt->hairColor.g,
                    specOpt->hairColor.b,
                    donorAOpt->extractedLive3dHairShader,
                    donorBOpt->extractedLive3dHairShader,
                    static_cast<unsigned>(donorAOpt->extractionTier),
                    static_cast<unsigned>(donorBOpt->extractionTier));
            }
        }

        if (!specOpt) {
            MORWHA_LOG_ERROR("SpawnMortal: Spec synthesis failed");
            return empty;
        }

        specOpt->forcePersist = request.forcePersist;
        ApplyMortalOverridesToSpec(*specOpt, request.archetype, request.ai, request.stats);

        return PersistHydrateAndSpawn(*specOpt, targetRace, request.anchor, donorARefId, donorBRefId);
    }

    bool SynthesisCoordinator::UpdateMortal(
        RE::Actor* targetActor,
        const MortalUpdateRequest& request) {
        if (!ValidateSpawnPreconditions(
                "UpdateMortal",
                IsReady(),
                registryCorrupt_.load(std::memory_order_acquire),
                targetActor)) {
            return false;
        }

        const auto linkOpt = Serialization::FindActiveLinkByActor(
            Foundation::FormRawId{ targetActor->GetFormID() });
        if (!linkOpt || linkOpt->slotKey.empty()) {
            MORWHA_LOG_ERROR(
                "UpdateMortal: Actor {:08X} is not an active MORWHA spawned mortal in this save",
                targetActor->GetFormID());
            return false;
        }

        State::DynamicNpcSpec updatedSpec{};
        {
            std::lock_guard lock(registryMutex_);
            const auto it = std::ranges::find_if(specs_, [&](const auto& existing) {
                return existing.slotKey == linkOpt->slotKey;
            });
            if (it == specs_.end()) {
                MORWHA_LOG_ERROR(
                    "UpdateMortal: SlotKey '{}' for actor {:08X} not found in DynamicNPCRegistry",
                    linkOpt->slotKey,
                    targetActor->GetFormID());
                return false;
            }
            updatedSpec = *it;
        }

        const bool nameChanged =
            request.displayName.has_value() &&
            !request.displayName->empty() &&
            *request.displayName != updatedSpec.displayName;
        if (nameChanged) {
            updatedSpec.displayName = *request.displayName;
        }
        if (request.forcePersist) {
            updatedSpec.forcePersist = *request.forcePersist;
        }

        bool visualHeadDirty = false;
        if (request.ageBracket.has_value() &&
            *request.ageBracket != Foundation::AgeBracket::Auto) {
            RE::TESRace* resolvedRace = Engine::ResolvePluginForm<RE::TESRace>(updatedSpec.raceRef);
            if (!resolvedRace) {
                resolvedRace = targetActor->GetRace();
            }
            const auto* faceRelated =
                resolvedRace ? Engine::ResolveRaceFaceRelatedData(resolvedRace, updatedSpec.sex) : nullptr;
            StripAgeTintLayers(updatedSpec.tintLayers);
            Engine::ApplyAgeProfileToSpec(updatedSpec, resolvedRace, faceRelated, *request.ageBracket);
            visualHeadDirty = true;
        }

        if (request.archetype.faceTextureSet.specified) {
            visualHeadDirty = true;
        }
        ApplyMortalOverridesToSpec(updatedSpec, request.archetype, request.ai, request.stats);

        std::string valErr;
        if (!State::ValidateDynamicNpcSpec(updatedSpec, &valErr)) {
            MORWHA_LOG_ERROR(
                "UpdateMortal: Updated spec '{}' failed validation: {}",
                updatedSpec.slotKey,
                valErr);
            return false;
        }

        const auto slot = Engine::DpfClient::GetSingleton().AcquireOrRecoverNpcSlot(updatedSpec.slotKey);
        if (!slot || !slot->npc) {
            MORWHA_LOG_ERROR("UpdateMortal: Failed to acquire DPF slot for '{}'", updatedSpec.slotKey);
            return false;
        }

        {
            std::lock_guard lock(registryMutex_);
            const auto it = std::ranges::find_if(specs_, [&](const auto& existing) {
                return existing.slotKey == updatedSpec.slotKey;
            });
            if (it == specs_.end()) {
                MORWHA_LOG_ERROR("UpdateMortal: SlotKey '{}' vanished before save", updatedSpec.slotKey);
                return false;
            }
            const State::DynamicNpcSpec previous = *it;
            *it = updatedSpec;
            std::string saveErr;
            if (!State::SaveRegistryAtomic(State::kDefaultRegistryPath, specs_, &saveErr)) {
                *it = previous;
                MORWHA_LOG_ERROR("UpdateMortal: Failed to save '{}' atomically: {}", updatedSpec.slotKey, saveErr);
                return false;
            }
        }

        if (!Engine::HydrateTesNpc(slot->npc, updatedSpec, /*isMidSession=*/true)) {
            MORWHA_LOG_ERROR(
                "UpdateMortal: Failed to re-hydrate TESNPC {:08X} ('{}')",
                slot->npc->GetFormID(),
                updatedSpec.slotKey);
            return false;
        }
        {
            std::lock_guard lock(registryMutex_);
            hydratedNpcToSlotKey_[slot->npc->GetFormID()] = updatedSpec.slotKey;
        }

        Engine::Mortal3dSyncService::GetSingleton().RegisterHydratedNpc(
            slot->npc->GetFormID(),
            updatedSpec.slotKey,
            updatedSpec.hairColor,
            Foundation::RgbColor{
                slot->npc->bodyTintColor.red,
                slot->npc->bodyTintColor.green,
                slot->npc->bodyTintColor.blue
            },
            updatedSpec.tailColor,
            updatedSpec.hornColor,
            ResolveSpecEarHairTint(updatedSpec),
            ResolveSpecEarMorphDeltas(updatedSpec));

        if (nameChanged) {
            targetActor->SetDisplayName(updatedSpec.displayName.c_str(), true);
        }

        Engine::SyncLiveActorFromSpec(targetActor, slot->npc, updatedSpec, request.refreshOutfit);
        if (visualHeadDirty) {
            Engine::RefreshHydratedMortal3D(targetActor, nullptr);
        }

        MORWHA_LOG_INFO(
            "UpdateMortal: Updated '{}' actor={:08X} base={:08X} age={} refreshOutfit={} visualDirty={}",
            updatedSpec.slotKey,
            targetActor->GetFormID(),
            slot->npc->GetFormID(),
            static_cast<unsigned>(updatedSpec.ageBracket),
            request.refreshOutfit,
            visualHeadDirty);
        return true;
    }

    RE::Actor* SynthesisCoordinator::SpawnMortalJson(
        RE::Actor* anchor,
        std::string_view optionsJson) {
        State::JsonSpawnRequest parsed{};
        std::string err;
        if (!State::ParseJsonSpawnRequest(optionsJson, parsed, &err)) {
            MORWHA_LOG_ERROR("SpawnMortalJson: Invalid JSON options: {}", err);
            return nullptr;
        }
        MortalSpawnRequest req{};
        if (!BuildMortalSpawnRequestFromJson(anchor, parsed, req, &err)) {
            MORWHA_LOG_ERROR("SpawnMortalJson: Failed resolving forms: {}", err);
            return nullptr;
        }
        return SpawnMortal(req).actor;
    }

    bool SynthesisCoordinator::UpdateMortalJson(
        RE::Actor* targetActor,
        std::string_view patchJson) {
        State::JsonUpdateRequest parsed{};
        std::string err;
        if (!State::ParseJsonUpdateRequest(patchJson, parsed, &err)) {
            MORWHA_LOG_ERROR("UpdateMortalJson: Invalid JSON patch: {}", err);
            return false;
        }
        MortalUpdateRequest req{};
        if (!BuildMortalUpdateRequestFromJson(parsed, req, &err)) {
            MORWHA_LOG_ERROR("UpdateMortalJson: Failed resolving forms: {}", err);
            return false;
        }
        return UpdateMortal(targetActor, req);
    }

    bool SynthesisCoordinator::RerollMortal(
        RE::Actor* targetActor,
        const MortalRerollRequest& request) {
        if (!ValidateSpawnPreconditions(
                "RerollMortal",
                IsReady(),
                registryCorrupt_.load(std::memory_order_acquire),
                targetActor)) {
            return false;
        }

        const auto linkOpt = Serialization::FindActiveLinkByActor(
            Foundation::FormRawId{ targetActor->GetFormID() });
        if (!linkOpt || linkOpt->slotKey.empty()) {
            MORWHA_LOG_ERROR(
                "RerollMortal: Actor {:08X} is not an active MORWHA spawned mortal in this save",
                targetActor->GetFormID());
            return false;
        }

        State::DynamicNpcSpec existingSpec{};
        {
            std::lock_guard lock(registryMutex_);
            const auto it = std::ranges::find_if(specs_, [&](const auto& existing) {
                return existing.slotKey == linkOpt->slotKey;
            });
            if (it == specs_.end()) {
                MORWHA_LOG_ERROR(
                    "RerollMortal: SlotKey '{}' for actor {:08X} not found in DynamicNPCRegistry",
                    linkOpt->slotKey,
                    targetActor->GetFormID());
                return false;
            }
            existingSpec = *it;
        }

        Foundation::Seed64 newSeed = NextSeed();
        if (newSeed == existingSpec.seed) {
            newSeed = NextSeed();
        }

        Foundation::Sex targetSex = existingSpec.sex;
        if (request.sexOverride == 0) {
            targetSex = Foundation::Sex::Male;
        } else if (request.sexOverride > 0) {
            targetSex = Foundation::Sex::Female;
        } else if (request.sexOverride <= -2) {
            targetSex = Core::ChooseRandomSex(newSeed);
        }

        const Foundation::AgeBracket targetAgeBracket =
            (request.ageBracketOverride != Foundation::AgeBracket::Auto)
                ? request.ageBracketOverride
                : existingSpec.ageBracket;

        RE::TESRace* lockedCurrentRace =
            Engine::ResolvePluginForm<RE::TESRace>(existingSpec.raceRef);
        if (!lockedCurrentRace) {
            lockedCurrentRace = targetActor->GetRace();
        }

        const std::string resolvedName =
            !request.nameOverride.empty()
                ? request.nameOverride
                : (!existingSpec.displayName.empty() ? existingSpec.displayName : "Morwha Mortal");

        Foundation::FormRawId resolvedDonorAId = linkOpt->donorARefId;
        Foundation::FormRawId resolvedDonorBId = linkOpt->donorBRefId;
        std::optional<State::DynamicNpcSpec> newSpecOpt;
        RE::TESRace* targetRace = nullptr;

        if (existingSpec.donorMode == Foundation::DonorMode::ZeroDonorProcedural) {
            RE::TESRace* requestedRace =
                request.raceOverride ? request.raceOverride : lockedCurrentRace;
            targetRace = ResolveTargetRace(requestedRace, targetSex);
            if (!targetRace) {
                MORWHA_LOG_ERROR("RerollMortal: Failed to resolve playable FaceGen race for '{}'", existingSpec.slotKey);
                return false;
            }
            newSpecOpt = Engine::BuildZeroDonorSpec(
                existingSpec.slotKey,
                resolvedName,
                targetRace,
                targetSex,
                newSeed,
                GetSettings(),
                targetAgeBracket);
        } else if (existingSpec.donorMode == Foundation::DonorMode::OneDonorRelative) {
            const auto donorAOpt = ResolveStoredDonor(
                linkOpt->donorARefId, existingSpec.donorARef, existingSpec.donorASlotKey, resolvedDonorAId);
            if (!donorAOpt) {
                MORWHA_LOG_ERROR(
                    "RerollMortal: Stored donorA for '{}' (actor {:08X}) could not be resolved",
                    existingSpec.slotKey,
                    targetActor->GetFormID());
                return false;
            }
            RE::TESRace* requestedRace = nullptr;
            if (request.raceOverride) {
                requestedRace = request.raceOverride;
            } else if (!request.rerollRaceFromDonors) {
                requestedRace = lockedCurrentRace;
            } else {
                requestedRace = donorAOpt->race;
            }
            targetRace = requestedRace ? UnwrapMortalSynthesisRace(requestedRace, targetSex) : nullptr;
            if (!targetRace) {
                targetRace = UnwrapMortalSynthesisRace(donorAOpt->race, targetSex);
            }
            if (!targetRace) {
                targetRace = ResolveTargetRace(nullptr, targetSex);
            }
            if (!targetRace) {
                MORWHA_LOG_ERROR("RerollMortal: Failed to resolve playable race for '{}'", existingSpec.slotKey);
                return false;
            }
            newSpecOpt = Engine::BuildOneDonorRelativeSpec(
                existingSpec.slotKey,
                resolvedName,
                *donorAOpt,
                targetRace,
                targetSex,
                newSeed,
                GetSettings(),
                targetAgeBracket);
        } else {
            const auto donorAOpt = ResolveStoredDonor(
                linkOpt->donorARefId, existingSpec.donorARef, existingSpec.donorASlotKey, resolvedDonorAId);
            const auto donorBOpt = ResolveStoredDonor(
                linkOpt->donorBRefId, existingSpec.donorBRef, existingSpec.donorBSlotKey, resolvedDonorBId);
            if (!donorAOpt || !donorBOpt) {
                MORWHA_LOG_ERROR(
                    "RerollMortal: Stored parents for '{}' (actor {:08X}, donorA={}, donorB={}) could not be resolved",
                    existingSpec.slotKey,
                    targetActor->GetFormID(),
                    donorAOpt.has_value(),
                    donorBOpt.has_value());
                return false;
            }
            const auto settings = GetSettings();
            RE::TESRace* requestedRace =
                request.raceOverride ? request.raceOverride : (request.rerollRaceFromDonors ? nullptr : lockedCurrentRace);
            Core::InheritedParentSlot forcedRaceParent{};
            AssignTwoDonorTargetRace(
                requestedRace, *donorAOpt, *donorBOpt, targetSex, newSeed, settings, targetRace, forcedRaceParent);
            if (!targetRace) {
                MORWHA_LOG_ERROR("RerollMortal: Failed to resolve playable race for '{}'", existingSpec.slotKey);
                return false;
            }
            newSpecOpt = Engine::BuildTwoDonorOffspringSpec(
                existingSpec.slotKey,
                resolvedName,
                *donorAOpt,
                *donorBOpt,
                targetRace,
                forcedRaceParent,
                targetSex,
                newSeed,
                settings,
                targetAgeBracket);
        }

        if (!newSpecOpt) {
            MORWHA_LOG_ERROR("RerollMortal: Spec synthesis failed for '{}'", existingSpec.slotKey);
            return false;
        }

        newSpecOpt->committed = existingSpec.committed;
        newSpecOpt->donorARef = existingSpec.donorARef;
        newSpecOpt->donorBRef = existingSpec.donorBRef;
        newSpecOpt->donorASlotKey = existingSpec.donorASlotKey;
        newSpecOpt->donorBSlotKey = existingSpec.donorBSlotKey;
        CopyPreservedArchetypeAndStats(existingSpec, *newSpecOpt);
        ApplyMortalOverridesToSpec(*newSpecOpt, request.archetype, request.ai, request.stats);

        std::string valErr;
        if (!State::ValidateDynamicNpcSpec(*newSpecOpt, &valErr)) {
            MORWHA_LOG_ERROR("RerollMortal: Spec '{}' failed validation: {}", existingSpec.slotKey, valErr);
            return false;
        }

        const auto slot = Engine::DpfClient::GetSingleton().AcquireOrRecoverNpcSlot(existingSpec.slotKey);
        if (!slot || !slot->npc) {
            MORWHA_LOG_ERROR("RerollMortal: Failed to acquire DPF slot for '{}'", existingSpec.slotKey);
            return false;
        }

        {
            std::lock_guard lock(registryMutex_);
            const auto it = std::ranges::find_if(specs_, [&](const auto& existing) {
                return existing.slotKey == existingSpec.slotKey;
            });
            if (it == specs_.end()) {
                MORWHA_LOG_ERROR("RerollMortal: SlotKey '{}' vanished before save", existingSpec.slotKey);
                return false;
            }
            const State::DynamicNpcSpec previous = *it;
            *it = *newSpecOpt;
            std::string saveErr;
            if (!State::SaveRegistryAtomic(State::kDefaultRegistryPath, specs_, &saveErr)) {
                *it = previous;
                MORWHA_LOG_ERROR("RerollMortal: Failed to save '{}' atomically: {}", existingSpec.slotKey, saveErr);
                return false;
            }
        }

        if (!Engine::HydrateTesNpc(slot->npc, *newSpecOpt, /*isMidSession=*/true)) {
            MORWHA_LOG_ERROR(
                "RerollMortal: Failed to re-hydrate TESNPC {:08X} ('{}')",
                slot->npc->GetFormID(),
                existingSpec.slotKey);
            return false;
        }
        {
            std::lock_guard lock(registryMutex_);
            hydratedNpcToSlotKey_[slot->npc->GetFormID()] = newSpecOpt->slotKey;
        }

        Engine::Mortal3dSyncService::GetSingleton().RegisterHydratedNpc(
            slot->npc->GetFormID(),
            newSpecOpt->slotKey,
            newSpecOpt->hairColor,
            Foundation::RgbColor{
                slot->npc->bodyTintColor.red,
                slot->npc->bodyTintColor.green,
                slot->npc->bodyTintColor.blue
            },
            newSpecOpt->tailColor,
            newSpecOpt->hornColor,
            ResolveSpecEarHairTint(*newSpecOpt),
            ResolveSpecEarMorphDeltas(*newSpecOpt));

        Serialization::RegisterSpawnedLink(State::SpawnedMortalLink{
            .actorRefId = linkOpt->actorRefId,
            .dpfLocalId = slot->localId,
            .slotKey = newSpecOpt->slotKey,
            .donorMode = newSpecOpt->donorMode,
            .seed = newSpecOpt->seed,
            .donorARefId = resolvedDonorAId,
            .donorBRefId = resolvedDonorBId
        });

        if (!request.nameOverride.empty()) {
            targetActor->SetDisplayName(newSpecOpt->displayName.c_str(), true);
        }

        Engine::SyncLiveActorFromSpec(targetActor, slot->npc, *newSpecOpt, /*refreshOutfit=*/false);

        RE::TESRace* raceToSwitch =
            (targetActor->GetRace() != targetRace) ? targetRace : nullptr;
        Engine::RefreshHydratedMortal3D(targetActor, raceToSwitch);

        MORWHA_LOG_INFO(
            "RerollMortal: Rerolled '{}' actor={:08X} base={:08X} newSeed=0x{:016X} donors=({:08X},{:08X}) race={:08X} sex={} age={}",
            newSpecOpt->slotKey,
            targetActor->GetFormID(),
            slot->npc->GetFormID(),
            newSpecOpt->seed.raw(),
            resolvedDonorAId.raw(),
            resolvedDonorBId.raw(),
            targetRace->GetFormID(),
            static_cast<unsigned>(targetSex),
            static_cast<unsigned>(newSpecOpt->ageBracket));

        if (auto* console = RE::ConsoleLog::GetSingleton()) {
            console->Print(
                "MORWHA: Rerolled '%s' (Actor %08X, key '%s', donors %08X/%08X, newSeed 0x%016llX)",
                newSpecOpt->displayName.c_str(),
                targetActor->GetFormID(),
                newSpecOpt->slotKey.c_str(),
                resolvedDonorAId.raw(),
                resolvedDonorBId.raw(),
                static_cast<unsigned long long>(newSpecOpt->seed.raw()));
        }

        return true;
    }

    std::optional<Engine::ResolvedEngineHeritage> SynthesisCoordinator::QueryMortalHeritage(
        RE::TESForm* targetActorOrBaseNpc) const {
        if (!IsReady() || !targetActorOrBaseNpc || targetActorOrBaseNpc->IsDeleted()) {
            return std::nullopt;
        }

        std::string slotKey;
        const RE::TESNPC* baseNpc = nullptr;
        const RE::TESRace* liveRace = nullptr;

        if (auto* actor = targetActorOrBaseNpc->As<RE::Actor>()) {
            liveRace = actor->GetRace();
            baseNpc = actor->GetActorBase();
            if (const auto linkOpt = Serialization::FindActiveLinkByActor(
                    Foundation::FormRawId{ actor->GetFormID() })) {
                slotKey = linkOpt->slotKey;
            }
        } else if (auto* npc = targetActorOrBaseNpc->As<RE::TESNPC>()) {
            baseNpc = npc;
            liveRace = npc->race;
        } else {
            return std::nullopt;
        }

        State::DynamicNpcSpec matchedSpec{};
        {
            std::lock_guard lock(registryMutex_);
            if (slotKey.empty() && baseNpc != nullptr) {
                if (const auto mapIt = hydratedNpcToSlotKey_.find(baseNpc->GetFormID());
                    mapIt != hydratedNpcToSlotKey_.end()) {
                    slotKey = mapIt->second;
                }
            }
            if (slotKey.empty()) {
                return std::nullopt;
            }
            const auto it = std::ranges::find_if(specs_, [&](const auto& existing) {
                return existing.slotKey == slotKey;
            });
            if (it == specs_.end()) {
                return std::nullopt;
            }
            matchedSpec = *it;
        }

        return Engine::MortalHeritageKeywordService::GetSingleton().ResolveSpecHeritage(
            matchedSpec, liveRace);
    }

    std::string SynthesisCoordinator::QueryMortalHeritageJson(
        RE::TESForm* targetActorOrBaseNpc) const {
        const auto resolvedOpt = QueryMortalHeritage(targetActorOrBaseNpc);
        if (!resolvedOpt) {
            return {};
        }
        const auto& s = resolvedOpt->summary;
        auto appendEscaped = [](std::string& dst, std::string_view text) {
            dst.push_back('"');
            for (const unsigned char ch : text) {
                if (ch == '"' || ch == '\\') {
                    dst.push_back('\\');
                }
                dst.push_back(static_cast<char>(ch));
            }
            dst.push_back('"');
        };

        std::string json;
        json.reserve(256u);
        json.append("{\"isMixedHeritage\":");
        json.append(s.isMixedHeritage ? "true" : "false");
        json.append(std::format(",\"chimeraTier\":{}", static_cast<unsigned>(s.chimeraTier)));
        json.append(",\"primaryRace\":");
        appendEscaped(json, s.primaryRace);
        json.append(",\"secondaryRace\":");
        appendEscaped(json, s.secondaryRace);
        json.append(",\"groupPair\":");
        appendEscaped(json, s.groupPair);
        json.append(",\"racePair\":");
        appendEscaped(json, s.racePair);
        json.append(",\"lineageLabel\":");
        appendEscaped(json, s.lineageLabel);
        json.append(",\"loreName\":");
        appendEscaped(json, s.loreName);
        json.append(",\"keywords\":[");
        for (std::size_t i = 0u; i < s.keywords.size(); ++i) {
            if (i > 0u) {
                json.push_back(',');
            }
            appendEscaped(json, s.keywords[i]);
        }
        json.append("]}");
        return json;
    }

}  // namespace MORWHA::App
