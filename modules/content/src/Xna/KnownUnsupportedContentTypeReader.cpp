// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Content/KnownUnsupportedContentTypeReader.hpp"

#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReaderManager.hpp"

namespace Microsoft::Xna::Framework::Content
{
    namespace
    {
        std::string ReasonText(UnsupportedContentReaderReason reason)
        {
            switch (reason)
            {
                case UnsupportedContentReaderReason::CompiledPlatformShaderBytecode:
                    return "compiled platform shader bytecode is not supported; "
                           "see plan_xnb.md XNB-32A and plan_graphics.md Phase 74";
                default:
                    return "reason unspecified";
            }
        }
    }

    KnownUnsupportedContentTypeReader::KnownUnsupportedContentTypeReader(
        std::string targetTypeName, UnsupportedContentReaderReason reason)
        : ContentTypeReaderBase(std::move(targetTypeName)), reason_(reason)
    {
    }

    std::any KnownUnsupportedContentTypeReader::ReadUntyped(
        ContentReader& /*input*/, std::any /*existingInstance*/)
    {
        throw ContentLoadException(
            "The '" + getTargetTypeNameProperty() + "' content type reader is recognized but not "
            "supported by CNA (" + ReasonText(reason_) + ").");
    }

    void RegisterKnownUnsupportedXnbReaders()
    {
        // No built-in reader is currently in this category. The general EffectReader moved to
        // CNA::Internal::Xnb::RegisterEffectXnbReader when compiled Effect Framework support
        // landed; keep this idempotent hook as the registry's extension point for future formats.
    }
}
