#pragma once

#include "morwha/foundation/DomainTypes.h"
#include "morwha/foundation/StrongIds.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace MORWHA::Core {

    enum class RaceGroup : std::uint8_t {
        Humanoid = 0,
        Khajiit = 1,
        Argonian = 2,
        Other = 3
    };

    enum class InheritedParentSlot : std::uint8_t {
        ParentA = 0,
        ParentB = 1
    };

    enum class BeastChimeraTier : std::uint8_t {
        Disabled = 0,
        Tier1DominantMorph = 1,
        Tier2EquippedParts = 2
    };

    enum class ChimeraLineageKind : std::uint8_t {
        None = 0,
        HumanoidKhajiit = 1,
        HumanoidArgonian = 2,
        KhajiitArgonian = 3
    };

    enum class ChimeraTargetRacePolicy : std::uint8_t {
        DominantParent5050 = 0,
        ForceHumanoidParent = 1
    };

    enum class HybridCoatGroup : std::uint8_t {
        HumanoidKhajiitFur = 0,
        HumanoidArgonianLizard = 1,
        ArgonianKhajiitFur = 2,
        KhajiitArgonianLizard = 3
    };

    enum class HybridStencilKind : std::uint8_t {
        FurTabby = 0,
        FurSpotted = 1,
        LizardPebble = 2,
        LizardKeeled = 3
    };

    enum class BodyUvFamily : std::uint8_t {
        Cbbe = 0,
        Bhunp = 1,
        Vanilla = 2,
        Ube = 3,
        Himbo = 4,
        Sam = 5
    };

    enum class MortalMacroGroup : std::uint8_t {
        Man = 0,
        Mer = 1,
        Beast = 2,
        Other = 3
    };

    struct MortalHeritageSummary {
        bool isMixedHeritage{ false };
        BeastChimeraTier chimeraTier{ BeastChimeraTier::Disabled };
        MortalMacroGroup primaryGroup{ MortalMacroGroup::Man };
        MortalMacroGroup secondaryGroup{ MortalMacroGroup::Other };
        std::string primaryRace{};
        std::string secondaryRace{};
        std::string groupPair{};
        std::string racePair{};
        std::string lineageLabel{};
        std::string loreName{};
        std::vector<std::string> keywords{};
    };

    inline constexpr std::string_view kDefaultHybridStencilsDir =
        "Data/SKSE/Plugins/MORWHA/SkinStencils";

    struct Float3 {
        float x{ 0.0f };
        float y{ 0.0f };
        float z{ 0.0f };
    };

    struct MeshView {
        std::span<const float> vertices{};
        std::span<const std::uint32_t> triangles{};
    };

    struct MorphDeltaView {
        std::string_view name{};
        std::span<const float> deltas{};
    };

    struct MouthRigInput {
        std::span<const float> mouthBaseVertices{};
        std::span<const std::uint32_t> mouthTriangles{};
        std::span<const float> optionalMouthRaceDeltas{};
        std::span<const MorphDeltaView> mouthChargenMorphs{};
        std::span<const MorphDeltaView> mouthExprMorphs{};
    };

    // Opaque handles to internal synthesis, mesh-rig, .tri, and hybrid-stencil objects.
    struct OpaqueRaceRig;
    struct OpaqueTargetRig;
    struct OpaqueDonorRig;
    struct OpaqueTriAsset;
    struct OpaqueSkinStencil;

    struct OpaqueRaceRigDeleter {
        void operator()(OpaqueRaceRig* ptr) const noexcept;
    };
    struct OpaqueTargetRigDeleter {
        void operator()(OpaqueTargetRig* ptr) const noexcept;
    };
    struct OpaqueDonorRigDeleter {
        void operator()(OpaqueDonorRig* ptr) const noexcept;
    };
    struct OpaqueTriAssetDeleter {
        void operator()(OpaqueTriAsset* ptr) const noexcept;
    };
    struct OpaqueSkinStencilDeleter {
        void operator()(OpaqueSkinStencil* ptr) const noexcept;
    };

    using RaceRigPtr = std::unique_ptr<OpaqueRaceRig, OpaqueRaceRigDeleter>;
    using TargetRigPtr = std::unique_ptr<OpaqueTargetRig, OpaqueTargetRigDeleter>;
    using DonorRigPtr = std::unique_ptr<OpaqueDonorRig, OpaqueDonorRigDeleter>;
    using TriAssetPtr = std::unique_ptr<OpaqueTriAsset, OpaqueTriAssetDeleter>;
    using SkinStencilPtr = std::unique_ptr<OpaqueSkinStencil, OpaqueSkinStencilDeleter>;

    // Opaque fixed-size descriptors hiding internal vision, color, probe, and DDS state.
    struct alignas(8) EyeSample {
        std::uint8_t opaque[48]{};
    };

    struct alignas(4) EyeTarget {
        std::uint8_t opaque[16]{};
    };

    struct alignas(4) ComplexionSample {
        std::uint8_t opaque[24]{};
    };

    struct alignas(4) EyeMeshProbe {
        std::uint8_t opaque[28]{};
    };

    struct alignas(8) DdsSlicePlan {
        std::uint32_t topWidth{ 0 };
        std::uint32_t topHeight{ 0 };
        std::size_t fileByteOffset{ 0 };
        std::size_t sliceByteSize{ 0 };
        std::uint8_t opaque[24]{};
    };

    struct ExtractedMeshBlock {
        std::string name{};
        std::string blockType{};
        std::uint32_t vertexCount{ 0 };
        std::vector<float> vertices{};
        std::vector<std::uint32_t> triangles{};
        std::vector<float> uvs{};
        std::uint32_t shaderType{ 0xFFFFFFFFu };
        bool hasActiveEmissiveGlow{ false };
        std::string diffuseTexturePath{};
        bool hasHairTintColor{ false };
        std::array<float, 3> hairTintColor{};
    };

    struct ExtractedHeadScene {
        std::vector<ExtractedMeshBlock> allMeshes{};
        std::optional<std::size_t> headMeshIndex{};
        std::vector<std::size_t> eyeMeshIndices{};
        std::optional<std::size_t> mouthMeshIndex{};
        std::string eyeDiffuseTexturePath{};
        std::optional<Foundation::RgbColor> hairShaderColor{};

        [[nodiscard]] const ExtractedMeshBlock* HeadMesh() const noexcept {
            return (headMeshIndex && *headMeshIndex < allMeshes.size())
                       ? &allMeshes[*headMeshIndex]
                       : nullptr;
        }

        [[nodiscard]] const ExtractedMeshBlock* MouthMesh() const noexcept {
            return (mouthMeshIndex && *mouthMeshIndex < allMeshes.size())
                       ? &allMeshes[*mouthMeshIndex]
                       : nullptr;
        }
    };

    struct HybridCoatDescriptor {
        std::string_view id{};
        HybridCoatGroup group{ HybridCoatGroup::HumanoidKhajiitFur };
        HybridStencilKind stencilKind{ HybridStencilKind::FurTabby };
        Foundation::RgbColor primaryRgb{ 128, 128, 128 };
        Foundation::RgbColor secondaryRgb{ 64, 64, 64 };
        float patternStrength{ 0.85f };
        float skinHarmony{ 0.15f };
    };

    struct SkinMapDimensions {
        std::uint32_t width{ 1024u };
        std::uint32_t height{ 1024u };
        bool operator==(const SkinMapDimensions&) const noexcept = default;
    };

    using ParallelForSliceFn =
        std::function<void(std::uint32_t totalItems, const std::function<void(std::uint32_t, std::uint32_t)>& body)>;

    struct SharedTriPeerPresets {
        std::span<const std::int32_t> presets{};
        bool sameRaceGroup{ false };
        bool sameMerClass{ false };
        bool isElder{ false };
        bool isOrc{ false };
    };

    struct AnchorMorphCandidate {
        std::string_view raceKey{};
        Foundation::Sex sex{ Foundation::Sex::Female };
        Foundation::FaceMorphVector faceMorphs{};
    };

    struct ChimeraHybridCoatPlan {
        std::string coatId{};
        Foundation::RgbColor adjustedHairColor{};
        std::optional<Foundation::RgbColor> adjustedTailColor{};
        bool adjustHairColor{ false };
        bool clearTailColor{ false };
    };

    struct DonorPhenotype {
        RaceGroup raceGroup{ RaceGroup::Humanoid };
        Foundation::Sex sex{ Foundation::Sex::Female };
        Foundation::FaceMorphVector faceMorphs{};
        bool hasNonZeroMorphs{ false };
        Foundation::RgbColor skinColor{ 128, 128, 128 };
        Foundation::RgbColor hairColor{ 64, 44, 32 };
        std::optional<Foundation::RgbColor> secondaryColor{};
        float weight{ 50.0f };
        float height{ 1.0f };
        std::array<float, 2> raceBaseHeights{ 1.0f, 1.0f };
        Foundation::FormRawId raceId{};
    };

    struct RecombinedPhenotype {
        InheritedParentSlot chosenRaceParent{ InheritedParentSlot::ParentA };
        bool crossCompatibleDonors{ true };
        std::array<float, static_cast<std::size_t>(Foundation::FaceCluster::kCount)> clusterDominanceA{};
        Foundation::FaceMorphVector faceMorphs{};
        Foundation::RgbColor skinColor{ 128, 128, 128 };
        Foundation::RgbColor hairColor{ 64, 44, 32 };
        InheritedParentSlot eyeDonorPreference{ InheritedParentSlot::ParentA };
        float weight{ 50.0f };
        float height{ 1.0f };
    };

    struct TierAvailability {
        bool tier2HumanoidAccessoriesReady{ false };
        bool tier2KhajiitAccessoriesReady{ false };
        bool tier2ArgonianAccessoriesReady{ false };
        bool tier2KhajiitOnArgonianReady{ false };
        bool tier2ArgonianOnKhajiitReady{ false };
    };

    struct ChimeraResolutionPlan {
        ChimeraLineageKind lineage{ ChimeraLineageKind::None };
        BeastChimeraTier requestedTier{ BeastChimeraTier::Tier1DominantMorph };
        BeastChimeraTier effectiveTier{ BeastChimeraTier::Disabled };
        ChimeraTargetRacePolicy targetRacePolicy{ ChimeraTargetRacePolicy::DominantParent5050 };
        InheritedParentSlot chosenRaceParent{ InheritedParentSlot::ParentA };
        InheritedParentSlot humanoidParent{ InheritedParentSlot::ParentA };
        InheritedParentSlot beastParent{ InheritedParentSlot::ParentB };
        bool usedFallbackTier{ false };
    };

    struct ChimeraPigmentResult {
        Foundation::RgbColor skinColor{};
        Foundation::RgbColor hairColor{};
        std::optional<EyeTarget> targetEye{};
        bool inheritedDarkEyes{ false };
        float weight{ 50.0f };
        float height{ 1.0f };
        Foundation::RgbColor eyelinerColor{ 24, 20, 20 };
        std::uint16_t eyelinerAlpha{ 34 };
        Foundation::RgbColor noseTintColor{ 48, 36, 32 };
        std::uint16_t noseTintAlpha{ 0 };
        Foundation::RgbColor coatTargetColor{ 128, 128, 128 };
        Foundation::RgbColor tailColor{ 128, 128, 128 };
        Foundation::RgbColor hornColor{ 128, 128, 128 };
    };

    struct BeastSwatchInput {
        Foundation::RgbColor color{};
        float defaultOpacity{ 1.0f };
    };

    struct BeastTintAssetInput {
        std::uint8_t semanticType{ 0 };
        std::uint16_t explicitTintIndex{ 0xFFFFu };
        std::vector<BeastSwatchInput> swatches{};
    };

    struct DonorBeastLayerInput {
        std::uint8_t semanticType{ 0 };
        std::size_t ordinalInSemanticType{ 0 };
        Foundation::RgbColor color{};
        std::uint16_t interpolationValue{ 100 };
    };

    struct BeastLayerOutput {
        std::uint8_t semanticType{ 0 };
        std::uint16_t explicitTintIndex{ 0xFFFFu };
        Foundation::RgbColor color{};
        std::uint16_t interpolationValue{ 100 };
    };

    struct AgeVisualResult {
        Foundation::RgbColor hairColor{};
        Foundation::RgbColor ageShadowColor{ 92, 70, 62 };
        std::uint16_t laughLinesAlpha{ 0 };
        std::uint16_t eyeSocketLowerAlpha{ 0 };
        std::uint16_t foreheadAlpha{ 0 };
    };

    struct DonorMeshInput {
        bool valid{ false };
        Foundation::Sex donorSex{ Foundation::Sex::Female };
        const OpaqueRaceRig* donorCanonicalRig{ nullptr };
        const OpaqueDonorRig* donorBaselineRig{ nullptr };
        MeshView headMesh{};
        std::span<const MeshView> eyeMeshes{};
        std::span<const float> mouthVertices{};
    };

    struct MeshProjectionRequest {
        const OpaqueTargetRig* targetRig{ nullptr };
        Foundation::Sex targetSex{ Foundation::Sex::Female };
        bool isOneDonorRelative{ false };
        const DonorMeshInput* donorA{ nullptr };
        const DonorMeshInput* donorB{ nullptr };
        std::array<float, static_cast<std::size_t>(Foundation::FaceCluster::kCount)> clusterDominanceA{};
        std::span<const float> sanitizedAnchorVertices{};
        std::array<float, 18> morphNoise{};
        std::span<const std::int32_t> allowedNosePresets{};
        std::span<const std::int32_t> allowedEyesPresets{};
        std::span<const std::int32_t> allowedMouthPresets{};
        Foundation::Seed64 seed{};
    };

    struct MeshProjectionOutput {
        bool succeeded{ false };
        Foundation::FaceMorphVector faceMorphs{};
        float fitError{ 0.0f };
        float fitScore{ 0.0f };
        float donorAResidual{ 0.0f };
        float donorBResidual{ 0.0f };
    };

    // --- General Synthesis & Recombination ---
    [[nodiscard]] Foundation::Sex ChooseRandomSex(Foundation::Seed64 seed) noexcept;
    [[nodiscard]] InheritedParentSlot ChooseOffspringRaceParent(Foundation::Seed64 seed) noexcept;
    [[nodiscard]] RaceGroup ClassifyRaceKey(std::string_view normalizedRaceKey) noexcept;
    [[nodiscard]] bool IsMerRaceKey(std::string_view normalizedRaceKey) noexcept;
    [[nodiscard]] bool CanBlendDonorGroups(RaceGroup groupA, RaceGroup groupB) noexcept;

    [[nodiscard]] Foundation::RgbColor MixColors(
        Foundation::RgbColor colorA,
        Foundation::RgbColor colorB,
        float weightB) noexcept;

    [[nodiscard]] Foundation::FaceMorphVector SampleAnchorFromPool(
        std::span<const Foundation::FaceMorphVector> pool,
        Foundation::Seed64 seed);

    [[nodiscard]] Foundation::FaceMorphVector SynthesizeZeroDonorMorphs(
        Foundation::Sex targetSex,
        const Foundation::FaceMorphVector& anchor,
        const std::array<float, Foundation::kVanillaFaceMorphCount>& noise) noexcept;

    [[nodiscard]] RecombinedPhenotype RecombineOneDonor(
        const DonorPhenotype& donorA,
        Foundation::Sex targetSex,
        RaceGroup targetRaceGroup,
        const Foundation::FaceMorphVector& targetAnchor,
        Foundation::RgbColor fallbackRacialSkin,
        Foundation::RgbColor fallbackRacialHair,
        Foundation::Seed64 seed,
        float targetRaceHeight = 1.0f) noexcept;

    [[nodiscard]] RecombinedPhenotype RecombineTwoDonors(
        const DonorPhenotype& donorA,
        const DonorPhenotype& donorB,
        Foundation::Sex targetSex,
        std::optional<InheritedParentSlot> forcedRaceParent,
        const Foundation::FaceMorphVector& targetAnchor,
        Foundation::Seed64 seed,
        float targetRaceHeight = 1.0f,
        bool allowCrossRaceMorphs = false) noexcept;

    // --- Binary .tri / .nif / .dds Asset Parsers & Mesh Probes ---
    [[nodiscard]] TriAssetPtr LoadTriAsset(std::span<const std::uint8_t> bytes);
    [[nodiscard]] std::uint32_t GetTriAssetVertexCount(const OpaqueTriAsset* tri) noexcept;
    [[nodiscard]] std::span<const std::uint32_t> GetTriAssetTriangles(const OpaqueTriAsset* tri) noexcept;
    [[nodiscard]] std::string MatchTriRaceMorphName(
        const OpaqueTriAsset* raceTri,
        std::uint32_t expectedVertexCount,
        std::string_view primaryRaceEdid,
        std::string_view fallbackRaceEdid);

    [[nodiscard]] float DecodeHalfFloat(std::uint16_t halfBits) noexcept;

    [[nodiscard]] bool ParseHeadSceneNif(
        std::span<const std::uint8_t> nifBytes,
        ExtractedHeadScene& outScene,
        std::string_view preferredFacePartEditorId = {},
        std::uint32_t preferredVertexCount = 0);

    void FinalizeExtractedHeadScene(
        ExtractedHeadScene& ioScene,
        std::string_view preferredFacePartEditorId = {},
        std::uint32_t preferredVertexCount = 0);

    [[nodiscard]] EyeMeshProbe ProbeEyeMeshNif(std::span<const std::uint8_t> nifBytes);
    [[nodiscard]] bool IsEyeMeshProbeValidAndBilateral(const EyeMeshProbe& probe) noexcept;
    [[nodiscard]] bool AreEyeMeshProbesCompatible(const EyeMeshProbe& a, const EyeMeshProbe& b) noexcept;
    [[nodiscard]] bool IsHairMeshCrownSparse(std::span<const std::uint8_t> nifBytes);

    [[nodiscard]] bool PlanDdsSlice(
        std::span<const std::uint8_t> headerBytes,
        std::size_t totalFileBytes,
        DdsSlicePlan& outPlan,
        std::uint32_t targetDim = 32u) noexcept;

    [[nodiscard]] bool DecodeDdsSlice32x32(
        const DdsSlicePlan& plan,
        std::span<const std::uint8_t> sliceBytes,
        std::array<std::uint8_t, 32u * 32u * 4u>& outPixels) noexcept;

    [[nodiscard]] bool DecodeDdsSlice64x64(
        const DdsSlicePlan& plan,
        std::span<const std::uint8_t> sliceBytes,
        std::array<std::uint8_t, 64u * 64u * 4u>& outPixels) noexcept;

    [[nodiscard]] bool DecodeDdsSliceRgba(
        const DdsSlicePlan& plan,
        std::span<const std::uint8_t> sliceBytes,
        std::uint32_t outWidth,
        std::uint32_t outHeight,
        std::vector<std::uint8_t>& outRgba);

    // --- 3D Mesh Rigs & Projection ---
    [[nodiscard]] std::optional<Float3> MeasureEyePairRestCenter(
        std::span<const MeshView> eyeMeshes) noexcept;

    [[nodiscard]] std::array<float, 2> LocateEyeTextureCenter(
        std::span<const float> eyeVertices,
        std::span<const float> eyeUvs,
        std::span<const std::uint32_t> eyeTriangles = {}) noexcept;

    [[nodiscard]] Float3 EvaluateEyeMorphShift(
        std::span<const float> eyeBaseVertices,
        std::span<const std::uint32_t> eyeTriangles,
        std::span<const MorphDeltaView> eyeMorphs,
        const Foundation::FaceMorphVector& morphs);

    [[nodiscard]] Float3 EvaluateEyeTriMorphShift(
        const OpaqueTriAsset* optionalEyeChargenTri,
        const Foundation::FaceMorphVector& morphs);

    [[nodiscard]] RaceRigPtr CreateRaceRig(
        std::span<const float> chargenBaseVertices,
        std::span<const float> optionalRaceDeltas,
        std::span<const MorphDeltaView> chargenMorphs,
        std::span<const float> optionalEyeChargenBaseVertices,
        std::span<const MorphDeltaView> optionalEyeChargenMorphs,
        std::span<const MorphDeltaView> optionalExprMorphs,
        std::span<const float> eyeNifVertices,
        std::span<const float> eyeNifUvs,
        std::span<const std::uint32_t> eyeNifTriangles,
        std::span<const float> optionalEyeRaceDeltas,
        std::span<const std::uint32_t> optionalHeadTriangles,
        std::span<const MorphDeltaView> optionalEyeExprMorphs,
        const MouthRigInput& optionalMouthRig = {});

    [[nodiscard]] RaceRigPtr CreateRaceRigFromTriAssets(
        const OpaqueTriAsset* headChargenTri,
        const OpaqueTriAsset* optionalHeadRaceTri,
        const OpaqueTriAsset* optionalHeadExprTri,
        const OpaqueTriAsset* optionalEyeChargenTri,
        const OpaqueTriAsset* optionalEyeRaceTri,
        const OpaqueTriAsset* optionalEyeExprTri,
        std::span<const float> eyeNifVertices,
        std::span<const float> eyeNifUvs,
        std::span<const std::uint32_t> eyeNifTriangles,
        const OpaqueTriAsset* optionalMouthChargenTri,
        const OpaqueTriAsset* optionalMouthRaceTri,
        const OpaqueTriAsset* optionalMouthExprTri,
        std::string_view primaryRaceEdid,
        std::string_view fallbackRaceEdid);

    [[nodiscard]] std::uint32_t GetRaceRigVertexCount(const OpaqueRaceRig* rig) noexcept;

    [[nodiscard]] TargetRigPtr CreateTargetRig(
        const OpaqueRaceRig* canonicalRig,
        Float3 restRightEyeCenter,
        std::span<const Foundation::FaceMorphVector> anchorPool);

    [[nodiscard]] DonorRigPtr CreateDonorRig(
        const OpaqueTargetRig* targetRig,
        const OpaqueRaceRig* donorCanonicalRig,
        Float3 donorRestRightEyeCenter);

    [[nodiscard]] std::vector<float> BuildMorphedVertices(
        const OpaqueRaceRig* rig,
        std::span<const MorphDeltaView> chargenMorphs,
        const Foundation::FaceMorphVector& faceMorphs);

    [[nodiscard]] std::vector<float> BuildMorphedVerticesFromTri(
        const OpaqueRaceRig* rig,
        const OpaqueTriAsset* chargenTri,
        const Foundation::FaceMorphVector& faceMorphs);

    void SanitizeHeadMorphClearance(
        const OpaqueRaceRig* rig,
        Foundation::FaceMorphVector& ioFaceMorphs,
        std::span<const std::int32_t> allowedEyesPresets = {},
        std::span<const std::int32_t> allowedMouthPresets = {});

    [[nodiscard]] MeshProjectionOutput ProjectDonorMeshesToTargetMorphs(
        const MeshProjectionRequest& request);

    // --- Eye Sampling & Pool Selection ---
    [[nodiscard]] EyeSample AnalyzeEyeMipmaps(
        std::span<const std::uint8_t, 32u * 32u * 4u> rgba32x32,
        const std::array<std::uint8_t, 64u * 64u * 4u>* optionalRgba64x64,
        std::array<float, 2> uvCenterHint = { -1.0f, -1.0f }) noexcept;

    [[nodiscard]] bool IsEyeSampleValid(const EyeSample& sample) noexcept;
    [[nodiscard]] bool IsEyeSampleNormalMortal(const EyeSample& sample) noexcept;
    [[nodiscard]] bool HasEyeSampleEmissive(const EyeSample& sample) noexcept;
    void SetEyeSampleEmissive(EyeSample& sample, bool hasEmissive) noexcept;
    [[nodiscard]] Foundation::RgbColor GetEyeSamplePreviewRgb(const EyeSample& sample) noexcept;
    [[nodiscard]] bool HasEyeSampleAltPupil(const EyeSample& sample) noexcept;

    void DeriveInheritedEyeTarget(
        const EyeSample& donorA,
        const EyeSample* optionalDonorB,
        bool blendBothDonors,
        float eyeClusterDominanceA,
        bool allowSupernaturalEyes,
        float variationRoll,
        std::optional<EyeTarget>& outTarget,
        bool& outInheritedDarkEyes) noexcept;

    [[nodiscard]] std::optional<std::size_t> SelectEyePoolIndex(
        std::span<const EyeSample> poolSamples,
        const std::optional<EyeTarget>& optionalTarget,
        bool allowSupernaturalEyes,
        bool allowDarkEyes,
        bool preferAltPupil,
        std::uint64_t fallbackChoiceSeed,
        std::size_t nativePoolCount = 0) noexcept;

    // --- Age, Voice, Preset, Beast Markings & Chimeras ---
    [[nodiscard]] Foundation::AgeBracket ResolveAgeBracket(
        Foundation::AgeBracket requested,
        Foundation::DonorMode donorMode,
        Foundation::Seed64 seed) noexcept;

    [[nodiscard]] ComplexionSample AnalyzeComplexionMipmap(
        std::span<const std::uint8_t, 32u * 32u * 4u> rgba32x32) noexcept;

    [[nodiscard]] bool IsComplexionNonBaseline(const ComplexionSample& sample) noexcept;

    [[nodiscard]] std::size_t SelectComplexionPoolIndex(
        std::span<const ComplexionSample> detailSamples,
        std::span<const ComplexionSample> normalFallbackSamples,
        Foundation::AgeBracket effectiveBracket,
        std::size_t defaultIndex,
        Foundation::Seed64 seed) noexcept;

    [[nodiscard]] AgeVisualResult ComputeAgeVisuals(
        Foundation::AgeBracket effectiveBracket,
        Foundation::RgbColor baseHairColor,
        bool allowHairSilvering,
        Foundation::Seed64 seed) noexcept;

    [[nodiscard]] std::vector<std::uint32_t> SelectSkyrimVoicePoolCandidates(
        std::uint32_t primarySkyrimRaceLocalId,
        std::uint32_t morphSkyrimRaceLocalId,
        Foundation::Sex sex,
        Foundation::AgeBracket bracket);

    [[nodiscard]] std::size_t PickSeededVoicePoolIndex(
        std::size_t poolSize,
        Foundation::Seed64 seed) noexcept;

    [[nodiscard]] std::string NormalizeRaceKey(std::string_view rawRaceName);
    [[nodiscard]] bool IsHumanoidAnchorRaceKey(std::string_view raceKey) noexcept;

    [[nodiscard]] std::vector<Foundation::FaceMorphVector> SelectAnchorMorphPool(
        std::span<const AnchorMorphCandidate> entries,
        std::string_view raceKey,
        std::string_view morphKey,
        Foundation::Sex sex,
        bool isHumanoidGroup = false);

    [[nodiscard]] bool HasSufficientPresetDiversity(std::size_t presetCount) noexcept;

    [[nodiscard]] std::vector<std::int32_t> ResolveNativeMorphPresetPool(
        std::span<const std::int32_t> ownPresets,
        std::span<const std::int32_t> morphRacePresets,
        std::span<const SharedTriPeerPresets> sharedTriPeers,
        bool targetIsElder = false,
        bool targetIsOrc = false);

    [[nodiscard]] std::vector<std::int32_t> DecodeMpavMorphPresetMask(
        std::uint32_t morphFlagsWord0,
        std::uint32_t extraWord1,
        std::span<const std::int32_t> nativePresets);

    [[nodiscard]] bool IsMpavAllMorphsUnlocked(
        std::uint32_t noseWord0,
        std::uint32_t eyesWord0) noexcept;

    [[nodiscard]] Foundation::RgbColor CompositeSkinTintOverMidGrey(
        Foundation::RgbColor tintRgb,
        float alpha) noexcept;

    [[nodiscard]] std::vector<BeastLayerOutput> SynthesizeBeastLayers(
        RaceGroup targetGroup,
        std::span<const BeastTintAssetInput> targetAssets,
        std::span<const DonorBeastLayerInput> primaryDonorLayers,
        std::span<const DonorBeastLayerInput> secondaryDonorLayers,
        Foundation::Seed64 seed);

    [[nodiscard]] ChimeraLineageKind ClassifyChimeraLineage(
        RaceGroup groupA,
        RaceGroup groupB) noexcept;

    [[nodiscard]] ChimeraResolutionPlan ResolveChimeraPlan(
        RaceGroup groupA,
        RaceGroup groupB,
        BeastChimeraTier requestedTier,
        const TierAvailability& availability,
        Foundation::Seed64 seed,
        std::optional<InheritedParentSlot> forcedRaceParent = std::nullopt) noexcept;

    [[nodiscard]] Float3 ComputeShaderTintFactors(
        Foundation::RgbColor targetColor,
        Foundation::RgbColor sampleColor) noexcept;

    [[nodiscard]] ChimeraPigmentResult SynthesizeChimeraPigment(
        ChimeraLineageKind lineage,
        const DonorPhenotype& humanoidDonor,
        const DonorPhenotype& beastDonor,
        const EyeSample& humanoidEye,
        const EyeSample& beastEye,
        Foundation::RgbColor baseHumanoidSkin,
        Foundation::RgbColor baseHumanoidHair,
        Foundation::Sex targetSex,
        float targetRaceSexHeight,
        bool allowSupernaturalEyes,
        Foundation::Seed64 seed) noexcept;

    // --- Hybrid Skin Stencils & Direct-to-DDS Compositors ---
    [[nodiscard]] SkinStencilPtr LoadSkinStencilFile(const std::filesystem::path& filePath);
    [[nodiscard]] SkinStencilPtr DownsampleSkinStencil2x2(const OpaqueSkinStencil* src);
    [[nodiscard]] SkinMapDimensions GetSkinStencilDimensions(const OpaqueSkinStencil* stencil) noexcept;
    [[nodiscard]] std::uint64_t GetSkinStencilSignature(const OpaqueSkinStencil* stencil) noexcept;

    [[nodiscard]] std::span<const HybridCoatDescriptor> GetHybridCoatCatalog() noexcept;
    [[nodiscard]] const HybridCoatDescriptor* FindHybridCoat(std::string_view id) noexcept;
    [[nodiscard]] std::string_view HybridStencilKindStem(HybridStencilKind kind) noexcept;
    [[nodiscard]] std::string_view BodyUvFamilyStem(BodyUvFamily family) noexcept;
    [[nodiscard]] BodyUvFamily ClassifyBodyMeshUvFamily(
        std::span<const std::uint8_t> nifBytes,
        Foundation::Sex sex);
    [[nodiscard]] float DefaultSubsurfaceKeepForStencil(HybridStencilKind kind) noexcept;

    [[nodiscard]] SkinMapDimensions ResolveHybridSkinDimensions(
        std::uint32_t nativeWidth,
        std::uint32_t nativeHeight,
        std::uint32_t fallbackDim,
        std::uint32_t maxDimensionCap) noexcept;

    [[nodiscard]] Foundation::RgbColor NormalizeKhajiitPelageSrgb(
        Foundation::RgbColor rawBeastSkin,
        std::optional<Foundation::RgbColor> beastSecondary = std::nullopt) noexcept;

    [[nodiscard]] Foundation::RgbColor SanitizeYouthfulHumanHairTint(
        Foundation::RgbColor hairTint,
        Foundation::AgeBracket requestedAgeBracket,
        Foundation::Seed64 seed) noexcept;

    [[nodiscard]] ChimeraHybridCoatPlan PlanChimeraHybridCoat(
        HybridCoatGroup group,
        Foundation::RgbColor targetBeastColor,
        Foundation::RgbColor humanInheritedHair,
        Foundation::RgbColor currentSpecHair,
        Foundation::RgbColor npcSkinColor,
        bool hasBeastTail,
        bool matchTailColor,
        Foundation::Seed64 seed);

    [[nodiscard]] std::optional<Foundation::RgbColor> ResolveChimeraEarHairTint(
        std::string_view hybridSkinId,
        Foundation::RgbColor npcSkinColor,
        bool hasKhajiitTail,
        std::optional<Foundation::RgbColor> tailColor) noexcept;

    [[nodiscard]] bool BuildHybridDiffuseDds(
        std::span<const std::uint8_t> baseDiffuseRgba,
        std::uint32_t width,
        std::uint32_t height,
        const OpaqueSkinStencil* stencil,
        std::string_view coatId,
        std::vector<std::uint8_t>& outDdsBytes,
        const ParallelForSliceFn& parallelFor = {});

    [[nodiscard]] bool BuildHybridTailDiffuseDds(
        std::span<const std::uint8_t> baseTailRgba,
        std::uint32_t width,
        std::uint32_t height,
        std::string_view coatId,
        std::vector<std::uint8_t>& outDdsBytes,
        const ParallelForSliceFn& parallelFor = {});

    [[nodiscard]] bool BuildHybridNormalDds(
        std::span<const std::uint8_t> baseMsnRgba,
        std::uint32_t width,
        std::uint32_t height,
        const OpaqueSkinStencil* stencil,
        std::vector<std::uint8_t>& outDdsBytes,
        const ParallelForSliceFn& parallelFor = {});

    [[nodiscard]] bool BuildHybridSpecularDds(
        std::span<const std::uint8_t> baseSpecRgba,
        std::uint32_t width,
        std::uint32_t height,
        const OpaqueSkinStencil* stencil,
        std::vector<std::uint8_t>& outDdsBytes,
        const ParallelForSliceFn& parallelFor = {});

    [[nodiscard]] bool BuildHybridSubsurfaceDds(
        std::span<const std::uint8_t> baseSubsurfRgba,
        std::uint32_t width,
        std::uint32_t height,
        const OpaqueSkinStencil* stencil,
        float subsurfaceKeep,
        std::vector<std::uint8_t>& outDdsBytes,
        const ParallelForSliceFn& parallelFor = {});

    // --- Mixed-Race Heritage & Runtime Keyword Classification ---
    [[nodiscard]] std::string CanonicalHeritageRaceStem(
        std::string_view rawEditorId,
        std::uint32_t skyrimLocalFormId = 0u);

    [[nodiscard]] MortalMacroGroup ClassifyMortalMacroGroup(
        std::string_view canonicalRaceStem,
        std::string_view rawEditorId = {}) noexcept;

    [[nodiscard]] MortalHeritageSummary ResolveMortalHeritage(
        std::string_view primaryRaceEdid,
        std::uint32_t primarySkyrimLocalId,
        std::string_view secondaryRaceEdid,
        std::uint32_t secondarySkyrimLocalId,
        BeastChimeraTier effectiveChimeraTier);

    [[nodiscard]] std::vector<std::string> GetStandardHeritageKeywordCatalog();

}  // namespace MORWHA::Core
