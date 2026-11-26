#pragma once

#include <concepts>
#include <cstdint>
#include <format>
#include <source_location>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace MORWHA::Tests {

    struct TestAbortException final {};

    struct FailureDiagnostic {
        std::string expression{};
        std::string detail{};
        std::string file{};
        std::uint_least32_t line{ 0 };
    };

    struct TestCaseEntry {
        std::string_view suite{};
        std::string_view name{};
        void (*fn)(){ nullptr };
    };

    std::vector<TestCaseEntry>& Registry();
    void RecordAssertion(bool passed, std::string_view expr, std::string detail, std::source_location loc);

    struct AutoRegisterTest {
        AutoRegisterTest(std::string_view suite, std::string_view name, void (*fn)()) {
            Registry().push_back(TestCaseEntry{ .suite = suite, .name = name, .fn = fn });
        }
    };

    template <typename T>
    concept HasRawMethod = requires(const T& v) {
        { v.raw() } -> std::convertible_to<std::uint64_t>;
    };

    template <typename T>
    [[nodiscard]] std::string FormatTestValue(const T& value) {
        using CleanT = std::remove_cvref_t<T>;
        if constexpr (std::is_same_v<CleanT, bool>) {
            return value ? "true" : "false";
        } else if constexpr (std::is_convertible_v<CleanT, std::string_view>) {
            return std::format("\"{}\"", std::string_view(value));
        } else if constexpr (HasRawMethod<CleanT>) {
            return std::format("0x{:X}", static_cast<std::uint64_t>(value.raw()));
        } else if constexpr (std::is_enum_v<CleanT>) {
            return std::format("enum({})", static_cast<long long>(std::to_underlying(value)));
        } else if constexpr (std::is_integral_v<CleanT>) {
            if constexpr (std::is_signed_v<CleanT>) {
                return std::format("{}", static_cast<long long>(value));
            } else {
                return std::format("{} (0x{:X})", static_cast<unsigned long long>(value),
                                   static_cast<unsigned long long>(value));
            }
        } else if constexpr (std::is_floating_point_v<CleanT>) {
            return std::format("{:.6g}", static_cast<double>(value));
        } else {
            return "<object>";
        }
    }

    inline void CheckImpl(
        bool passed,
        std::string_view expr,
        std::string detail,
        std::source_location loc) {
        RecordAssertion(passed, expr, std::move(detail), loc);
    }

    inline void RequireImpl(
        bool passed,
        std::string_view expr,
        std::string detail,
        std::source_location loc) {
        RecordAssertion(passed, expr, std::move(detail), loc);
        if (!passed) {
            throw TestAbortException{};
        }
    }

    template <typename Lhs, typename Rhs>
    void CheckEqImpl(
        const Lhs& lhs,
        const Rhs& rhs,
        std::string_view lhsExpr,
        std::string_view rhsExpr,
        std::source_location loc) {
        const bool ok = (lhs == rhs);
        std::string detail{};
        if (!ok) {
            detail = std::format("lhs ({}) = {}, rhs ({}) = {}",
                                 lhsExpr, FormatTestValue(lhs),
                                 rhsExpr, FormatTestValue(rhs));
        }
        RecordAssertion(ok, std::format("{} == {}", lhsExpr, rhsExpr), std::move(detail), loc);
    }

    template <typename Lhs, typename Rhs>
    void RequireEqImpl(
        const Lhs& lhs,
        const Rhs& rhs,
        std::string_view lhsExpr,
        std::string_view rhsExpr,
        std::source_location loc) {
        const bool ok = (lhs == rhs);
        std::string detail{};
        if (!ok) {
            detail = std::format("lhs ({}) = {}, rhs ({}) = {}",
                                 lhsExpr, FormatTestValue(lhs),
                                 rhsExpr, FormatTestValue(rhs));
        }
        RecordAssertion(ok, std::format("{} == {}", lhsExpr, rhsExpr), std::move(detail), loc);
        if (!ok) {
            throw TestAbortException{};
        }
    }

}  // namespace MORWHA::Tests

#define MORWHA_CONCAT_INNER(a, b) a##b
#define MORWHA_CONCAT(a, b) MORWHA_CONCAT_INNER(a, b)

#define MORWHA_TEST(suite, name)                                                                  \
    static void MORWHA_CONCAT(MorwhaTestFn_, __LINE__)();                                         \
    static ::MORWHA::Tests::AutoRegisterTest MORWHA_CONCAT(MorwhaTestReg_, __LINE__)(             \
        #suite, #name, &MORWHA_CONCAT(MorwhaTestFn_, __LINE__));                                  \
    static void MORWHA_CONCAT(MorwhaTestFn_, __LINE__)()

#define MORWHA_CHECK(expr) \
    ::MORWHA::Tests::CheckImpl(static_cast<bool>(expr), #expr, {}, std::source_location::current())

#define MORWHA_REQUIRE(expr) \
    ::MORWHA::Tests::RequireImpl(static_cast<bool>(expr), #expr, {}, std::source_location::current())

#define MORWHA_CHECK_EQ(lhs, rhs) \
    ::MORWHA::Tests::CheckEqImpl((lhs), (rhs), #lhs, #rhs, std::source_location::current())

#define MORWHA_REQUIRE_EQ(lhs, rhs) \
    ::MORWHA::Tests::RequireEqImpl((lhs), (rhs), #lhs, #rhs, std::source_location::current())
