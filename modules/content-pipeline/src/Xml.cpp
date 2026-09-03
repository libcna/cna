// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Xml.hpp"

#include <algorithm>
#include <cstdint>

namespace CNA::Internal
{
    const XmlElement* XmlElement::Find(const std::string& childName) const
    {
        for (const XmlElement& child : children)
        {
            if (child.name == childName) { return &child; }
        }
        return nullptr;
    }

    std::vector<const XmlElement*> XmlElement::FindAll(const std::string& childName) const
    {
        std::vector<const XmlElement*> found;
        for (const XmlElement& child : children)
        {
            if (child.name == childName) { found.push_back(&child); }
        }
        return found;
    }

    std::string XmlElement::TrimmedText() const
    {
        const auto isSpace = [](const unsigned char character)
        {
            return character == ' ' || character == '\t' || character == '\r' ||
                   character == '\n';
        };
        std::size_t begin = 0u;
        std::size_t end = text.size();
        while (begin < end && isSpace(static_cast<unsigned char>(text[begin]))) { ++begin; }
        while (end > begin && isSpace(static_cast<unsigned char>(text[end - 1u]))) { --end; }
        return text.substr(begin, end - begin);
    }

    namespace
    {
        /** @brief A bounds-checked cursor that can report where in the document it is. */
        class Cursor
        {
        public:
            Cursor(const std::string& text, std::string origin)
                : text_(text), origin_(std::move(origin))
            {
            }

            [[nodiscard]] bool AtEnd() const noexcept { return position_ >= text_.size(); }

            [[nodiscard]] char Peek() const
            {
                if (AtEnd()) { Fail("unexpected end of document"); }
                return text_[position_];
            }

            [[nodiscard]] bool Starts(const std::string_view prefix) const noexcept
            {
                return text_.compare(position_, prefix.size(), prefix) == 0;
            }

            char Take()
            {
                const char character = Peek();
                ++position_;
                if (character == '\n') { ++line_; column_ = 1u; }
                else { ++column_; }
                return character;
            }

            void Expect(const std::string_view prefix)
            {
                if (!Starts(prefix))
                {
                    Fail("expected '" + std::string(prefix) + "'");
                }
                for (std::size_t index = 0u; index < prefix.size(); ++index) { (void)Take(); }
            }

            void SkipWhitespace()
            {
                while (!AtEnd())
                {
                    const char character = text_[position_];
                    if (character != ' ' && character != '\t' && character != '\r' &&
                        character != '\n')
                    {
                        break;
                    }
                    (void)Take();
                }
            }

            [[noreturn]] void Fail(const std::string& reason) const
            {
                throw XmlParseException(origin_ + "(" + std::to_string(line_) + "," +
                                        std::to_string(column_) + "): " + reason + ".");
            }

            [[nodiscard]] std::size_t Position() const noexcept { return position_; }

            [[nodiscard]] const std::string& Text() const noexcept { return text_; }

        private:
            const std::string& text_;
            std::string origin_;
            std::size_t position_ = 0u;
            std::size_t line_ = 1u;
            std::size_t column_ = 1u;
        };

        [[nodiscard]] bool IsNameStart(const unsigned char character)
        {
            return (character >= 'A' && character <= 'Z') ||
                   (character >= 'a' && character <= 'z') || character == '_' ||
                   character == ':' || character >= 0x80u;
        }

        [[nodiscard]] bool IsNameChar(const unsigned char character)
        {
            return IsNameStart(character) || (character >= '0' && character <= '9') ||
                   character == '-' || character == '.';
        }

        [[nodiscard]] std::string ReadName(Cursor& cursor)
        {
            if (!IsNameStart(static_cast<unsigned char>(cursor.Peek())))
            {
                cursor.Fail("expected an element or attribute name");
            }
            std::string name;
            while (!cursor.AtEnd() &&
                   IsNameChar(static_cast<unsigned char>(cursor.Peek())))
            {
                name += cursor.Take();
            }
            return name;
        }

        /** @brief Appends one Unicode scalar value to @p out as UTF-8. */
        void AppendUtf8(std::string& out, const std::uint32_t code, Cursor& cursor)
        {
            if (code > 0x10FFFFu || (code >= 0xD800u && code <= 0xDFFFu))
            {
                cursor.Fail("character reference is not a Unicode scalar value");
            }
            if (code < 0x80u) { out += static_cast<char>(code); }
            else if (code < 0x800u)
            {
                out += static_cast<char>(0xC0u | (code >> 6));
                out += static_cast<char>(0x80u | (code & 0x3Fu));
            }
            else if (code < 0x10000u)
            {
                out += static_cast<char>(0xE0u | (code >> 12));
                out += static_cast<char>(0x80u | ((code >> 6) & 0x3Fu));
                out += static_cast<char>(0x80u | (code & 0x3Fu));
            }
            else
            {
                out += static_cast<char>(0xF0u | (code >> 18));
                out += static_cast<char>(0x80u | ((code >> 12) & 0x3Fu));
                out += static_cast<char>(0x80u | ((code >> 6) & 0x3Fu));
                out += static_cast<char>(0x80u | (code & 0x3Fu));
            }
        }

        void ReadEntity(Cursor& cursor, std::string& out)
        {
            cursor.Expect("&");
            std::string name;
            while (!cursor.AtEnd() && cursor.Peek() != ';' && name.size() < 16u)
            {
                name += cursor.Take();
            }
            cursor.Expect(";");
            if (name == "amp") { out += '&'; return; }
            if (name == "lt") { out += '<'; return; }
            if (name == "gt") { out += '>'; return; }
            if (name == "quot") { out += '"'; return; }
            if (name == "apos") { out += '\''; return; }
            if (!name.empty() && name[0] == '#')
            {
                const bool hexadecimal = name.size() > 1u && (name[1] == 'x' || name[1] == 'X');
                const std::string digits = name.substr(hexadecimal ? 2u : 1u);
                if (digits.empty()) { cursor.Fail("empty character reference"); }
                std::uint32_t code = 0u;
                for (const char digit : digits)
                {
                    std::uint32_t value = 0u;
                    if (digit >= '0' && digit <= '9') { value = static_cast<std::uint32_t>(digit - '0'); }
                    else if (hexadecimal && digit >= 'a' && digit <= 'f')
                    {
                        value = static_cast<std::uint32_t>(digit - 'a') + 10u;
                    }
                    else if (hexadecimal && digit >= 'A' && digit <= 'F')
                    {
                        value = static_cast<std::uint32_t>(digit - 'A') + 10u;
                    }
                    else { cursor.Fail("malformed character reference '&" + name + ";'"); }
                    if (code > (0x10FFFFu - value) / (hexadecimal ? 16u : 10u))
                    {
                        cursor.Fail("character reference is out of range");
                    }
                    code = code * (hexadecimal ? 16u : 10u) + value;
                }
                AppendUtf8(out, code, cursor);
                return;
            }
            cursor.Fail("unknown entity '&" + name + ";'; this reader resolves only the five "
                        "predefined entities and numeric character references");
        }

        [[nodiscard]] std::string ReadAttributeValue(Cursor& cursor)
        {
            const char quote = cursor.Take();
            if (quote != '"' && quote != '\'')
            {
                cursor.Fail("an attribute value must be quoted");
            }
            std::string value;
            while (true)
            {
                if (cursor.AtEnd()) { cursor.Fail("unterminated attribute value"); }
                if (cursor.Peek() == quote) { (void)cursor.Take(); break; }
                if (cursor.Peek() == '&') { ReadEntity(cursor, value); continue; }
                if (cursor.Peek() == '<') { cursor.Fail("'<' is not allowed in an attribute"); }
                value += cursor.Take();
            }
            return value;
        }

        void SkipComment(Cursor& cursor)
        {
            cursor.Expect("<!--");
            while (!cursor.Starts("-->"))
            {
                if (cursor.AtEnd()) { cursor.Fail("unterminated comment"); }
                (void)cursor.Take();
            }
            cursor.Expect("-->");
        }

        void SkipProcessingInstruction(Cursor& cursor)
        {
            cursor.Expect("<?");
            while (!cursor.Starts("?>"))
            {
                if (cursor.AtEnd()) { cursor.Fail("unterminated processing instruction"); }
                (void)cursor.Take();
            }
            cursor.Expect("?>");
        }

        void ReadCdata(Cursor& cursor, std::string& out)
        {
            cursor.Expect("<![CDATA[");
            while (!cursor.Starts("]]>"))
            {
                if (cursor.AtEnd()) { cursor.Fail("unterminated CDATA section"); }
                out += cursor.Take();
            }
            cursor.Expect("]]>");
        }

        constexpr int kMaxDepth = 64;

        XmlElement ReadElement(Cursor& cursor, const int depth)
        {
            if (depth > kMaxDepth)
            {
                cursor.Fail("elements nest deeper than " + std::to_string(kMaxDepth) + " levels");
            }
            cursor.Expect("<");
            XmlElement element;
            element.name = ReadName(cursor);

            while (true)
            {
                cursor.SkipWhitespace();
                if (cursor.Starts("/>"))
                {
                    cursor.Expect("/>");
                    return element;
                }
                if (cursor.Peek() == '>') { (void)cursor.Take(); break; }
                const std::string attribute = ReadName(cursor);
                cursor.SkipWhitespace();
                cursor.Expect("=");
                cursor.SkipWhitespace();
                if (!element.attributes.emplace(attribute, ReadAttributeValue(cursor)).second)
                {
                    cursor.Fail("attribute '" + attribute + "' appears more than once");
                }
            }

            while (true)
            {
                if (cursor.AtEnd())
                {
                    cursor.Fail("element '" + element.name + "' is never closed");
                }
                if (cursor.Starts("</"))
                {
                    cursor.Expect("</");
                    const std::string closing = ReadName(cursor);
                    if (closing != element.name)
                    {
                        cursor.Fail("'</" + closing + ">' closes '<" + element.name + ">'");
                    }
                    cursor.SkipWhitespace();
                    cursor.Expect(">");
                    return element;
                }
                if (cursor.Starts("<!--")) { SkipComment(cursor); continue; }
                if (cursor.Starts("<![CDATA[")) { ReadCdata(cursor, element.text); continue; }
                if (cursor.Starts("<?")) { SkipProcessingInstruction(cursor); continue; }
                if (cursor.Starts("<!"))
                {
                    cursor.Fail("a DOCTYPE or other declaration is not supported by this reader");
                }
                if (cursor.Peek() == '<')
                {
                    element.children.push_back(ReadElement(cursor, depth + 1));
                    continue;
                }
                if (cursor.Peek() == '&') { ReadEntity(cursor, element.text); continue; }
                element.text += cursor.Take();
            }
        }
    }

    XmlElement ParseXml(const std::string& text, const std::string& origin)
    {
        Cursor cursor(text, origin.empty() ? std::string("<memory>") : origin);
        // A UTF-8 byte-order mark is legal and common in an XNA-authored .spritefont.
        if (cursor.Text().compare(0u, 3u, "\xEF\xBB\xBF") == 0)
        {
            cursor.Expect("\xEF\xBB\xBF");
        }
        while (true)
        {
            cursor.SkipWhitespace();
            if (cursor.AtEnd()) { cursor.Fail("the document has no root element"); }
            if (cursor.Starts("<?")) { SkipProcessingInstruction(cursor); continue; }
            if (cursor.Starts("<!--")) { SkipComment(cursor); continue; }
            if (cursor.Starts("<!"))
            {
                cursor.Fail("a DOCTYPE or other declaration is not supported by this reader");
            }
            break;
        }
        XmlElement root = ReadElement(cursor, 0);
        while (!cursor.AtEnd())
        {
            cursor.SkipWhitespace();
            if (cursor.AtEnd()) { break; }
            if (cursor.Starts("<!--")) { SkipComment(cursor); continue; }
            if (cursor.Starts("<?")) { SkipProcessingInstruction(cursor); continue; }
            cursor.Fail("content follows the root element");
        }
        return root;
    }
}
