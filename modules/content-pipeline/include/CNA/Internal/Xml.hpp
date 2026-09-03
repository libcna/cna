// SPDX-License-Identifier: MS-PL
#pragma once

#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace CNA::Internal
{
    /**
     * @brief Raised for any document this reader refuses.
     *
     * The message always names a line and column, because a content author's first question
     * about a rejected `.spritefont` is where in the file the problem is.
     */
    class XmlParseException : public std::runtime_error
    {
    public:
        /**
         * @brief Creates a parse failure.
         *
         * @param message Complete diagnostic, including the source location.
         */
        explicit XmlParseException(const std::string& message) : std::runtime_error(message) {}
    };

    /** @brief One parsed element: its name, its attributes, its text and its children. */
    struct XmlElement
    {
        /** @brief The element's tag name, with any namespace prefix intact. */
        std::string name;

        /** @brief Attributes in document order of first appearance, keyed by name. */
        std::map<std::string, std::string> attributes;

        /** @brief Concatenated character data directly inside this element, entities resolved. */
        std::string text;

        /** @brief Child elements in document order. */
        std::vector<XmlElement> children;

        /**
         * @brief Finds the first child with a given tag name.
         *
         * @param childName Tag name to find.
         * @return Pointer valid while this element lives, or null when absent.
         */
        [[nodiscard]] const XmlElement* Find(const std::string& childName) const;

        /**
         * @brief Returns every child with a given tag name, in document order.
         *
         * @param childName Tag name to collect.
         * @return Pointers valid while this element lives.
         */
        [[nodiscard]] std::vector<const XmlElement*> FindAll(const std::string& childName) const;

        /**
         * @brief Returns this element's text with leading and trailing whitespace removed.
         *
         * @return The trimmed text.
         */
        [[nodiscard]] std::string TrimmedText() const;
    };

    /**
     * @brief Parses the XML subset CNA's content project formats need.
     *
     * Deliberately a subset, and deliberately strict. It handles elements, attributes, character
     * data, CDATA sections, comments, processing instructions, the XML declaration, self-closing
     * elements, the five predefined entities and both numeric character references. It refuses
     * everything else -- a DOCTYPE, an internal subset, an external entity -- rather than
     * ignoring it, because a build tool that silently drops part of a document it was handed is
     * worse than one that says it cannot read it. External entity resolution in particular is a
     * well-known way to make a parser read files it was never pointed at, and this parser has no
     * mechanism for it at all.
     *
     * @param text Complete UTF-8 document.
     * @param origin Path or other identity used in diagnostics.
     * @return The root element.
     * @throws XmlParseException for a malformed or unsupported document.
     */
    [[nodiscard]] XmlElement ParseXml(const std::string& text, const std::string& origin);
}
