// SPDX-License-Identifier: MS-PL
#pragma once

#include "Microsoft/Xna/Framework/Content/ContentReader.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReader.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

// plans/plan_xnb.md XNB-23/24: the first real Graphics .xnb reader -- see PrimitiveContentTypeReaders.hpp's
// own note on why this lives in CNA::Internal::Xnb (FNA's Texture2DReader is `internal class`
// too, never subclassed by game code).

namespace CNA::Internal::Xnb
{
    struct XnbTextureData;

    /**
     * @brief Constructs the runtime Texture2D adapter from already parsed canonical XNB data.
     *
     * @param input Reader supplying the owning ContentManager and GraphicsDevice.
     * @param decoded Validated source texture data.
     * @param existingInstance Optional runtime texture to reload.
     * @return Runtime texture with the same supported upload semantics as Texture2DReader.
     */
    [[nodiscard]] Microsoft::Xna::Framework::Graphics::Texture2D CreateTexture2DFromXnbData(
        Microsoft::Xna::Framework::Content::ContentReader& input,
        const XnbTextureData& decoded,
        std::optional<Microsoft::Xna::Framework::Graphics::Texture2D> existingInstance =
            std::nullopt);

    /**
     * @brief FNA's inert base `Microsoft.Xna.Framework.Content.TextureReader`.
     *
     * The reader exists in XNB manifests that contain a field declared as the abstract
     * `Texture` base, even when the serialized object is dispatched to a concrete texture
     * reader. Like FNA, it consumes no data and returns the existing instance.
     */
    class TextureReader
        : public Microsoft::Xna::Framework::Content::ContentTypeReader<Microsoft::Xna::Framework::Graphics::Texture*>
    {
    public:
        /** @brief Constructs the inert base texture reader. */
        TextureReader()
            : Microsoft::Xna::Framework::Content::ContentTypeReader<Microsoft::Xna::Framework::Graphics::Texture*>(
                  "Microsoft.Xna.Framework.Graphics.Texture") {}

    protected:
        Microsoft::Xna::Framework::Graphics::Texture* Read(
            Microsoft::Xna::Framework::Content::ContentReader& input,
            std::optional<Microsoft::Xna::Framework::Graphics::Texture*> existingInstance) override;
    };

    /**
     * @brief FNA's real `Microsoft.Xna.Framework.Content.Texture2DReader`
     *        (`src/Content/ContentReaders/Texture2DReader.cs`), implemented strictly against
     *        CNA's renderer-neutral `Texture2D`/`GraphicsDevice` API (plans/plan_xnb.md XNB-23) --
     *        never against any one renderer's internals directly.
     *
     * **Current coverage** (first pass, scoped to reach the M2 milestone): `SurfaceFormat.Color`
     * uploads raw bytes directly; `Dxt1`/`Dxt3`/`Dxt5` are always software-decompressed to
     * `Color` via the existing `CNA::Internal::Graphics::DxtUtil` (reused, not reimplemented) --
     * unlike FNA, which only decompresses when the active renderer's hardware lacks native
     * DXT/S3TC support. CNA's own per-renderer "does this renderer accept compressed data
     * natively" capability query is plans/plan_xnb.md XNB-24's fuller scope, deferred; always
     * decompressing is always correct, just not always the most efficient path. Every other
     * `SurfaceFormat` (`Bgr565`, `Bgra5551`, `Bgra4444`, `NormalizedByte2/4`, `Rgba1010102`,
     * `Rg32`, `Rgba64`, `Alpha8`, `Single`, `Vector2`, `Vector4`, `HalfSingle`, `HalfVector2/4`,
     * `HdrBlendable`) is not yet implemented and throws a clear
     * `Microsoft::Xna::Framework::Content::ContentLoadException` naming the format, rather than
     * silently uploading garbage pixels.
     */
    class Texture2DReader
        : public Microsoft::Xna::Framework::Content::ContentTypeReader<Microsoft::Xna::Framework::Graphics::Texture2D>
    {
    public:
        Texture2DReader()
            : Microsoft::Xna::Framework::Content::ContentTypeReader<Microsoft::Xna::Framework::Graphics::Texture2D>(
                  "Microsoft.Xna.Framework.Graphics.Texture2D") {}

    protected:
        Microsoft::Xna::Framework::Graphics::Texture2D Read(
            Microsoft::Xna::Framework::Content::ContentReader& input,
            std::optional<Microsoft::Xna::Framework::Graphics::Texture2D> existingInstance) override;
    };

    /** @brief Registers TextureReader and Texture2DReader under their real FNA canonical names. Idempotent. */
    void RegisterTexture2DXnbReader();
}
