#include "morwha/state/CoSaveCodec.h"

#include <cstring>

namespace MORWHA::State {

    namespace {

        template <typename T>
        void AppendPod(std::vector<std::uint8_t>& buf, const T& value) {
            const auto* bytes = reinterpret_cast<const std::uint8_t*>(&value);
            buf.insert(buf.end(), bytes, bytes + sizeof(T));
        }

        template <typename T>
        bool ReadPod(std::span<const std::uint8_t> payload, std::size_t& offset, T& out) noexcept {
            if (offset > payload.size() || (payload.size() - offset) < sizeof(T)) {
                return false;
            }
            std::memcpy(&out, payload.data() + offset, sizeof(T));
            offset += sizeof(T);
            return true;
        }

    }  // namespace

    std::vector<std::uint8_t> EncodeSpawnedLinks(std::span<const SpawnedMortalLink> links) {
        std::vector<std::uint8_t> out;
        const std::uint32_t count = static_cast<std::uint32_t>(
            links.size() > kMaxSpawnedLinks ? kMaxSpawnedLinks : links.size());
        AppendPod(out, kCoSaveFormatVersion);
        AppendPod(out, count);

        for (std::uint32_t i = 0; i < count; ++i) {
            const auto& link = links[i];
            const std::uint32_t keyLen = static_cast<std::uint32_t>(
                link.slotKey.size() > kMaxSlotKeyBytes ? kMaxSlotKeyBytes : link.slotKey.size());
            const auto donorModeRaw = static_cast<std::uint8_t>(link.donorMode);

            AppendPod(out, link.actorRefId.raw());
            AppendPod(out, link.dpfLocalId);
            AppendPod(out, donorModeRaw);
            AppendPod(out, link.seed.raw());
            AppendPod(out, link.donorARefId.raw());
            AppendPod(out, link.donorBRefId.raw());
            AppendPod(out, keyLen);
            if (keyLen > 0) {
                const auto* keyBytes = reinterpret_cast<const std::uint8_t*>(link.slotKey.data());
                out.insert(out.end(), keyBytes, keyBytes + keyLen);
            }
        }
        return out;
    }

    bool DecodeSpawnedLinks(
        std::span<const std::uint8_t> payload,
        std::vector<SpawnedMortalLink>& out) {
        out.clear();

        std::size_t offset = 0;
        std::uint32_t version = 0;
        std::uint32_t count = 0;
        if (!ReadPod(payload, offset, version) || !ReadPod(payload, offset, count)) {
            return false;
        }
        if (version != kCoSaveFormatVersion || count > kMaxSpawnedLinks) {
            return false;
        }

        std::vector<SpawnedMortalLink> parsed;
        parsed.reserve(count);

        for (std::uint32_t i = 0; i < count; ++i) {
            std::uint32_t actorRefRaw = 0;
            std::uint32_t dpfLocalId = 0;
            std::uint8_t donorModeRaw = 0;
            std::uint64_t seedRaw = 0;
            std::uint32_t donorARefRaw = 0;
            std::uint32_t donorBRefRaw = 0;
            std::uint32_t keyLen = 0;

            if (!ReadPod(payload, offset, actorRefRaw) ||
                !ReadPod(payload, offset, dpfLocalId) ||
                !ReadPod(payload, offset, donorModeRaw) ||
                !ReadPod(payload, offset, seedRaw) ||
                !ReadPod(payload, offset, donorARefRaw) ||
                !ReadPod(payload, offset, donorBRefRaw) ||
                !ReadPod(payload, offset, keyLen)) {
                return false;
            }
            if (donorModeRaw > static_cast<std::uint8_t>(Foundation::DonorMode::TwoDonorOffspring) ||
                keyLen > kMaxSlotKeyBytes) {
                return false;
            }
            if (offset > payload.size() || (payload.size() - offset) < keyLen) {
                return false;
            }

            std::string key(reinterpret_cast<const char*>(payload.data() + offset), keyLen);
            offset += keyLen;

            parsed.push_back(SpawnedMortalLink{
                .actorRefId = Foundation::FormRawId{ actorRefRaw },
                .dpfLocalId = dpfLocalId,
                .slotKey = std::move(key),
                .donorMode = static_cast<Foundation::DonorMode>(donorModeRaw),
                .seed = Foundation::Seed64{ seedRaw },
                .donorARefId = Foundation::FormRawId{ donorARefRaw },
                .donorBRefId = Foundation::FormRawId{ donorBRefRaw },
            });
        }

        if (offset != payload.size()) {
            return false;
        }

        out = std::move(parsed);
        return true;
    }

}  // namespace MORWHA::State
