#include "morwha/state/DiagnosticZipWriter.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>

namespace MORWHA::State {

    namespace {

        constexpr auto kCrc32Table = []() constexpr {
            std::array<std::uint32_t, 256> table{};
            for (std::uint32_t i = 0u; i < 256u; ++i) {
                std::uint32_t c = i;
                for (int k = 0; k < 8; ++k) {
                    c = (c & 1u) ? (0xEDB88320u ^ (c >> 1u)) : (c >> 1u);
                }
                table[i] = c;
            }
            return table;
        }();

        void AppendU16Le(std::vector<std::uint8_t>& out, std::uint16_t v) {
            out.push_back(static_cast<std::uint8_t>(v & 0xFFu));
            out.push_back(static_cast<std::uint8_t>((v >> 8u) & 0xFFu));
        }

        void AppendU32Le(std::vector<std::uint8_t>& out, std::uint32_t v) {
            out.push_back(static_cast<std::uint8_t>(v & 0xFFu));
            out.push_back(static_cast<std::uint8_t>((v >> 8u) & 0xFFu));
            out.push_back(static_cast<std::uint8_t>((v >> 16u) & 0xFFu));
            out.push_back(static_cast<std::uint8_t>((v >> 24u) & 0xFFu));
        }

        [[nodiscard]] std::string NormalizeArchivePath(std::string raw) {
            std::string clean;
            clean.reserve(raw.size());
            for (const char ch : raw) {
                const char norm = (ch == '\\') ? '/' : ch;
                if (norm == ':' || static_cast<unsigned char>(norm) < 0x20u) {
                    clean.push_back('_');
                    continue;
                }
                if (norm == '/' && (clean.empty() || clean.back() == '/')) {
                    continue;
                }
                clean.push_back(norm);
            }
            while (!clean.empty() && clean.back() == '/') {
                clean.pop_back();
            }
            if (clean.empty()) {
                clean = "entry.bin";
            }
            return clean;
        }

    }  // namespace

    std::uint32_t ComputeCrc32(std::span<const std::uint8_t> bytes) noexcept {
        std::uint32_t crc = 0xFFFFFFFFu;
        for (const std::uint8_t b : bytes) {
            crc = kCrc32Table[(crc ^ b) & 0xFFu] ^ (crc >> 8u);
        }
        return crc ^ 0xFFFFFFFFu;
    }

    bool EncodeRgbaToBmp24(
        std::span<const std::uint8_t> rgba,
        std::uint32_t width,
        std::uint32_t height,
        std::vector<std::uint8_t>& outBmpBytes) {
        outBmpBytes.clear();
        if (width == 0u || height == 0u || width > 4096u || height > 4096u) {
            return false;
        }
        const std::size_t expectedRgba = static_cast<std::size_t>(width) * height * 4u;
        if (rgba.size() != expectedRgba) {
            return false;
        }

        const std::size_t rowStride = ((static_cast<std::size_t>(width) * 3u + 3u) / 4u) * 4u;
        const std::size_t pixelDataSize = rowStride * height;
        const std::size_t totalSize = 54u + pixelDataSize;
        if (totalSize > 0x7FFFFFFFu) {
            return false;
        }

        outBmpBytes.reserve(totalSize);
        // BITMAPFILEHEADER (14 bytes)
        outBmpBytes.push_back('B');
        outBmpBytes.push_back('M');
        AppendU32Le(outBmpBytes, static_cast<std::uint32_t>(totalSize));
        AppendU16Le(outBmpBytes, 0u);
        AppendU16Le(outBmpBytes, 0u);
        AppendU32Le(outBmpBytes, 54u);

        // BITMAPINFOHEADER (40 bytes, bottom-up standard compatibility)
        AppendU32Le(outBmpBytes, 40u);
        AppendU32Le(outBmpBytes, width);
        AppendU32Le(outBmpBytes, height);
        AppendU16Le(outBmpBytes, 1u);   // biPlanes
        AppendU16Le(outBmpBytes, 24u);  // biBitCount
        AppendU32Le(outBmpBytes, 0u);   // BI_RGB
        AppendU32Le(outBmpBytes, static_cast<std::uint32_t>(pixelDataSize));
        AppendU32Le(outBmpBytes, 2835u);
        AppendU32Le(outBmpBytes, 2835u);
        AppendU32Le(outBmpBytes, 0u);
        AppendU32Le(outBmpBytes, 0u);

        const std::size_t padBytes = rowStride - static_cast<std::size_t>(width) * 3u;
        for (std::uint32_t row = 0u; row < height; ++row) {
            const std::uint32_t srcY = height - 1u - row;
            const std::size_t srcRowOff = static_cast<std::size_t>(srcY) * width * 4u;
            for (std::uint32_t x = 0u; x < width; ++x) {
                const std::size_t px = srcRowOff + static_cast<std::size_t>(x) * 4u;
                outBmpBytes.push_back(rgba[px + 2u]);  // B
                outBmpBytes.push_back(rgba[px + 1u]);  // G
                outBmpBytes.push_back(rgba[px + 0u]);  // R
            }
            for (std::size_t p = 0u; p < padBytes; ++p) {
                outBmpBytes.push_back(0u);
            }
        }
        return true;
    }

    void DiagnosticZipArchive::Clear() noexcept {
        entries_.clear();
    }

    void DiagnosticZipArchive::AddFileBytes(
        std::string archivePath,
        std::span<const std::uint8_t> content) {
        Entry entry{};
        entry.path = NormalizeArchivePath(std::move(archivePath));
        entry.data.assign(content.begin(), content.end());
        entry.crc32 = ComputeCrc32(entry.data);
        entries_.push_back(std::move(entry));
    }

    void DiagnosticZipArchive::AddTextFile(std::string archivePath, std::string_view text) {
        AddFileBytes(
            std::move(archivePath),
            std::span<const std::uint8_t>(
                reinterpret_cast<const std::uint8_t*>(text.data()), text.size()));
    }

    bool DiagnosticZipArchive::BuildZipBytes(
        std::vector<std::uint8_t>& outZipBytes,
        std::string* outError) const {
        outZipBytes.clear();
        if (entries_.size() > 0xFFFFu) {
            if (outError) {
                *outError = "ZIP archive exceeds 65535 standard entry limit";
            }
            return false;
        }

        std::size_t approxSize = 22u;
        for (const auto& e : entries_) {
            if (e.path.size() > 0xFFFFu || e.data.size() > 0xFFFFFFFFull) {
                if (outError) {
                    *outError = "ZIP entry exceeds standard 32-bit size limit";
                }
                return false;
            }
            approxSize += 30u + e.path.size() + e.data.size() + 46u + e.path.size();
        }
        if (approxSize > 0xFFFFFFFFull) {
            if (outError) {
                *outError = "Total ZIP size exceeds standard 4 GB limit";
            }
            return false;
        }

        outZipBytes.reserve(approxSize);
        std::vector<std::uint32_t> localHeaderOffsets;
        localHeaderOffsets.reserve(entries_.size());

        constexpr std::uint16_t kVersionNeeded = 20u;
        constexpr std::uint16_t kUtf8Flag = 0x0800u;
        constexpr std::uint16_t kStoreMethod = 0u;
        constexpr std::uint16_t kDefaultDosTime = 0x0000u;
        constexpr std::uint16_t kDefaultDosDate = 0x5C21u;  // valid non-zero DOS date

        for (const auto& e : entries_) {
            localHeaderOffsets.push_back(static_cast<std::uint32_t>(outZipBytes.size()));
            AppendU32Le(outZipBytes, 0x04034B50u);  // Local file header signature
            AppendU16Le(outZipBytes, kVersionNeeded);
            AppendU16Le(outZipBytes, kUtf8Flag);
            AppendU16Le(outZipBytes, kStoreMethod);
            AppendU16Le(outZipBytes, kDefaultDosTime);
            AppendU16Le(outZipBytes, kDefaultDosDate);
            AppendU32Le(outZipBytes, e.crc32);
            AppendU32Le(outZipBytes, static_cast<std::uint32_t>(e.data.size()));
            AppendU32Le(outZipBytes, static_cast<std::uint32_t>(e.data.size()));
            AppendU16Le(outZipBytes, static_cast<std::uint16_t>(e.path.size()));
            AppendU16Le(outZipBytes, 0u);  // extra field length
            outZipBytes.insert(outZipBytes.end(), e.path.begin(), e.path.end());
            outZipBytes.insert(outZipBytes.end(), e.data.begin(), e.data.end());
        }

        const auto centralDirOffset = static_cast<std::uint32_t>(outZipBytes.size());
        for (std::size_t i = 0u; i < entries_.size(); ++i) {
            const auto& e = entries_[i];
            AppendU32Le(outZipBytes, 0x02014B50u);  // Central directory file header signature
            AppendU16Le(outZipBytes, kVersionNeeded);
            AppendU16Le(outZipBytes, kVersionNeeded);
            AppendU16Le(outZipBytes, kUtf8Flag);
            AppendU16Le(outZipBytes, kStoreMethod);
            AppendU16Le(outZipBytes, kDefaultDosTime);
            AppendU16Le(outZipBytes, kDefaultDosDate);
            AppendU32Le(outZipBytes, e.crc32);
            AppendU32Le(outZipBytes, static_cast<std::uint32_t>(e.data.size()));
            AppendU32Le(outZipBytes, static_cast<std::uint32_t>(e.data.size()));
            AppendU16Le(outZipBytes, static_cast<std::uint16_t>(e.path.size()));
            AppendU16Le(outZipBytes, 0u);  // extra field length
            AppendU16Le(outZipBytes, 0u);  // file comment length
            AppendU16Le(outZipBytes, 0u);  // disk number start
            AppendU16Le(outZipBytes, 0u);  // internal file attributes
            AppendU32Le(outZipBytes, 0u);  // external file attributes
            AppendU32Le(outZipBytes, localHeaderOffsets[i]);
            outZipBytes.insert(outZipBytes.end(), e.path.begin(), e.path.end());
        }

        const auto centralDirSize =
            static_cast<std::uint32_t>(outZipBytes.size() - centralDirOffset);
        AppendU32Le(outZipBytes, 0x06054B50u);  // End of central directory signature
        AppendU16Le(outZipBytes, 0u);           // number of this disk
        AppendU16Le(outZipBytes, 0u);           // disk with start of central directory
        AppendU16Le(outZipBytes, static_cast<std::uint16_t>(entries_.size()));
        AppendU16Le(outZipBytes, static_cast<std::uint16_t>(entries_.size()));
        AppendU32Le(outZipBytes, centralDirSize);
        AppendU32Le(outZipBytes, centralDirOffset);
        AppendU16Le(outZipBytes, 0u);  // comment length
        return true;
    }

    bool DiagnosticZipArchive::WriteZipFileAtomic(
        const std::filesystem::path& outputPath,
        std::size_t* outTotalZipBytes,
        std::string* outError) const {
        std::vector<std::uint8_t> zipBytes;
        if (!BuildZipBytes(zipBytes, outError)) {
            return false;
        }

        std::error_code ec;
        if (const auto parent = outputPath.parent_path(); !parent.empty()) {
            std::filesystem::create_directories(parent, ec);
        }

        std::filesystem::path tmpPath = outputPath;
        tmpPath += ".tmp";
        {
            std::ofstream out(tmpPath, std::ios::binary | std::ios::trunc);
            if (!out.is_open()) {
                if (outError) {
                    *outError = "Could not open temporary diagnostic archive file for writing";
                }
                return false;
            }
            out.write(
                reinterpret_cast<const char*>(zipBytes.data()),
                static_cast<std::streamsize>(zipBytes.size()));
            out.flush();
            if (!out.good()) {
                out.close();
                std::filesystem::remove(tmpPath, ec);
                if (outError) {
                    *outError = "Failed flushing diagnostic archive bytes to disk";
                }
                return false;
            }
        }

        std::filesystem::rename(tmpPath, outputPath, ec);
        if (ec) {
            std::filesystem::copy_file(
                tmpPath, outputPath, std::filesystem::copy_options::overwrite_existing, ec);
            std::error_code rmEc;
            std::filesystem::remove(tmpPath, rmEc);
            if (ec) {
                if (outError) {
                    *outError = ec.message();
                }
                return false;
            }
        }

        if (outTotalZipBytes) {
            *outTotalZipBytes = zipBytes.size();
        }
        return true;
    }

}  // namespace MORWHA::State
