// SPDX-License-Identifier: MS-PL
// The .xnb readers of plain values -- primitives, math types and their collections -- which a
// value asset and the readers of fonts, effects and models all need. Decimal, DateTime and
// TimeSpan are not among them: no font, effect material or plain value holds one, and their
// readers link the sharp-runtime classes; a type that may hold anything registers every reader
// (detail::BuiltinContent).
//
// Link boundary: its own translation unit, apart from RegisterAllBuiltInXnbReaders(), so that
// registering the value readers does not link the readers of every asset type.
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReaderManager.hpp"
#include "CNA/Internal/Xnb/MathContentTypeReaders.hpp"
#include "CNA/Internal/Xnb/PrimitiveContentTypeReaders.hpp"

namespace Microsoft::Xna::Framework::Content::detail
{
    void RegisterValueXnbReaders()
    {
        RegisterDeclaredXnbReaders([] {
            CNA::Internal::Xnb::RegisterPrimitiveXnbReaders();
            CNA::Internal::Xnb::RegisterMathXnbReaders();
        });
    }
}
