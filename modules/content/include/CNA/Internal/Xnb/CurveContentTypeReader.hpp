// SPDX-License-Identifier: MS-PL
#pragma once

#include "Microsoft/Xna/Framework/Content/ContentReader.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReader.hpp"
#include "Microsoft/Xna/Framework/Curve.hpp"
#include "CNA/Internal/Xnb/XnbCanonicalData.hpp"

// plans/plan_xnb.md XNB-20: see PrimitiveContentTypeReaders.hpp's own note on why this lives in
// CNA::Internal::Xnb (FNA's CurveReader is `internal class` too).

namespace CNA::Internal::Xnb
{
    using Microsoft::Xna::Framework::Content::ContentReader;
    using Microsoft::Xna::Framework::Content::ContentTypeReader;

    /**
     * @brief FNA's real `Microsoft.Xna.Framework.Content.CurveReader`
     *        (`src/Content/ContentReaders/CurveReader.cs`), against CNA's existing `Curve`/
     *        `CurveKey`/`CurveKeyCollection` runtime API (confirmed present and FNA-faithful by
     *        the XNB-1 revalidation -- only this reader was missing).
     */
    class CurveReader : public ContentTypeReader<Microsoft::Xna::Framework::Curve>
    {
    public:
        CurveReader() : ContentTypeReader<Microsoft::Xna::Framework::Curve>("Microsoft.Xna.Framework.Curve") {}

    protected:
        Microsoft::Xna::Framework::Curve Read(
            ContentReader& input, std::optional<Microsoft::Xna::Framework::Curve> existingInstance) override
        {
            return DecodeCurveXnbData(input, std::move(existingInstance));
        }
    };

    /** @brief Registers CurveReader under its real FNA canonical name. Idempotent. */
    void RegisterCurveXnbReader();
}
