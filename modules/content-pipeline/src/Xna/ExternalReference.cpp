// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Content/Pipeline/ExternalReference.hpp"

namespace Microsoft::Xna::Framework::Content::Pipeline
{
    std::string ResolveExternalReferenceFilename(const std::string& filename,
                                                 const ContentIdentity& relativeToContent)
    {
        if (filename.empty())
        {
            throw System::ArgumentException("An external reference needs a filename.", "filename");
        }
        const std::filesystem::path authored(filename);
        if (authored.is_absolute())
        {
            return authored.lexically_normal().generic_string();
        }
        const std::string& source = relativeToContent.getSourceFilenameProperty();
        if (source.empty())
        {
            throw System::ArgumentException(
                "A relative external reference needs a referencing content identity with a "
                "source filename to resolve against.",
                "relativeToContent");
        }
        const std::filesystem::path base = std::filesystem::path(source).parent_path();
        return (base / authored).lexically_normal().generic_string();
    }
}
