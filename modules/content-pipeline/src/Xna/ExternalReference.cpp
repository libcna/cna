// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Content/Pipeline/ExternalReference.hpp"

#include <algorithm>
#include <cctype>
#include <system_error>

#include "CNA/Internal/PathUtf8.hpp"

namespace Microsoft::Xna::Framework::Content::Pipeline
{
    namespace
    {
        /** @brief ASCII lowercase, which is the comparison Windows makes for a filename. */
        [[nodiscard]] std::string Folded(const std::string& text)
        {
            std::string folded(text);
            std::transform(folded.begin(), folded.end(), folded.begin(), [](const unsigned char c)
                           { return static_cast<char>(std::tolower(c)); });
            return folded;
        }
    }

    std::filesystem::path ResolveNamedSourceFileEXT(const std::filesystem::path& directory,
                                                    const std::string& named)
    {
        std::string spelled(named);
        std::replace(spelled.begin(), spelled.end(), '\\', '/');
        const std::filesystem::path spelledPath = CNA::Internal::PathFromUtf8(spelled);
        const std::filesystem::path authored = (directory / spelledPath).lexically_normal();
        std::error_code error;
        if (std::filesystem::exists(authored, error)) { return authored; }

        std::filesystem::path walked = directory.lexically_normal();
        for (const std::filesystem::path& part : spelledPath.lexically_normal())
        {
            if (part == "." ) { continue; }
            if (part == "..")
            {
                walked = walked.parent_path();
                continue;
            }
            if (std::filesystem::exists(walked / part, error))
            {
                walked /= part;
                continue;
            }
            // Every enumerated sibling is folded against this, so the conversion has to hold a
            // name the ANSI code page cannot spell.
            const std::string wanted = Folded(CNA::Internal::PathToUtf8(part));
            std::filesystem::path match;
            std::size_t matches = 0u;
            if (std::filesystem::is_directory(walked, error))
            {
                for (const std::filesystem::directory_entry& entry :
                     std::filesystem::directory_iterator(walked, error))
                {
                    if (Folded(CNA::Internal::PathToUtf8(entry.path().filename())) != wanted)
                    {
                        continue;
                    }
                    match = entry.path();
                    ++matches;
                }
            }
            // One match or none: two entries differing only in case name no file at all here.
            if (matches != 1u) { return authored; }
            walked = match;
        }
        return walked;
    }

    std::string ResolveExternalReferenceFilename(const std::string& filename,
                                                 const ContentIdentity& relativeToContent)
    {
        if (filename.empty())
        {
            throw System::ArgumentException("An external reference needs a filename.", "filename");
        }
        const std::filesystem::path authored = CNA::Internal::PathFromUtf8(filename);
        if (authored.is_absolute())
        {
            return CNA::Internal::PathToGenericUtf8(authored.lexically_normal());
        }
        const std::string& source = relativeToContent.getSourceFilenameProperty();
        if (source.empty())
        {
            throw System::ArgumentException(
                "A relative external reference needs a referencing content identity with a "
                "source filename to resolve against.",
                "relativeToContent");
        }
        const std::filesystem::path base = CNA::Internal::PathFromUtf8(source).parent_path();
        return CNA::Internal::PathToGenericUtf8((base / authored).lexically_normal());
    }
}
