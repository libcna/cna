// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Xnb/PrimitiveContentTypeReaders.hpp"

#include <cstdint>

#include "CNA/Internal/Xnb/CollectionContentTypeReaders.hpp"
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

        // XNA 4.0's automatic writer serializes a processor-returned List<string> with this
        // framework reader pair. ContentManifestExtensions uses it as the root object of its
        // generated deployment manifest; it is a standard primitive collection, not a reader
        // owned by that sample (SAMPLE-092).
        ContentTypeReaderManager::AddTypeCreator(
            "Microsoft.Xna.Framework.Content.ListReader`1[[System.String]]",
            [] {
                return std::make_unique<ListReader<std::string>>(
                    "System.Collections.Generic.List`1[[System.String]]",
                    "Microsoft.Xna.Framework.Content.StringReader");
            });

        // A `List<int>`, which is how a skeleton hierarchy -- one parent index per bone -- reaches
        // a game from a custom model processor. The template existed but no instantiation of it
        // was registered, and an .xnb's type-reader table must resolve IN FULL before a single
        // object is read, so the whole asset failed. Found by cna-samples SAMPLE-051.
        ContentTypeReaderManager::AddTypeCreator(
            "Microsoft.Xna.Framework.Content.ListReader`1[[System.Int32]]",
            [] {
                return std::make_unique<ListReader<std::int32_t>>(
                    "System.Collections.Generic.List`1[[System.Int32]]",
                    "Microsoft.Xna.Framework.Content.Int32Reader");
            });

        // A standard primitive-only dictionary is content-pipeline infrastructure, not metadata
        // owned by whichever game happens to serialize one. SkinnedModelExtensions uses this
        // shape for its processor-generated bone-name lookup table.
        ContentTypeReaderManager::AddTypeCreator(
            "Microsoft.Xna.Framework.Content.DictionaryReader`2[[System.String],[System.Int32]]",
            [] {
                return std::make_unique<DictionaryReader<std::string, std::int32_t>>(
                    "System.Collections.Generic.Dictionary`2[[System.String],[System.Int32]]",
                    "Microsoft.Xna.Framework.Content.StringReader",
                    "Microsoft.Xna.Framework.Content.Int32Reader");
            });
    }
}
