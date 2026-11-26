#pragma once

#include "morwha/core/MorwhaCore.h"
#include "morwha/state/RegistryTypes.h"

#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>

namespace RE {
    class BGSKeyword;
    class TESNPC;
    class TESRace;
}

namespace MORWHA::Engine {

    struct ResolvedEngineHeritage {
        Core::MortalHeritageSummary summary{};
        std::uint32_t primaryRaceFormId{ 0 };
        std::uint32_t secondaryRaceFormId{ 0 };
    };

    /// Pre-registers ESP-less runtime BGSKeywords at kDataLoaded, attaches mixed-heritage and
    /// chimera lore keywords to hydrated DPF TESNPCs, and resolves heritage metadata for C/Papyrus queries.
    class MortalHeritageKeywordService {
    public:
        static MortalHeritageKeywordService& GetSingleton() noexcept;

        void InitializeAtDataLoaded();

        [[nodiscard]] RE::BGSKeyword* GetOrCreateKeyword(std::string_view editorId);

        [[nodiscard]] ResolvedEngineHeritage ResolveSpecHeritage(
            const State::DynamicNpcSpec& spec,
            const RE::TESRace* resolvedPrimaryRace = nullptr) const;

        void SyncNpcHeritageKeywords(
            RE::TESNPC* targetNpc,
            const State::DynamicNpcSpec& spec,
            const RE::TESRace* resolvedPrimaryRace = nullptr);

    private:
        MortalHeritageKeywordService() = default;

        mutable std::mutex mutex_{};
        std::unordered_map<std::string, RE::BGSKeyword*> keywordCache_{};
    };

}  // namespace MORWHA::Engine
