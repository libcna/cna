// SPDX-License-Identifier: MS-PL
#include "CNA/Content/Pipeline/DirectXFileReader.hpp"

#include <cctype>
#include <stdexcept>
#include <string>
#include <charconv>
#include <cstring>
#include <string_view>

namespace CNA::Content::Pipeline
{
    namespace
    {
        /** @brief What a token is, in both encodings. The binary values are the format's own. */
        enum class Kind
        {
            End,
            Name,        // a bare identifier: a template name, an object name, a reference
            String,      // a quoted string
            Integer,
            Float,
            OpenBrace,
            CloseBrace,
            OpenParen,
            CloseParen,
            OpenBracket,
            CloseBracket,
            OpenAngle,
            CloseAngle,
            Dot,
            Comma,
            Semicolon,
        };

        struct Token
        {
            Kind kind = Kind::End;
            std::string text;
            double number = 0.0;
        };

        [[noreturn]] void Fail(DirectXFileError error, const std::string& detail)
        {
            throw DirectXFileException(error, detail);
        }

        /** @brief Reads the text encoding, one token at a time. */
        class TextLexer
        {
        public:
            TextLexer(std::span<const std::uint8_t> bytes, std::size_t at,
                      const DirectXFileLimits& limits)
                : bytes_(bytes), at_(at), limits_(limits)
            {
            }

            [[nodiscard]] Token Next()
            {
                SkipSpaceAndComments();
                if (at_ >= bytes_.size())
                {
                    return Token{Kind::End, {}, 0.0};
                }
                const char c = static_cast<char>(bytes_[at_]);
                switch (c)
                {
                    case '{': ++at_; return Token{Kind::OpenBrace, {}, 0.0};
                    case '}': ++at_; return Token{Kind::CloseBrace, {}, 0.0};
                    case '(': ++at_; return Token{Kind::OpenParen, {}, 0.0};
                    case ')': ++at_; return Token{Kind::CloseParen, {}, 0.0};
                    case '[': ++at_; return Token{Kind::OpenBracket, {}, 0.0};
                    case ']': ++at_; return Token{Kind::CloseBracket, {}, 0.0};
                    case '<': return Guid();
                    case ',': ++at_; return Token{Kind::Comma, {}, 0.0};
                    case ';': ++at_; return Token{Kind::Semicolon, {}, 0.0};
                    case '.': ++at_; return Token{Kind::Dot, {}, 0.0};
                    case '"': return String();
                    default: break;
                }
                if (c == '-' || c == '+' || (std::isdigit(static_cast<unsigned char>(c)) != 0))
                {
                    return Number();
                }
                if (std::isalpha(static_cast<unsigned char>(c)) != 0 || c == '_')
                {
                    return Name();
                }
                Fail(DirectXFileError::ParseError,
                     std::string("the character '") + c + "' does not begin any token.");
            }

        private:
            void SkipSpaceAndComments()
            {
                for (;;)
                {
                    while (at_ < bytes_.size() &&
                           std::isspace(static_cast<unsigned char>(bytes_[at_])) != 0)
                    {
                        ++at_;
                    }
                    const bool hash = at_ < bytes_.size() && bytes_[at_] == '#';
                    const bool slashes = at_ + 1u < bytes_.size() && bytes_[at_] == '/' &&
                                         bytes_[at_ + 1u] == '/';
                    if (!hash && !slashes)
                    {
                        return;
                    }
                    while (at_ < bytes_.size() && bytes_[at_] != '\n')
                    {
                        ++at_;
                    }
                }
            }

            /** @brief A `<...>` template identifier, skipped whole: it names nothing this reads. */
            [[nodiscard]] Token Guid()
            {
                ++at_;
                while (at_ < bytes_.size() && bytes_[at_] != '>')
                {
                    ++at_;
                }
                if (at_ >= bytes_.size())
                {
                    Fail(DirectXFileError::ParseError, "a template identifier is never closed.");
                }
                ++at_;
                return Token{Kind::OpenAngle, {}, 0.0};
            }

            [[nodiscard]] Token String()
            {
                ++at_;
                const std::size_t start = at_;
                while (at_ < bytes_.size() && bytes_[at_] != '"')
                {
                    ++at_;
                }
                if (at_ >= bytes_.size())
                {
                    Fail(DirectXFileError::ParseError, "a string is never closed.");
                }
                const std::size_t length = at_ - start;
                if (length > limits_.maximumStringLength)
                {
                    Fail(DirectXFileError::ParseError, "a string is longer than this reader accepts.");
                }
                ++at_;
                return Token{Kind::String,
                             std::string(reinterpret_cast<const char*>(bytes_.data()) + start, length),
                             0.0};
            }

            [[nodiscard]] Token Number()
            {
                const std::size_t start = at_;
                if (bytes_[at_] == '-' || bytes_[at_] == '+')
                {
                    ++at_;
                }
                bool point = false;
                while (at_ < bytes_.size())
                {
                    const char c = static_cast<char>(bytes_[at_]);
                    if (std::isdigit(static_cast<unsigned char>(c)) != 0)
                    {
                        ++at_;
                    }
                    else if (c == '.' && !point)
                    {
                        point = true;
                        ++at_;
                    }
                    else if ((c == 'e' || c == 'E') && at_ + 1u < bytes_.size())
                    {
                        point = true;
                        ++at_;
                        if (bytes_[at_] == '-' || bytes_[at_] == '+')
                        {
                            ++at_;
                        }
                    }
                    else
                    {
                        break;
                    }
                }
                const std::string text(reinterpret_cast<const char*>(bytes_.data()) + start, at_ - start);
                double value = 0.0;
                try
                {
                    value = std::stod(text);
                }
                catch (const std::exception&)
                {
                    Fail(DirectXFileError::ParseError, "'" + text + "' is not a number.");
                }
                return Token{point ? Kind::Float : Kind::Integer, {}, value};
            }

            [[nodiscard]] Token Name()
            {
                const std::size_t start = at_;
                while (at_ < bytes_.size())
                {
                    const char c = static_cast<char>(bytes_[at_]);
                    if (std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_' || c == '-')
                    {
                        ++at_;
                    }
                    else
                    {
                        break;
                    }
                }
                const std::size_t length = at_ - start;
                if (length > limits_.maximumStringLength)
                {
                    Fail(DirectXFileError::ParseError, "a name is longer than this reader accepts.");
                }
                return Token{Kind::Name,
                             std::string(reinterpret_cast<const char*>(bytes_.data()) + start, length),
                             0.0};
            }

            std::span<const std::uint8_t> bytes_;
            std::size_t at_ = 0u;
            const DirectXFileLimits& limits_;
        };

        /** @brief Reads the binary token stream, which carries the same object model. */
        class BinaryLexer
        {
        public:
            BinaryLexer(std::span<const std::uint8_t> bytes, std::size_t at,
                        const DirectXFileLimits& limits)
                : bytes_(bytes), at_(at), limits_(limits)
            {
            }

            [[nodiscard]] Token Next()
            {
                // A list token expands into the numbers it carries, one Next() at a time, so the
                // parser above sees the same stream a text file gives it.
                if (pending_ > 0u)
                {
                    --pending_;
                    return TakePendingNumber();
                }
                if (at_ + 2u > bytes_.size())
                {
                    return Token{Kind::End, {}, 0.0};
                }
                const std::uint16_t token = Read16();
                switch (token)
                {
                    case 1: return ReadName(Kind::Name);
                    case 2: return ReadName(Kind::String);
                    case 3: { const std::uint32_t value = Read32();
                              return Token{Kind::Integer, {}, static_cast<double>(value)}; }
                    case 5: SkipGuid(); return Token{Kind::OpenAngle, {}, 0.0};
                    case 6: pending_ = Read32(); pendingFloat_ = false; Require(pending_);
                            return Next();
                    case 7: pending_ = Read32(); pendingFloat_ = true; Require(pending_);
                            return Next();
                    case 10: return Token{Kind::OpenBrace, {}, 0.0};
                    case 11: return Token{Kind::CloseBrace, {}, 0.0};
                    case 12: return Token{Kind::OpenParen, {}, 0.0};
                    case 13: return Token{Kind::CloseParen, {}, 0.0};
                    case 14: return Token{Kind::OpenBracket, {}, 0.0};
                    case 15: return Token{Kind::CloseBracket, {}, 0.0};
                    case 16: return Token{Kind::OpenAngle, {}, 0.0};
                    case 17: return Token{Kind::CloseAngle, {}, 0.0};
                    case 18: return Token{Kind::Dot, {}, 0.0};
                    case 19: return Token{Kind::Comma, {}, 0.0};
                    case 20: return Token{Kind::Semicolon, {}, 0.0};
                    // Every remaining token is a keyword (template, array, and the primitive type
                    // names), which a reader that does not interpret templates skips.
                    case 31: case 40: case 41: case 42: case 43: case 44: case 45: case 46:
                    case 47: case 48: case 49: case 50: case 51:
                        return Token{Kind::Name, "<keyword>", 0.0};
                    default:
                        Fail(DirectXFileError::ParseError,
                             "the binary token " + std::to_string(token) + " is not one this reader knows.");
                }
            }

        private:
            void Require(const std::size_t count) const
            {
                if (count > limits_.maximumNumbersPerObject)
                {
                    Fail(DirectXFileError::ParseError, "a list is longer than this reader accepts.");
                }
                const std::size_t bytesNeeded = count * 4u;
                if (bytesNeeded / 4u != count || at_ + bytesNeeded > bytes_.size())
                {
                    Fail(DirectXFileError::ParseError, "a list runs past the end of the file.");
                }
            }

            [[nodiscard]] Token TakePendingNumber()
            {
                if (pendingFloat_)
                {
                    float value = 0.0f;
                    std::memcpy(&value, bytes_.data() + at_, sizeof(value));
                    at_ += 4u;
                    return Token{Kind::Float, {}, static_cast<double>(value)};
                }
                const std::uint32_t value = Read32();
                return Token{Kind::Integer, {}, static_cast<double>(value)};
            }

            [[nodiscard]] std::uint16_t Read16()
            {
                std::uint16_t value = 0u;
                std::memcpy(&value, bytes_.data() + at_, sizeof(value));
                at_ += 2u;
                return value;
            }

            [[nodiscard]] std::uint32_t Read32()
            {
                if (at_ + 4u > bytes_.size())
                {
                    Fail(DirectXFileError::ParseError, "the file ends inside a value.");
                }
                std::uint32_t value = 0u;
                std::memcpy(&value, bytes_.data() + at_, sizeof(value));
                at_ += 4u;
                return value;
            }

            void SkipGuid()
            {
                if (at_ + 16u > bytes_.size())
                {
                    Fail(DirectXFileError::ParseError, "the file ends inside a template identifier.");
                }
                at_ += 16u;
            }

            [[nodiscard]] Token ReadName(const Kind kind)
            {
                const std::uint32_t length = Read32();
                if (length > limits_.maximumStringLength || at_ + length > bytes_.size())
                {
                    Fail(DirectXFileError::ParseError, "a name runs past the end of the file.");
                }
                std::string text(reinterpret_cast<const char*>(bytes_.data()) + at_, length);
                at_ += length;
                return Token{kind, std::move(text), 0.0};
            }

            std::span<const std::uint8_t> bytes_;
            std::size_t at_ = 0u;
            const DirectXFileLimits& limits_;
            std::size_t pending_ = 0u;
            bool pendingFloat_ = false;
        };

        /** @brief Builds the object tree out of whichever lexer the encoding chose. */
        template<typename Lexer>
        class Parser
        {
        public:
            Parser(Lexer lexer, const DirectXFileLimits& limits) : lexer_(std::move(lexer)), limits_(limits)
            {
                Advance();
            }

            [[nodiscard]] std::vector<DirectXFileObject> ReadTopLevel()
            {
                std::vector<DirectXFileObject> objects;
                while (token_.kind != Kind::End)
                {
                    if (token_.kind != Kind::Name)
                    {
                        Fail(DirectXFileError::ParseError,
                             "a top-level object must begin with a template name.");
                    }
                    // A `template` block declares a layout this reader does not interpret; it is
                    // read far enough to be skipped whole.
                    if (token_.text == "template")
                    {
                        SkipTemplate();
                        continue;
                    }
                    objects.push_back(ReadObject(0u));
                }
                return objects;
            }

        private:
            void Advance() { token_ = lexer_.Next(); }

            void SkipTemplate()
            {
                Advance();                       // the template's name
                if (token_.kind == Kind::Name)
                {
                    Advance();
                }
                if (token_.kind != Kind::OpenBrace)
                {
                    Fail(DirectXFileError::ParseError, "a template declaration has no body.");
                }
                std::size_t depth = 0u;
                do
                {
                    if (token_.kind == Kind::OpenBrace) { ++depth; }
                    else if (token_.kind == Kind::CloseBrace) { --depth; }
                    else if (token_.kind == Kind::End)
                    {
                        Fail(DirectXFileError::ParseError, "a template declaration is never closed.");
                    }
                    Advance();
                } while (depth > 0u);
            }

            [[nodiscard]] DirectXFileObject ReadObject(const std::size_t depth)
            {
                if (depth > limits_.maximumDepth)
                {
                    Fail(DirectXFileError::ParseError, "the objects nest deeper than this reader accepts.");
                }
                if (++objects_ > limits_.maximumObjects)
                {
                    Fail(DirectXFileError::ParseError, "the file holds more objects than this reader accepts.");
                }
                DirectXFileObject object;
                object.type = token_.text;
                Advance();
                if (token_.kind == Kind::Name)
                {
                    object.name = token_.text;
                    Advance();
                }
                if (token_.kind != Kind::OpenBrace)
                {
                    Fail(DirectXFileError::ParseError,
                         "the object '" + object.type + "' has no body.");
                }
                Advance();
                for (;;)
                {
                    switch (token_.kind)
                    {
                        case Kind::CloseBrace:
                            Advance();
                            return object;
                        case Kind::End:
                            Fail(DirectXFileError::ParseError,
                                 "the object '" + object.type + "' is never closed.");
                        case Kind::Integer:
                        case Kind::Float:
                            if (object.numbers.size() >= limits_.maximumNumbersPerObject)
                            {
                                Fail(DirectXFileError::ParseError,
                                     "the object '" + object.type + "' holds more numbers than this "
                                     "reader accepts.");
                            }
                            object.numbers.push_back(token_.number);
                            Advance();
                            break;
                        case Kind::String:
                            object.strings.push_back(token_.text);
                            Advance();
                            break;
                        case Kind::Semicolon:
                        case Kind::Comma:
                        case Kind::OpenAngle:
                        case Kind::CloseAngle:
                        case Kind::Dot:
                            Advance();
                            break;
                        case Kind::OpenBrace:
                        {
                            // `{ Name }` is a reference to an object declared elsewhere.
                            Advance();
                            if (token_.kind != Kind::Name)
                            {
                                Fail(DirectXFileError::ParseError,
                                     "a reference inside '" + object.type + "' names nothing.");
                            }
                            object.references.push_back(token_.text);
                            Advance();
                            if (token_.kind != Kind::CloseBrace)
                            {
                                Fail(DirectXFileError::ParseError,
                                     "a reference inside '" + object.type + "' is never closed.");
                            }
                            Advance();
                            break;
                        }
                        case Kind::Name:
                            object.children.push_back(ReadObject(depth + 1u));
                            break;
                        default:
                            Advance();
                            break;
                    }
                }
            }

            Lexer lexer_;
            const DirectXFileLimits& limits_;
            Token token_;
            std::size_t objects_ = 0u;
        };
    }

    DirectXFileException::DirectXFileException(const DirectXFileError error, std::string detail)
        : error_(error), detail_(std::move(detail))
    {
    }

    DirectXFileError DirectXFileException::Error() const noexcept { return error_; }

    const char* DirectXFileException::CodeName() const noexcept
    {
        switch (error_)
        {
            case DirectXFileError::BadFile: return "D3DXFERR_BADFILE";
            case DirectXFileError::BadFileType: return "D3DXFERR_BADFILETYPE";
            case DirectXFileError::BadFileVersion: return "D3DXFERR_BADFILEVERSION";
            case DirectXFileError::ParseError: break;
        }
        return "D3DXFERR_PARSEERROR";
    }

    const char* DirectXFileException::what() const noexcept { return detail_.c_str(); }

    DirectXFile ReadDirectXFile(std::span<const std::uint8_t> bytes, const DirectXFileLimits& limits)
    {
        if (bytes.empty())
        {
            Fail(DirectXFileError::BadFile, "the file holds no bytes.");
        }
        if (bytes.size() > limits.maximumBytes)
        {
            Fail(DirectXFileError::ParseError, "the file is larger than this reader accepts.");
        }
        // The header is exactly sixteen bytes: `xof `, four version digits, four format characters
        // and four float-size digits.
        if (bytes.size() < 16u || std::memcmp(bytes.data(), "xof ", 4u) != 0)
        {
            Fail(DirectXFileError::BadFileType, "the file does not begin with a `xof ` header.");
        }
        const std::string version(reinterpret_cast<const char*>(bytes.data()) + 4u, 4u);
        const std::string encoding(reinterpret_cast<const char*>(bytes.data()) + 8u, 4u);
        const std::string floatSize(reinterpret_cast<const char*>(bytes.data()) + 12u, 4u);
        if (version != "0302" && version != "0303")
        {
            Fail(DirectXFileError::BadFileVersion,
                 "the version '" + version + "' is not one this reader knows.");
        }
        if (floatSize != "0032" && floatSize != "0064")
        {
            Fail(DirectXFileError::BadFileVersion,
                 "the float size '" + floatSize + "' is not 32 or 64 bits.");
        }
        DirectXFile file;
        if (encoding == "txt ")
        {
            file.encoding = "txt";
            Parser<TextLexer> parser(TextLexer(bytes, 16u, limits), limits);
            file.objects = parser.ReadTopLevel();
            return file;
        }
        if (encoding == "bin ")
        {
            if (floatSize == "0064")
            {
                // The reader's binary float list is 32-bit; a 64-bit one would be silently
                // mis-read, so it is refused instead.
                Fail(DirectXFileError::BadFileVersion,
                     "a binary file with 64-bit floats is not one this reader handles.");
            }
            file.encoding = "bin";
            Parser<BinaryLexer> parser(BinaryLexer(bytes, 16u, limits), limits);
            file.objects = parser.ReadTopLevel();
            return file;
        }
        if (encoding == "tzip" || encoding == "bzip")
        {
            Fail(DirectXFileError::BadFileVersion,
                 "the compressed encoding '" + encoding +
                     "' is MSZIP, which this reader does not decompress.");
        }
        Fail(DirectXFileError::BadFileVersion,
             "the encoding '" + encoding + "' is not one this reader knows.");
    }
}
