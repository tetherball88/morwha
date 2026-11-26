#include "JsonDom.h"

#include <cmath>

namespace MORWHA::State::Detail {

    namespace {

        class JsonParser {
        public:
            JsonParser(std::string_view input, bool allowDuplicateKeys) noexcept
                : text_(input), allowDuplicateKeys_(allowDuplicateKeys) {
                if (text_.size() >= 3 &&
                    static_cast<unsigned char>(text_[0]) == 0xEFu &&
                    static_cast<unsigned char>(text_[1]) == 0xBBu &&
                    static_cast<unsigned char>(text_[2]) == 0xBFu) {
                    pos_ = 3;
                }
            }

            bool ParseRoot(JsonValue& out, std::string* outError) {
                SkipWhitespace();
                if (!ParseValue(out, 0, outError)) {
                    return false;
                }
                SkipWhitespace();
                if (pos_ != text_.size()) {
                    return SetJsonError(outError, std::format("Unexpected trailing JSON data at byte {}", pos_));
                }
                return true;
            }

        private:
            void SkipWhitespace() noexcept {
                while (pos_ < text_.size()) {
                    const char ch = text_[pos_];
                    if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') {
                        ++pos_;
                    } else {
                        break;
                    }
                }
            }

            [[nodiscard]] bool Consume(char expected) noexcept {
                if (pos_ < text_.size() && text_[pos_] == expected) {
                    ++pos_;
                    return true;
                }
                return false;
            }

            static void AppendUtf8Codepoint(std::string& out, std::uint32_t cp) {
                if (cp <= 0x7Fu) {
                    out.push_back(static_cast<char>(cp));
                } else if (cp <= 0x7FFu) {
                    out.push_back(static_cast<char>(0xC0u | (cp >> 6)));
                    out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
                } else if (cp <= 0xFFFFu) {
                    out.push_back(static_cast<char>(0xE0u | (cp >> 12)));
                    out.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
                    out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
                } else {
                    out.push_back(static_cast<char>(0xF0u | (cp >> 18)));
                    out.push_back(static_cast<char>(0x80u | ((cp >> 12) & 0x3Fu)));
                    out.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
                    out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
                }
            }

            bool ParseHex4(std::uint16_t& outUnit) noexcept {
                if ((text_.size() - pos_) < 4) {
                    return false;
                }
                std::uint16_t val = 0;
                const auto [ptr, ec] = std::from_chars(text_.data() + pos_, text_.data() + pos_ + 4, val, 16);
                if (ec != std::errc{} || ptr != text_.data() + pos_ + 4) {
                    return false;
                }
                pos_ += 4;
                outUnit = val;
                return true;
            }

            bool ParseString(std::string& out, std::string* outError) {
                if (!Consume('"')) {
                    return SetJsonError(outError, std::format("Expected '\"' at byte {}", pos_));
                }
                out.clear();
                while (pos_ < text_.size()) {
                    const unsigned char ch = static_cast<unsigned char>(text_[pos_++]);
                    if (ch == '"') {
                        return true;
                    }
                    if (ch < 0x20u) {
                        return SetJsonError(outError, "Unescaped control character in JSON string");
                    }
                    if (ch != '\\') {
                        out.push_back(static_cast<char>(ch));
                        continue;
                    }
                    if (pos_ >= text_.size()) {
                        return SetJsonError(outError, "Unterminated JSON string escape");
                    }
                    const char esc = text_[pos_++];
                    switch (esc) {
                        case '"':
                            out.push_back('"');
                            break;
                        case '\\':
                            out.push_back('\\');
                            break;
                        case '/':
                            out.push_back('/');
                            break;
                        case 'b':
                            out.push_back('\b');
                            break;
                        case 'f':
                            out.push_back('\f');
                            break;
                        case 'n':
                            out.push_back('\n');
                            break;
                        case 'r':
                            out.push_back('\r');
                            break;
                        case 't':
                            out.push_back('\t');
                            break;
                        case 'u': {
                            std::uint16_t u1 = 0;
                            if (!ParseHex4(u1)) {
                                return SetJsonError(outError, "Invalid \\uXXXX escape in JSON string");
                            }
                            std::uint32_t cp = u1;
                            if (u1 >= 0xD800u && u1 <= 0xDBFFu) {
                                if ((text_.size() - pos_) < 2 || text_[pos_] != '\\' || text_[pos_ + 1] != 'u') {
                                    return SetJsonError(outError, "Unpaired high surrogate in JSON string");
                                }
                                pos_ += 2;
                                std::uint16_t u2 = 0;
                                if (!ParseHex4(u2) || u2 < 0xDC00u || u2 > 0xDFFFu) {
                                    return SetJsonError(outError, "Invalid low surrogate in JSON string");
                                }
                                cp = 0x10000u + (((static_cast<std::uint32_t>(u1) - 0xD800u) << 10) |
                                                 (static_cast<std::uint32_t>(u2) - 0xDC00u));
                            } else if (u1 >= 0xDC00u && u1 <= 0xDFFFu) {
                                return SetJsonError(outError, "Unexpected lone low surrogate in JSON string");
                            }
                            AppendUtf8Codepoint(out, cp);
                            break;
                        }
                        default:
                            return SetJsonError(outError, "Unknown escape sequence in JSON string");
                    }
                }
                return SetJsonError(outError, "Unterminated JSON string");
            }

            bool ParseNumber(JsonNumber& out, std::string* outError) {
                const std::size_t start = pos_;
                bool isFloat = false;
                if (pos_ < text_.size() && text_[pos_] == '-') {
                    ++pos_;
                }
                if (pos_ >= text_.size()) {
                    return SetJsonError(outError, "Truncated JSON number");
                }
                if (text_[pos_] == '0') {
                    ++pos_;
                } else if (text_[pos_] >= '1' && text_[pos_] <= '9') {
                    while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9') {
                        ++pos_;
                    }
                } else {
                    return SetJsonError(outError, std::format("Invalid JSON number at byte {}", pos_));
                }
                if (pos_ < text_.size() && text_[pos_] == '.') {
                    isFloat = true;
                    ++pos_;
                    if (pos_ >= text_.size() || text_[pos_] < '0' || text_[pos_] > '9') {
                        return SetJsonError(outError, "Invalid fractional part in JSON number");
                    }
                    while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9') {
                        ++pos_;
                    }
                }
                if (pos_ < text_.size() && (text_[pos_] == 'e' || text_[pos_] == 'E')) {
                    isFloat = true;
                    ++pos_;
                    if (pos_ < text_.size() && (text_[pos_] == '+' || text_[pos_] == '-')) {
                        ++pos_;
                    }
                    if (pos_ >= text_.size() || text_[pos_] < '0' || text_[pos_] > '9') {
                        return SetJsonError(outError, "Invalid exponent in JSON number");
                    }
                    while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9') {
                        ++pos_;
                    }
                }
                out.raw = std::string(text_.substr(start, pos_ - start));
                out.isFloat = isFloat;
                return true;
            }

            bool ParseValue(JsonValue& out, std::size_t depth, std::string* outError) {
                if (depth > kMaxJsonDepth) {
                    return SetJsonError(outError, "JSON nesting depth limit exceeded");
                }
                SkipWhitespace();
                if (pos_ >= text_.size()) {
                    return SetJsonError(outError, "Unexpected end of JSON input");
                }
                const char ch = text_[pos_];
                if (ch == '{') {
                    return ParseObject(out, depth + 1, outError);
                }
                if (ch == '[') {
                    return ParseArray(out, depth + 1, outError);
                }
                if (ch == '"') {
                    std::string s;
                    if (!ParseString(s, outError)) {
                        return false;
                    }
                    out.data = std::move(s);
                    return true;
                }
                if (ch == 't' && text_.substr(pos_, 4) == "true") {
                    pos_ += 4;
                    out.data = true;
                    return true;
                }
                if (ch == 'f' && text_.substr(pos_, 5) == "false") {
                    pos_ += 5;
                    out.data = false;
                    return true;
                }
                if (ch == 'n' && text_.substr(pos_, 4) == "null") {
                    pos_ += 4;
                    out.data = nullptr;
                    return true;
                }
                if (ch == '-' || (ch >= '0' && ch <= '9')) {
                    JsonNumber num;
                    if (!ParseNumber(num, outError)) {
                        return false;
                    }
                    out.data = std::move(num);
                    return true;
                }
                return SetJsonError(outError, std::format("Unexpected JSON token at byte {}", pos_));
            }

            bool ParseArray(JsonValue& out, std::size_t depth, std::string* outError) {
                ++pos_;  // '['
                JsonArray arr;
                SkipWhitespace();
                if (Consume(']')) {
                    out.data = std::move(arr);
                    return true;
                }
                while (true) {
                    JsonValue elem;
                    if (!ParseValue(elem, depth, outError)) {
                        return false;
                    }
                    arr.push_back(std::move(elem));
                    SkipWhitespace();
                    if (Consume(']')) {
                        out.data = std::move(arr);
                        return true;
                    }
                    if (!Consume(',')) {
                        return SetJsonError(outError, std::format("Expected ',' or ']' at byte {}", pos_));
                    }
                }
            }

            bool ParseObject(JsonValue& out, std::size_t depth, std::string* outError) {
                ++pos_;  // '{'
                JsonObject obj;
                SkipWhitespace();
                if (Consume('}')) {
                    out.data = std::move(obj);
                    return true;
                }
                while (true) {
                    SkipWhitespace();
                    std::string key;
                    if (!ParseString(key, outError)) {
                        return false;
                    }
                    SkipWhitespace();
                    if (!Consume(':')) {
                        return SetJsonError(outError, std::format("Expected ':' at byte {}", pos_));
                    }
                    JsonValue val;
                    if (!ParseValue(val, depth, outError)) {
                        return false;
                    }
                    if (allowDuplicateKeys_) {
                        obj.insert_or_assign(std::move(key), std::move(val));
                    } else if (!obj.emplace(std::move(key), std::move(val)).second) {
                        return SetJsonError(outError, "Duplicate key in JSON object");
                    }
                    SkipWhitespace();
                    if (Consume('}')) {
                        out.data = std::move(obj);
                        return true;
                    }
                    if (!Consume(',')) {
                        return SetJsonError(outError, std::format("Expected ',' or '}}' at byte {}", pos_));
                    }
                }
            }

            std::string_view text_;
            std::size_t pos_{ 0 };
            bool allowDuplicateKeys_{ false };
        };

    }  // namespace

    bool SetJsonError(std::string* outError, std::string msg) {
        if (outError) {
            *outError = std::move(msg);
        }
        return false;
    }

    bool ParseJsonDocument(
        std::string_view input,
        JsonValue& out,
        std::string* outError,
        bool allowDuplicateKeys) {
        JsonParser parser(input, allowDuplicateKeys);
        return parser.ParseRoot(out, outError);
    }

    bool ParseDoubleFromJson(
        const JsonValue& val,
        double& out,
        std::string_view field,
        std::string* outError) {
        const auto* num = val.AsNumber();
        if (!num) {
            return SetJsonError(outError, std::format("Field '{}' requires number", field));
        }
        const auto [ptr, ec] = std::from_chars(num->raw.data(), num->raw.data() + num->raw.size(), out);
        if (ec != std::errc{} || ptr != num->raw.data() + num->raw.size() || !std::isfinite(out)) {
            return SetJsonError(outError, std::format("Field '{}' invalid float '{}'", field, num->raw));
        }
        return true;
    }

    bool ParseFloatFromJson(
        const JsonValue& val,
        float& out,
        std::string_view field,
        std::string* outError) {
        const auto* num = val.AsNumber();
        if (!num) {
            return SetJsonError(outError, std::format("Field '{}' requires number", field));
        }
        const auto [ptr, ec] = std::from_chars(num->raw.data(), num->raw.data() + num->raw.size(), out);
        if (ec != std::errc{} || ptr != num->raw.data() + num->raw.size() || !std::isfinite(out)) {
            return SetJsonError(outError, std::format("Field '{}' invalid float '{}'", field, num->raw));
        }
        return true;
    }

}  // namespace MORWHA::State::Detail
