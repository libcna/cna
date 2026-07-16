// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Xnb/PrimitiveContentTypeReaders.hpp"

#include "Microsoft/Xna/Framework/Content/ContentTypeReaderManager.hpp"

namespace CNA::Internal::Xnb
{
    using Microsoft::Xna::Framework::Content::ContentTypeReaderManager;

    void RegisterPrimitiveXnbReaders()
    {
        ContentTypeReaderManager::AddTypeCreator(
            "Microsoft.Xna.Framework.Content.BooleanReader", [] { return std::make_unique<BooleanReader>(); });
        ContentTypeReaderManager::AddTypeCreator(
            "Microsoft.Xna.Framework.Content.ByteReader", [] { return std::make_unique<ByteReader>(); });
        ContentTypeReaderManager::AddTypeCreator(
            "Microsoft.Xna.Framework.Content.SByteReader", [] { return std::make_unique<SByteReader>(); });
        ContentTypeReaderManager::AddTypeCreator(
            "Microsoft.Xna.Framework.Content.Int16Reader", [] { return std::make_unique<Int16Reader>(); });
        ContentTypeReaderManager::AddTypeCreator(
            "Microsoft.Xna.Framework.Content.UInt16Reader", [] { return std::make_unique<UInt16Reader>(); });
        ContentTypeReaderManager::AddTypeCreator(
            "Microsoft.Xna.Framework.Content.Int32Reader", [] { return std::make_unique<Int32Reader>(); });
        ContentTypeReaderManager::AddTypeCreator(
            "Microsoft.Xna.Framework.Content.UInt32Reader", [] { return std::make_unique<UInt32Reader>(); });
        ContentTypeReaderManager::AddTypeCreator(
            "Microsoft.Xna.Framework.Content.Int64Reader", [] { return std::make_unique<Int64Reader>(); });
        ContentTypeReaderManager::AddTypeCreator(
            "Microsoft.Xna.Framework.Content.UInt64Reader", [] { return std::make_unique<UInt64Reader>(); });
        ContentTypeReaderManager::AddTypeCreator(
            "Microsoft.Xna.Framework.Content.SingleReader", [] { return std::make_unique<SingleReader>(); });
        ContentTypeReaderManager::AddTypeCreator(
            "Microsoft.Xna.Framework.Content.DoubleReader", [] { return std::make_unique<DoubleReader>(); });
        ContentTypeReaderManager::AddTypeCreator(
            "Microsoft.Xna.Framework.Content.CharReader", [] { return std::make_unique<CharReader>(); });
        ContentTypeReaderManager::AddTypeCreator(
            "Microsoft.Xna.Framework.Content.StringReader", [] { return std::make_unique<StringReader>(); });
    }
}
