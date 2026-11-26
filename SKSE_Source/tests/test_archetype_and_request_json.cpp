#include "TestHarness.h"

#include "morwha/state/DynamicNpcRegistry.h"
#include "morwha/state/MortalRequestJson.h"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace {

    using MORWHA::Foundation::AgeBracket;
    using MORWHA::Foundation::DonorMode;
    using MORWHA::Foundation::FacePartSlot;
    using MORWHA::Foundation::kUnusedFacePartPreset;
    using MORWHA::Foundation::Seed64;
    using MORWHA::Foundation::Sex;
    using MORWHA::State::DynamicNpcSpec;
    using MORWHA::State::FactionRankSpec;
    using MORWHA::State::JsonSpawnRequest;
    using MORWHA::State::JsonUpdateRequest;
    using MORWHA::State::ParseJsonSpawnRequest;
    using MORWHA::State::ParseJsonUpdateRequest;
    using MORWHA::State::ParseRegistryJson;
    using MORWHA::State::PluginFormRef;
    using MORWHA::State::SerializeRegistryJson;
    using MORWHA::State::ValidateDynamicNpcSpec;

}  // namespace

MORWHA_TEST(DynamicNpcRegistry, ExtendedArchetypeAiAndStatsRoundtrip) {
    DynamicNpcSpec spec{};
    spec.slotKey = "maras_adult_child_01";
    spec.displayName = "Sigrid Battle-Born";
    spec.donorMode = DonorMode::TwoDonorOffspring;
    spec.seed = Seed64{ 0xABCDEF0123456789ull };
    spec.ageBracket = AgeBracket::Mature;
    spec.forcePersist = false;
    spec.raceRef = PluginFormRef{ "Skyrim.esm", 0x013746u };
    spec.sex = Sex::Female;
    spec.voiceTypeRef = PluginFormRef{ "Skyrim.esm", 0x013AE3u };
    spec.npcClassRef = PluginFormRef{ "Skyrim.esm", 0x013176u };
    spec.combatStyleRef = PluginFormRef{ "Skyrim.esm", 0x03BE1Cu };
    spec.defaultOutfitRef = PluginFormRef{ "Skyrim.esm", 0x028A72u };
    spec.sleepOutfitRef = PluginFormRef{ "Skyrim.esm", 0x01697Du };
    spec.crimeFactionRef = PluginFormRef{ "Skyrim.esm", 0x0267EAu };
    spec.faceMorphs.parts[static_cast<std::size_t>(FacePartSlot::Unknown)] = kUnusedFacePartPreset;
    spec.ai = { .aggression = 1, .confidence = 4, .assistance = 2, .morality = 3, .isProtected = true, .isEssential = false };
    spec.stats.level = 25;
    spec.stats.autoCalcStats = false;
    spec.stats.health = 240;
    spec.stats.stamina = 190;
    spec.stats.skills[0] = 75;
    spec.stats.skills[5] = 68;
    spec.perkRefs = { PluginFormRef{ "Skyrim.esm", 0x0BABE4u } };
    spec.spellRefs = { PluginFormRef{ "Skyrim.esm", 0x012FCCu } };
    spec.factions = { FactionRankSpec{ PluginFormRef{ "Skyrim.esm", 0x05C84Du }, -1 } };

    std::string err;
    MORWHA_REQUIRE(ValidateDynamicNpcSpec(spec, &err));
    const std::string json = SerializeRegistryJson(std::span<const DynamicNpcSpec>(&spec, 1));
    std::vector<DynamicNpcSpec> out;
    MORWHA_REQUIRE(ParseRegistryJson(json, out, &err));
    MORWHA_REQUIRE_EQ(out.size(), std::size_t{ 1u });
    MORWHA_CHECK(out[0] == spec);
}

MORWHA_TEST(MortalRequestJson, ParsesSpawnAndUpdateRequests) {
    constexpr std::string_view kSpawnJson = R"({
      "name": "Erik the Younger",
      "sex": "Male",
      "ageBracket": "MiddleAged",
      "forcePersist": false,
      "donorA": "0x0001A69A",
      "donorB": "Skyrim.esm|0x01348C",
      "voiceType": "Skyrim.esm|0x013AD8",
      "archetype": { "npcClass": "0x00013176", "sleepOutfit": null },
      "ai": { "confidence": 4, "isProtected": true },
      "stats": {
        "level": 30,
        "autoCalcStats": false,
        "skills": { "oneHanded": 80, "archery": 65 }
      },
      "perks": ["Skyrim.esm|0x0BABE4"],
      "factions": [{ "faction": "Skyrim.esm|0x05C84D", "rank": -1 }]
    })";

    JsonSpawnRequest spawnReq{};
    std::string err;
    MORWHA_REQUIRE(ParseJsonSpawnRequest(kSpawnJson, spawnReq, &err));
    MORWHA_CHECK(spawnReq.donorMode == DonorMode::TwoDonorOffspring);
    MORWHA_CHECK_EQ(spawnReq.sex, 0);
    MORWHA_CHECK(spawnReq.ageBracket == AgeBracket::MiddleAged);
    MORWHA_CHECK(!spawnReq.forcePersist);
    MORWHA_CHECK_EQ(spawnReq.donorA.runtimeId.raw(), 0x0001A69Au);
    MORWHA_CHECK_EQ(spawnReq.donorB.pluginRef.localId, 0x01348Cu);
    MORWHA_CHECK_EQ(spawnReq.archetype.voiceType.pluginRef.localId, 0x013AD8u);
    MORWHA_CHECK_EQ(spawnReq.archetype.npcClass.runtimeId.raw(), 0x00013176u);
    MORWHA_CHECK(spawnReq.archetype.sleepOutfit.clear);
    MORWHA_CHECK_EQ(spawnReq.ai.confidence.value_or(0u), 4u);
    MORWHA_CHECK(spawnReq.ai.isProtected.value_or(false));
    MORWHA_CHECK_EQ(spawnReq.stats.level.value_or(0u), std::uint16_t{ 30u });
    MORWHA_CHECK_EQ(spawnReq.stats.skills[0].value_or(0u), std::uint8_t{ 80u });
    MORWHA_CHECK_EQ(spawnReq.stats.skills[2].value_or(0u), std::uint8_t{ 65u });
    MORWHA_REQUIRE_EQ(spawnReq.stats.perks.size(), std::size_t{ 1u });
    MORWHA_REQUIRE_EQ(spawnReq.stats.factions.size(), std::size_t{ 1u });
    MORWHA_CHECK_EQ(spawnReq.stats.factions[0].rank, std::int8_t{ -1 });

    JsonUpdateRequest updReq{};
    MORWHA_REQUIRE(ParseJsonUpdateRequest(
        R"({"ageBracket":"Elder","refreshOutfit":true,"voiceType":null,"perks":[]})", updReq, &err));
    MORWHA_CHECK(updReq.ageBracket == AgeBracket::Elder);
    MORWHA_CHECK(updReq.refreshOutfit);
    MORWHA_CHECK(updReq.archetype.voiceType.clear);
    MORWHA_CHECK(updReq.stats.replacePerks && updReq.stats.perks.empty());
}
