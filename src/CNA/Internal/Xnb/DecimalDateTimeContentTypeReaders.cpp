// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Xnb/DecimalDateTimeContentTypeReaders.hpp"

#include "Microsoft/Xna/Framework/Content/ContentTypeReaderManager.hpp"

namespace CNA::Internal::Xnb
{
    using Microsoft::Xna::Framework::Content::ContentTypeReaderManager;

    void RegisterDecimalDateTimeXnbReaders()
    {
#if !defined(_MSC_VER)
        ContentTypeReaderManager::AddTypeCreator(
            "Microsoft.Xna.Framework.Content.DecimalReader", [] { return std::make_unique<DecimalReader>(); });
#endif
        ContentTypeReaderManager::AddTypeCreator(
            "Microsoft.Xna.Framework.Content.TimeSpanReader", [] { return std::make_unique<TimeSpanReader>(); });
        ContentTypeReaderManager::AddTypeCreator(
            "Microsoft.Xna.Framework.Content.DateTimeReader", [] { return std::make_unique<DateTimeReader>(); });
    }
}
