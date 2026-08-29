// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Xnb/SpriteFontContentTypeReader.hpp"

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

        if (!input.getContentManagerProperty())
        {
            throw Microsoft::Xna::Framework::Content::ContentLoadException(
                "SpriteFontReader: no GraphicsDevice available (ContentManager was not set on "
                "this ContentReader).");
        }
        const auto maximumDimension = static_cast<std::uint32_t>(
            input.getContentManagerProperty()->getGraphicsDeviceInternal()
                .GetMaxTextureDimension());
        XnbSpriteFontData decoded = DecodeSpriteFontXnbData(input, maximumDimension);
        Texture2D texture = CreateTexture2DFromXnbData(input, decoded.atlas);

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
