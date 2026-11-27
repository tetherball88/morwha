#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace MORWHA::State {

    /// Computes standard IEEE 802.3 CRC-32 (polynomial 0xEDB88320).
    [[nodiscard]] std::uint32_t ComputeCrc32(std::span<const std::uint8_t> bytes) noexcept;

    /// Encodes a top-down RGBA8 image into an uncompressed 24-bit Windows BMP buffer
    /// so diagnostic 64x64 texture previews can be viewed directly in any OS file explorer.
    [[nodiscard]] bool EncodeRgbaToBmp24(
        std::span<const std::uint8_t> rgba,
        std::uint32_t width,
        std::uint32_t height,
        std::vector<std::uint8_t>& outBmpBytes);

    /// Pure engine-free PKZIP v2.0 (STORE + CRC-32 + UTF-8 path flag) archive builder
    /// for self-contained in-game diagnostic bundles.
    class DiagnosticZipArchive {
    public:
        void Clear() noexcept;

        [[nodiscard]] std::size_t FileCount() const noexcept { return entries_.size(); }

        void AddFileBytes(std::string archivePath, std::span<const std::uint8_t> content);
        void AddTextFile(std::string archivePath, std::string_view text);

        [[nodiscard]] bool BuildZipBytes(
            std::vector<std::uint8_t>& outZipBytes,
            std::string* outError = nullptr) const;

        [[nodiscard]] bool WriteZipFileAtomic(
            const std::filesystem::path& outputPath,
            std::size_t* outTotalZipBytes = nullptr,
            std::string* outError = nullptr) const;

    private:
        struct Entry {
            std::string path{};
            std::vector<std::uint8_t> data{};
            std::uint32_t crc32{ 0 };
        };

        std::vector<Entry> entries_{};
    };

}  // namespace MORWHA::State
