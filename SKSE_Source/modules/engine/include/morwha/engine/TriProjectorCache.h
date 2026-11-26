#pragma once

#include "morwha/core/MorwhaCore.h"
#include "morwha/foundation/DomainTypes.h"
#include "morwha/foundation/StrongIds.h"

#include <RE/T/TESRace.h>

#include <array>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace RE {
    class Actor;
    class BGSHeadPart;
    class TESNPC;
    class TESRace;
}

namespace MORWHA::Engine {

    enum class DonorExtractionTier : std::uint8_t {
        Tier1LiveFaceData = 1,
        Tier2NifProjected = 2,
        Tier3SeededAnchor = 3
    };

    struct DonorGeometryBundle {
        bool has3dGeometry{ false };
        DonorExtractionTier sourceTier{ DonorExtractionTier::Tier3SeededAnchor };
        std::uint32_t sourceVertexCount{ 0 };
        RE::TESRace* unwrappedDonorRace{ nullptr };
        Foundation::Sex donorSex{ Foundation::Sex::Female };
        Core::ExtractedHeadScene faceScene{};
        Core::EyeSample irisSignature{};
        std::optional<Foundation::RgbColor> nifHairShaderColor{};
    };

    struct Target3dMorphSynthesisResult {
        bool succeeded{ false };
        DonorExtractionTier effectiveTier{ DonorExtractionTier::Tier3SeededAnchor };
        Foundation::FaceMorphVector faceMorphs{};
        float projectedRmse{ 0.0f };
        float rSquared{ 0.0f };
        float skullResidualA{ 0.0f };
        float skullResidualB{ 0.0f };
    };

    struct CanonicalRaceGeometry {
        const Core::OpaqueTriAsset* chargenTri{ nullptr };
        const Core::OpaqueRaceRig* rig{ nullptr };
        Core::Float3 restRightEyeCenter{ 2.42f, 7.35f, 2.76f };
    };

    class TriProjectorCache {
    public:
        static TriProjectorCache& GetSingleton() noexcept;

        [[nodiscard]] DonorGeometryBundle ExtractDonorGeometryAndIris(
            RE::Actor* optionalActor,
            RE::TESNPC* rootFaceNpc,
            RE::TESRace* originalRace,
            Foundation::Sex sex,
            bool isPlayer,
            const Foundation::FaceMorphVector& tier1Morphs,
            bool tier1HasNonZero,
            RE::BGSHeadPart* eyePart);

        [[nodiscard]] Core::EyeSample GetOrDecodeIrisTexture(
            std::string_view rawTexturePath,
            std::array<float, 2> uvCenter = { -1.0f, -1.0f });

        [[nodiscard]] Core::EyeSample GetOrDecodeEyeHeadPartIris(
            const RE::BGSHeadPart* eyePart);

        [[nodiscard]] Target3dMorphSynthesisResult SynthesizeTargetMorphsFromDonors3d(
            RE::TESRace* targetRace,
            Foundation::Sex targetSex,
            Foundation::TopologyPool targetTopology,
            const DonorGeometryBundle* donorA,
            const DonorGeometryBundle* donorB,
            const std::array<float, static_cast<std::size_t>(Foundation::FaceCluster::kCount)>& dominanceA,
            const Foundation::FaceMorphVector& individualAnchor,
            const std::array<float, 18>& morphNoise,
            Foundation::Seed64 seed,
            bool isOneDonorRelative,
            bool allowCrossRaceMorphs = false);

        [[nodiscard]] static std::vector<float> SynthesizeSanitizedAnchorVertices(
            const CanonicalRaceGeometry& geom,
            const RE::TESRace* morphAllowRace,
            Foundation::Sex targetSex,
            const RE::TESRace::FaceRelatedData* faceRelated,
            const RE::TESRace* secondRaceForNoseEyes,
            const std::vector<std::int32_t>& allowedNose,
            const std::vector<std::int32_t>& allowedEyes,
            const Foundation::FaceMorphVector& individualAnchor);

        void EnforceOrbitalClearanceForTarget(
            RE::TESRace* targetRace,
            Foundation::Sex targetSex,
            Foundation::TopologyPool targetTopology,
            Foundation::FaceMorphVector& faceMorphs,
            std::span<const std::int32_t> allowedEyesPresets = {},
            std::span<const std::int32_t> allowedMouthPresets = {});

        [[nodiscard]] std::vector<float> ComputeSpecCrossRaceEarDeltas(
            RE::TESRace* targetRace,
            RE::TESRace* secondaryRace,
            Foundation::Sex sex,
            Foundation::TopologyPool topology,
            Foundation::Seed64 seed);

    private:
        struct CanonicalTriAsset {
            const Core::OpaqueTriAsset* chargenTri{ nullptr };
            const Core::OpaqueTriAsset* eyeChargenTri{ nullptr };
            const Core::OpaqueRaceRig* rig{ nullptr };
            Core::Float3 restRightEyeCenter{ 2.42f, 7.35f, 2.76f };
        };

        struct TargetSynthesisContext {
            CanonicalTriAsset canonical{};
            Core::TargetRigPtr targetRig{};
            bool valid{ false };
        };

        struct EyeNifMetadata {
            std::array<float, 2> corneaUv{ -1.0f, -1.0f };
            std::string fallbackDiffusePath{};
            bool hasEmissive{ false };
            std::optional<Core::Float3> restRightEyeCenter{};
            std::vector<float> vertices{};
            std::vector<float> uvs{};
            std::vector<std::uint32_t> triangles{};
        };

        TriProjectorCache() = default;

        [[nodiscard]] const Core::OpaqueTriAsset* GetOrLoadTriLocked(std::string_view vfsPath);

        [[nodiscard]] EyeNifMetadata GetOrParseEyeNifMetadataLocked(std::string_view modelPath);

        [[nodiscard]] Foundation::TopologyPool SelectDonorTopologyForVertexCountLocked(
            RE::TESRace* donorRace,
            Foundation::Sex donorSex,
            std::uint32_t sourceVertexCount);

        [[nodiscard]] const Core::OpaqueRaceRig* GetOrBuildBasisLocked(
            const RE::BGSHeadPart* facePart,
            const RE::TESRace* race,
            Foundation::Sex sex);

        [[nodiscard]] Core::Float3 GetCanonicalRestEyeCenterLocked(
            RE::TESRace* race,
            Foundation::Sex sex);

        [[nodiscard]] const CanonicalTriAsset* GetOrBuildCanonicalTriAssetLocked(
            RE::TESRace* race,
            Foundation::Sex sex,
            Foundation::TopologyPool topology);

        [[nodiscard]] const TargetSynthesisContext* GetOrBuildTargetContextLocked(
            RE::TESRace* targetRace,
            Foundation::Sex targetSex,
            Foundation::TopologyPool targetTopology);

        [[nodiscard]] const Core::OpaqueDonorRig* GetOrBuildDonorBaselineLocked(
            const TargetSynthesisContext& targetCtx,
            std::string_view targetKey,
            RE::TESRace* donorRace,
            Foundation::Sex donorSex,
            Foundation::TopologyPool donorTopology);

        [[nodiscard]] Core::EyeSample DecodeIrisTextureLocked(
            std::string_view rawTexturePath,
            std::array<float, 2> uvCenter);

        std::mutex mutex_{};
        std::unordered_map<std::string, Core::TriAssetPtr> triCache_{};
        std::unordered_map<std::string, EyeNifMetadata> eyeNifMetadataCache_{};
        std::unordered_map<std::string, Core::RaceRigPtr> basisCache_{};
        std::unordered_map<std::string, Core::Float3> restEyeCenterCache_{};
        std::unordered_map<std::string, std::unique_ptr<CanonicalTriAsset>> canonicalAssetCache_{};
        std::unordered_map<std::string, std::unique_ptr<TargetSynthesisContext>> targetContextCache_{};
        std::unordered_map<std::string, Core::DonorRigPtr> donorBaselineCache_{};
        std::unordered_map<std::uint32_t, Core::ExtractedHeadScene> npcNifSceneCache_{};
        std::unordered_map<std::string, Core::EyeSample> irisTextureCache_{};
        std::unordered_map<std::uint32_t, Core::EyeSample> headPartIrisCache_{};
    };

}  // namespace MORWHA::Engine
