#include "morwha/engine/MortalSpawner.h"

#include "morwha/engine/ChimeraAccessoryCatalog.h"
#include "morwha/engine/EngineLogging.h"
#include "morwha/engine/Mortal3dSyncService.h"
#include "morwha/engine/SkseSerializationCallbacks.h"
#include "morwha/state/SessionEpoch.h"

namespace MORWHA::Engine {

    RE::Actor* SpawnHydratedMortalAtAnchor(
        RE::TESNPC* baseNpc,
        const DpfSlotResult& slot,
        const State::DynamicNpcSpec& spec,
        RE::TESObjectREFR* anchorRef,
        bool forcePersist,
        Foundation::FormRawId donorARefId,
        Foundation::FormRawId donorBRefId) {
        if (!baseNpc || !anchorRef) {
            MORWHA_LOG_ERROR("SpawnHydratedMortalAtAnchor: null baseNpc or anchorRef");
            return nullptr;
        }
        if (!baseNpc->IsInitialized()) {
            MORWHA_LOG_ERROR(
                "SpawnHydratedMortalAtAnchor: baseNpc {:08X} ('{}') is not initialized",
                baseNpc->GetFormID(),
                spec.slotKey);
            return nullptr;
        }
        if (baseNpc->faceNPC != nullptr || baseNpc->Respawns()) {
            MORWHA_LOG_ERROR(
                "SpawnHydratedMortalAtAnchor: baseNpc {:08X} ('{}') violated faceNPC==nullptr or !Respawns()",
                baseNpc->GetFormID(),
                spec.slotKey);
            return nullptr;
        }
        if (!baseNpc->attackDataMap) {
            MORWHA_LOG_ERROR(
                "SpawnHydratedMortalAtAnchor: baseNpc {:08X} ('{}') has null attackDataMap",
                baseNpc->GetFormID(),
                spec.slotKey);
            return nullptr;
        }

        auto spawnedRef = anchorRef->PlaceObjectAtMe(baseNpc, forcePersist);
        auto* actor = spawnedRef ? spawnedRef->As<RE::Actor>() : nullptr;
        if (!actor) {
            MORWHA_LOG_ERROR(
                "SpawnHydratedMortalAtAnchor: PlaceObjectAtMe failed for base {:08X} ('{}') at anchor {:08X}",
                baseNpc->GetFormID(),
                spec.slotKey,
                anchorRef->GetFormID());
            return nullptr;
        }

        Serialization::RegisterSpawnedLink(State::SpawnedMortalLink{
            .actorRefId = Foundation::FormRawId{ actor->GetFormID() },
            .dpfLocalId = slot.localId,
            .slotKey = spec.slotKey,
            .donorMode = spec.donorMode,
            .seed = spec.seed,
            .donorARefId = donorARefId,
            .donorBRefId = donorBRefId
        });

        Mortal3dSyncService::GetSingleton().QueueActor3dSync(actor);

        MORWHA_LOG_INFO(
            "SpawnHydratedMortalAtAnchor: Spawned actor {:08X} from base {:08X} ('{}', localId={:06X})",
            actor->GetFormID(),
            baseNpc->GetFormID(),
            spec.slotKey,
            slot.localId);

        return actor;
    }

    void RefreshHydratedMortal3D(
        RE::Actor* actor,
        RE::TESRace* targetRace) {
        if (!actor) {
            return;
        }

        const RE::ActorHandle actorHandle = actor->GetHandle();
        const RE::FormID targetRaceFormId = targetRace ? targetRace->GetFormID() : 0u;
        const State::SessionEpoch epoch = State::GetSessionEpoch();

        if (const auto* tasks = SKSE::GetTaskInterface()) {
            tasks->AddTask([actorHandle, targetRaceFormId, epoch]() {
                if (!State::IsSessionEpochCurrent(epoch)) {
                    return;
                }
                const auto actorPtr = actorHandle.get();
                if (!actorPtr || actorPtr->IsDeleted() || actorPtr->IsDisabled()) {
                    return;
                }
                if (targetRaceFormId != 0u) {
                    if (auto* newRace = RE::TESForm::LookupByID<RE::TESRace>(targetRaceFormId);
                        newRace && actorPtr->GetRace() != newRace) {
                        actorPtr->SwitchRace(newRace, false);
                    }
                }
                (void)ChimeraAccessoryCatalog::GetSingleton().SyncActorChimeraSkin(actorPtr.get());
                actorPtr->DoReset3D(true);
                Mortal3dSyncService::GetSingleton().QueueActor3dSync(actorPtr.get());
            });
        }
    }

}  // namespace MORWHA::Engine
