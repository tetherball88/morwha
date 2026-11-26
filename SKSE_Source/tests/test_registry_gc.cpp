#include "TestHarness.h"

#include "morwha/state/CoSaveCodec.h"
#include "morwha/state/DynamicNpcRegistry.h"
#include "morwha/state/RegistryGarbageCollector.h"

#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_set>
#include <vector>

namespace {

    using MORWHA::Foundation::DonorMode;
    using MORWHA::Foundation::FormRawId;
    using MORWHA::Foundation::Seed64;
    using MORWHA::State::DynamicNpcSpec;
    using MORWHA::State::EncodeSpawnedLinks;
    using MORWHA::State::ExtractSlotKeysFromSkseBytes;
    using MORWHA::State::kCoSaveFormatVersion;
    using MORWHA::State::kLinksChunkId;
    using MORWHA::State::kMorwhaPluginId;
    using MORWHA::State::kSkseCoSaveFormatVersion;
    using MORWHA::State::kSkseCoSaveSignature;
    using MORWHA::State::MarkCommittedSpecsForLinks;
    using MORWHA::State::ParseRegistryJson;
    using MORWHA::State::PlanRegistryGarbageCollection;
    using MORWHA::State::PluginFormRef;
    using MORWHA::State::ScanSaveDirectoriesForSlotKeys;
    using MORWHA::State::SpawnedMortalLink;

    void AppendU32(std::vector<std::uint8_t>& buf, std::uint32_t v) {
        const auto* p = reinterpret_cast<const std::uint8_t*>(&v);
        buf.insert(buf.end(), p, p + sizeof(v));
    }

    [[nodiscard]] std::vector<std::uint8_t> BuildMockSkseFile(
        std::span<const SpawnedMortalLink> morwhaLinks,
        bool includeMorwhaBlock = true) {
        std::vector<std::uint8_t> out;
        const std::uint32_t numPlugins = includeMorwhaBlock ? 2u : 1u;
        AppendU32(out, kSkseCoSaveSignature);
        AppendU32(out, kSkseCoSaveFormatVersion);
        AppendU32(out, 0x02020060u);
        AppendU32(out, 0x01064920u);
        AppendU32(out, numPlugins);

        // Plugin 0: dummy foreign plugin ('EEKS') to verify seek-skipping.
        AppendU32(out, 0x534B4545u);
        AppendU32(out, 1u);
        AppendU32(out, 16u);
        AppendU32(out, 0x44415441u);
        AppendU32(out, 1u);
        AppendU32(out, 4u);
        AppendU32(out, 0xDEADBEEFu);

        if (includeMorwhaBlock) {
            const auto linksBytes = EncodeSpawnedLinks(morwhaLinks);
            const auto chunkTotal = static_cast<std::uint32_t>(12u + linksBytes.size());
            AppendU32(out, kMorwhaPluginId);
            AppendU32(out, 1u);
            AppendU32(out, chunkTotal);
            AppendU32(out, kLinksChunkId);
            AppendU32(out, kCoSaveFormatVersion);
            AppendU32(out, static_cast<std::uint32_t>(linksBytes.size()));
            out.insert(out.end(), linksBytes.begin(), linksBytes.end());
        }
        return out;
    }

    [[nodiscard]] DynamicNpcSpec MakeMinimalSpec(
        std::string slotKey,
        bool committed,
        std::string donorA = {},
        std::string donorB = {}) {
        DynamicNpcSpec s{};
        s.slotKey = std::move(slotKey);
        s.displayName = s.slotKey;
        s.committed = committed;
        s.raceRef = PluginFormRef{ "Skyrim.esm", 0x013746u };
        s.donorASlotKey = std::move(donorA);
        s.donorBSlotKey = std::move(donorB);
        return s;
    }

    void WriteBinaryFile(const std::filesystem::path& p, std::span<const std::uint8_t> data) {
        std::ofstream out(p, std::ios::binary | std::ios::trunc);
        if (!data.empty()) {
            out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
        }
    }

}  // namespace

MORWHA_TEST(RegistryGarbageCollector, LegacyOmittedCommittedDefaultsTrueAndRejectsNonBool) {
    DynamicNpcSpec legacy = MakeMinimalSpec("legacy_slot_01", true);
    std::string json = MORWHA::State::SerializeRegistryJson(std::span<const DynamicNpcSpec>(&legacy, 1));
    const auto pos = json.find(",\n      \"committed\": true");
    MORWHA_REQUIRE(pos != std::string::npos);
    json.erase(pos, std::string_view(",\n      \"committed\": true").size());

    std::vector<DynamicNpcSpec> parsed;
    MORWHA_REQUIRE(ParseRegistryJson(json, parsed));
    MORWHA_REQUIRE_EQ(parsed.size(), 1u);
    MORWHA_CHECK(parsed[0].committed);

    std::string badJson = MORWHA::State::SerializeRegistryJson(std::span<const DynamicNpcSpec>(&legacy, 1));
    const auto boolPos = badJson.find("\"committed\": true");
    MORWHA_REQUIRE(boolPos != std::string::npos);
    badJson.replace(boolPos, std::string_view("\"committed\": true").size(), "\"committed\": 1");
    MORWHA_CHECK(!ParseRegistryJson(badJson, parsed));
}

MORWHA_TEST(RegistryGarbageCollector, SkseCoSaveBinaryParserAndSaveDirScan) {
    const std::vector<SpawnedMortalLink> save1Links = {
        SpawnedMortalLink{ .actorRefId = FormRawId{ 0xFF001001u }, .dpfLocalId = 0x801u, .slotKey = "child_alive_01", .seed = Seed64{ 1ull } },
        SpawnedMortalLink{ .actorRefId = FormRawId{ 0xFF001002u }, .dpfLocalId = 0x802u, .slotKey = "rel_alive_02", .seed = Seed64{ 2ull } },
    };
    const auto validSkse = BuildMockSkseFile(save1Links, true);
    const auto memRes = ExtractSlotKeysFromSkseBytes(validSkse);
    MORWHA_REQUIRE(memRes.ok);
    MORWHA_CHECK(memRes.hasMorwhaPlugin);
    MORWHA_CHECK_EQ(memRes.slotKeys.size(), 2u);
    MORWHA_CHECK(memRes.slotKeys.contains("child_alive_01"));
    MORWHA_CHECK(memRes.slotKeys.contains("rel_alive_02"));

    auto truncated = validSkse;
    truncated.pop_back();
    MORWHA_CHECK(!ExtractSlotKeysFromSkseBytes(truncated).ok);

    const auto stamp = static_cast<unsigned long long>(
        std::chrono::steady_clock::now().time_since_epoch().count());
    const auto tempDir = std::filesystem::temp_directory_path() / ("morwha_gc_test_" + std::to_string(stamp));
    std::filesystem::create_directories(tempDir);

    // Save1 has companion .ess -> scanned.
    WriteBinaryFile(tempDir / "Save1.ess", std::array<std::uint8_t, 4>{ 1, 2, 3, 4 });
    WriteBinaryFile(tempDir / "Save1.skse", validSkse);

    // Save2 has no MORWHA block -> scanned cleanly with 0 MORWHA keys.
    WriteBinaryFile(tempDir / "Save2.ess", std::array<std::uint8_t, 4>{ 1, 2, 3, 4 });
    WriteBinaryFile(tempDir / "Save2.skse", BuildMockSkseFile({}, false));

    // DeletedSave has orphaned .skse without .ess (Skyrim Load-menu delete) -> ignored!
    const std::vector<SpawnedMortalLink> deletedLinks = {
        SpawnedMortalLink{ .actorRefId = FormRawId{ 0xFF009999u }, .dpfLocalId = 0x899u, .slotKey = "deleted_save_npc", .seed = Seed64{ 9ull } }
    };
    WriteBinaryFile(tempDir / "DeletedSave.skse", BuildMockSkseFile(deletedLinks, true));

    const std::array<std::filesystem::path, 2> dirs{ tempDir, tempDir };
    const auto scan = ScanSaveDirectoriesForSlotKeys(dirs, /*requireCompanionEss=*/true);
    MORWHA_REQUIRE(scan.ok);
    MORWHA_CHECK(scan.canPruneCommitted);
    MORWHA_CHECK_EQ(scan.existingDirectoryCount, 1u);
    MORWHA_CHECK_EQ(scan.scannedCoSaveCount, 2u);
    MORWHA_CHECK_EQ(scan.skippedOrphanCoSaveCount, 1u);
    MORWHA_CHECK_EQ(scan.matchedMorwhaSaveCount, 1u);
    MORWHA_CHECK(scan.referencedSlotKeys.contains("child_alive_01"));
    MORWHA_CHECK(!scan.referencedSlotKeys.contains("deleted_save_npc"));

    // Corrupt paired .skse fails closed (canPruneCommitted = false).
    WriteBinaryFile(tempDir / "Corrupt.ess", std::array<std::uint8_t, 4>{ 1, 2, 3, 4 });
    WriteBinaryFile(tempDir / "Corrupt.skse", truncated);
    const auto badScan = ScanSaveDirectoriesForSlotKeys(dirs, /*requireCompanionEss=*/true);
    MORWHA_CHECK(!badScan.ok);
    MORWHA_CHECK(!badScan.canPruneCommitted);

    std::error_code ec;
    std::filesystem::remove_all(tempDir, ec);
}

MORWHA_TEST(RegistryGarbageCollector, CommitPromotionAndTransitiveLineageRetention) {
    std::vector<DynamicNpcSpec> specs = {
        MakeMinimalSpec("grandparent_dyn", false, "parent_dyn"),  // cycle guard check too
        MakeMinimalSpec("parent_dyn", false, "grandparent_dyn"),
        MakeMinimalSpec("saved_child", false, "parent_dyn"),
        MakeMinimalSpec("unsaved_session_alive", false),
        MakeMinimalSpec("unsaved_abandoned", false),
        MakeMinimalSpec("other_playthrough_dead", true),
    };

    const std::vector<SpawnedMortalLink> savedLinks = {
        SpawnedMortalLink{ .actorRefId = FormRawId{ 0xFF000010u }, .slotKey = "saved_child" }
    };
    MORWHA_CHECK(MarkCommittedSpecsForLinks(specs, savedLinks));
    MORWHA_CHECK(!MarkCommittedSpecsForLinks(specs, savedLinks));  // idempotent
    MORWHA_CHECK(specs[0].committed);   // grandparent promoted transitively
    MORWHA_CHECK(specs[1].committed);   // parent promoted transitively
    MORWHA_CHECK(specs[2].committed);   // child promoted directly
    MORWHA_CHECK(!specs[3].committed);
    MORWHA_CHECK(!specs[4].committed);

    const std::unordered_set<std::string> activeSession{ "unsaved_session_alive" };
    const std::unordered_set<std::string> coSaveKeys{ "saved_child" };
    const auto plan = PlanRegistryGarbageCollection(
        specs,
        activeSession,
        coSaveKeys,
        /*pruneUncommitted=*/true,
        /*pruneUnreferencedCommitted=*/true);

    MORWHA_CHECK_EQ(plan.prunedUncommittedKeys, std::vector<std::string>{ "unsaved_abandoned" });
    MORWHA_CHECK_EQ(plan.prunedCommittedKeys, std::vector<std::string>{ "other_playthrough_dead" });
    MORWHA_REQUIRE_EQ(plan.survivingSpecs.size(), 4u);
    MORWHA_CHECK_EQ(plan.survivingSpecs[0].slotKey, "grandparent_dyn");
    MORWHA_CHECK_EQ(plan.survivingSpecs[1].slotKey, "parent_dyn");
    MORWHA_CHECK_EQ(plan.survivingSpecs[2].slotKey, "saved_child");
    MORWHA_CHECK_EQ(plan.survivingSpecs[3].slotKey, "unsaved_session_alive");
}
