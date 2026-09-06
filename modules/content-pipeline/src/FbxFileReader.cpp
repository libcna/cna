// SPDX-License-Identifier: MS-PL
#include "CNA/Content/Pipeline/FbxFileReader.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <stdexcept>

#ifdef CNA_HAVE_ZLIB
#include <zlib.h>
#endif

namespace CNA::Content::Pipeline
{
    namespace
    {
        [[noreturn]] void Fail(const FbxFileError error, const std::string& detail)
        {
            throw FbxFileException(error, detail);
        }

        constexpr char BinaryMagic[] = "Kaydara FBX Binary  ";

        /** @brief Reads the text encoding, which is what an FBX 6.1 document is. */
        class TextReader
        {
        public:
            TextReader(std::span<const std::uint8_t> bytes, const FbxFileLimits& limits)
                : bytes_(bytes), limits_(limits)
            {
            }

            [[nodiscard]] std::vector<FbxNode> ReadDocument()
            {
                std::vector<FbxNode> nodes;
                for (;;)
                {
                    SkipTrivia();
                    if (at_ >= bytes_.size())
                    {
                        return nodes;
                    }
                    nodes.push_back(ReadNode(0u));
                }
            }

        private:
            void SkipTrivia()
            {
                for (;;)
                {
                    while (at_ < bytes_.size() &&
                           std::isspace(static_cast<unsigned char>(bytes_[at_])) != 0)
                    {
                        ++at_;
                    }
                    if (at_ >= bytes_.size() || bytes_[at_] != ';')
                    {
                        return;
                    }
                    while (at_ < bytes_.size() && bytes_[at_] != '\n')
                    {
                        ++at_;
                    }
                }
            }

            [[nodiscard]] char Peek() const
            {
                return at_ < bytes_.size() ? static_cast<char>(bytes_[at_]) : '\0';
            }

            [[nodiscard]] FbxNode ReadNode(const std::size_t depth)
            {
                if (depth > limits_.maximumDepth)
                {
                    Fail(FbxFileError::ParseError, "the nodes nest deeper than this reader accepts.");
                }
                if (++nodes_ > limits_.maximumNodes)
                {
                    Fail(FbxFileError::ParseError, "the document holds more nodes than this reader accepts.");
                }
                FbxNode node;
                node.name = ReadName();
                SkipTrivia();
                if (Peek() != ':')
                {
                    Fail(FbxFileError::ParseError, "the node '" + node.name + "' has no colon after its name.");
                }
                ++at_;
                // The properties run to the end of the line or to the opening brace, whichever
                // comes first; a `,` separates them and a `{` opens the body.
                for (;;)
                {
                    while (at_ < bytes_.size() && (bytes_[at_] == ' ' || bytes_[at_] == '\t'))
                    {
                        ++at_;
                    }
                    const char c = Peek();
                    if (c == '\0' || c == '\n' || c == '\r' || c == '{' || c == '}')
                    {
                        break;
                    }
                    if (c == ',')
                    {
                        ++at_;
                        continue;
                    }
                    if (c == ';')
                    {
                        while (at_ < bytes_.size() && bytes_[at_] != '\n') { ++at_; }
                        continue;
                    }
                    node.properties.push_back(ReadProperty());
                }
                SkipTrivia();
                if (Peek() == '{')
                {
                    ++at_;
                    for (;;)
                    {
                        SkipTrivia();
                        if (Peek() == '}')
                        {
                            ++at_;
                            break;
                        }
                        if (at_ >= bytes_.size())
                        {
                            Fail(FbxFileError::ParseError,
                                 "the node '" + node.name + "' is never closed.");
                        }
                        node.children.push_back(ReadNode(depth + 1u));
                    }
                }
                return node;
            }

            [[nodiscard]] std::string ReadName()
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
                if (at_ == start)
                {
                    Fail(FbxFileError::ParseError, "a node has no name.");
                }
                if (at_ - start > limits_.maximumStringLength)
                {
                    Fail(FbxFileError::ParseError, "a name is longer than this reader accepts.");
                }
                return std::string(reinterpret_cast<const char*>(bytes_.data()) + start, at_ - start);
            }

            [[nodiscard]] FbxProperty ReadProperty()
            {
                const char c = Peek();
                if (c == '"')
                {
                    ++at_;
                    const std::size_t start = at_;
                    while (at_ < bytes_.size() && bytes_[at_] != '"')
                    {
                        ++at_;
                    }
                    if (at_ >= bytes_.size())
                    {
                        Fail(FbxFileError::ParseError, "a string is never closed.");
                    }
                    const std::size_t length = at_ - start;
                    if (length > limits_.maximumStringLength)
                    {
                        Fail(FbxFileError::ParseError, "a string is longer than this reader accepts.");
                    }
                    ++at_;
                    return std::string(reinterpret_cast<const char*>(bytes_.data()) + start, length);
                }
                if (c == '*')
                {
                    // FBX 7's text encoding writes `*count { a: v,v,v }`; the count is advisory and
                    // the values are what matter.
                    ++at_;
                    (void)ReadNumber();
                    SkipTrivia();
                    std::vector<double> values;
                    if (Peek() == '{')
                    {
                        ++at_;
                        for (;;)
                        {
                            SkipTrivia();
                            if (Peek() == '}')
                            {
                                ++at_;
                                break;
                            }
                            if (at_ >= bytes_.size())
                            {
                                Fail(FbxFileError::ParseError, "an array is never closed.");
                            }
                            if (Peek() == ',' || Peek() == ':')
                            {
                                ++at_;
                                continue;
                            }
                            if (std::isalpha(static_cast<unsigned char>(Peek())) != 0)
                            {
                                (void)ReadName();
                                continue;
                            }
                            if (values.size() >= limits_.maximumArrayLength)
                            {
                                Fail(FbxFileError::ParseError,
                                     "an array is longer than this reader accepts.");
                            }
                            values.push_back(ReadNumber());
                        }
                    }
                    return values;
                }
                if (c == '-' || c == '+' || c == '.' || std::isdigit(static_cast<unsigned char>(c)) != 0)
                {
                    return ReadNumber();
                }
                // A bare identifier, `Y` or `N` or a shading name; carried as text.
                return ReadName();
            }

            [[nodiscard]] double ReadNumber()
            {
                const std::size_t start = at_;
                if (Peek() == '-' || Peek() == '+')
                {
                    ++at_;
                }
                while (at_ < bytes_.size())
                {
                    const char c = static_cast<char>(bytes_[at_]);
                    if (std::isdigit(static_cast<unsigned char>(c)) != 0 || c == '.')
                    {
                        ++at_;
                    }
                    else if ((c == 'e' || c == 'E') && at_ + 1u < bytes_.size())
                    {
                        ++at_;
                        if (Peek() == '-' || Peek() == '+') { ++at_; }
                    }
                    else
                    {
                        break;
                    }
                }
                const std::string text(reinterpret_cast<const char*>(bytes_.data()) + start, at_ - start);
                try
                {
                    return std::stod(text);
                }
                catch (const std::exception&)
                {
                    Fail(FbxFileError::ParseError, "'" + text + "' is not a number.");
                }
            }

            std::span<const std::uint8_t> bytes_;
            const FbxFileLimits& limits_;
            std::size_t at_ = 0u;
            std::size_t nodes_ = 0u;
        };

        /** @brief Reads the binary record stream every current exporter writes. */
        class BinaryReader
        {
        public:
            BinaryReader(std::span<const std::uint8_t> bytes, const std::uint32_t version,
                         const FbxFileLimits& limits)
                : bytes_(bytes), version_(version), limits_(limits), at_(27u)
            {
            }

            [[nodiscard]] std::vector<FbxNode> ReadDocument()
            {
                std::vector<FbxNode> nodes;
                for (;;)
                {
                    FbxNode node;
                    if (!ReadNode(node, 0u))
                    {
                        return nodes;
                    }
                    nodes.push_back(std::move(node));
                }
            }

        private:
            [[nodiscard]] bool Wide() const noexcept { return version_ >= 7500u; }

            [[nodiscard]] std::uint64_t ReadOffset()
            {
                return Wide() ? Read<std::uint64_t>() : static_cast<std::uint64_t>(Read<std::uint32_t>());
            }

            template<typename T>
            [[nodiscard]] T Read()
            {
                if (at_ + sizeof(T) > bytes_.size())
                {
                    Fail(FbxFileError::ParseError, "the document ends inside a value.");
                }
                T value{};
                std::memcpy(&value, bytes_.data() + at_, sizeof(T));
                at_ += sizeof(T);
                return value;
            }

            /** @brief One record; false at the null record that ends a list. */
            [[nodiscard]] bool ReadNode(FbxNode& node, const std::size_t depth)
            {
                if (depth > limits_.maximumDepth)
                {
                    Fail(FbxFileError::ParseError, "the nodes nest deeper than this reader accepts.");
                }
                const std::size_t headerSize = Wide() ? 25u : 13u;
                if (at_ + headerSize > bytes_.size())
                {
                    return false;
                }
                const std::uint64_t endOffset = ReadOffset();
                const std::uint64_t propertyCount = ReadOffset();
                (void)ReadOffset();                     // the property list's byte length
                const std::uint8_t nameLength = Read<std::uint8_t>();
                if (endOffset == 0u)
                {
                    return false;                       // the null record that ends a list
                }
                if (endOffset > bytes_.size())
                {
                    Fail(FbxFileError::ParseError, "a record claims to end past the document.");
                }
                if (++nodes_ > limits_.maximumNodes)
                {
                    Fail(FbxFileError::ParseError, "the document holds more nodes than this reader accepts.");
                }
                if (at_ + nameLength > bytes_.size())
                {
                    Fail(FbxFileError::ParseError, "a record's name runs past the document.");
                }
                node.name.assign(reinterpret_cast<const char*>(bytes_.data()) + at_, nameLength);
                at_ += nameLength;
                for (std::uint64_t i = 0; i < propertyCount; ++i)
                {
                    node.properties.push_back(ReadProperty());
                }
                // Anything left before the record's end is its nested records.
                while (at_ + headerSize <= static_cast<std::size_t>(endOffset))
                {
                    FbxNode child;
                    if (!ReadNode(child, depth + 1u))
                    {
                        break;
                    }
                    node.children.push_back(std::move(child));
                }
                at_ = static_cast<std::size_t>(endOffset);
                return true;
            }

            [[nodiscard]] FbxProperty ReadProperty()
            {
                const char type = static_cast<char>(Read<std::uint8_t>());
                switch (type)
                {
                    case 'Y': return static_cast<double>(Read<std::int16_t>());
                    case 'C': return static_cast<double>(Read<std::uint8_t>());
                    case 'I': return static_cast<double>(Read<std::int32_t>());
                    case 'F': return static_cast<double>(Read<float>());
                    case 'D': return Read<double>();
                    case 'L': return static_cast<double>(Read<std::int64_t>());
                    case 'f': return ReadArray<float>();
                    case 'd': return ReadArray<double>();
                    case 'l': return ReadArray<std::int64_t>();
                    case 'i': return ReadArray<std::int32_t>();
                    case 'b': return ReadArray<std::uint8_t>();
                    case 'S':
                    case 'R':
                    {
                        const std::uint32_t length = Read<std::uint32_t>();
                        if (length > limits_.maximumStringLength || at_ + length > bytes_.size())
                        {
                            Fail(FbxFileError::ParseError, "a string runs past the document.");
                        }
                        std::string text(reinterpret_cast<const char*>(bytes_.data()) + at_, length);
                        at_ += length;
                        return text;
                    }
                    default:
                        Fail(FbxFileError::ParseError,
                             std::string("the property type '") + type + "' is not one this reader knows.");
                }
            }

            template<typename T>
            [[nodiscard]] std::vector<double> ReadArray()
            {
                const std::uint32_t count = Read<std::uint32_t>();
                const std::uint32_t encoding = Read<std::uint32_t>();
                const std::uint32_t compressedLength = Read<std::uint32_t>();
                if (count > limits_.maximumArrayLength)
                {
                    Fail(FbxFileError::ParseError, "an array is longer than this reader accepts.");
                }
                const std::size_t plainBytes = static_cast<std::size_t>(count) * sizeof(T);
                if (plainBytes / (sizeof(T) == 0u ? 1u : sizeof(T)) != count ||
                    plainBytes > limits_.maximumDecompressedBytes)
                {
                    Fail(FbxFileError::ParseError, "an array is larger than this reader accepts.");
                }
                std::vector<std::uint8_t> plain;
                if (encoding == 0u)
                {
                    if (at_ + plainBytes > bytes_.size())
                    {
                        Fail(FbxFileError::ParseError, "an array runs past the document.");
                    }
                    plain.assign(bytes_.begin() + static_cast<std::ptrdiff_t>(at_),
                                 bytes_.begin() + static_cast<std::ptrdiff_t>(at_ + plainBytes));
                    at_ += plainBytes;
                }
                else
                {
                    if (at_ + compressedLength > bytes_.size())
                    {
                        Fail(FbxFileError::ParseError, "a compressed array runs past the document.");
                    }
#ifdef CNA_HAVE_ZLIB
                    plain.resize(plainBytes);
                    uLongf produced = static_cast<uLongf>(plainBytes);
                    const int status = uncompress(plain.data(), &produced, bytes_.data() + at_,
                                                  static_cast<uLong>(compressedLength));
                    if (status != Z_OK || produced != plainBytes)
                    {
                        Fail(FbxFileError::ParseError, "a compressed array could not be decompressed.");
                    }
                    at_ += compressedLength;
#else
                    Fail(FbxFileError::Unsupported,
                         "the document stores its arrays compressed and this build has no zlib. "
                         "Configure against zlib, or export the model as FBX text.");
#endif
                }
                std::vector<double> values(count);
                for (std::uint32_t i = 0; i < count; ++i)
                {
                    T one{};
                    std::memcpy(&one, plain.data() + static_cast<std::size_t>(i) * sizeof(T), sizeof(T));
                    values[i] = static_cast<double>(one);
                }
                return values;
            }

            std::span<const std::uint8_t> bytes_;
            std::uint32_t version_ = 0u;
            const FbxFileLimits& limits_;
            std::size_t at_ = 0u;
            std::size_t nodes_ = 0u;
        };
    }

    const FbxNode* FbxNode::Find(const std::string& childName) const
    {
        for (const FbxNode& child : children)
        {
            if (child.name == childName)
            {
                return &child;
            }
        }
        return nullptr;
    }

    double FbxNode::Number(const std::size_t index, const double fallback) const
    {
        if (index >= properties.size())
        {
            return fallback;
        }
        if (const double* value = std::get_if<double>(&properties[index]); value != nullptr)
        {
            return *value;
        }
        return fallback;
    }

    std::string FbxNode::Text(const std::size_t index, const std::string& fallback) const
    {
        if (index >= properties.size())
        {
            return fallback;
        }
        if (const std::string* value = std::get_if<std::string>(&properties[index]); value != nullptr)
        {
            return *value;
        }
        return fallback;
    }

    std::vector<double> FbxNode::Numbers() const
    {
        std::vector<double> values;
        for (const FbxProperty& property : properties)
        {
            if (const double* one = std::get_if<double>(&property); one != nullptr)
            {
                values.push_back(*one);
            }
            else if (const std::vector<double>* many = std::get_if<std::vector<double>>(&property);
                     many != nullptr)
            {
                values.insert(values.end(), many->begin(), many->end());
            }
        }
        return values;
    }

    const FbxNode* FbxFile::Find(const std::string& name) const
    {
        for (const FbxNode& node : nodes)
        {
            if (node.name == name)
            {
                return &node;
            }
        }
        return nullptr;
    }

    FbxFileException::FbxFileException(const FbxFileError error, std::string detail)
        : error_(error), detail_(std::move(detail))
    {
    }

    FbxFileError FbxFileException::Error() const noexcept { return error_; }

    const char* FbxFileException::what() const noexcept { return detail_.c_str(); }

    FbxFile ReadFbxFile(std::span<const std::uint8_t> bytes, const FbxFileLimits& limits)
    {
        if (bytes.empty())
        {
            Fail(FbxFileError::CannotInitialize, "the file holds no bytes.");
        }
        if (bytes.size() > limits.maximumBytes)
        {
            Fail(FbxFileError::ParseError, "the file is larger than this reader accepts.");
        }
        FbxFile file;
        const std::size_t magic = sizeof(BinaryMagic) - 1u;
        if (bytes.size() > 27u && std::memcmp(bytes.data(), BinaryMagic, magic) == 0)
        {
            file.binary = true;
            std::memcpy(&file.version, bytes.data() + 23u, sizeof(file.version));
            BinaryReader reader(bytes, file.version, limits);
            file.nodes = reader.ReadDocument();
            return file;
        }
        // A text FBX opens with a comment naming its version; anything that does not begin with a
        // comment or a node name is not an FBX at all, which is a different refusal from a
        // document that begins well and then does not parse.
        const std::string head(reinterpret_cast<const char*>(bytes.data()),
                               std::min<std::size_t>(bytes.size(), 4096u));
        // Two different refusals, and the difference is the content rather than the size: a
        // DirectX `.x` file -- a format the reader recognizes as something else -- answers "could
        // not detect file format", while text that matches nothing answers the loader's own
        // initialization failure, at 31 bytes and at 1024 alike (measured, fbx/an_x_file against
        // fbx/fbx_not_fbx.fbx and fbx/fbx_not_fbx_large.fbx).
        if (head.rfind("xof ", 0) == 0)
        {
            Fail(FbxFileError::NotFbx, "the file is a DirectX .x document, not an FBX one.");
        }
        if (head.find("FBXHeaderExtension") == std::string::npos &&
            head.find("FBXVersion") == std::string::npos)
        {
            Fail(FbxFileError::CannotInitialize, "the file carries no FBX header.");
        }
        const std::size_t versionAt = head.find("FBXVersion:");
        if (versionAt != std::string::npos)
        {
            try
            {
                file.version = static_cast<std::uint32_t>(
                    std::stoul(head.substr(versionAt + std::string("FBXVersion:").size())));
            }
            catch (const std::exception&)
            {
                file.version = 0u;
            }
        }
        TextReader reader(bytes, limits);
        file.nodes = reader.ReadDocument();
        return file;
    }
}
