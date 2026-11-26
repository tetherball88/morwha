#include "morwha/engine/VfsAssetReader.h"

#include "morwha/engine/FormRefResolver.h"
#include "morwha/engine/HeadPartPoolIndexer.h"

#include "RE/B/BSResourceNiBinaryStream.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <format>

namespace MORWHA::Engine {

    namespace {

        [[nodiscard]] std::string NormalizeDataRelativeVfsPath(
            std::string_view rawPath,
            std::string_view requiredPrefix) {
            while (!rawPath.empty() &&
                   (rawPath.front() == '\\' ||
                    rawPath.front() == '/' ||
                    std::isspace(static_cast<unsigned char>(rawPath.front())) != 0)) {
                rawPath.remove_prefix(1u);
            }
            while (!rawPath.empty() &&
                   std::isspace(static_cast<unsigned char>(rawPath.back())) != 0) {
                rawPath.remove_suffix(1u);
            }
            if (rawPath.empty()) {
                return {};
            }

            std::string normalized;
            normalized.reserve(rawPath.size() + requiredPrefix.size() + 2u);
            for (const char ch : rawPath) {
                normalized.push_back((ch == '/') ? '\\' : ch);
            }

            constexpr std::string_view kDataPrefix = "data\\";
            if (normalized.size() >= kDataPrefix.size() &&
                HeadPartPoolIndexer::EqualsCaseInsensitive(
                    std::string_view(normalized).substr(0u, kDataPrefix.size()),
                    kDataPrefix)) {
                normalized.erase(0u, kDataPrefix.size());
                while (!normalized.empty() && normalized.front() == '\\') {
                    normalized.erase(normalized.begin());
                }
            }

            if (normalized.empty()) {
                return {};
            }

            if (normalized.size() < requiredPrefix.size() ||
                !HeadPartPoolIndexer::EqualsCaseInsensitive(
                    std::string_view(normalized).substr(0u, requiredPrefix.size()),
                    requiredPrefix)) {
                normalized.insert(0u, requiredPrefix);
            }
            return normalized;
        }

        [[nodiscard]] bool DrainStreamBytes(
            RE::BSResourceNiBinaryStream& stream,
            std::size_t bytesToSkip) {
            std::array<std::uint8_t, 4096u> scratch{};
            std::size_t rem = bytesToSkip;
            while (rem > 0u) {
                const auto chunk = static_cast<std::uint32_t>((std::min)(rem, scratch.size()));
                if (!stream.read(scratch.data(), chunk)) {
                    return false;
                }
                rem -= chunk;
            }
            return true;
        }

    }  // namespace

    std::string BuildMeshVfsPath(std::string_view rawModelPath) {
        return NormalizeDataRelativeVfsPath(rawModelPath, "meshes\\");
    }

    std::string BuildTextureVfsPath(std::string_view rawTexturePath) {
        return NormalizeDataRelativeVfsPath(rawTexturePath, "textures\\");
    }

    std::string BuildNpcFaceGeomVfsPath(const RE::TESNPC* rootFaceNpc) {
        const auto ref = MakePluginFormRef(rootFaceNpc);
        if (ref.IsEmpty() || ref.plugin.empty() || ref.localId == 0u) {
            return {};
        }
        return std::format(
            "meshes\\actors\\character\\facegendata\\facegeom\\{}\\{:08X}.nif",
            ref.plugin,
            ref.localId);
    }

    bool ReadVfsFileBytes(
        std::string_view vfsPath,
        std::vector<std::uint8_t>& outBytes,
        std::size_t maxBytes) {
        outBytes.clear();
        if (vfsPath.empty() || maxBytes == 0u) {
            return false;
        }

        RE::BSResourceNiBinaryStream stream{ std::string(vfsPath) };
        if (!stream.good() || !stream.stream) {
            return false;
        }

        const std::uint32_t totalSize = stream.stream->totalSize;
        if (totalSize == 0u || static_cast<std::size_t>(totalSize) > maxBytes) {
            return false;
        }

        outBytes.resize(totalSize);
        if (!stream.read(outBytes.data(), totalSize)) {
            outBytes.clear();
            return false;
        }
        return true;
    }

    bool ReadVfsFileHeaderAndTotalSize(
        std::string_view vfsPath,
        std::span<std::uint8_t> outHeaderBuf,
        std::size_t& outBytesRead,
        std::size_t& outTotalFileBytes) {
        outBytesRead = 0u;
        outTotalFileBytes = 0u;
        if (vfsPath.empty() || outHeaderBuf.empty()) {
            return false;
        }

        RE::BSResourceNiBinaryStream stream{ std::string(vfsPath) };
        if (!stream.good() || !stream.stream) {
            return false;
        }

        const std::uint32_t totalSize = stream.stream->totalSize;
        if (totalSize == 0u) {
            return false;
        }

        const std::size_t toRead = (std::min)(static_cast<std::size_t>(totalSize), outHeaderBuf.size());
        if (!stream.read(outHeaderBuf.data(), static_cast<std::uint32_t>(toRead))) {
            return false;
        }
        outBytesRead = toRead;
        outTotalFileBytes = totalSize;
        return true;
    }

    bool ReadVfsFileSlice(
        std::string_view vfsPath,
        std::size_t byteOffset,
        std::size_t byteCount,
        std::vector<std::uint8_t>& outSlice) {
        outSlice.clear();
        if (vfsPath.empty() || byteCount == 0u || byteCount > 16ull * 1024ull * 1024ull) {
            return false;
        }

        RE::BSResourceNiBinaryStream stream{ std::string(vfsPath) };
        if (!stream.good() || !stream.stream) {
            return false;
        }

        const std::size_t totalSize = stream.stream->totalSize;
        if (byteOffset >= totalSize || byteCount > totalSize - byteOffset) {
            return false;
        }

        outSlice.resize(byteCount);
        if (byteOffset == 0u) {
            if (!stream.read(outSlice.data(), static_cast<std::uint32_t>(byteCount))) {
                outSlice.clear();
                return false;
            }
            return true;
        }

        if (byteOffset <= static_cast<std::size_t>((std::numeric_limits<std::int32_t>::max)())) {
            stream.seek(static_cast<std::int32_t>(byteOffset));
            if (stream.tell() == static_cast<std::uint32_t>(byteOffset) &&
                stream.read(outSlice.data(), static_cast<std::uint32_t>(byteCount))) {
                return true;
            }
        }

        // Sequential fallback if a compressed BSA stream rejects random seek().
        RE::BSResourceNiBinaryStream seqStream{ std::string(vfsPath) };
        if (!seqStream.good() || !DrainStreamBytes(seqStream, byteOffset) ||
            !seqStream.read(outSlice.data(), static_cast<std::uint32_t>(byteCount))) {
            outSlice.clear();
            return false;
        }
        return true;
    }

}  // namespace MORWHA::Engine
