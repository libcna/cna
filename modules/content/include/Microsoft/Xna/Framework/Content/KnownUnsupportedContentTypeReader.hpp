// SPDX-License-Identifier: MS-PL
#pragma once

#include <string>

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReader.hpp"

namespace Microsoft::Xna::Framework::Content
{
    /**
     * @brief CNAEXT reason a recognized `.xnb` type-reader name is deliberately unsupported.
     *
     * Extend as later phases identify further known-but-out-of-scope readers -- this is not
     * meant to grow unbounded; each value should map to a real, documented scope decision
     * The general `EffectReader` originally motivated this type but is now implemented; the enum
     * value remains source-compatible for external placeholder registrations.
     */
    enum class CNAEXT UnsupportedContentReaderReason
    {
        /**
         * @brief The general `Microsoft.Xna.Framework.Content.EffectReader` (compiled
         * platform shader bytecode), retained for source compatibility with older integrations.
         */
        CompiledPlatformShaderBytecode,
    };

    /**
     * @brief CNAEXT placeholder reader for a `.xnb` type-reader name CNA recognizes but does not
     *        (yet, or ever) support reading, so a fixture referencing it fails with a precise,
     *        documented error instead of a generic "unknown content reader" (plans/plan_xnb.md XNB-14B).
     */
    class CNAEXT KnownUnsupportedContentTypeReader : public ContentTypeReaderBase
    {
    public:
        KnownUnsupportedContentTypeReader(std::string targetTypeName, UnsupportedContentReaderReason reason);

        /** @brief Always throws ContentLoadException naming @ref targetTypeName and the reason. */
        std::any ReadUntyped(ContentReader& input, std::any existingInstance) override;

    private:
        UnsupportedContentReaderReason reason_;
    };

    /**
     * @brief Registers built-in known-unsupported placeholders. Currently a no-op because every
     *        formerly listed reader has a concrete implementation. Idempotent.
     */
    CNAEXT void RegisterKnownUnsupportedXnbReaders();
}
