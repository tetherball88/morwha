#pragma once

#include "morwha/core/MorwhaCore.h"
#include "morwha/foundation/DomainTypes.h"
#include "morwha/foundation/StrongIds.h"
#include "morwha/state/ChimeraAccessoryManifest.h"
#include "morwha/state/MorwhaIniConfig.h"
#include "morwha/state/RegistryTypes.h"

#include <mutex>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace RE {
    class Actor;
    class BGSHeadPart;
    class BGSListForm;
    class BGSTextureSet;
    class TESObjectARMA;
    class TESObjectARMO;
    class TESRace;
}

namespace MORWHA::Engine {

    /// Loads declarative Data/SKSE/Plugins/MORWHA/ChimeraAccessories/*.json catalogs at kDataLoaded,
    /// validates VFS mesh/texture existence and skee64.dll requirements, clones private runtime tail
    /// ARMAs without mutating vanilla/mod records, caches per-chimera WNAM skin ARMOs, promotes
    /// curated SkinArmorAddon horns into private runtime PNAM = 32 BGSHeadParts, and builds sharp
    /// beast mouth BGSHeadParts.
    class ChimeraAccessoryCatalog {
    public:
        static ChimeraAccessoryCatalog& GetSingleton() noexcept;

        void InitializeAtDataLoaded();

        [[nodiscard]] Core::TierAvailability GetTierAvailability() const noexcept;
        [[nodiscard]] Core::TierAvailability GetTierAvailability(
            const State::ChimeraLineageFeatureConfig& khajiitCfg,
            const State::ChimeraLineageFeatureConfig& argonianCfg) const noexcept;
        [[nodiscard]] bool IsSkee64Loaded() const noexcept { return skee64Loaded_; }
        [[nodiscard]] bool HasBeastMouthTextureSet(State::BeastTeethKind teethKind) const noexcept;

        [[nodiscard]] RE::BGSHeadPart* PickEarHeadPart(
            Core::ChimeraLineageKind lineage,
            const RE::TESRace* race,
            Foundation::Sex sex,
            Foundation::Seed64 seed) const;

        [[nodiscard]] std::vector<RE::BGSHeadPart*> GetValidHornHeadParts(
            Core::ChimeraLineageKind lineage,
            const RE::TESRace* race,
            Foundation::Sex sex) const;

        [[nodiscard]] std::vector<RE::TESObjectARMA*> GetValidSkinHornAddons(
            Core::ChimeraLineageKind lineage,
            const RE::TESRace* race,
            Foundation::Sex sex) const;

        [[nodiscard]] RE::BGSHeadPart* PickCatalogedMouthHeadPart(
            Core::ChimeraLineageKind lineage,
            const RE::TESRace* race,
            Foundation::Sex sex,
            Foundation::Seed64 seed) const;

        [[nodiscard]] bool IsApprovedAccessoryHeadPart(
            const RE::BGSHeadPart* part,
            const RE::TESRace* race,
            Foundation::Sex sex) const noexcept;

        [[nodiscard]] bool IsCatalogedAccessoryMouth(
            const RE::BGSHeadPart* part) const noexcept;

        [[nodiscard]] bool IsRuntimeBeastMouthPart(
            const RE::BGSHeadPart* part) const noexcept;

        [[nodiscard]] bool IsRuntimePromotedHornPart(
            const RE::BGSHeadPart* part) const noexcept;

        [[nodiscard]] bool IsCatalogedHornHeadPart(
            const RE::BGSHeadPart* part) const noexcept;

        [[nodiscard]] bool IsCatalogedEarHeadPart(
            const RE::BGSHeadPart* part) const noexcept;

        [[nodiscard]] bool IsRuntimeAccessoryHeadPartClone(
            const RE::BGSHeadPart* part) const noexcept;

        [[nodiscard]] RE::BGSHeadPart* ResolveRaceCompatibleAccessoryHeadPart(
            RE::BGSHeadPart* part,
            const RE::TESRace* resolvedRace);

        [[nodiscard]] RE::TESObjectARMA* GetClonedTailArma(
            State::BeastTailKind tailKind) const noexcept;

        [[nodiscard]] std::vector<RE::BGSHeadPart*> ResolvePromotedHornHeadParts(
            RE::TESRace* resolvedRace,
            Foundation::Sex sex,
            std::span<const State::PluginFormRef> addonRefs);

        [[nodiscard]] RE::BGSHeadPart* ResolveHydratedCatalogMouthPart(
            RE::BGSHeadPart* catalogMouth);

        [[nodiscard]] RE::BGSHeadPart* ResolveRuntimeBeastMouthPart(
            RE::BGSHeadPart* baseMouth,
            State::BeastTeethKind teethKind);

        [[nodiscard]] RE::TESObjectARMO* GetOrCreateChimeraSkinArmor(
            RE::TESObjectARMO* baseSkin,
            RE::TESRace* resolvedRace,
            State::BeastTailKind tailKind);

        void RecordNpcChimeraState(
            RE::FormID baseNpcFormId,
            RE::TESRace* resolvedRace,
            Foundation::Sex sex,
            State::BeastTailKind tailKind,
            std::string_view hybridSkinId = {});

        bool SyncActorChimeraSkin(RE::Actor* actor);

        [[nodiscard]] bool IsRuntimeChimeraSkin(const RE::TESObjectARMO* skin) const noexcept;

    private:
        struct HeadPartEntry {
            RE::BGSHeadPart* part{ nullptr };
            std::uint8_t lineageMask{ 0 };
            std::uint8_t sexMask{ 0 };
            bool isMouth{ false };
            std::string resolvedModelRelPath{};
            bool needsModelOverride{ false };
        };

        struct SkinAddonEntry {
            RE::TESObjectARMA* arma{ nullptr };
            std::uint8_t lineageMask{ 0 };
            std::uint8_t sexMask{ 0 };
        };

        struct NpcChimeraState {
            RE::TESRace* resolvedRace{ nullptr };
            Foundation::Sex sex{ Foundation::Sex::Female };
            State::BeastTailKind tailKind{ State::BeastTailKind::None };
            std::string hybridSkinId{};
        };

        ChimeraAccessoryCatalog() = default;

        static void AppendUniqueRaces(
            RE::TESObjectARMA* arma,
            const std::vector<RE::TESRace*>& humanoidRaces);

        [[nodiscard]] RE::BGSHeadPart* GetOrCreatePromotedHornHeadPart(
            RE::TESObjectARMA* srcArma,
            Foundation::Sex sex);

        [[nodiscard]] RE::TESObjectARMA* CloneRuntimeArmaForHumanoids(
            RE::TESObjectARMA* srcArma,
            std::string_view editorId) const;

        [[nodiscard]] static bool MatchesEntryFilter(
            const RE::BGSHeadPart* part,
            std::uint8_t sexMask,
            const RE::TESRace* race,
            Foundation::Sex sex) noexcept;

        [[nodiscard]] RE::BGSTextureSet* BuildMouthTextureSetIfPresent(
            std::string_view folderName,
            std::string_view editorId);

        bool initialized_{ false };
        bool skee64Loaded_{ false };

        std::vector<RE::TESRace*> humanoidRaces_{};
        RE::BGSListForm* humanoidValidRacesList_{ nullptr };

        RE::TESObjectARMA* clonedTailKhajiit_{ nullptr };
        RE::TESObjectARMA* clonedTailArgonian_{ nullptr };

        RE::BGSTextureSet* sharpMouthTxst_{ nullptr };

        std::vector<RE::BGSHeadPart*> khajiitEarParts_{};
        std::vector<RE::BGSHeadPart*> argonianHornParts_{};
        std::vector<RE::BGSHeadPart*> chimeraMouthParts_{};
        std::vector<RE::TESObjectARMA*> argonianSkinHornAddons_{};

        std::unordered_map<const RE::BGSHeadPart*, HeadPartEntry> approvedHeadParts_{};
        std::unordered_map<const RE::TESObjectARMA*, SkinAddonEntry> approvedSkinAddons_{};

        std::unordered_map<std::string, RE::BGSHeadPart*> runtimeMouthCache_{};
        std::unordered_map<RE::FormID, RE::BGSHeadPart*> runtimeCatalogMouthCache_{};
        std::vector<const RE::BGSHeadPart*> runtimeMouthSet_{};
        std::unordered_map<std::uint64_t, RE::BGSHeadPart*> runtimePromotedHornCache_{};
        std::vector<const RE::BGSHeadPart*> runtimePromotedHornSet_{};
        std::vector<const RE::BGSHeadPart*> runtimeClonedEarSet_{};
        std::unordered_map<RE::FormID, RE::BGSHeadPart*> runtimeRaceCompatHeadPartCache_{};
        std::vector<const RE::BGSHeadPart*> runtimeAccessoryHeadPartCloneSet_{};
        std::unordered_map<std::string, RE::TESObjectARMO*> runtimeSkinCache_{};
        std::vector<const RE::TESObjectARMO*> runtimeChimeraSkinSet_{};

        mutable std::mutex accessorySyncMutex_{};
        std::unordered_map<RE::FormID, NpcChimeraState> npcChimeraStates_{};
    };

}  // namespace MORWHA::Engine
