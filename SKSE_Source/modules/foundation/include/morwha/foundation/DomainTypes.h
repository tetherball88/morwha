#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace MORWHA::Foundation {

    enum class Sex : std::uint8_t {
        Male = 0,
        Female = 1
    };

    enum class DonorMode : std::uint8_t {
        ZeroDonorProcedural = 0,
        OneDonorRelative = 1,
        TwoDonorOffspring = 2
    };

    enum class TopologyPool : std::uint8_t {
        Vanilla = 0,
        HighPolyHead = 1,
        CustomFallback = 2
    };

    enum class AgeBracket : std::uint8_t {
        Auto = 0,
        YoungAdult = 1,
        Mature = 2,
        MiddleAged = 3,
        Elder = 4
    };

    enum class FaceCluster : std::uint8_t {
        Eyes = 0,
        Nose = 1,
        Brows = 2,
        Mouth = 3,
        Cheeks = 4,
        JawChin = 5,
        kCount = 6
    };

    // Matches RE::TESNPC::FaceData::Parts memory layout (TESNPC.h).
    enum class FacePartSlot : std::uint8_t {
        Nose = 0,
        Unknown = 1,
        Eyes = 2,
        Mouth = 3,
        kCount = 4
    };

    inline constexpr std::int32_t kUnusedFacePartPreset = 0x7F7FFFFF;
    inline constexpr std::size_t kVanillaFaceMorphCount = 19;

    struct RgbColor {
        std::uint8_t r{ 0 };
        std::uint8_t g{ 0 };
        std::uint8_t b{ 0 };

        constexpr bool operator==(const RgbColor&) const noexcept = default;
    };

    struct FaceMorphVector {
        std::array<float, kVanillaFaceMorphCount> morphs{};
        std::array<std::int32_t, static_cast<std::size_t>(FacePartSlot::kCount)> parts{
            kUnusedFacePartPreset,
            kUnusedFacePartPreset,
            kUnusedFacePartPreset,
            kUnusedFacePartPreset
        };

        constexpr bool operator==(const FaceMorphVector&) const noexcept = default;
    };

}  // namespace MORWHA::Foundation
