// SPDX-License-Identifier: MS-PL
#pragma once

#include <string>

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReader.hpp"

namespace Microsoft::Xna::Framework::Content
{
    /**
     * @brief NOXNA reason a recognized `.xnb` type-reader name is deliberately unsupported.
     *
     * Extend as later phases identify further known-but-out-of-scope readers -- this is not
     * meant to grow unbounded; each value should map to a real, documented scope decision
     * (see plan_xnb.md/cnb.md for the general `EffectReader` case this was introduced for).
     */
    enum class NOXNA UnsupportedContentReaderReason
    {
        /**
         * @brief The general `Microsoft.Xna.Framework.Content.EffectReader` (compiled
         * platform shader bytecode) -- see plan_xnb.md XNB-32A / plan_graphics.md Phase 74.
         */
        CompiledPlatformShaderBytecode,
    };

    /**
     * @brief NOXNA placeholder reader for a `.xnb` type-reader name CNA recognizes but does not
     *        (yet, or ever) support reading, so a fixture referencing it fails with a precise,
     *        documented error instead of a generic "unknown content reader" (plan_xnb.md XNB-14B).
     */
    class NOXNA KnownUnsupportedContentTypeReader : public ContentTypeReaderBase
    {
    public:
        KnownUnsupportedContentTypeReader(std::string targetTypeName, UnsupportedContentReaderReason reason);

        /** @brief Always throws ContentLoadException naming @ref targetTypeName and the reason. */
        std::any ReadUntyped(ContentReader& input, std::any existingInstance) override;

    private:
        UnsupportedContentReaderReason reason_;
    };

    /**
     * @brief Pre-registers the known-unsupported reader placeholders (currently: the general
     *        `EffectReader`) with ContentTypeReaderManager, so a fixture referencing one of them
     *        fails with a clear "recognized but unsupported" error even before the corresponding
     *        phase (e.g. Phase E's stock-effect readers) exists. Idempotent.
     */
    NOXNA void RegisterKnownUnsupportedXnbReaders();
}
