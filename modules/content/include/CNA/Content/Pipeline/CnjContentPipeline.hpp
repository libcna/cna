// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "CNA/Content/Cnb/CnbTextureCodec.hpp"
#include "CNA/Content/Pipeline/ContentPipeline.hpp"
#include "CNA/Content/Pipeline/Texture2DContentPipeline.hpp"
#include "Microsoft/Xna/Framework/Curve.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedModelEXT.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "SharpRuntime/SharpRuntimeHelper.hpp"

namespace CNA::Content::Pipeline
{
    /** @brief Stable in-memory type identity for source-oriented Texture3D pixels. */
    inline constexpr const char* ImportedTexture3DType =
        "CNA.Content.Pipeline.ImportedTexture3D";

    /** @brief Stable in-memory type identity for processed Texture3D CNB data. */
    inline constexpr const char* ProcessedTexture3DType = "CNA.Content.Cnb.Texture3DData";

    /** @brief Stable in-memory type identity for a source DDS cube map. */
    inline constexpr const char* ImportedTextureCubeType =
        "CNA.Content.Pipeline.ImportedTextureCube";

    /** @brief Stable in-memory type identity for processed TextureCube CNB data. */
    inline constexpr const char* ProcessedTextureCubeType = "CNA.Content.Cnb.TextureCubeData";

    /** @brief Stable in-memory type identity for Curve source semantics. */
    inline constexpr const char* ImportedCurveType = "CNA.Content.Pipeline.ImportedCurve";

    /** @brief Stable in-memory type identity for processed Curve semantics. */
    inline constexpr const char* ProcessedCurveType = "CNA.Content.Compiled.Curve";

    /** @brief Stable in-memory type identity for AnimationClip source semantics. */
    inline constexpr const char* ImportedAnimationClipType =
        "CNA.Content.Pipeline.ImportedAnimationClip";

    /** @brief Stable in-memory type identity for processed AnimationClip semantics. */
    inline constexpr const char* ProcessedAnimationClipType =
        "CNA.Content.Compiled.AnimationClip";

    /** @brief Stable in-memory type identity for source-oriented imported SpriteFont data. */
    inline constexpr const char* ImportedSpriteFontType =
        "CNA.Content.Pipeline.ImportedSpriteFont";

    /** @brief Stable in-memory type identity for processed SpriteFont CNB data. */
    inline constexpr const char* ProcessedSpriteFontType = "CNA.Content.Cnb.SpriteFontData";

    /** @brief Raw source-oriented Rgba8 volume imported from a Texture3D CNJ sidecar. */
    struct ImportedTexture3D
    {
        /** @brief Volume width in texels. */
        std::uint32_t width = 0u;

        /** @brief Volume height in texels. */
        std::uint32_t height = 0u;

        /** @brief Volume depth in texels. */
        std::uint32_t depth = 0u;

        /** @brief One tightly packed Rgba8 mip level. */
        std::vector<std::uint8_t> rgbaPixels;

        /** @brief Optional additional tightly packed Rgba8 mip levels. */
        std::vector<std::vector<std::uint8_t>> additionalRgbaMipLevels;
    };

    /** @brief DDS cube-map source decoded by CNA's existing shared DDS importer. */
    struct ImportedTextureCube
    {
        /** @brief Canonical decoded faces and representations before processor policy. */
        Cnb::CnbTextureData sourceData;
    };

    /** @brief Curve source semantics parsed by the canonical shared CNJ reader. */
    struct ImportedCurve
    {
        /** @brief Parsed source value. */
        Microsoft::Xna::Framework::Curve value;
    };

    /** @brief Runtime-oriented Curve value ready for the existing CNB encoder. */
    struct ProcessedCurve
    {
        /** @brief Processed curve value. */
        Microsoft::Xna::Framework::Curve value;
    };

    /** @brief AnimationClip source semantics parsed by the canonical shared CNJ reader. */
    struct ImportedAnimationClip
    {
        /** @brief Parsed source value. */
        Microsoft::Xna::Framework::Graphics::AnimationClipEXT value;
    };

    /** @brief Runtime-oriented AnimationClip ready for the existing CNB encoder. */
    struct ProcessedAnimationClip
    {
        /** @brief Processed animation value. */
        Microsoft::Xna::Framework::Graphics::AnimationClipEXT value;
    };

    /** @brief One glyph imported from a SpriteFont CNJ descriptor. */
    struct ImportedSpriteFontGlyph
    {
        /** @brief Character rendered by this glyph. */
        SharpRuntime::charcs character = 0;

        /** @brief Glyph rectangle within the imported atlas. */
        Microsoft::Xna::Framework::Rectangle source;

        /** @brief Glyph cropping/offset rectangle. */
        Microsoft::Xna::Framework::Rectangle crop;

        /** @brief Left bearing, advance width and right bearing. */
        Microsoft::Xna::Framework::Vector3 kerning;
    };

    /** @brief Source-oriented SpriteFont descriptor plus its decoded atlas image. */
    struct ImportedSpriteFont
    {
        /** @brief Decoded atlas before embedded Texture2D policy is applied. */
        ImportedImage atlas;

        /** @brief Glyphs in canonical character order. */
        std::vector<ImportedSpriteFontGlyph> glyphs;

        /** @brief Vertical distance between text lines. */
        std::int32_t lineSpacing = 0;

        /** @brief Extra horizontal spacing between characters. */
        float spacing = 0.0f;

        /** @brief Fallback character, or absent. */
        std::optional<SharpRuntime::charcs> defaultCharacter;
    };

    /**
     * @brief Imports self-describing CNJ documents into existing source-oriented pipeline types.
     *
     * Every CNJ type supported by the existing compiler is routed to a source-oriented value.
     */
    class CnjImporter final : public ContentImporter
    {
    public:
        /**
         * @brief Returns the stable built-in importer identity.
         * @return `CNA.CnjImporter/1`.
         */
        [[nodiscard]] ContentComponentIdentity Identity() const override;

        /**
         * @brief Returns the `.cnj` source route.
         * @return A vector containing `.cnj`.
         */
        [[nodiscard]] std::vector<std::string> SourceExtensions() const override;

        /**
         * @brief Returns the bounded intermediate types selected by supported CNJ envelopes.
         * @return Stable imported-type identities in deterministic order.
         */
        [[nodiscard]] std::vector<std::string> OutputTypes() const override;

        /**
         * @brief Validates the CNJ envelope and imports through the selected shared source path.
         *
         * @param context Call-scoped importer context.
         * @return A bounded imported value selected by the validated CNJ envelope type.
         */
        [[nodiscard]] ContentValue Import(ContentImporterContext& context) const override;
    };

    /** @brief Converts imported raw volume pixels into canonical Texture3D CNB data. */
    class Texture3DProcessor final : public ContentProcessor
    {
    public:
        /**
         * @brief Returns the stable built-in processor identity.
         * @return `CNA.Texture3DProcessor/1`.
         */
        [[nodiscard]] ContentComponentIdentity Identity() const override;
        /**
         * @brief Returns the accepted imported type.
         * @return ImportedTexture3DType.
         */
        [[nodiscard]] std::string InputType() const override;
        /**
         * @brief Returns the produced type.
         * @return ProcessedTexture3DType.
         */
        [[nodiscard]] std::string OutputType() const override;
        /**
         * @brief Rejects parameters because Texture3D policy is currently fixed.
         * @param parameters Parameters to validate.
         */
        void ValidateParameters(const ContentProcessorParameters& parameters) const override;
        /**
         * @brief Constructs one canonical Rgba8 Texture3D representation.
         * @param input ImportedTexture3D value.
         * @param context Call-scoped processor context.
         * @return Canonical CnbTextureData boxed as ProcessedTexture3DType.
         */
        [[nodiscard]] ContentValue Process(const ContentValue& input,
                                           ContentProcessorContext& context) const override;
    };

    /** @brief Pipeline writer adapter over the authoritative Texture3D CNB codec. */
    class Texture3DContentWriter final : public ContentTypeWriter
    {
    public:
        /**
         * @brief Returns the stable built-in writer identity.
         * @return `CNA.Texture3DContentWriter/1`.
         */
        [[nodiscard]] ContentComponentIdentity Identity() const override;
        /**
         * @brief Returns the frozen Texture3D schema and encoder identity.
         * @return One stable Texture3D asset/schema/codec declaration.
         */
        [[nodiscard]] std::vector<ContentWriterSchemaIdentity>
        OutputSchemaIdentities() const override;
        /**
         * @brief Returns the accepted processed type.
         * @return ProcessedTexture3DType.
         */
        [[nodiscard]] std::string InputType() const override;
        /**
         * @brief Calls the existing EncodeTexture3DToCnb() implementation.
         * @param input Canonical Texture3D data.
         * @param logicalName Logical asset name written to CNB metadata.
         * @return Complete CNB bytes and frozen Texture3D identity.
         */
        [[nodiscard]] ContentWriteResult Write(const ContentValue& input,
                                               const std::string& logicalName) const override;
    };

    /** @brief Applies the current identity policy to decoded DDS cube-map source data. */
    class TextureCubeProcessor final : public ContentProcessor
    {
    public:
        /**
         * @brief Returns the stable built-in processor identity.
         * @return `CNA.TextureCubeProcessor/1`.
         */
        [[nodiscard]] ContentComponentIdentity Identity() const override;
        /**
         * @brief Returns the accepted imported type.
         * @return ImportedTextureCubeType.
         */
        [[nodiscard]] std::string InputType() const override;
        /**
         * @brief Returns the produced type.
         * @return ProcessedTextureCubeType.
         */
        [[nodiscard]] std::string OutputType() const override;
        /**
         * @brief Rejects parameters because TextureCube policy is currently fixed.
         * @param parameters Parameters to validate.
         */
        void ValidateParameters(const ContentProcessorParameters& parameters) const override;
        /**
         * @brief Preserves the decoded DDS representations as canonical cube content.
         * @param input ImportedTextureCube value.
         * @param context Call-scoped processor context.
         * @return Canonical CnbTextureData boxed as ProcessedTextureCubeType.
         */
        [[nodiscard]] ContentValue Process(const ContentValue& input,
                                           ContentProcessorContext& context) const override;
    };

    /** @brief Pipeline writer adapter over the authoritative TextureCube CNB codec. */
    class TextureCubeContentWriter final : public ContentTypeWriter
    {
    public:
        /**
         * @brief Returns the stable built-in writer identity.
         * @return `CNA.TextureCubeContentWriter/1`.
         */
        [[nodiscard]] ContentComponentIdentity Identity() const override;
        /**
         * @brief Returns the frozen TextureCube schema and encoder identity.
         * @return One stable TextureCube asset/schema/codec declaration.
         */
        [[nodiscard]] std::vector<ContentWriterSchemaIdentity>
        OutputSchemaIdentities() const override;
        /**
         * @brief Returns the accepted processed type.
         * @return ProcessedTextureCubeType.
         */
        [[nodiscard]] std::string InputType() const override;
        /**
         * @brief Calls the existing EncodeTextureCubeToCnb() implementation.
         * @param input Canonical TextureCube data.
         * @param logicalName Logical asset name written to CNB metadata.
         * @return Complete CNB bytes and frozen TextureCube identity.
         */
        [[nodiscard]] ContentWriteResult Write(const ContentValue& input,
                                               const std::string& logicalName) const override;
    };

    /** @brief Makes source Curve semantics an explicit processing stage. */
    class CurveProcessor final : public ContentProcessor
    {
    public:
        /**
         * @brief Returns the stable built-in processor identity.
         * @return `CNA.CurveProcessor/1`.
         */
        [[nodiscard]] ContentComponentIdentity Identity() const override;
        /**
         * @brief Returns the accepted imported type.
         * @return ImportedCurveType.
         */
        [[nodiscard]] std::string InputType() const override;
        /**
         * @brief Returns the produced type.
         * @return ProcessedCurveType.
         */
        [[nodiscard]] std::string OutputType() const override;
        /**
         * @brief Rejects parameters because Curve policy currently preserves source semantics.
         * @param parameters Parameters to validate.
         */
        void ValidateParameters(const ContentProcessorParameters& parameters) const override;
        /**
         * @brief Produces a runtime-oriented Curve without changing authored values.
         * @param input ImportedCurve value.
         * @param context Call-scoped processor context.
         * @return ProcessedCurve value.
         */
        [[nodiscard]] ContentValue Process(const ContentValue& input,
                                           ContentProcessorContext& context) const override;
    };

    /** @brief Pipeline writer adapter over the authoritative Curve CNB codec. */
    class CurveContentWriter final : public ContentTypeWriter
    {
    public:
        /**
         * @brief Returns the stable built-in writer identity.
         * @return `CNA.CurveContentWriter/1`.
         */
        [[nodiscard]] ContentComponentIdentity Identity() const override;
        /**
         * @brief Returns the frozen Curve schema and encoder identity.
         * @return One stable Curve asset/schema/codec declaration.
         */
        [[nodiscard]] std::vector<ContentWriterSchemaIdentity>
        OutputSchemaIdentities() const override;
        /**
         * @brief Returns the accepted processed type.
         * @return ProcessedCurveType.
         */
        [[nodiscard]] std::string InputType() const override;
        /**
         * @brief Calls the existing EncodeCurveToCnb() implementation.
         * @param input Processed Curve value.
         * @param logicalName Logical asset name written to CNB metadata.
         * @return Complete CNB bytes and frozen Curve identity.
         */
        [[nodiscard]] ContentWriteResult Write(const ContentValue& input,
                                               const std::string& logicalName) const override;
    };

    /** @brief Makes source AnimationClip semantics an explicit processing stage. */
    class AnimationClipProcessor final : public ContentProcessor
    {
    public:
        /**
         * @brief Returns the stable built-in processor identity.
         * @return `CNA.AnimationClipProcessor/1`.
         */
        [[nodiscard]] ContentComponentIdentity Identity() const override;
        /**
         * @brief Returns the accepted imported type.
         * @return ImportedAnimationClipType.
         */
        [[nodiscard]] std::string InputType() const override;
        /**
         * @brief Returns the produced type.
         * @return ProcessedAnimationClipType.
         */
        [[nodiscard]] std::string OutputType() const override;
        /**
         * @brief Rejects parameters because AnimationClip policy preserves source semantics.
         * @param parameters Parameters to validate.
         */
        void ValidateParameters(const ContentProcessorParameters& parameters) const override;
        /**
         * @brief Produces a runtime-oriented clip without changing authored values.
         * @param input ImportedAnimationClip value.
         * @param context Call-scoped processor context.
         * @return ProcessedAnimationClip value.
         */
        [[nodiscard]] ContentValue Process(const ContentValue& input,
                                           ContentProcessorContext& context) const override;
    };

    /** @brief Pipeline writer adapter over the authoritative AnimationClip CNB codec. */
    class AnimationClipContentWriter final : public ContentTypeWriter
    {
    public:
        /**
         * @brief Returns the stable built-in writer identity.
         * @return `CNA.AnimationClipContentWriter/1`.
         */
        [[nodiscard]] ContentComponentIdentity Identity() const override;
        /**
         * @brief Returns the frozen AnimationClip schema and encoder identity.
         * @return One stable AnimationClip asset/schema/codec declaration.
         */
        [[nodiscard]] std::vector<ContentWriterSchemaIdentity>
        OutputSchemaIdentities() const override;
        /**
         * @brief Returns the accepted processed type.
         * @return ProcessedAnimationClipType.
         */
        [[nodiscard]] std::string InputType() const override;
        /**
         * @brief Calls the existing EncodeAnimationClipToCnb() implementation.
         * @param input Processed AnimationClip value.
         * @param logicalName Logical asset name written to CNB metadata.
         * @return Complete CNB bytes and frozen AnimationClip identity.
         */
        [[nodiscard]] ContentWriteResult Write(const ContentValue& input,
                                               const std::string& logicalName) const override;
    };

    /** @brief Converts imported SpriteFont semantics into canonical CnbSpriteFontData. */
    class SpriteFontProcessor final : public ContentProcessor
    {
    public:
        /**
         * @brief Returns the stable built-in processor identity.
         * @return `CNA.SpriteFontProcessor/1`.
         */
        [[nodiscard]] ContentComponentIdentity Identity() const override;

        /**
         * @brief Returns the accepted imported type.
         * @return ImportedSpriteFontType.
         */
        [[nodiscard]] std::string InputType() const override;

        /**
         * @brief Returns the produced type.
         * @return ProcessedSpriteFontType.
         */
        [[nodiscard]] std::string OutputType() const override;

        /**
         * @brief Rejects every parameter because the initial font policy preserves CNJ semantics.
         *
         * @param parameters Parameters to validate.
         */
        void ValidateParameters(const ContentProcessorParameters& parameters) const override;

        /**
         * @brief Builds canonical SpriteFont data with an embedded Rgba8 atlas.
         *
         * @param input ImportedSpriteFont value.
         * @param context Call-scoped processor context.
         * @return Canonical CnbSpriteFontData boxed as ProcessedSpriteFontType.
         */
        [[nodiscard]] ContentValue Process(const ContentValue& input,
                                           ContentProcessorContext& context) const override;
    };

    /** @brief Pipeline writer adapter over the authoritative SpriteFont CNB codec. */
    class SpriteFontContentWriter final : public ContentTypeWriter
    {
    public:
        /**
         * @brief Returns the stable built-in writer identity.
         * @return `CNA.SpriteFontContentWriter/1`.
         */
        [[nodiscard]] ContentComponentIdentity Identity() const override;

        /**
         * @brief Returns the frozen SpriteFont schema and encoder identity.
         * @return One stable SpriteFont asset/schema/codec declaration.
         */
        [[nodiscard]] std::vector<ContentWriterSchemaIdentity>
        OutputSchemaIdentities() const override;

        /**
         * @brief Returns the accepted processed type.
         * @return ProcessedSpriteFontType.
         */
        [[nodiscard]] std::string InputType() const override;

        /**
         * @brief Calls the existing EncodeSpriteFontToCnb() implementation.
         *
         * @param input Canonical CnbSpriteFontData value.
         * @param logicalName Logical asset name written to CNB metadata.
         * @return Complete CNB bytes and the frozen SpriteFont asset identity.
         */
        [[nodiscard]] ContentWriteResult Write(const ContentValue& input,
                                               const std::string& logicalName) const override;
    };

    /**
     * @brief Registers the CNJ importer and CNJ-specific SpriteFont processor/writer.
     *
     * Texture2D, SoundEffect and Model CNJ documents intentionally converge on their already
     * registered processors and writers, so callers must register those built-in slices too.
     *
     * @param registry Explicit registry to configure before builds begin.
     */
    void RegisterCnjContentPipeline(ContentPipelineRegistry& registry);
}
