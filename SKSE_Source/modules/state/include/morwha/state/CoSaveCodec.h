#pragma once

#include "morwha/state/RegistryTypes.h"

#include <cstdint>
#include <span>
#include <vector>

namespace MORWHA::State {

    inline constexpr std::uint32_t kMorwhaPluginId = 0x4D525748u;     // 'MRWH'
    inline constexpr std::uint32_t kLinksChunkId = 0x4C4E4B53u;       // 'LNKS'
    inline constexpr std::uint32_t kCoSaveFormatVersion = 1u;
    inline constexpr std::uint32_t kMaxSpawnedLinks = 65536u;
    inline constexpr std::uint32_t kMaxSlotKeyBytes = 256u;

    [[nodiscard]] std::vector<std::uint8_t> EncodeSpawnedLinks(
        std::span<const SpawnedMortalLink> links);

    [[nodiscard]] bool DecodeSpawnedLinks(
        std::span<const std::uint8_t> payload,
        std::vector<SpawnedMortalLink>& out);

}  // namespace MORWHA::State
