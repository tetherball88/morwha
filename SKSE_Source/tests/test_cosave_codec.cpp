#include "TestHarness.h"

#include "morwha/state/CoSaveCodec.h"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace {

    using MORWHA::Foundation::DonorMode;
    using MORWHA::Foundation::FormRawId;
    using MORWHA::Foundation::Seed64;
    using MORWHA::State::DecodeSpawnedLinks;
    using MORWHA::State::EncodeSpawnedLinks;
    using MORWHA::State::SpawnedMortalLink;

}  // namespace

MORWHA_TEST(CoSaveCodec, RoundtripAndTruncationGuard) {
    const std::vector<SpawnedMortalLink> input = {
        SpawnedMortalLink{
            .actorRefId = FormRawId{ 0xFF001234u },
            .dpfLocalId = 0x00000801u,
            .slotKey = "mortal_bandit_001",
            .donorMode = DonorMode::OneDonorRelative,
            .seed = Seed64{ 0x1122334455667788ull },
            .donorARefId = FormRawId{ 0x02001111u },
        },
        SpawnedMortalLink{
            .actorRefId = FormRawId{ 0x04000902u },
            .dpfLocalId = 0x00000802u,
            .slotKey = "child_nord_002",
            .donorMode = DonorMode::TwoDonorOffspring,
            .seed = Seed64{ 0xCAFEBABEDEADBEEFull },
            .donorARefId = FormRawId{ 0x00000014u },
            .donorBRefId = FormRawId{ 0x0001A697u },
        },
    };

    const auto encoded = EncodeSpawnedLinks(input);
    std::vector<SpawnedMortalLink> decoded;
    MORWHA_REQUIRE(DecodeSpawnedLinks(encoded, decoded));
    MORWHA_REQUIRE_EQ(decoded.size(), input.size());
    MORWHA_CHECK(decoded[0] == input[0]);
    MORWHA_CHECK(decoded[1] == input[1]);

    auto badVer = encoded;
    badVer[0] = 2u;
    std::vector<SpawnedMortalLink> badVerOut = input;
    MORWHA_CHECK(!DecodeSpawnedLinks(badVer, badVerOut));
    MORWHA_CHECK(badVerOut.empty());

    for (std::size_t truncLen = 0; truncLen < encoded.size(); ++truncLen) {
        std::vector<SpawnedMortalLink> badOut = input;
        const bool ok = DecodeSpawnedLinks(
            std::span<const std::uint8_t>(encoded.data(), truncLen), badOut);
        MORWHA_CHECK(!ok);
        MORWHA_CHECK(badOut.empty());
    }

    auto withGarbage = encoded;
    withGarbage.push_back(0xAA);
    std::vector<SpawnedMortalLink> trailingOut = input;
    MORWHA_CHECK(!DecodeSpawnedLinks(withGarbage, trailingOut));
    MORWHA_CHECK(trailingOut.empty());
}
