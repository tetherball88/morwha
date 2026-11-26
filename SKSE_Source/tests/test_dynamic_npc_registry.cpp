#include "TestHarness.h"

#include "morwha/state/ChimeraAccessoryManifest.h"
#include "morwha/state/DynamicNpcRegistry.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

namespace {

    using MORWHA::Foundation::DonorMode;
    using MORWHA::Foundation::FacePartSlot;
    using MORWHA::Foundation::kUnusedFacePartPreset;
    using MORWHA::Foundation::RgbColor;
    using MORWHA::Foundation::Seed64;
    using MORWHA::Foundation::Sex;
    using MORWHA::Foundation::TopologyPool;
    using MORWHA::State::DynamicNpcSpec;
    using MORWHA::State::kUnsetExplicitTintIndex;
    using MORWHA::State::LoadRegistrySafe;
    using MORWHA::State::ParseRegistryJson;
    using MORWHA::State::PluginFormRef;
    using MORWHA::State::SaveRegistryAtomic;
    using MORWHA::State::SerializeRegistryJson;
    using MORWHA::State::TintLayerSpec;
    using MORWHA::State::TintSemanticType;
    using MORWHA::State::ValidateDynamicNpcSpec;

    [[nodiscard]] DynamicNpcSpec MakeValidSpec(std::string slotKey, std::string displayName) {
        DynamicNpcSpec spec{};
        spec.slotKey = std::move(slotKey);
        spec.displayName = std::move(displayName);
        spec.donorMode = DonorMode::TwoDonorOffspring;
        spec.topology = TopologyPool::Vanilla;
        spec.seed = Seed64{ 0xDEADBEEFCAFE1234ull };
        spec.raceRef = PluginFormRef{ "Skyrim.esm", 0x013746u };
        spec.sex = Sex::Female;
        spec.voiceTypeRef = PluginFormRef{ "Skyrim.esm", 0x013AE3u };
        spec.npcClassRef = PluginFormRef{ "Skyrim.esm", 0x01317Eu };
        spec.defaultOutfitRef = PluginFormRef{ "Skyrim.esm", 0x028A72u };
        spec.faceTextureSetRef = PluginFormRef{ "Skyrim.esm", 0x051500u };
        spec.headParts = {
            PluginFormRef{ "Skyrim.esm", 0x05107Eu },
            PluginFormRef{ "Skyrim.esm", 0x05162Au }
        };
        spec.height = 1.015f;
        spec.weight = 42.5f;
        for (std::size_t i = 0; i < spec.faceMorphs.morphs.size(); ++i) {
            spec.faceMorphs.morphs[i] = static_cast<float>(static_cast<int>(i) - 9) * 0.0625f;
        }
        spec.faceMorphs.parts[static_cast<std::size_t>(FacePartSlot::Nose)] = 3;
        spec.faceMorphs.parts[static_cast<std::size_t>(FacePartSlot::Unknown)] = kUnusedFacePartPreset;
        spec.faceMorphs.parts[static_cast<std::size_t>(FacePartSlot::Eyes)] = 5;
        spec.faceMorphs.parts[static_cast<std::size_t>(FacePartSlot::Mouth)] = 2;
        spec.skinColor = RgbColor{ 224, 185, 164 };
        spec.hairColor = RgbColor{ 98, 62, 46 };
        spec.tintLayers = {
            TintLayerSpec{ TintSemanticType::SkinTone, kUnsetExplicitTintIndex, RgbColor{ 224, 185, 164 }, 100 },
            TintLayerSpec{ TintSemanticType::LipColor, 24u, RgbColor{ 140, 62, 68 }, 18 }
        };
        return spec;
    }

}  // namespace

MORWHA_TEST(DynamicNpcRegistry, RoundtripFullSpec) {
    DynamicNpcSpec second = MakeValidSpec("mortal_test_02", "Bjorn \"Iron-Jaw\"\tII");
    second.sex = Sex::Male;
    second.donorMode = DonorMode::TwoDonorOffspring;
    second.hairColorFormRef = PluginFormRef{ "Skyrim.esm", 0x0A0436u };
    second.tintLayers.resize(1);
    second.tintLayers[0].interpolationValue = 0;
    second.donorARef = PluginFormRef{ "Skyrim.esm", 0x000014u };
    second.donorBRef = PluginFormRef{ "Skyrim.esm", 0x01C19Eu };
    second.donorASlotKey = "parent_a_0001";
    second.donorBSlotKey = "parent_b_0002";
    second.secondaryRaceRef = PluginFormRef{ "Skyrim.esm", 0x013745u };
    second.chimeraTier = 2u;
    second.hybridSkinId = "fur_tabby_charcoal";
    second.committed = false;

    const std::vector<DynamicNpcSpec> input = {
        MakeValidSpec("mortal_test_01", "Astrid\\Runa\nLine"),
        std::move(second)
    };

    const std::string json = SerializeRegistryJson(input);
    std::vector<DynamicNpcSpec> parsed;
    std::string err;
    MORWHA_REQUIRE(ParseRegistryJson(json, parsed, &err));
    MORWHA_REQUIRE_EQ(parsed.size(), input.size());
    MORWHA_CHECK(parsed[0] == input[0]);
    MORWHA_CHECK(parsed[1] == input[1]);
}

MORWHA_TEST(DynamicNpcRegistry, ValidationAndMalformedJsonGuards) {
    DynamicNpcSpec bad = MakeValidSpec("", "Valid");
    MORWHA_CHECK(!ValidateDynamicNpcSpec(bad));

    bad = MakeValidSpec(std::string(257u, 'x'), "TooLong");
    MORWHA_CHECK(!ValidateDynamicNpcSpec(bad));

    bad = MakeValidSpec("ok_key", "MissingRace");
    bad.raceRef = PluginFormRef{};
    MORWHA_CHECK(!ValidateDynamicNpcSpec(bad));

    bad = MakeValidSpec("ok_key", "NanMorph");
    bad.faceMorphs.morphs[4] = std::numeric_limits<float>::quiet_NaN();
    MORWHA_CHECK(!ValidateDynamicNpcSpec(bad));

    bad = MakeValidSpec("ok_key", "NegativeOutOfBoundsMorph");
    bad.faceMorphs.morphs[0] = -1.25f;
    MORWHA_CHECK(!ValidateDynamicNpcSpec(bad));

    bad = MakeValidSpec("ok_key", "ExcessiveMorph");
    bad.faceMorphs.morphs[0] = 1.25f;
    MORWHA_CHECK(!ValidateDynamicNpcSpec(bad));

    bad = MakeValidSpec("ok_key", "InvalidChimeraTier");
    bad.chimeraTier = 3u;
    MORWHA_CHECK(!ValidateDynamicNpcSpec(bad));

    bad.faceMorphs.morphs[0] = 3.25f;
    MORWHA_CHECK(!ValidateDynamicNpcSpec(bad));
    {
        std::vector<DynamicNpcSpec> rejectedOut;
        MORWHA_CHECK(!ParseRegistryJson(
            SerializeRegistryJson(std::span<const DynamicNpcSpec>(&bad, 1)), rejectedOut));
    }

    bad = MakeValidSpec("ok_key", "UnusedMorphSlot");
    bad.faceMorphs.morphs[18] = 1.50f;
    MORWHA_CHECK(!ValidateDynamicNpcSpec(bad));

    bad = MakeValidSpec("ok_key", "BadTintAlpha");
    bad.tintLayers[0].interpolationValue = 101;
    MORWHA_CHECK(!ValidateDynamicNpcSpec(bad));

    const auto one = MakeValidSpec("dup_key", "One");
    const std::string validJson = SerializeRegistryJson(std::span<const DynamicNpcSpec>(&one, 1));
    std::vector<DynamicNpcSpec> out = { one };

    MORWHA_CHECK(!ParseRegistryJson(SerializeRegistryJson(std::vector<DynamicNpcSpec>{ one, one }), out));
    MORWHA_CHECK(out.empty());

    MORWHA_CHECK(!ParseRegistryJson("{\"schemaVersion\":99,\"owner\":\"MORWHA\",\"npcs\":[]}", out));
    MORWHA_CHECK(!ParseRegistryJson(validJson.substr(0, validJson.size() / 2), out));
    MORWHA_CHECK(!ParseRegistryJson(validJson + " trailing", out));
}

MORWHA_TEST(DynamicNpcRegistry, AtomicTempSwapAndBackupRecovery) {
    const auto stamp = static_cast<unsigned long long>(
        std::chrono::steady_clock::now().time_since_epoch().count());
    const auto tempRoot = std::filesystem::temp_directory_path() /
                          ("morwha_reg_test_" + std::to_string(stamp));
    std::filesystem::create_directories(tempRoot);
    const auto cleanup = [&]() {
        std::error_code ec;
        std::filesystem::remove_all(tempRoot, ec);
    };

    const auto target = tempRoot / "DynamicNPCRegistry.json";
    auto tmpPath = target;
    tmpPath += ".tmp";
    auto bakPath = target;
    bakPath += ".bak";

    const auto bootLoad = LoadRegistrySafe(target);
    MORWHA_CHECK(bootLoad.ok);
    MORWHA_CHECK(!bootLoad.fileExisted);
    MORWHA_CHECK(!bootLoad.recoveredFromBackup);
    MORWHA_CHECK(bootLoad.specs.empty());

    const std::vector<DynamicNpcSpec> v1 = { MakeValidSpec("slot_v1", "First") };
    const std::vector<DynamicNpcSpec> v2 = {
        MakeValidSpec("slot_v1", "First"),
        MakeValidSpec("slot_v2", "Second")
    };
    MORWHA_REQUIRE(SaveRegistryAtomic(target, v1));
    MORWHA_REQUIRE(SaveRegistryAtomic(target, v2));
    MORWHA_CHECK(!std::filesystem::exists(tmpPath));
    MORWHA_CHECK(std::filesystem::exists(bakPath));

    const auto normalLoad = LoadRegistrySafe(target);
    MORWHA_REQUIRE(normalLoad.ok);
    MORWHA_CHECK(!normalLoad.recoveredFromBackup);
    MORWHA_REQUIRE_EQ(normalLoad.specs.size(), 2u);
    MORWHA_CHECK(normalLoad.specs == v2);

    {
        std::ofstream corrupt(target, std::ios::binary | std::ios::trunc);
        corrupt << "{ corrupt_json";
    }
    const auto bakLoad = LoadRegistrySafe(target);
    MORWHA_REQUIRE(bakLoad.ok);
    MORWHA_CHECK(bakLoad.fileExisted);
    MORWHA_CHECK(bakLoad.recoveredFromBackup);
    MORWHA_REQUIRE_EQ(bakLoad.specs.size(), 1u);
    MORWHA_CHECK(bakLoad.specs == v1);

    {
        std::ofstream corruptBak(bakPath, std::ios::binary | std::ios::trunc);
        corruptBak << "not json";
    }
    const auto failedLoad = LoadRegistrySafe(target);
    MORWHA_CHECK(!failedLoad.ok);
    MORWHA_CHECK(failedLoad.fileExisted);
    MORWHA_CHECK(failedLoad.specs.empty());

    cleanup();
}

MORWHA_TEST(DynamicNpcRegistry, Tier2AccessoryFieldsAndManifestParsing) {
    using MORWHA::State::BeastTailKind;
    using MORWHA::State::BeastTeethKind;
    using MORWHA::State::ChimeraAccessoryManifest;
    using MORWHA::State::ChimeraAccessorySexFilter;
    using MORWHA::State::ChimeraAccessorySlotKind;
    using MORWHA::State::ChimeraManifestLineage;
    using MORWHA::State::kMaxSkinAddonsPerNpc;
    using MORWHA::State::ParseChimeraAccessoryManifestJson;

    DynamicNpcSpec tier2Spec = MakeValidSpec("mortal_chimera_t2", "Kharjo-Son");
    tier2Spec.beastTailKind = BeastTailKind::Khajiit;
    tier2Spec.beastTeethKind = BeastTeethKind::Sharp;
    tier2Spec.tailColor = RgbColor{ 185, 112, 48 };
    tier2Spec.hornColor = RgbColor{ 140, 190, 90 };
    tier2Spec.skinAddonRefs = { PluginFormRef{ "PraedysHorns.esp", 0x000805u } };
    tier2Spec.hybridSkinId = "fur_tabby_tawny";

    std::string err;
    MORWHA_REQUIRE(ValidateDynamicNpcSpec(tier2Spec, &err));

    const DynamicNpcSpec defaultSpec = MakeValidSpec("mortal_vanilla_default", "Lydia-Daughter");
    const std::vector<DynamicNpcSpec> specs{ tier2Spec, defaultSpec };
    const std::string json = SerializeRegistryJson(specs);
    std::vector<DynamicNpcSpec> parsed;
    MORWHA_REQUIRE(ParseRegistryJson(json, parsed, &err));
    MORWHA_REQUIRE_EQ(parsed.size(), std::size_t{ 2u });
    MORWHA_CHECK(parsed[0] == tier2Spec);
    MORWHA_CHECK(parsed[1] == defaultSpec);

    DynamicNpcSpec badTail = tier2Spec;
    badTail.beastTailKind = static_cast<BeastTailKind>(99);
    MORWHA_CHECK(!ValidateDynamicNpcSpec(badTail, &err));

    DynamicNpcSpec tooManyAddons = tier2Spec;
    tooManyAddons.skinAddonRefs.assign(kMaxSkinAddonsPerNpc + 1u, PluginFormRef{ "Skyrim.esm", 0x069CE2u });
    MORWHA_CHECK(!ValidateDynamicNpcSpec(tooManyAddons, &err));

    DynamicNpcSpec badHybridId = tier2Spec;
    badHybridId.hybridSkinId = "../evil";
    MORWHA_CHECK(!ValidateDynamicNpcSpec(badHybridId, &err));

    constexpr std::string_view kManifestJson = R"({
      "schemaVersion": 1,
      "plugin": "ChooeyDintEarsEdit.esp",
      "requiresSkee64": true,
      "entries": [
        {
          "lineage": "HumanoidKhajiit",
          "slotKind": "HeadPart",
          "sex": "Female",
          "localId": "0x000838"
        },
        {
          "lineage": "HumanoidArgonian",
          "slotKind": "SkinArmorAddon",
          "sex": "Any",
          "localId": 2053
        }
      ]
    })";
    ChimeraAccessoryManifest manifest{};
    MORWHA_REQUIRE(ParseChimeraAccessoryManifestJson(kManifestJson, manifest, &err));
    MORWHA_CHECK_EQ(manifest.plugin, std::string_view("ChooeyDintEarsEdit.esp"));
    MORWHA_CHECK(manifest.requiresSkee64);
    MORWHA_REQUIRE_EQ(manifest.entries.size(), std::size_t{ 2u });
    MORWHA_CHECK(manifest.entries[0].lineage == ChimeraManifestLineage::HumanoidKhajiit);
    MORWHA_CHECK(manifest.entries[0].slotKind == ChimeraAccessorySlotKind::HeadPart);
    MORWHA_CHECK(manifest.entries[0].sex == ChimeraAccessorySexFilter::Female);
    MORWHA_CHECK_EQ(manifest.entries[0].localId, 0x000838u);
    MORWHA_CHECK(manifest.entries[1].lineage == ChimeraManifestLineage::HumanoidArgonian);
    MORWHA_CHECK(manifest.entries[1].slotKind == ChimeraAccessorySlotKind::SkinArmorAddon);
    MORWHA_CHECK(manifest.entries[1].sex == ChimeraAccessorySexFilter::Any);
    MORWHA_CHECK_EQ(manifest.entries[1].localId, 2053u);

    ChimeraAccessoryManifest badManifest{};
    MORWHA_CHECK(!ParseChimeraAccessoryManifestJson(R"({"schemaVersion":99,"plugin":"x.esp","entries":[]})", badManifest, &err));
    MORWHA_CHECK(!ParseChimeraAccessoryManifestJson(R"({"schemaVersion":1,"plugin":"","entries":[]})", badManifest, &err));
    MORWHA_CHECK(!ParseChimeraAccessoryManifestJson(R"({"schemaVersion":1,"plugin":"x.esp"})", badManifest, &err));
}
