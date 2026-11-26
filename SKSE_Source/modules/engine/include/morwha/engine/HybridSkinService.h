#pragma once

#include "morwha/core/MorwhaCore.h"
#include "morwha/engine/SynthesisSettings.h"
#include "morwha/foundation/DomainTypes.h"
#include "morwha/state/RegistryTypes.h"

#include <cstdint>
#include <filesystem>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace RE {
    class BGSHeadPart;
    class BGSTextureSet;
    class TESObjectARMA;
    class TESObjectARMO;
    class TESRace;
}

namespace MORWHA::Engine {

    /// Discovers pre-baked 3D .mrst skin stencils and pre-generates/validates cached 4-map
    /// (.dds BC1 + _msn.dds BC7 + _s.dds BC1 + _sk.dds BC1) hybrid skin sets at kDataLoaded over
    /// the player's active VFS skin mod (matching native per-map resolution or capping to 1K/2K/4K
    /// without upscaling), then binds runtime BGSTextureSet / TESObjectARMA / TESObjectARMO /
    /// kFace BGSHeadPart forms in <1 ms when spawning hybrid NPCs.
    class HybridSkinService {
    public:
        struct ResolvedHybridForms {
            RE::TESObjectARMO* skinArmor{ nullptr };
            RE::BGSTextureSet* faceTextureSet{ nullptr };
            RE::BGSHeadPart* faceHeadPartClone{ nullptr };
        };

        static HybridSkinService& GetSingleton() noexcept;

        void InitializeAtDataLoaded(const SynthesisSettings& settings);
        void UpdateSettings(const SynthesisSettings& settings);

        [[nodiscard]] bool IsCoatGroupReadyForSettings(
            Core::HybridCoatGroup group,
            Foundation::Sex sex,
            bool enableGeneratedHybridSkins) const noexcept;

        [[nodiscard]] ResolvedHybridForms ResolveHybridNpcForms(
            std::string_view hybridSkinId,
            RE::TESObjectARMO* baseSkin,
            RE::TESRace* resolvedRace,
            Foundation::Sex sex,
            State::BeastTailKind tailKind,
            RE::BGSHeadPart* baseFaceHeadPart);

        [[nodiscard]] bool EnsureSkinArmorMatchesHybridState(
            RE::TESObjectARMO* skin,
            RE::TESRace* resolvedRace,
            Foundation::Sex sex,
            std::string_view hybridSkinId,
            State::BeastTailKind tailKind) const noexcept;

        [[nodiscard]] bool IsRuntimeHybridSkinArmor(const RE::TESObjectARMO* skin) const noexcept;
        [[nodiscard]] bool IsRuntimeHybridFacePart(const RE::BGSHeadPart* part) const noexcept;
        [[nodiscard]] bool IsRuntimeHybridFaceTextureSet(const RE::BGSTextureSet* txst) const noexcept;

    private:
        enum class BaseSkinFamily : std::uint8_t {
            Humanoid = 0,
            Khajiit = 1,
            Argonian = 2
        };

        enum class SkinSurfaceSlot : std::uint8_t {
            Face = 0,
            Body = 1,
            Hands = 2
        };

        struct SurfaceSourcePaths {
            std::string diffuseRel{};
            std::string normalRel{};
            std::string subsurfRel{};
            std::string specRel{};
            std::string detailRel{};
        };

        struct FamilySexBasePaths {
            bool valid{ false };
            Core::BodyUvFamily uvFamily{ Core::BodyUvFamily::Cbbe };
            SurfaceSourcePaths face{};
            SurfaceSourcePaths body{};
            SurfaceSourcePaths hands{};
        };

        struct GeneratedPartPaths {
            std::string diffuseRel{};
            std::string normalRel{};
            std::string subsurfRel{};
            std::string specRel{};
            std::string detailRel{};
        };

        struct GeneratedArchetypeSet {
            RE::BGSTextureSet* faceTxst{ nullptr };
            RE::BGSTextureSet* bodyTxst{ nullptr };
            RE::BGSTextureSet* handsTxst{ nullptr };
            RE::BGSTextureSet* tailTxst{ nullptr };
        };

        struct VfsTextureMeta {
            bool exists{ false };
            std::uint32_t topWidth{ 0 };
            std::uint32_t topHeight{ 0 };
            std::uint64_t headerHash{ 0 };
        };

        struct PrewarmStats {
            std::size_t generatedFiles{ 0 };
            std::size_t cachedFiles{ 0 };
        };

        HybridSkinService() = default;

        [[nodiscard]] static BaseSkinFamily FamilyForCoatGroup(Core::HybridCoatGroup group) noexcept;

        [[nodiscard]] std::filesystem::path FindStencilPath(
            BaseSkinFamily family,
            Foundation::Sex sex,
            SkinSurfaceSlot slot,
            Core::HybridStencilKind kind) const;

        [[nodiscard]] VfsTextureMeta ProbeVfsTexture(std::string_view relTexPath);

        void DiscoverBaseSkinPathsFromEngine();
        void LoadCacheManifest();
        void SaveCacheManifest() const;

        void PrewarmAllHybridSkins(PrewarmStats& outStats);

        [[nodiscard]] bool EnsureSurfaceSlotMaps(
            BaseSkinFamily family,
            Foundation::Sex sex,
            SkinSurfaceSlot slot,
            std::span<const Core::HybridCoatDescriptor* const> coats,
            PrewarmStats& ioStats);

        [[nodiscard]] bool EnsureTailCoatMaps(
            State::BeastTailKind tailKind,
            Foundation::Sex sex,
            std::span<const Core::HybridCoatDescriptor* const> coats,
            PrewarmStats& ioStats);

        [[nodiscard]] const GeneratedArchetypeSet* GetOrGenerateArchetypeTextureSets(
            const Core::HybridCoatDescriptor& coat,
            Foundation::Sex sex);

        [[nodiscard]] RE::BGSTextureSet* CreateRuntimeSkinTextureSet(
            std::string_view editorId,
            const GeneratedPartPaths& paths);

        [[nodiscard]] RE::TESObjectARMA* GetOrCreateHybridArmaClone(
            RE::TESObjectARMA* srcArma,
            Foundation::Sex sex,
            RE::BGSTextureSet* overrideTxst,
            std::string_view tag);

        [[nodiscard]] RE::BGSHeadPart* GetOrCreateHybridFaceHeadPartClone(
            RE::BGSHeadPart* baseFacePart,
            RE::BGSTextureSet* faceTxst);

        mutable std::mutex mutex_{};
        bool initialized_{ false };
        bool enabled_{ true };
        State::GeneratedSkinResolution resolutionMode_{ State::GeneratedSkinResolution::Auto };

        std::unordered_set<std::string> availableStencilFiles_{};
        FamilySexBasePaths basePaths_[3][2]{};
        SurfaceSourcePaths tailSourcePaths_[2][2]{};

        std::unordered_map<std::string, VfsTextureMeta> vfsMetaCache_{};
        std::unordered_map<std::string, std::string> cacheFingerprints_{};
        bool cacheDirty_{ false };

        std::unordered_map<std::string, GeneratedPartPaths> generatedSlotPaths_{};
        std::unordered_map<std::string, GeneratedArchetypeSet> archetypeTxstCache_{};
        std::unordered_map<std::string, RE::TESObjectARMA*> hybridArmaCache_{};
        std::unordered_map<std::string, RE::TESObjectARMO*> hybridSkinArmorCache_{};
        std::unordered_map<std::string, RE::BGSHeadPart*> hybridFacePartCache_{};

        std::vector<const RE::TESObjectARMO*> runtimeHybridSkins_{};
        std::vector<const RE::BGSHeadPart*> runtimeHybridFaceParts_{};
        std::vector<const RE::BGSTextureSet*> runtimeHybridFaceTxsts_{};
    };

}  // namespace MORWHA::Engine
