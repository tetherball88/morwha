#pragma once

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

namespace MORWHA::State::Detail {

    inline constexpr std::size_t kMaxJsonDepth = 32u;

    struct JsonNumber {
        std::string raw{};
        bool isFloat{ false };
    };

    struct JsonValue;
    using JsonArray = std::vector<JsonValue>;
    using JsonObject = std::unordered_map<std::string, JsonValue>;

    struct JsonValue {
        std::variant<std::nullptr_t, bool, JsonNumber, std::string, JsonArray, JsonObject> data{ nullptr };

        [[nodiscard]] bool IsNull() const noexcept { return std::holds_alternative<std::nullptr_t>(data); }
        [[nodiscard]] const bool* AsBool() const noexcept { return std::get_if<bool>(&data); }
        [[nodiscard]] const JsonNumber* AsNumber() const noexcept { return std::get_if<JsonNumber>(&data); }
        [[nodiscard]] const std::string* AsString() const noexcept { return std::get_if<std::string>(&data); }
        [[nodiscard]] const JsonArray* AsArray() const noexcept { return std::get_if<JsonArray>(&data); }
        [[nodiscard]] const JsonObject* AsObject() const noexcept { return std::get_if<JsonObject>(&data); }
    };

    bool SetJsonError(std::string* outError, std::string msg);

    [[nodiscard]] bool ParseJsonDocument(
        std::string_view input,
        JsonValue& out,
        std::string* outError,
        bool allowDuplicateKeys = false);

    [[nodiscard]] bool ParseDoubleFromJson(
        const JsonValue& val,
        double& out,
        std::string_view field,
        std::string* outError);

    [[nodiscard]] bool ParseFloatFromJson(
        const JsonValue& val,
        float& out,
        std::string_view field,
        std::string* outError);

    template <typename UIntT>
    [[nodiscard]] bool ParseUIntFromJson(
        const JsonValue& val,
        UIntT& out,
        std::string_view field,
        std::string* outError) {
        if (const auto* num = val.AsNumber()) {
            if (num->isFloat || num->raw.empty() || num->raw.front() == '-') {
                return SetJsonError(outError, std::format("Field '{}' requires unsigned integer", field));
            }
            const auto [ptr, ec] = std::from_chars(num->raw.data(), num->raw.data() + num->raw.size(), out, 10);
            if (ec != std::errc{} || ptr != num->raw.data() + num->raw.size()) {
                return SetJsonError(outError, std::format("Field '{}' integer overflow or syntax error", field));
            }
            return true;
        }
        if (const auto* str = val.AsString()) {
            std::string_view sv(*str);
            int base = 10;
            if (sv.size() >= 2 && sv[0] == '0' && (sv[1] == 'x' || sv[1] == 'X')) {
                base = 16;
                sv.remove_prefix(2);
            }
            if (sv.empty()) {
                return SetJsonError(outError, std::format("Field '{}' has empty integer string", field));
            }
            const auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), out, base);
            if (ec != std::errc{} || ptr != sv.data() + sv.size()) {
                return SetJsonError(outError, std::format("Field '{}' has invalid integer string '{}'", field, *str));
            }
            return true;
        }
        return SetJsonError(outError, std::format("Field '{}' requires integer or hex string", field));
    }

    template <typename IntT>
    [[nodiscard]] bool ParseSignedIntFromJson(
        const JsonValue& val,
        IntT& out,
        std::string_view field,
        std::string* outError) {
        const auto* num = val.AsNumber();
        if (!num || num->isFloat) {
            return SetJsonError(outError, std::format("Field '{}' requires integer", field));
        }
        const auto [ptr, ec] = std::from_chars(num->raw.data(), num->raw.data() + num->raw.size(), out, 10);
        if (ec != std::errc{} || ptr != num->raw.data() + num->raw.size()) {
            return SetJsonError(outError, std::format("Field '{}' signed integer out of range", field));
        }
        return true;
    }

}  // namespace MORWHA::State::Detail
