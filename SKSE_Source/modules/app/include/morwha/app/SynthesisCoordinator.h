#pragma once

#include "morwha/app/MortalSpawnTypes.h"
#include "morwha/engine/MortalHeritageKeywordService.h"
#include "morwha/engine/SynthesisSettings.h"
#include "morwha/state/RegistryTypes.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace RE {
    class Actor;
    class TESForm;
    class TESRace;
}

namespace MORWHA::App {

    struct GarbageCollectionReport {
        bool ok{ false };
        bool saveScanPerformed{ false };
        std::size_t scannedCoSaveCount{ 0 };
        std::size_t skippedOrphanCoSaveCount{ 0 };
        std::size_t matchedMorwhaSaveCount{ 0 };
        std::size_t prunedUncommittedCount{ 0 };
        std::size_t prunedCommittedCount{ 0 };
        std::size_t releasedDpfSlotCount{ 0 };
        std::size_t remainingSpecCount{ 0 };
        std::string error{};
    };

    /// Coordinates startup slot hydration, save lifecycle epochs, and runtime synthesis readiness.
    class SynthesisCoordinator {
    public:
        static SynthesisCoordinator& GetSingleton() noexcept;

        void OnDataLoaded() noexcept;
        void OnSessionRevert() noexcept;
        void OnPostLoadGame() noexcept;
        void OnGameSaved(std::span<const State::SpawnedMortalLink> savedLinks) noexcept;

        [[nodiscard]] bool IsReady() const noexcept;
        [[nodiscard]] std::string GetVersion() const;
        [[nodiscard]] std::size_t GetRegisteredSpecCount() const;
        [[nodiscard]] std::size_t GetUncommittedSpecCount() const;
        [[nodiscard]] Engine::SynthesisSettings GetSettings() const;
        void SetSettings(const Engine::SynthesisSettings& settings);
        bool ReloadSettingsFromIni();
        std::size_t PurgeUncommittedSpecs() noexcept;
        GarbageCollectionReport RunGarbageCollection(bool includeCrossSaveScan = true);

        [[nodiscard]] MortalSpawnOutcome SpawnMortal(const MortalSpawnRequest& request);

        [[nodiscard]] bool UpdateMortal(
            RE::Actor* targetActor,
            const MortalUpdateRequest& request);

        [[nodiscard]] RE::Actor* SpawnMortalJson(
            RE::Actor* anchor,
            std::string_view optionsJson);

        [[nodiscard]] bool UpdateMortalJson(
            RE::Actor* targetActor,
            std::string_view patchJson);

        [[nodiscard]] bool RerollMortal(
            RE::Actor* targetActor,
            const MortalRerollRequest& request);

        [[nodiscard]] std::optional<Engine::ResolvedEngineHeritage> QueryMortalHeritage(
            RE::TESForm* targetActorOrBaseNpc) const;

        [[nodiscard]] std::string QueryMortalHeritageJson(
            RE::TESForm* targetActorOrBaseNpc) const;

    private:
        SynthesisCoordinator() = default;

        [[nodiscard]] Foundation::Seed64 NextSeed() noexcept;

        [[nodiscard]] bool ReserveSlotKeyAndSeed(
            std::string_view keyPrefix,
            Foundation::Seed64 explicitSeed,
            std::string& outSlotKey,
            Foundation::Seed64& outSeed);

        [[nodiscard]] MortalSpawnOutcome PersistHydrateAndSpawn(
            const State::DynamicNpcSpec& spec,
            RE::TESRace* resolvedRace,
            RE::Actor* anchor,
            Foundation::FormRawId donorARefId = {},
            Foundation::FormRawId donorBRefId = {});

        std::atomic<bool> ready_{ false };
        std::atomic<bool> registryCorrupt_{ false };
        mutable std::mutex registryMutex_{};
        mutable std::mutex settingsMutex_{};
        Engine::SynthesisSettings settings_{};
        std::vector<State::DynamicNpcSpec> specs_{};
        std::unordered_map<std::uint32_t, std::string> hydratedNpcToSlotKey_{};
        std::uint64_t seedCounter_{ 0 };
        std::uint32_t nextSlotIndex_{ 1 };
    };

}  // namespace MORWHA::App
