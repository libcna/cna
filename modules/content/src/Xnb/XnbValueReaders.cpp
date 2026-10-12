// SPDX-License-Identifier: MS-PL
// The .xnb readers of plain values -- primitives, math types, Decimal/DateTime/TimeSpan and their
// collections -- which a value asset and the readers of fonts, effects and models all need.
//
// Link boundary: its own translation unit, apart from RegisterAllBuiltInXnbReaders(), so that
// registering the value readers does not link the readers of every asset type.
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReaderManager.hpp"
#include "CNA/Internal/Xnb/DecimalDateTimeContentTypeReaders.hpp"
#include "CNA/Internal/Xnb/MathContentTypeReaders.hpp"
#include "CNA/Internal/Xnb/PrimitiveContentTypeReaders.hpp"

namespace Microsoft::Xna::Framework::Content::detail
{
    void RegisterValueXnbReaders()
    {
        RegisterDeclaredXnbReaders([] {
            CNA::Internal::Xnb::RegisterPrimitiveXnbReaders();
            CNA::Internal::Xnb::RegisterMathXnbReaders();
            CNA::Internal::Xnb::RegisterDecimalDateTimeXnbReaders();
        });
    }
}
