// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Xnb/CurveContentTypeReader.hpp"

#include "Microsoft/Xna/Framework/Content/ContentTypeReaderManager.hpp"

namespace CNA::Internal::Xnb
{
    void RegisterCurveXnbReader()
    {
        Microsoft::Xna::Framework::Content::ContentTypeReaderManager::AddTypeCreator(
            "Microsoft.Xna.Framework.Content.CurveReader", [] { return std::make_unique<CurveReader>(); });
    }
}
