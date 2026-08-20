// SPDX-License-Identifier: MS-PL
#pragma once

#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
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
     * Not a general-purpose JSON library -- just enough of the grammar for CNA's own `.cnj`
     * content documents (plans/plan_cnj.md CNB-35): correct nesting/depth tracking, correct string
     * escape decoding (`\"`, `\\`, `\/`, `\b`, `\f`, `\n`, `\r`, `\t`, `\uXXXX` including
     * surrogate pairs), and correct number-token grammar (so `"1abc"` is a parse error, not a
     * silently-truncated `1`). Object member order is preserved; lookups are linear (`.cnj`
     * documents are small).
     */
    struct JsonValue;

    /**
     * @brief One member of a JSON object: its key and its value, in source order.
     *
     * A distinct type rather than `std::pair<std::string, JsonValue>` because a pair cannot be
     * instantiated with a type that is still incomplete, which JsonValue necessarily is inside
     * its own definition. `std::vector` may name an incomplete element type; `std::pair` may not,
     * and libstdc++ enforces that under clang. Its two members are public and in this order, so
     * `for (auto& [key, value] : objectValue)` reads exactly as it did before.
     */
    struct JsonMember;

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
        std::vector<JsonMember> objectValue;

        /**
         * @brief Default-constructs a null value.
         *
         * These six are user-declared and defined out of line only so that JsonMember can stay
         * incomplete inside this definition: an implicit destructor here would instantiate
         * `std::vector<JsonMember>::~vector` on the spot, and a vector may name an incomplete
         * element type only until one of its members is referenced.
         */
        JsonValue();
        /** @brief Destroys the value and everything nested in it. */
        ~JsonValue();
        /** @brief Copies a value, including nested arrays and object members. */
        JsonValue(const JsonValue& other);
        /** @brief Moves a value, including nested arrays and object members. */
        JsonValue(JsonValue&& other) noexcept;
        /** @brief Copy-assigns a value, including nested arrays and object members. */
        JsonValue& operator=(const JsonValue& other);
        /** @brief Move-assigns a value, including nested arrays and object members. */
        JsonValue& operator=(JsonValue&& other) noexcept;

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
        [[nodiscard]] const JsonValue* FindMember(const std::string& key) const;

        // --- Construction helpers (Task 4.2: achievement/leaderboard local persistence) ---

        /** @brief Creates an empty JSON object value. */
        [[nodiscard]] static JsonValue MakeObject() { JsonValue v; v.type = JsonType::Object; return v; }
        /** @brief Creates an empty JSON array value. */
        [[nodiscard]] static JsonValue MakeArray() { JsonValue v; v.type = JsonType::Array; return v; }
        /** @brief Creates a JSON string value. */
        [[nodiscard]] static JsonValue MakeString(std::string s) { JsonValue v; v.type = JsonType::String; v.stringValue = std::move(s); return v; }
        /** @brief Creates a JSON number value. */
        [[nodiscard]] static JsonValue MakeNumber(double n) { JsonValue v; v.type = JsonType::Number; v.numberValue = n; return v; }
        /** @brief Creates a JSON boolean value. */
        [[nodiscard]] static JsonValue MakeBool(bool b) { JsonValue v; v.type = JsonType::Boolean; v.boolValue = b; return v; }

        /**
         * @brief Sets (adds or overwrites) a member on this object value.
         *
         * @param key Member name.
         * @param value Member value.
         */
        void Set(const std::string& key, JsonValue value);
    };

    struct JsonMember
    {
        /** @brief The member's name, exactly as it appeared in the document. */
        std::string key;

        /** @brief The member's value. */
        JsonValue value;
    };

    inline JsonValue::JsonValue() = default;
    inline JsonValue::~JsonValue() = default;
    inline JsonValue::JsonValue(const JsonValue& other) = default;
    inline JsonValue::JsonValue(JsonValue&& other) noexcept = default;
    inline JsonValue& JsonValue::operator=(const JsonValue& other) = default;
    inline JsonValue& JsonValue::operator=(JsonValue&& other) noexcept = default;

    inline const JsonValue* JsonValue::FindMember(const std::string& key) const
    {
        if (type != JsonType::Object) return nullptr;
        for (const auto& [memberKey, memberValue] : objectValue)
        {
            if (memberKey == key) return &memberValue;
        }
        return nullptr;
    }

    inline void JsonValue::Set(const std::string& key, JsonValue value)
    {
        for (auto& [memberKey, memberValue] : objectValue)
        {
            if (memberKey == key) { memberValue = std::move(value); return; }
        }
        objectValue.push_back(JsonMember{key, std::move(value)});
    }

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
                    result.objectValue.push_back(
                        JsonMember{std::move(key.stringValue), std::move(value)});

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

    namespace Detail
    {
        inline void WriteJsonString(std::string& out, const std::string& s)
        {
            out += '"';
            for (const char c : s)
            {
                switch (c)
                {
                    case '"':  out += "\\\""; break;
                    case '\\': out += "\\\\"; break;
                    case '\b': out += "\\b"; break;
                    case '\f': out += "\\f"; break;
                    case '\n': out += "\\n"; break;
                    case '\r': out += "\\r"; break;
                    case '\t': out += "\\t"; break;
                    default:
                        if (static_cast<unsigned char>(c) < 0x20)
                        {
                            char buf[8];
                            std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(c));
                            out += buf;
                        }
                        else
                        {
                            out += c;
                        }
                }
            }
            out += '"';
        }

        inline void WriteJsonValue(std::string& out, const JsonValue& value)
        {
            switch (value.type)
            {
                case JsonType::Null:
                    out += "null";
                    break;
                case JsonType::Boolean:
                    out += value.boolValue ? "true" : "false";
                    break;
                case JsonType::Number:
                {
                    // Numbers used by this codebase's own local persistence are always either
                    // exact integers (ticks, ratings, counts) or ordinary finite doubles/floats -
                    // format as an integer when the value round-trips exactly, matching what a
                    // human/diff would expect (no trailing ".0" on a Ticks value), falling back to
                    // %.17g (round-trip-safe for IEEE 754 double) otherwise.
                    char buf[64];
                    const double n = value.numberValue;
                    if (n == static_cast<double>(static_cast<long long>(n))
                        && std::abs(n) < 1e18)
                    {
                        std::snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(n));
                    }
                    else
                    {
                        std::snprintf(buf, sizeof(buf), "%.17g", n);
                    }
                    out += buf;
                    break;
                }
                case JsonType::String:
                    WriteJsonString(out, value.stringValue);
                    break;
                case JsonType::Array:
                {
                    out += '[';
                    bool first = true;
                    for (const JsonValue& element : value.arrayValue)
                    {
                        if (!first) out += ',';
                        first = false;
                        WriteJsonValue(out, element);
                    }
                    out += ']';
                    break;
                }
                case JsonType::Object:
                {
                    out += '{';
                    bool first = true;
                    for (const auto& [memberKey, memberValue] : value.objectValue)
                    {
                        if (!first) out += ',';
                        first = false;
                        WriteJsonString(out, memberKey);
                        out += ':';
                        WriteJsonValue(out, memberValue);
                    }
                    out += '}';
                    break;
                }
            }
        }
    }

    /**
     * @brief Serializes a JsonValue tree to compact JSON text.
     *
     * The inverse of ParseJson() - `ParseJson(WriteJson(v))` round-trips `v` exactly for every
     * value this codebase's own local persistence writes (ticks-as-integers, plain strings,
     * objects/arrays - the same "just enough of the grammar" scope ParseJson documents).
     *
     * @param value The value to serialize.
     * @return Compact (no extraneous whitespace) JSON text.
     */
    inline std::string WriteJson(const JsonValue& value)
    {
        std::string out;
        Detail::WriteJsonValue(out, value);
        return out;
    }
}
