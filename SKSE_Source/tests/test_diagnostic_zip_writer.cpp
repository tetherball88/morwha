#include "TestHarness.h"

#include "morwha/state/DiagnosticZipWriter.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

    using MORWHA::State::ComputeCrc32;
    using MORWHA::State::DiagnosticZipArchive;
    using MORWHA::State::EncodeRgbaToBmp24;

    [[nodiscard]] std::uint32_t ReadU32Le(const std::uint8_t* p) noexcept {
        std::uint32_t v = 0;
        std::memcpy(&v, p, sizeof(v));
        return v;
    }

}  // namespace

MORWHA_TEST(DiagnosticZipWriter, Crc32Bmp24AndPkzipArchiveStructure) {
    constexpr std::string_view kCheckDigits = "123456789";
    const auto checkSpan = std::span<const std::uint8_t>(
        reinterpret_cast<const std::uint8_t*>(kCheckDigits.data()), kCheckDigits.size());
    MORWHA_CHECK_EQ(ComputeCrc32(checkSpan), 0xCBF43926u);

    // 2x2 RGBA -> 24-bit bottom-up BMP (row stride = 8 bytes, total = 54 + 16 = 70 bytes).
    const std::array<std::uint8_t, 16> rgba2x2{
        10u, 20u, 30u, 255u,   40u, 50u, 60u, 255u,
        70u, 80u, 90u, 255u,  100u, 110u, 120u, 255u
    };
    std::vector<std::uint8_t> bmp;
    MORWHA_REQUIRE(EncodeRgbaToBmp24(rgba2x2, 2u, 2u, bmp));
    MORWHA_CHECK_EQ(bmp.size(), std::size_t{ 70u });
    MORWHA_CHECK_EQ(bmp[0], std::uint8_t{ 'B' });
    MORWHA_CHECK_EQ(bmp[1], std::uint8_t{ 'M' });
    // Bottom row (srcY=1) pixel 0 = RGB(70,80,90) stored BGR(90,80,70) at offset 54.
    MORWHA_CHECK_EQ(bmp[54], std::uint8_t{ 90u });
    MORWHA_CHECK_EQ(bmp[55], std::uint8_t{ 80u });
    MORWHA_CHECK_EQ(bmp[56], std::uint8_t{ 70u });

    DiagnosticZipArchive zip;
    zip.AddTextFile("\\summary.json", "{\"ok\":true}");
    zip.AddFileBytes("thumbnails\\preview.bmp", bmp);
    MORWHA_CHECK_EQ(zip.FileCount(), std::size_t{ 2u });

    const std::filesystem::path tmpZip =
        std::filesystem::temp_directory_path() / "morwha_test_diag_bundle.zip";
    std::error_code ec;
    std::filesystem::remove(tmpZip, ec);

    std::size_t writtenBytes = 0u;
    std::string err;
    MORWHA_REQUIRE(zip.WriteZipFileAtomic(tmpZip, &writtenBytes, &err));
    MORWHA_CHECK(writtenBytes > 100u);

    std::ifstream in(tmpZip, std::ios::binary | std::ios::ate);
    MORWHA_REQUIRE(in.is_open());
    std::vector<std::uint8_t> raw(static_cast<std::size_t>(in.tellg()));
    in.seekg(0, std::ios::beg);
    in.read(reinterpret_cast<char*>(raw.data()), static_cast<std::streamsize>(raw.size()));
    in.close();
    std::filesystem::remove(tmpZip, ec);

    MORWHA_CHECK_EQ(raw.size(), writtenBytes);
    MORWHA_CHECK_EQ(ReadU32Le(raw.data()), 0x04034B50u);
    MORWHA_CHECK_EQ(ReadU32Le(raw.data() + raw.size() - 22u), 0x06054B50u);
}
