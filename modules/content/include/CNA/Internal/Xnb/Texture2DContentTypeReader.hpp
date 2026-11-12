// SPDX-License-Identifier: MS-PL
#pragma once

#include "Microsoft/Xna/Framework/Content/ContentReader.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReader.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

// XNB-23/24: the first real Graphics .xnb reader -- see PrimitiveContentTypeReaders.hpp's
// own note on why this lives in CNA::Internal::Xnb (FNA's Texture2DReader is `internal class`
// too, never subclassed by game code).

namespace CNA::Internal::Xnb
{
    struct XnbTextureData;

    /**
     * @brief Constructs the runtime Texture2D adapter from already parsed canonical XNB data.
     *
     * @param input The reader of the asset being read.
     * @param device The device the reader looked up once for this read, as Refresh's readers ask
     *        GraphicsContentHelper.GraphicsDeviceFromContentReader once.
     * @param decoded Validated source texture data.
     * @param existingInstance Optional runtime texture to reload.
     * @return Runtime texture with the same supported upload semantics as Texture2DReader.
     */
    [[nodiscard]] Microsoft::Xna::Framework::Graphics::Texture2D CreateTexture2DFromXnbData(
        Microsoft::Xna::Framework::Content::ContentReader& input,
        Microsoft::Xna::Framework::Graphics::GraphicsDevice& device,
        const XnbTextureData& decoded,
        std::optional<Microsoft::Xna::Framework::Graphics::Texture2D> existingInstance =
            std::nullopt);

    /**
     * @brief Constructs, without levels, the runtime Texture2D a decoded header describes.
     *
     * The constructor's own validation -- size, the profile's format list and size limit, and the
     * renderer's -- runs here, which is where Refresh's Texture2DReader constructs it
     * (IL_001f-002f).
     *
     * @param device The device the texture is created on.
     * @param header Decoded format, size and level count; its levels are not used.
     * @return A texture with a full mip chain when the header has more than one level.
     */
    [[nodiscard]] Microsoft::Xna::Framework::Graphics::Texture2D ConstructTexture2DForXnbData(
        Microsoft::Xna::Framework::Graphics::GraphicsDevice& device, const XnbTextureData& header);

    /**
     * @brief Submits one validated XNB level, converting DXT only when required by the renderer.
     * @param texture Constructed destination texture.
     * @param device The texture's graphics device.
     * @param header Source texture metadata.
     * @param level Destination mip index.
     * @param bytes Source bytes for this mip.
     */
    void UploadTexture2DXnbLevel(Microsoft::Xna::Framework::Graphics::Texture2D& texture,
                                Microsoft::Xna::Framework::Graphics::GraphicsDevice& device,
                                const XnbTextureData& header, std::uint32_t level,
                                const std::vector<std::uint8_t>& bytes);

    /**
     * @brief Sets every decoded level of @p texture, constructed for @p decoded.
     *
     * @param texture The texture ConstructTexture2DForXnbData made for @p decoded.
     * @param device The device it was created on.
     * @param decoded Validated source texture data, levels included.
     */
    void UploadTexture2DXnbData(Microsoft::Xna::Framework::Graphics::Texture2D& texture,
                                Microsoft::Xna::Framework::Graphics::GraphicsDevice& device,
                                const XnbTextureData& decoded);

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
     *        CNA's renderer-neutral `Texture2D`/`GraphicsDevice` API (XNB-23) --
     *        never against any one renderer's internals directly.
     *
     * **Current coverage** (first pass, scoped to reach the M2 milestone): `SurfaceFormat.Color`
     * uploads raw bytes directly; `Dxt1`/`Dxt3`/`Dxt5` are always software-decompressed to
     * `Color` via the existing `CNA::Internal::Graphics::DxtUtil` (reused, not reimplemented) --
     * unlike FNA, which only decompresses when the active renderer's hardware lacks native
     * DXT/S3TC support. CNA's own per-renderer "does this renderer accept compressed data
     * natively" capability query is XNB-24's fuller scope, deferred; always
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
