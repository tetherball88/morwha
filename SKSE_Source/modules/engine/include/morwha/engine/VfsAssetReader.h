#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace RE {
    class TESNPC;
}

namespace MORWHA::Engine {

    /// Normalizes a BGSHeadPart / TESModelTri path into a Skyrim Data-relative VFS path
    /// ("meshes\\...") suitable for RE::BSResourceNiBinaryStream.
    [[nodiscard]] std::string BuildMeshVfsPath(std::string_view rawModelPath);

    /// Normalizes a BSShaderTextureSet diffuse path into a Skyrim Data-relative VFS path
    /// ("textures\\...") suitable for RE::BSResourceNiBinaryStream.
    [[nodiscard]] std::string BuildTextureVfsPath(std::string_view rawTexturePath);

    /// Builds the expected FaceGeom .nif VFS path for rootFaceNpc
    /// ("meshes\\actors\\character\\facegendata\\facegeom\\<DefiningPlugin>\\<00XXXXXX>.nif").
    [[nodiscard]] std::string BuildNpcFaceGeomVfsPath(const RE::TESNPC* rootFaceNpc);

    /// Reads an asset from Skyrim's VFS (BSA archives or loose Data/MO2 overrides) via
    /// RE::BSResourceNiBinaryStream with bounded maximum size.
    [[nodiscard]] bool ReadVfsFileBytes(
        std::string_view vfsPath,
        std::vector<std::uint8_t>& outBytes,
        std::size_t maxBytes = 32ull * 1024ull * 1024ull);

    /// Reads up to outHeaderBuf.size() leading bytes of a VFS asset along with its total file size.
    [[nodiscard]] bool ReadVfsFileHeaderAndTotalSize(
        std::string_view vfsPath,
        std::span<std::uint8_t> outHeaderBuf,
        std::size_t& outBytesRead,
        std::size_t& outTotalFileBytes);

    /// Reads an exact byte slice [byteOffset, byteOffset + byteCount) from a VFS asset
    /// using stream seek with sequential drain fallback on compressed BSA streams.
    [[nodiscard]] bool ReadVfsFileSlice(
        std::string_view vfsPath,
        std::size_t byteOffset,
        std::size_t byteCount,
        std::vector<std::uint8_t>& outSlice);

}  // namespace MORWHA::Engine
