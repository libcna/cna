// SPDX-License-Identifier: MS-PL
#pragma once

#include "Microsoft/Xna/Framework/Content/ContentReader.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReader.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteFont.hpp"

// plans/plan_xnb.md XNB-31: SpriteFontReader -- see PrimitiveContentTypeReaders.hpp's own note on why
// this lives in CNA::Internal::Xnb (FNA's SpriteFontReader is `internal class`, never subclassed
// by game code).

namespace CNA::Internal::Xnb
{
    /**
     * @brief FNA's real `Microsoft.Xna.Framework.Content.SpriteFontReader`
     *        (`src/Content/ContentReaders/SpriteFontReader.cs`).
     *
     * Depends on `Microsoft.Xna.Framework.Content.ListReader\`1[[...]]` being registered for
     * `Rectangle`, `char16_t` (`System.Char`), and `Vector3` -- see @ref RegisterSpriteFontXnbReader().
     */
    class SpriteFontReader
        : public Microsoft::Xna::Framework::Content::ContentTypeReader<Microsoft::Xna::Framework::Graphics::SpriteFont>
    {
    public:
        SpriteFontReader()
            : Microsoft::Xna::Framework::Content::ContentTypeReader<Microsoft::Xna::Framework::Graphics::SpriteFont>(
                  "Microsoft.Xna.Framework.Graphics.SpriteFont") {}

    protected:
        Microsoft::Xna::Framework::Graphics::SpriteFont Read(
            Microsoft::Xna::Framework::Content::ContentReader& input,
            std::optional<Microsoft::Xna::Framework::Graphics::SpriteFont> existingInstance) override;
    };

    /**
     * @brief Registers SpriteFontReader and its `ListReader<Rectangle>`/`ListReader<char16_t>`/
     *        `ListReader<Vector3>` element-collection dependencies under their real FNA canonical
     *        names. Idempotent. Does not register `Texture2DReader` (the glyph atlas reader) --
     *        call @ref RegisterTexture2DXnbReader() separately, matching a real `.xnb` file's own
     *        independent type-reader-table entries.
     */
    void RegisterSpriteFontXnbReader();
}
