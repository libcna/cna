// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "CNA/Content/Pipeline/ContentPipeline.hpp"

namespace CNA::Content::Pipeline
{
    /** @brief Stable in-memory type identity for source-oriented decoded images. */
    inline constexpr const char* ImportedImageType = "CNA.Content.Pipeline.ImportedImage";

    /** @brief Stable in-memory type identity for processed Texture2D CNB data. */
    inline constexpr const char* ProcessedTexture2DType = "CNA.Content.Cnb.Texture2DData";

    /** @brief TextureProcessor parameter containing an optional decimal `R,G,B` colour key. */
    inline constexpr const char* TextureColorKeyParameter = "colorKey";

    /** @brief Source-oriented image data produced before texture policy is applied. */
    struct ImportedImage
    {
        /** @brief Decoded image width in texels. */
        std::uint32_t width = 0u;

        /** @brief Decoded image height in texels. */
        std::uint32_t height = 0u;

        /** @brief Exact level-zero pixels in R, G, B, A byte order. */
        std::vector<std::uint8_t> rgbaPixels;
    };

    /** @brief Headless source image importer backed by CNA's shared image decoder. */
    class ImageImporter final : public ContentImporter
    {
    public:
        /** @brief Returns the stable built-in importer identity. */
        [[nodiscard]] ContentComponentIdentity Identity() const override;

        /** @brief Returns every source image extension supported by default routing. */
        [[nodiscard]] std::vector<std::string> SourceExtensions() const override;

        /** @brief Returns ImportedImageType. */
        [[nodiscard]] std::string OutputType() const override;

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
    };

    /** @brief Pipeline writer adapter over the authoritative Texture2D CNB codec. */
    class Texture2DContentWriter final : public ContentTypeWriter
    {
    public:
        /** @brief Returns the stable built-in writer identity. */
        [[nodiscard]] ContentComponentIdentity Identity() const override;

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
     */
    void RegisterTexture2DContentPipeline(ContentPipelineRegistry& registry);
}
