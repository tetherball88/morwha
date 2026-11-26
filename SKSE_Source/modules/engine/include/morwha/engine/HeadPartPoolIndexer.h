#pragma once

#include "morwha/foundation/DomainTypes.h"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace RE {
    class BGSHeadPart;
    class TESNPC;
    class TESRace;
}

namespace MORWHA::Engine {

    struct RaceSexHeadPartPool {
        RE::BGSHeadPart* primaryFace{ nullptr };
        RE::BGSHeadPart* primaryMouth{ nullptr };
        std::vector<RE::BGSHeadPart*> faces{};
        std::vector<RE::BGSHeadPart*> mouths{};
        std::vector<RE::BGSHeadPart*> eyes{};
        std::vector<RE::BGSHeadPart*> hairs{};
        std::vector<RE::BGSHeadPart*> eyebrows{};
        std::vector<RE::BGSHeadPart*> facialHairs{};
        std::vector<RE::BGSHeadPart*> scars{};
    };

    /// Indexes loaded BGSHeadPart records by race, sex, and vertex topology (Vanilla vs HighPolyHead),
    /// while quarantining incompatible custom head frameworks (UBE, COtR, CITRUS) and gating SMP hair.
    class HeadPartPoolIndexer {
    public:
        static HeadPartPoolIndexer& GetSingleton() noexcept;

        [[nodiscard]] static bool ContainsCaseInsensitive(
            std::string_view haystack,
            std::string_view needle) noexcept;

        [[nodiscard]] static bool EqualsCaseInsensitive(
            std::string_view a,
            std::string_view b) noexcept;

        [[nodiscard]] static Foundation::TopologyPool ClassifyHeadPart(
            const RE::BGSHeadPart* hp) noexcept;

        [[nodiscard]] static bool IsSmpHairPart(const RE::BGSHeadPart* hp) noexcept;

        [[nodiscard]] static bool IsPartCompatibleWithTopology(
            const RE::BGSHeadPart* hp,
            Foundation::TopologyPool topology,
            bool smpLoaded) noexcept;

        [[nodiscard]] static bool HaveCompatibleEyeGeometry(
            const RE::BGSHeadPart* a,
            const RE::BGSHeadPart* b) noexcept;

        [[nodiscard]] static bool HaveCompatibleBrowGeometry(
            const RE::BGSHeadPart* a,
            const RE::BGSHeadPart* b) noexcept;

        [[nodiscard]] static bool IsAuthoritativeBaseOrHphPart(
            const RE::BGSHeadPart* hp) noexcept;

        void BuildIndex(bool smpLoaded);

        [[nodiscard]] bool IsBuilt() const noexcept { return built_; }
        [[nodiscard]] bool IsHighPolyHeadAvailable() const noexcept { return highPolyHeadAvailable_; }

        [[nodiscard]] Foundation::TopologyPool SelectPreferredTopology(
            const RE::TESRace* race,
            Foundation::Sex sex) const noexcept;

        [[nodiscard]] const RaceSexHeadPartPool* GetPool(
            const RE::TESRace* race,
            Foundation::Sex sex,
            Foundation::TopologyPool topology) const noexcept;

        [[nodiscard]] bool IsPartInIndexedPool(
            const RE::BGSHeadPart* hp,
            const RE::TESRace* race,
            Foundation::Sex sex,
            Foundation::TopologyPool topology) const noexcept;

        [[nodiscard]] bool IsCompatibleIndexedEyeForRace(
            const RE::BGSHeadPart* eyePart,
            const RE::TESRace* targetRace,
            Foundation::Sex sex,
            Foundation::TopologyPool topology) const noexcept;

        [[nodiscard]] bool IsCompatibleIndexedBrowForRace(
            const RE::BGSHeadPart* browPart,
            const RE::TESRace* targetRace,
            Foundation::Sex sex,
            Foundation::TopologyPool topology) const noexcept;

        [[nodiscard]] std::vector<std::int32_t> GetNativeMorphPresets(
            const RE::TESRace* race,
            Foundation::Sex sex,
            std::size_t slot) const;

        [[nodiscard]] std::vector<std::int32_t> GetSharedTriMorphPresets(
            const RE::TESRace* race,
            Foundation::Sex sex,
            std::size_t slot) const;

        [[nodiscard]] bool SharesHumanoidChargenTri(const RE::TESRace* race) const noexcept;

        [[nodiscard]] std::vector<RE::TESNPC*> GatherCompatiblePeerPresets(
            const RE::TESRace* race,
            Foundation::Sex sex) const;

    private:
        struct DualTopologyPools {
            const RE::TESRace* race{ nullptr };
            RaceSexHeadPartPool vanilla{};
            RaceSexHeadPartPool highPolyHead{};
        };

        HeadPartPoolIndexer() = default;

        [[nodiscard]] static std::uint64_t MakeRaceSexKey(
            const RE::TESRace* race,
            Foundation::Sex sex) noexcept;

        std::unordered_map<std::uint64_t, DualTopologyPools> pools_{};
        std::unordered_map<std::uint64_t, std::array<std::vector<std::int32_t>, 4>> nativeMorphPresets_{};
        std::vector<std::string> humanoidChargenTris_{};
        bool built_{ false };
        bool highPolyHeadAvailable_{ false };
    };

}  // namespace MORWHA::Engine
