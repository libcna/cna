// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Xnb/SpriteFontContentTypeReader.hpp"

#include <limits>
#include "CNA/Internal/ContentManagerAccess.hpp"

#include "CNA/Internal/Xnb/CollectionContentTypeReaders.hpp"
#include "CNA/Internal/Xnb/MathContentTypeReaders.hpp"
#include "CNA/Internal/Xnb/Texture2DContentTypeReader.hpp"
#include "CNA/Internal/Xnb/XnbCanonicalData.hpp"
#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReaderManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "System/NotImplementedException.hpp"

namespace CNA::Internal::Xnb
{
    using Microsoft::Xna::Framework::Rectangle;
    using Microsoft::Xna::Framework::Vector3;
    using Microsoft::Xna::Framework::Content::ContentReader;
    using Microsoft::Xna::Framework::Content::ContentTypeReaderManager;
    using Microsoft::Xna::Framework::Graphics::SpriteFont;
    using Microsoft::Xna::Framework::Graphics::Texture2D;

    Microsoft::Xna::Framework::Graphics::SpriteFont SpriteFontReader::Read(
        ContentReader& input, std::optional<SpriteFont> existingInstance)
    {
        if (existingInstance.has_value())
        {
            // FNA's own existingInstance branch only reloads the glyph atlas's GPU resource
            // in-place and discards the rest, but is unreachable via normal ContentManager
            // dispatch: SpriteFontReader never overrides CanDeserializeIntoExistingObject, so it
            // stays false (base default) and ContentManager never supplies an existingInstance.
            // Faithfully porting the in-place reload would require a SpriteFont mutator that
            // serves no other purpose than this dead path, so it is not implemented.
            throw System::NotImplementedException(
                "SpriteFontReader: reloading into an existing SpriteFont instance is not "
                "supported (CanDeserializeIntoExistingObject is false, matching FNA).");
        }

        Microsoft::Xna::Framework::Graphics::GraphicsDevice* device = nullptr;
        // Refresh SpriteFontReader delegates its atlas to Texture2DReader: lookup/construction
        // follows the atlas header (Texture2DReader.Read IL_001f-002f), then each read is
        // followed by SetData before the next level (IL_0039-0064).
        std::optional<Texture2D> atlas;
        XnbSpriteFontData decoded = DecodeSpriteFontXnbData(
            input, std::numeric_limits<std::uint32_t>::max(),
            [&](const XnbTextureData& header) {
                device = &CNA::Internal::ContentManagerAccess::GraphicsDeviceFromContentReader(input);
                RequireXnbTextureFormatBeforeConstruction(input, header);
                atlas.emplace(ConstructTexture2DForXnbData(*device, header));
            },
            [&](const XnbTextureData& header, std::uint32_t, std::uint32_t level,
                const std::vector<std::uint8_t>& bytes) {
                UploadTexture2DXnbLevel(*atlas, *device, header, level, bytes);
            });
        Texture2D texture = std::move(*atlas);
        // Canonical decoding bypasses ReadObject<Texture2D>, but the atlas is
        // still a newly materialized disposable belonging to this read.
        input.RecordDisposable(texture);

        return SpriteFont(
            texture, decoded.glyphs, decoded.cropping, decoded.characters,
            decoded.lineSpacing, decoded.spacing, decoded.kerning, decoded.defaultCharacter);
    }

    void RegisterSpriteFontXnbReader()
    {
        ContentTypeReaderManager::AddTypeCreator(
            "Microsoft.Xna.Framework.Content.SpriteFontReader",
            [] { return std::make_unique<SpriteFontReader>(); });

        ContentTypeReaderManager::AddTypeCreator(
            "Microsoft.Xna.Framework.Content.ListReader`1[[Microsoft.Xna.Framework.Rectangle]]",
            [] {
                return std::make_unique<ListReader<Rectangle>>(
                    "System.Collections.Generic.List`1[[Microsoft.Xna.Framework.Rectangle]]",
                    "Microsoft.Xna.Framework.Content.RectangleReader");
            });
        ContentTypeReaderManager::AddTypeCreator(
            "Microsoft.Xna.Framework.Content.ListReader`1[[System.Char]]",
            [] {
                return std::make_unique<ListReader<char16_t>>(
                    "System.Collections.Generic.List`1[[System.Char]]",
                    "Microsoft.Xna.Framework.Content.CharReader");
            });
        ContentTypeReaderManager::AddTypeCreator(
            "Microsoft.Xna.Framework.Content.ListReader`1[[Microsoft.Xna.Framework.Vector3]]",
            [] {
                return std::make_unique<ListReader<Vector3>>(
                    "System.Collections.Generic.List`1[[Microsoft.Xna.Framework.Vector3]]",
                    "Microsoft.Xna.Framework.Content.Vector3Reader");
            });
    }
}
