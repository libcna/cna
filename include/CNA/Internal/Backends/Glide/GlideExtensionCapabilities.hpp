#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace CNA::Internal::Backends::Glide
{
    /**
     * Parses the space-separated extension-name list returned by
     * `grGetString(GR_EXTENSION)` (Glide 3.0 Reference Manual). Per the specification, the
     * extension names themselves never contain spaces, and a runtime that supports no extensions
     * returns a single space rather than an empty string -- both cases correctly yield no tokens
     * here. A null pointer (grGetString's documented error return) also yields no tokens rather
     * than dereferencing it.
     */
    [[nodiscard]] inline std::vector<std::string> ParseGlideExtensionList(const char* extensionString)
    {
        std::vector<std::string> extensions;
        if (extensionString == nullptr)
        {
            return extensions;
        }
        std::string current;
        for (const char* cursor = extensionString;; ++cursor)
        {
            const char character = *cursor;
            if (character == ' ' || character == '\0')
            {
                if (!current.empty())
                {
                    extensions.push_back(current);
                    current.clear();
                }
                if (character == '\0')
                {
                    break;
                }
            }
            else
            {
                current.push_back(character);
            }
        }
        return extensions;
    }

    /**
     * Whether a Glide extension is present in a previously-parsed `GR_EXTENSION` list.
     *
     * This is the only correct way to gate use of an optional Glide export or capability: never
     * infer extension support from the loaded DLL's file name/path, since a caller-installed
     * emulator can rename or masquerade as another implementation.
     */
    [[nodiscard]] inline bool GlideExtensionListContains(
        const std::vector<std::string>& extensions, std::string_view name)
    {
        for (const std::string& extension : extensions)
        {
            if (extension == name)
            {
                return true;
            }
        }
        return false;
    }
} // namespace CNA::Internal::Backends::Glide
