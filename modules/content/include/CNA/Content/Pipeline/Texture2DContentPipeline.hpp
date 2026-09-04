// SPDX-License-Identifier: MS-PL
#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "CNA/Content/Cnb/CnbTextureCodec.hpp"
#include "CNA/Content/Pipeline/ContentPipeline.hpp"

namespace CNA::Content::Pipeline
{
    /** @brief Stable in-memory type identity for source-oriented decoded images. */
    inline constexpr const char* ImportedImageType = "CNA.Content.Pipeline.ImportedImage";

    /** @brief Stable in-memory type identity for processed Texture2D CNB data. */
    inline constexpr const char* ProcessedTexture2DType = "CNA.Content.Cnb.Texture2DData";

    /** @brief TextureProcessor parameter containing an optional decimal `R,G,B` colour key. */
    inline constexpr const char* TextureColorKeyParameter = "colorKey";

    /**
     * @brief TextureProcessor parameter naming the compiled texture format.
     *
     * Accepts XNA 4.0's own three values -- `NoChange`, `Color` and `DxtCompressed` -- plus the
     * explicit `Dxt1`, `Dxt3` and `Dxt5`, which XNA has no way to request but a native pipeline
     * has no reason to hide. Matching is case-insensitive.
     */
    inline constexpr const char* TextureFormatParameter = "textureFormat";

    /** @brief TextureProcessor parameter (`true`/`false`) generating a full mip chain. */
    inline constexpr const char* TextureGenerateMipmapsParameter = "generateMipmaps";

    /**
     * @brief TextureProcessor parameter (`true`/`false`) multiplying colour by alpha.
     *
     * **Defaults to `true`, exactly as XNA 4.0's `TextureProcessor.PremultiplyAlpha` does**
     * (plans/plan_xnapipeline.md `XNAP-96`). `BlendState::AlphaBlend` -- what
     * `SpriteBatch::Begin()` selects when given no blend state -- is the premultiplied blend in
     * both frameworks, so a texture that is *not* premultiplied renders with dark fringes under
     * the default blend state. Setting this to `false` keeps straight alpha, for a game that
     * selects `BlendState::NonPremultiplied` itself.
     *
     * Two importers override the default because their sources carry their own policy: a `.cnj`
     * document (CNJ v1 has no such member and its compiled result is defined as straight alpha)
     * and an already-built `.xnb` being transcoded (its pixels are already whatever produced them,
     * and premultiplying a second time would corrupt them).
     */
    inline constexpr const char* TexturePremultiplyAlphaParameter = "premultiplyAlpha";

    /** @brief TextureProcessor parameter (`true`/`false`) rounding level zero up to powers of two. */
    inline constexpr const char* TextureResizeToPowerOfTwoParameter = "resizeToPowerOfTwo";

    /** @brief The compiled representation a texture build produces. */
    enum class TextureBuildFormat
    {
        /** @brief Keep the imported representation. For an image source that is `Color`. */
        NoChange,

        /** @brief Uncompressed 8-bit RGBA, the portable baseline. */
        Color,

        /** @brief BC1 when the image has no partial alpha, BC3 when it has. */
        DxtCompressed,

        /** @brief BC1 / DXT1 explicitly, with one bit of alpha. */
        Dxt1,

        /** @brief BC2 / DXT3 explicitly, with four bits of explicit alpha. */
        Dxt3,

        /** @brief BC3 / DXT5 explicitly, with interpolated alpha. */
        Dxt5
    };

    /**
     * @brief Parses a `textureFormat` parameter value.
     *
     * @param value Authored value, matched case-insensitively.
     * @return The parsed format, or no value when the string names none.
     */
    [[nodiscard]] std::optional<TextureBuildFormat> TryParseTextureBuildFormat(
        const std::string& value);

    /**
     * @brief Returns the canonical spelling of a build format.
     *
     * @param format The format to name.
     * @return `NoChange`, `Color`, `DxtCompressed`, `Dxt1`, `Dxt3` or `Dxt5`.
     */
    [[nodiscard]] std::string TextureBuildFormatName(TextureBuildFormat format);

    /**
     * @brief A build-time block-compression encoder.
     *
     * Block compression is a build-time concern, so the encoder itself lives in the build-time
     * `cna_content_pipeline` module and reaches this processor as a callable rather than as a
     * link dependency. A registry configured without one still builds every uncompressed
     * texture; only a `textureFormat` that asks for compression is refused, and the refusal says
     * which configuration would provide it.
     *
     * @param format The block-compressed CNB format to produce.
     * @param rgba Exactly `width * height * 4` bytes of 8-bit RGBA.
     * @param width Level width in texels.
     * @param height Level height in texels.
     * @return The encoded blocks.
     */
    using TextureBlockEncoder = std::function<std::vector<std::uint8_t>(
        Cnb::CnbTextureFormat format, std::span<const std::uint8_t> rgba, std::uint32_t width,
        std::uint32_t height)>;

    /**
     * @brief Returns the smallest power of two greater than or equal to @p value.
     *
     * @param value Any dimension from 1 upward.
     * @return The rounded dimension; 1 for an input of 0 or 1.
     */
    [[nodiscard]] std::uint32_t NextPowerOfTwoDimension(std::uint32_t value);

    /**
     * @brief Resamples an 8-bit RGBA image to new dimensions.
     *
     * Each axis is filtered independently: a shrinking axis is area-averaged, which is the
     * correct filter for a mip chain, and a growing axis is linearly interpolated. Everything is
     * integer arithmetic, so the result depends on nothing but the inputs.
     *
     * @param rgba Exactly `sourceWidth * sourceHeight * 4` bytes.
     * @param sourceWidth Source width in texels, at least 1.
     * @param sourceHeight Source height in texels, at least 1.
     * @param targetWidth Target width in texels, at least 1.
     * @param targetHeight Target height in texels, at least 1.
     * @return Exactly `targetWidth * targetHeight * 4` bytes.
     * @throws std::invalid_argument for a zero dimension or a wrongly sized buffer.
     */
    [[nodiscard]] std::vector<std::uint8_t> ResampleRgbaImage(std::span<const std::uint8_t> rgba,
                                                              std::uint32_t sourceWidth,
                                                              std::uint32_t sourceHeight,
                                                              std::uint32_t targetWidth,
                                                              std::uint32_t targetHeight);

    /**
     * @brief Multiplies each colour channel by its own alpha, in place.
     *
     * This is what XNA 4.0's `TextureProcessor` does by default, and what
     * `BlendState::AlphaBlend` -- `SpriteBatch`'s own default -- expects to receive.
     *
     * @param rgba Whole texels; a trailing partial texel is left alone.
     */
    void PremultiplyRgbaAlpha(std::vector<std::uint8_t>& rgba);

    /**
     * @brief Builds every mip level below level zero, halving until 1x1.
     *
     * @param level0 Exactly `width * height * 4` bytes.
     * @param width Level-zero width in texels, at least 1.
     * @param height Level-zero height in texels, at least 1.
     * @return The levels after level zero, largest first; empty for a 1x1 image.
     */
    [[nodiscard]] std::vector<std::vector<std::uint8_t>> GenerateRgbaMipChain(
        std::span<const std::uint8_t> level0, std::uint32_t width, std::uint32_t height);

    /** @brief Source-oriented image data produced before texture policy is applied. */
    struct ImportedImage
    {
        /** @brief Decoded image width in texels. */
        std::uint32_t width = 0u;

        /** @brief Decoded image height in texels. */
        std::uint32_t height = 0u;

        /** @brief Exact level-zero pixels in R, G, B, A byte order. */
        std::vector<std::uint8_t> rgbaPixels;

        /** @brief Optional additional Rgba8 mip levels in descending dimension order. */
        std::vector<std::vector<std::uint8_t>> additionalRgbaMipLevels;

        /** @brief Source-authored colour-key policy, or absent for ordinary image sources. */
        std::optional<std::array<std::uint8_t, 3>> authoredColorKey;

        /**
         * @brief Source-authored premultiplied-alpha policy, or absent to use the processor default.
         *
         * Absent for an ordinary image source, where the XNA-compatible default (`true`) applies.
         * Set to `false` by the two importers whose sources define their own answer: a `.cnj`
         * document, whose v1 compiled result is straight alpha, and an already-built `.xnb`, whose
         * pixels have already had whatever alpha policy produced them applied once.
         *
         * An explicit `premultiplyAlpha` processor parameter always wins over this, so a build can
         * still ask either way; this only decides what happens when nobody asked.
         */
        std::optional<bool> authoredPremultiplyAlpha;
    };

    /**
     * @brief Decodes one image through CNA's shared decoder into source-oriented data.
     *
     * @param source Native image path.
     * @return Validated dimensions and exact Rgba8 pixels.
     */
    [[nodiscard]] ImportedImage DecodeImportedImage(const std::filesystem::path& source);

    /**
     * @brief Converts validated source-oriented pixels into canonical Texture2D CNB data.
     *
     * This is the parameter-free core used by TextureProcessor and generated glTF texture
     * children after any source-specific policy has already been applied.
     *
     * @param image Validated decoded image and optional mip levels.
     * @return One canonical Rgba8 Texture2D representation.
     */
    [[nodiscard]] Cnb::CnbTextureData BuildCnbTexture2DData(ImportedImage image);

    /** @brief Headless source image importer backed by CNA's shared image decoder. */
    class ImageImporter final : public ContentImporter
    {
    public:
        /** @brief Returns the stable built-in importer identity. */
        [[nodiscard]] ContentComponentIdentity Identity() const override;

        /** @brief Returns every source image extension supported by default routing. */
        [[nodiscard]] std::vector<std::string> SourceExtensions() const override;

        /**
         * @brief Returns the only imported type this component can produce.
         * @return A vector containing ImportedImageType.
         */
        [[nodiscard]] std::vector<std::string> OutputTypes() const override;

        /**
         * @brief Decodes the primary source through CNA's shared ImageLoader.
         *
         * @param context Call-scoped importer context.
         * @return A source-oriented ImportedImage value.
         */
        [[nodiscard]] ContentValue Import(ContentImporterContext& context) const override;
    };

    /** @brief Applies Texture2D build policy and produces canonical CnbTextureData. */
    class TextureProcessor final : public ContentProcessor
    {
    public:
        /** @brief Creates a processor that can produce uncompressed textures only. */
        TextureProcessor() = default;

        /**
         * @brief Creates a processor that can also produce block-compressed textures.
         *
         * @param encoder Build-time block encoder, or empty for the uncompressed-only behaviour.
         */
        explicit TextureProcessor(TextureBlockEncoder encoder);

        /** @brief Returns the stable built-in processor identity. */
        [[nodiscard]] ContentComponentIdentity Identity() const override;

        /** @brief Returns ImportedImageType. */
        [[nodiscard]] std::string InputType() const override;

        /** @brief Returns ProcessedTexture2DType. */
        [[nodiscard]] std::string OutputType() const override;

        /**
         * @brief Validates the optional `colorKey` `R,G,B` string.
         *
         * @param parameters Parameters to validate before image transformation.
         */
        void ValidateParameters(const ContentProcessorParameters& parameters) const override;

        /**
         * @brief Applies configured texture policy to a decoded image.
         *
         * @param input ImportedImage value.
         * @param context Processor context containing validated parameters.
         * @return Canonical CnbTextureData boxed as ProcessedTexture2DType.
         */
        [[nodiscard]] ContentValue Process(const ContentValue& input,
                                           ContentProcessorContext& context) const override;

    private:
        TextureBlockEncoder encoder_;
    };

    /** @brief Pipeline writer adapter over the authoritative Texture2D CNB codec. */
    class Texture2DContentWriter final : public ContentTypeWriter
    {
    public:
        /** @brief Returns the stable built-in writer identity. */
        [[nodiscard]] ContentComponentIdentity Identity() const override;

        /**
         * @brief Returns the frozen Texture2D schema and encoder identity.
         * @return One stable Texture2D asset/schema/codec declaration.
         */
        [[nodiscard]] std::vector<ContentWriterSchemaIdentity>
        OutputSchemaIdentities() const override;

        /** @brief Returns ProcessedTexture2DType. */
        [[nodiscard]] std::string InputType() const override;

        /**
         * @brief Calls the existing EncodeTexture2DToCnb() implementation.
         *
         * @param input Canonical CnbTextureData value.
         * @param logicalName Logical asset name written to CNB metadata.
         * @return Complete CNB bytes and the frozen Texture2D asset identity.
         */
        [[nodiscard]] ContentWriteResult Write(const ContentValue& input,
                                               const std::string& logicalName) const override;
    };

    /**
     * @brief Registers the built-in ImageImporter, TextureProcessor and Texture2DContentWriter.
     *
     * @param registry Explicit registry to configure before builds begin.
     * @param encoder Optional build-time block encoder enabling compressed `textureFormat`
     *        values. Supplied by `cna_content_pipeline`; absent in a registry built from the
     *        runtime module alone.
     */
    void RegisterTexture2DContentPipeline(ContentPipelineRegistry& registry,
                                          TextureBlockEncoder encoder = {});
}
