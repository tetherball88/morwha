#pragma once

#include "morwha/foundation/DomainTypes.h"
#include "morwha/state/SessionEpoch.h"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace MORWHA::Engine {

    struct HydratedVisualEntry {
        std::string slotKey{};
        Foundation::RgbColor hairColor{};
        Foundation::RgbColor skinColor{};
        std::optional<Foundation::RgbColor> tailColor{};
        std::optional<Foundation::RgbColor> hornColor{};
        std::optional<Foundation::RgbColor> earColor{};
    };

    /// Listens for 3D load, NiNode update, and throttled frame events on MORWHA-hydrated actors
    /// and executes epoch-guarded main-thread shader sync (UpdateHairColor + UpdateSkinColor).
    /// Does not invoke UpdateNeck or reset 3D, because BSFaceGenManager already morphs runtime
    /// head neck geometry to npc->weight once during dynamic head generation.
    class Mortal3dSyncService final : public RE::BSTEventSink<RE::TESObjectLoadedEvent>,
                                      public RE::BSTEventSink<RE::TESEquipEvent>,
                                      public RE::BSTEventSink<SKSE::NiNodeUpdateEvent>,
                                      public RE::BSTEventSink<RE::InputEvent*> {
    public:
        static Mortal3dSyncService& GetSingleton() noexcept;

        void RegisterEventSink() noexcept;
        void RegisterInputSink() noexcept;
        void ClearPendingOnRevert() noexcept;

        void RegisterHydratedNpc(
            RE::FormID baseNpcFormId,
            std::string slotKey,
            Foundation::RgbColor hairColor,
            Foundation::RgbColor skinColor,
            std::optional<Foundation::RgbColor> tailColor = std::nullopt,
            std::optional<Foundation::RgbColor> hornColor = std::nullopt,
            std::optional<Foundation::RgbColor> earColor = std::nullopt);

        void UnregisterHydratedNpcsBySlotKeys(const std::unordered_set<std::string>& slotKeys);

        [[nodiscard]] std::optional<HydratedVisualEntry> LookupEntry(RE::FormID baseNpcFormId) const;

        void QueueActor3dSync(RE::Actor* actor);

        RE::BSEventNotifyControl ProcessEvent(
            const RE::TESObjectLoadedEvent* event,
            RE::BSTEventSource<RE::TESObjectLoadedEvent>* eventSource) override;

        RE::BSEventNotifyControl ProcessEvent(
            const RE::TESEquipEvent* event,
            RE::BSTEventSource<RE::TESEquipEvent>* eventSource) override;

        RE::BSEventNotifyControl ProcessEvent(
            const SKSE::NiNodeUpdateEvent* event,
            RE::BSTEventSource<SKSE::NiNodeUpdateEvent>* eventSource) override;

        RE::BSEventNotifyControl ProcessEvent(
            RE::InputEvent* const* event,
            RE::BSTEventSource<RE::InputEvent*>* eventSource) override;

    private:
        struct PendingActorSync {
            RE::ActorHandle handle{};
            RE::FormID actorFormId{ 0 };
            State::SessionEpoch epoch{ 0 };
            std::uint32_t retriesRemaining{ 100 };
        };

        Mortal3dSyncService() = default;

        void ScheduleMainThreadDrain() noexcept;

        mutable std::mutex mutex_{};
        std::unordered_map<RE::FormID, HydratedVisualEntry> hydratedByBaseId_{};
        std::vector<PendingActorSync> pendingActors_{};
        std::atomic<bool> hasPending_{ false };
        std::atomic<bool> drainTaskQueued_{ false };
        std::atomic<bool> applyingSync_{ false };
        std::atomic<std::int64_t> lastPollMs_{ 0 };
        bool objectSinkRegistered_{ false };
        bool inputSinkRegistered_{ false };
    };

}  // namespace MORWHA::Engine
