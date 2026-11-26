#pragma once

#include "morwha/engine/DpfClient.h"
#include "morwha/foundation/DomainTypes.h"
#include "morwha/state/RegistryTypes.h"

namespace RE {
    class Actor;
    class TESNPC;
    class TESObjectREFR;
    class TESRace;
}

namespace MORWHA::Engine {

    /// Spawns a hydrated DPF-backed RE::TESNPC at anchorRef via PlaceObjectAtMe (never PlaceActorAtMe),
    /// records the 'MRWH' co-save link, and queues main-thread 3D FaceGen synchronization.
    [[nodiscard]] RE::Actor* SpawnHydratedMortalAtAnchor(
        RE::TESNPC* baseNpc,
        const DpfSlotResult& slot,
        const State::DynamicNpcSpec& spec,
        RE::TESObjectREFR* anchorRef,
        bool forcePersist = true,
        Foundation::FormRawId donorARefId = {},
        Foundation::FormRawId donorBRefId = {});

    /// Refreshes an already-spawned MORWHA actor's live 3D after its underlying TESNPC has been
    /// re-hydrated in-place (optional SwitchRace + DoReset3D + Mortal3dSyncService::QueueActor3dSync).
    void RefreshHydratedMortal3D(
        RE::Actor* actor,
        RE::TESRace* targetRace = nullptr);

}  // namespace MORWHA::Engine
