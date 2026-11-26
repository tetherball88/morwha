#pragma once

#include <compare>
#include <cstdint>

namespace MORWHA::Foundation {

    inline constexpr std::uint32_t kNullRawId = 0x00000000u;

    template <typename Tag, typename ValueT = std::uint32_t>
    struct StrongId {
        ValueT value{};

        constexpr StrongId() noexcept = default;
        explicit constexpr StrongId(ValueT raw) noexcept : value(raw) {}

        [[nodiscard]] constexpr ValueT raw() const noexcept { return value; }
        [[nodiscard]] constexpr bool IsNull() const noexcept { return value == ValueT{}; }
        explicit constexpr operator bool() const noexcept { return value != ValueT{}; }

        auto operator<=>(const StrongId&) const noexcept = default;
    };

    struct FormRawIdTag {};
    struct Seed64Tag {};

    using FormRawId = StrongId<FormRawIdTag, std::uint32_t>;
    using Seed64 = StrongId<Seed64Tag, std::uint64_t>;

}  // namespace MORWHA::Foundation
