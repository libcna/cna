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

    std::shared_ptr<void> KnownUnsupportedContentTypeReader::ReadUntyped(
        ContentReader& /*input*/, std::shared_ptr<void> /*existingInstance*/)
    {
        throw ContentLoadException(
            "The '" + getTargetTypeNameProperty() + "' content type reader is recognized but not "
            "supported by CNA (" + ReasonText(reason_) + ").");
    }

    void RegisterKnownUnsupportedXnbReaders()
    {
        ContentTypeReaderManager::AddTypeCreator(
            "Microsoft.Xna.Framework.Content.EffectReader",
            [] {
                return std::make_unique<KnownUnsupportedContentTypeReader>(
                    "Microsoft.Xna.Framework.Graphics.Effect",
                    UnsupportedContentReaderReason::CompiledPlatformShaderBytecode);
            });
    }
}
