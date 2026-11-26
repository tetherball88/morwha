#pragma once

#include "morwha/foundation/DomainTypes.h"
#include "morwha/foundation/StrongIds.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace MORWHA::State {

    struct PluginFormRef {
        std::string plugin{};
        std::uint32_t localId{ 0 };

        [[nodiscard]] bool IsEmpty() const noexcept {
            return plugin.empty() && localId == 0;
        }

        bool operator==(const PluginFormRef&) const noexcept = default;
    };

    // Mirrors RE::TESRace::FaceRelatedData::TintAsset::TintLayer::SkinTone.
    enum class TintSemanticType : std::uint8_t {
        None = 0,
        LipColor = 1,
        CheekColor = 2,
        Eyeliner = 3,
        EyeSocketUpper = 4,
        EyeSocketLower = 5,
        SkinTone = 6,
        Paint = 7,
        LaughLines = 8,
        CheekColorLower = 9,
        Nose = 10,
        Chin = 11,
        Neck = 12,
        Forehead = 13,
        Dirt = 14
    };

    inline constexpr std::uint16_t kUnsetExplicitTintIndex = 0xFFFFu;

    struct TintLayerSpec {
        TintSemanticType semanticType{ TintSemanticType::None };
        std::uint16_t explicitTintIndex{ kUnsetExplicitTintIndex };
        Foundation::RgbColor color{};
        std::uint16_t interpolationValue{ 100 };

        bool operator==(const TintLayerSpec&) const noexcept = default;
    };

    enum class BeastTailKind : std::uint8_t {
        None = 0,
        Khajiit = 1,
        Argonian = 2
    };

    enum class BeastTeethKind : std::uint8_t {
        None = 0,
        Fangs = 1,
        Sharp = 2
    };

    inline constexpr std::size_t kNpcSkillCount = 18u;

    struct NpcAiSpec {
        std::uint8_t aggression{ 0 };   // 0=Unaggressive..3=Frenzied
        std::uint8_t confidence{ 2 };   // 0=Cowardly..4=Foolhardy
        std::uint8_t assistance{ 1 };   // 0=HelpsNobody..2=HelpsFriendsAndAllies
        std::uint8_t morality{ 3 };     // 0=AnyCrime..3=NoCrime
        bool isProtected{ false };
        bool isEssential{ false };

        [[nodiscard]] constexpr bool IsDefaultCitizen() const noexcept {
            return aggression == 0u && confidence == 2u && assistance == 1u &&
                   morality == 3u && !isProtected && !isEssential;
        }

        bool operator==(const NpcAiSpec&) const noexcept = default;
    };

    struct NpcStatsSpec {
        std::uint16_t level{ 10 };
        std::uint16_t calcLevelMin{ 1 };
        std::uint16_t calcLevelMax{ 50 };
        bool autoCalcStats{ true };
        bool pcLevelMult{ false };
        std::uint16_t health{ 50 };
        std::uint16_t magicka{ 50 };
        std::uint16_t stamina{ 50 };
        std::array<std::uint8_t, kNpcSkillCount> skills{
            15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15
        };

        [[nodiscard]] constexpr bool IsDefaultCitizen() const noexcept {
            for (const std::uint8_t s : skills) {
                if (s != 15u) {
                    return false;
                }
            }
            return level == 10u && calcLevelMin == 1u && calcLevelMax == 50u &&
                   autoCalcStats && !pcLevelMult &&
                   health == 50u && magicka == 50u && stamina == 50u;
        }

        bool operator==(const NpcStatsSpec&) const noexcept = default;
    };

    struct FactionRankSpec {
        PluginFormRef factionRef{};
        std::int8_t rank{ 0 };

        bool operator==(const FactionRankSpec&) const noexcept = default;
    };

    struct DynamicNpcSpec {
        std::string slotKey{};
        std::string displayName{};
        Foundation::DonorMode donorMode{ Foundation::DonorMode::ZeroDonorProcedural };
        Foundation::TopologyPool topology{ Foundation::TopologyPool::Vanilla };
        Foundation::Seed64 seed{};
        bool committed{ true };
        Foundation::AgeBracket ageBracket{ Foundation::AgeBracket::YoungAdult };
        bool forcePersist{ true };

        PluginFormRef raceRef{};
        Foundation::Sex sex{ Foundation::Sex::Female };
        PluginFormRef templateNpcRef{};
        PluginFormRef voiceTypeRef{};
        PluginFormRef npcClassRef{};
        PluginFormRef combatStyleRef{};
        PluginFormRef defaultOutfitRef{};
        PluginFormRef sleepOutfitRef{};
        PluginFormRef crimeFactionRef{};
        PluginFormRef faceTextureSetRef{};
        PluginFormRef hairColorFormRef{};
        std::vector<PluginFormRef> headParts{};

        NpcAiSpec ai{};
        NpcStatsSpec stats{};
        std::vector<PluginFormRef> perkRefs{};
        std::vector<PluginFormRef> spellRefs{};
        std::vector<FactionRankSpec> factions{};

        float height{ 1.0f };
        float weight{ 50.0f };
        Foundation::FaceMorphVector faceMorphs{};
        Foundation::RgbColor skinColor{ 210, 175, 155 };
        Foundation::RgbColor hairColor{ 72, 50, 36 };
        std::vector<TintLayerSpec> tintLayers{};
        BeastTailKind beastTailKind{ BeastTailKind::None };
        BeastTeethKind beastTeethKind{ BeastTeethKind::None };
        std::vector<PluginFormRef> skinAddonRefs{};
        std::string hybridSkinId{};
        std::optional<Foundation::RgbColor> tailColor{};
        std::optional<Foundation::RgbColor> hornColor{};
        PluginFormRef donorARef{};
        PluginFormRef donorBRef{};
        std::string donorASlotKey{};
        std::string donorBSlotKey{};
        PluginFormRef secondaryRaceRef{};
        std::uint8_t chimeraTier{ 0 };

        bool operator==(const DynamicNpcSpec&) const noexcept = default;
    };

    struct SpawnedMortalLink {
        Foundation::FormRawId actorRefId{};
        std::uint32_t dpfLocalId{ 0 };
        std::string slotKey{};
        Foundation::DonorMode donorMode{ Foundation::DonorMode::ZeroDonorProcedural };
        Foundation::Seed64 seed{};
        Foundation::FormRawId donorARefId{};
        Foundation::FormRawId donorBRefId{};

        bool operator==(const SpawnedMortalLink&) const noexcept = default;
    };

}  // namespace MORWHA::State
