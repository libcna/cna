// SPDX-License-Identifier: MS-PL
#pragma once

#include <cctype>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace CNA::Internal
{
    /** @brief Discriminates which alternative a JsonValue currently holds. */
    enum class JsonType
    {
        Null,
        Boolean,
        Number,
        String,
        Array,
        Object
    };

    /**
     * @brief A minimal, complete parsed JSON value (object/array/string/number/bool/null).
     *
     * Not a general-purpose JSON library -- just enough of the grammar for CNA's own `.cnb`
     * content documents (plan_cnb.md CNB-35): correct nesting/depth tracking, correct string
     * escape decoding (`\"`, `\\`, `\/`, `\b`, `\f`, `\n`, `\r`, `\t`, `\uXXXX` including
     * surrogate pairs), and correct number-token grammar (so `"1abc"` is a parse error, not a
     * silently-truncated `1`). Object member order is preserved; lookups are linear (`.cnb`
     * documents are small).
     */
    struct JsonValue
    {
        /** @brief Which alternative this value holds. */
        JsonType type = JsonType::Null;

        /** @brief Valid when @ref type is `Boolean`. */
        bool boolValue = false;

        /** @brief Valid when @ref type is `Number`. */
        double numberValue = 0.0;

        /** @brief Valid when @ref type is `String`. */
        std::string stringValue;

        /** @brief Valid when @ref type is `Array`. */
        std::vector<JsonValue> arrayValue;

        /** @brief Valid when @ref type is `Object`; preserves source member order. */
        std::vector<std::pair<std::string, JsonValue>> objectValue;

        /** @brief True if @ref type is `Object`. */
        [[nodiscard]] bool IsObject() const { return type == JsonType::Object; }

        /** @brief True if @ref type is `String`. */
        [[nodiscard]] bool IsString() const { return type == JsonType::String; }

        /** @brief True if @ref type is `Number`. */
        [[nodiscard]] bool IsNumber() const { return type == JsonType::Number; }

        /**
         * @brief Looks up a member by key when this value is an object.
         *
         * @param key Member name to find.
         * @return Pointer to the member's value, or `nullptr` if this is not an object or the
         *         key is absent.
         */
        [[nodiscard]] const JsonValue* FindMember(const std::string& key) const
        {
            if (type != JsonType::Object) return nullptr;
            for (const auto& [memberKey, memberValue] : objectValue)
            {
                if (memberKey == key) return &memberValue;
            }
            return nullptr;
        }
    };

    /** @brief Thrown by ParseJson() for any malformed JSON document. */
    class JsonParseException : public std::runtime_error
    {
    public:
        /**
         * @brief Constructs a JsonParseException with the given error message.
         * @param message Description of the parse failure, including position where practical.
         */
        explicit JsonParseException(const std::string& message) : std::runtime_error(message) {}
    };

    namespace Detail
    {
        class JsonParser
        {
        public:
            explicit JsonParser(const std::string& text) : text_(text) {}

            JsonValue ParseDocument()
            {
                SkipWhitespace();
                JsonValue root = ParseValue();
                SkipWhitespace();
                if (pos_ != text_.size())
                {
                    throw JsonParseException(
                        "Unexpected trailing content after JSON value at offset " +
                        std::to_string(pos_) + ".");
                }
                return root;
            }

        private:
            const std::string& text_;
            std::size_t pos_ = 0;

            [[nodiscard]] bool AtEnd() const { return pos_ >= text_.size(); }

            [[nodiscard]] char Peek() const
            {
                if (AtEnd()) throw JsonParseException("Unexpected end of JSON document.");
                return text_[pos_];
            }

            char Advance()
            {
                const char c = Peek();
                ++pos_;
                return c;
            }

            void SkipWhitespace()
            {
                while (!AtEnd())
                {
                    const char c = text_[pos_];
                    if (c == ' ' || c == '\t' || c == '\n' || c == '\r') { ++pos_; continue; }
                    break;
                }
            }

            void Expect(char expected)
            {
                if (AtEnd() || text_[pos_] != expected)
                {
                    throw JsonParseException(
                        std::string("Expected '") + expected + "' at offset " +
                        std::to_string(pos_) + ".");
                }
                ++pos_;
            }

            [[nodiscard]] bool TryConsumeLiteral(const char* literal)
            {
                const std::size_t len = std::string(literal).size();
                if (text_.compare(pos_, len, literal) == 0)
                {
                    pos_ += len;
                    return true;
                }
                return false;
            }

            JsonValue ParseValue()
            {
                SkipWhitespace();
                if (AtEnd()) throw JsonParseException("Unexpected end of JSON document.");

                const char c = Peek();
                if (c == '{') return ParseObject();
                if (c == '[') return ParseArray();
                if (c == '"') return ParseStringValue();
                if (c == 't' || c == 'f') return ParseBoolean();
                if (c == 'n') return ParseNull();
                if (c == '-' || (c >= '0' && c <= '9')) return ParseNumber();

                throw JsonParseException(
                    "Unexpected character '" + std::string(1, c) + "' at offset " +
                    std::to_string(pos_) + ".");
            }

            JsonValue ParseObject()
            {
                Expect('{');
                JsonValue result;
                result.type = JsonType::Object;

                SkipWhitespace();
                if (!AtEnd() && Peek() == '}') { ++pos_; return result; }

                while (true)
                {
                    SkipWhitespace();
                    if (AtEnd() || Peek() != '"')
                    {
                        throw JsonParseException(
                            "Expected string object key at offset " + std::to_string(pos_) + ".");
                    }
                    JsonValue key = ParseStringValue();

                    SkipWhitespace();
                    Expect(':');

                    JsonValue value = ParseValue();
                    result.objectValue.emplace_back(std::move(key.stringValue), std::move(value));

                    SkipWhitespace();
                    if (AtEnd()) throw JsonParseException("Unterminated JSON object.");
                    const char next = Advance();
                    if (next == ',') continue;
                    if (next == '}') break;
                    throw JsonParseException(
                        "Expected ',' or '}' in JSON object at offset " + std::to_string(pos_ - 1) + ".");
                }

                return result;
            }

            JsonValue ParseArray()
            {
                Expect('[');
                JsonValue result;
                result.type = JsonType::Array;

                SkipWhitespace();
                if (!AtEnd() && Peek() == ']') { ++pos_; return result; }

                while (true)
                {
                    result.arrayValue.push_back(ParseValue());
                    SkipWhitespace();
                    if (AtEnd()) throw JsonParseException("Unterminated JSON array.");
                    const char next = Advance();
                    if (next == ',') continue;
                    if (next == ']') break;
                    throw JsonParseException(
                        "Expected ',' or ']' in JSON array at offset " + std::to_string(pos_ - 1) + ".");
                }

                return result;
            }

            static void AppendUtf8(std::string& out, uint32_t codepoint)
            {
                if (codepoint <= 0x7F)
                {
                    out += static_cast<char>(codepoint);
                }
                else if (codepoint <= 0x7FF)
                {
                    out += static_cast<char>(0xC0 | (codepoint >> 6));
                    out += static_cast<char>(0x80 | (codepoint & 0x3F));
                }
                else if (codepoint <= 0xFFFF)
                {
                    out += static_cast<char>(0xE0 | (codepoint >> 12));
                    out += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
                    out += static_cast<char>(0x80 | (codepoint & 0x3F));
                }
                else
                {
                    out += static_cast<char>(0xF0 | (codepoint >> 18));
                    out += static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F));
                    out += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
                    out += static_cast<char>(0x80 | (codepoint & 0x3F));
                }
            }

            uint32_t ParseHex4()
            {
                if (pos_ + 4 > text_.size())
                {
                    throw JsonParseException("Truncated \\u escape in JSON string.");
                }
                uint32_t value = 0;
                for (int i = 0; i < 4; ++i)
                {
                    const char c = text_[pos_ + static_cast<std::size_t>(i)];
                    value <<= 4;
                    if (c >= '0' && c <= '9') value |= static_cast<uint32_t>(c - '0');
                    else if (c >= 'a' && c <= 'f') value |= static_cast<uint32_t>(c - 'a' + 10);
                    else if (c >= 'A' && c <= 'F') value |= static_cast<uint32_t>(c - 'A' + 10);
                    else throw JsonParseException("Invalid \\u escape in JSON string.");
                }
                pos_ += 4;
                return value;
            }

            JsonValue ParseStringValue()
            {
                Expect('"');
                std::string out;

                while (true)
                {
                    if (AtEnd()) throw JsonParseException("Unterminated JSON string.");
                    const char c = Advance();
                    if (c == '"') break;

                    if (c == '\\')
                    {
                        if (AtEnd()) throw JsonParseException("Unterminated escape in JSON string.");
                        const char esc = Advance();
                        switch (esc)
                        {
                            case '"':  out += '"';  break;
                            case '\\': out += '\\'; break;
                            case '/':  out += '/';  break;
                            case 'b':  out += '\b'; break;
                            case 'f':  out += '\f'; break;
                            case 'n':  out += '\n'; break;
                            case 'r':  out += '\r'; break;
                            case 't':  out += '\t'; break;
                            case 'u':
                            {
                                uint32_t codepoint = ParseHex4();
                                if (codepoint >= 0xD800 && codepoint <= 0xDBFF &&
                                    pos_ + 1 < text_.size() && text_[pos_] == '\\' && text_[pos_ + 1] == 'u')
                                {
                                    const std::size_t save = pos_;
                                    pos_ += 2;
                                    const uint32_t low = ParseHex4();
                                    if (low >= 0xDC00 && low <= 0xDFFF)
                                    {
                                        codepoint = 0x10000 + ((codepoint - 0xD800) << 10) + (low - 0xDC00);
                                    }
                                    else
                                    {
                                        pos_ = save; // not a valid low surrogate; leave it for the next iteration
                                    }
                                }
                                AppendUtf8(out, codepoint);
                                break;
                            }
                            default:
                                throw JsonParseException(
                                    std::string("Invalid escape '\\") + esc + "' in JSON string.");
                        }
                        continue;
                    }

                    if (static_cast<unsigned char>(c) < 0x20)
                    {
                        throw JsonParseException("Unescaped control character in JSON string.");
                    }

                    out += c;
                }

                JsonValue result;
                result.type = JsonType::String;
                result.stringValue = std::move(out);
                return result;
            }

            JsonValue ParseNumber()
            {
                const std::size_t start = pos_;

                if (!AtEnd() && Peek() == '-') ++pos_;

                if (AtEnd() || !std::isdigit(static_cast<unsigned char>(Peek())))
                {
                    throw JsonParseException("Invalid JSON number at offset " + std::to_string(start) + ".");
                }
                if (Peek() == '0')
                {
                    ++pos_;
                }
                else
                {
                    while (!AtEnd() && std::isdigit(static_cast<unsigned char>(Peek()))) ++pos_;
                }

                if (!AtEnd() && Peek() == '.')
                {
                    ++pos_;
                    if (AtEnd() || !std::isdigit(static_cast<unsigned char>(Peek())))
                    {
                        throw JsonParseException("Invalid JSON number (bad fraction) at offset " +
                                                  std::to_string(start) + ".");
                    }
                    while (!AtEnd() && std::isdigit(static_cast<unsigned char>(Peek()))) ++pos_;
                }

                if (!AtEnd() && (Peek() == 'e' || Peek() == 'E'))
                {
                    ++pos_;
                    if (!AtEnd() && (Peek() == '+' || Peek() == '-')) ++pos_;
                    if (AtEnd() || !std::isdigit(static_cast<unsigned char>(Peek())))
                    {
                        throw JsonParseException("Invalid JSON number (bad exponent) at offset " +
                                                  std::to_string(start) + ".");
                    }
                    while (!AtEnd() && std::isdigit(static_cast<unsigned char>(Peek()))) ++pos_;
                }

                // A number token must not be immediately followed by another number/identifier
                // character (e.g. "1abc" is not "1" followed by garbage -- it is not valid JSON
                // at all). This is what makes trailing-garbage version values a parse error
                // instead of a silently-truncated std::stoi() result.
                if (!AtEnd())
                {
                    const char after = Peek();
                    if (std::isalnum(static_cast<unsigned char>(after)) || after == '.' || after == '_')
                    {
                        throw JsonParseException(
                            "Invalid JSON number: unexpected trailing character '" +
                            std::string(1, after) + "' at offset " + std::to_string(pos_) + ".");
                    }
                }

                JsonValue result;
                result.type = JsonType::Number;
                result.numberValue = std::stod(text_.substr(start, pos_ - start));
                return result;
            }

            JsonValue ParseBoolean()
            {
                JsonValue result;
                result.type = JsonType::Boolean;
                if (TryConsumeLiteral("true")) { result.boolValue = true; return result; }
                if (TryConsumeLiteral("false")) { result.boolValue = false; return result; }
                throw JsonParseException("Invalid JSON literal at offset " + std::to_string(pos_) + ".");
            }

            JsonValue ParseNull()
            {
                if (TryConsumeLiteral("null")) { return JsonValue{}; }
                throw JsonParseException("Invalid JSON literal at offset " + std::to_string(pos_) + ".");
            }
        };
    }

    /**
     * @brief Parses a complete JSON document from text.
     *
     * @param text Raw JSON document text.
     * @return The parsed root value.
     * @throws JsonParseException if @p text is not a single, complete, valid JSON document
     *         (including trailing content after the root value).
     */
    inline JsonValue ParseJson(const std::string& text)
    {
        Detail::JsonParser parser(text);
        return parser.ParseDocument();
    }
}
