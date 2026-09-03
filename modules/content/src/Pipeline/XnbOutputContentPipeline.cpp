// SPDX-License-Identifier: MS-PL
#include "CNA/Content/Pipeline/XnbOutputContentPipeline.hpp"

#include <memory>
#include <utility>
#include <variant>

#include "CNA/Content/Cnb/CnbMediaCodec.hpp"
#include "CNA/Content/Cnb/CnbSoundEffectCodec.hpp"
#include "CNA/Content/Cnb/CnbSpriteFontCodec.hpp"
#include "CNA/Content/Cnb/CnbTextureCodec.hpp"
#include "CNA/Content/Cnb/CnbModelV2Data.hpp"
#include "CNA/Content/Cnb/CnbTextureFormat.hpp"
#include "CNA/Content/Pipeline/CnjContentPipeline.hpp"
#include "CNA/Content/Pipeline/ModelContentPipeline.hpp"
#include "CNA/Content/Pipeline/SongContentPipeline.hpp"
#include "CNA/Content/Pipeline/SoundEffectContentPipeline.hpp"
#include "CNA/Content/Pipeline/Texture2DContentPipeline.hpp"
#include "CNA/Content/Pipeline/VideoContentPipeline.hpp"
#include "CNA/Internal/Xnb/XnbAssetTypeWriters.hpp"
#include "CNA/Internal/Xnb/XnbAssetWriter.hpp"

namespace CNA::Content::Pipeline
{
    namespace Xnb = CNA::Internal::Xnb;

    using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
    using Xnb::XnbWriteException;

    namespace
    {
        /** @brief The stable codec identity every XNB writer shares. */
        constexpr const char* kXnbCodecName = "CNA.XnbSerializer";

        /** @brief Bumped whenever the serializer's byte output changes for unchanged inputs. */
        constexpr const char* kXnbCodecVersion = "1";

        /**
         * @brief Returns the XNA runtime type name recorded for one emitted root.
         *
         * @param asset The emitted root identifier.
         * @return A process-lifetime string literal.
         */
        [[nodiscard]] const char* XnbOutputAssetTypeName(const XnbOutputAssetId asset) noexcept
        {
            switch (asset)
            {
                case XnbOutputAssetId::Texture2D:
                    return "Microsoft.Xna.Framework.Graphics.Texture2D";
                case XnbOutputAssetId::Texture3D:
                    return "Microsoft.Xna.Framework.Graphics.Texture3D";
                case XnbOutputAssetId::TextureCube:
                    return "Microsoft.Xna.Framework.Graphics.TextureCube";
                case XnbOutputAssetId::SpriteFont:
                    return "Microsoft.Xna.Framework.Graphics.SpriteFont";
                case XnbOutputAssetId::SoundEffect:
                    return "Microsoft.Xna.Framework.Audio.SoundEffect";
                case XnbOutputAssetId::Song: return "Microsoft.Xna.Framework.Media.Song";
                case XnbOutputAssetId::Video: return "Microsoft.Xna.Framework.Media.Video";
                case XnbOutputAssetId::Model: return "Microsoft.Xna.Framework.Graphics.Model";
                case XnbOutputAssetId::Curve: return "Microsoft.Xna.Framework.Curve";
            }
            return "";
        }

        /**
         * @brief Base of every XNB pipeline writer: identity, format and schema declaration.
         *
         * The container options are held here rather than read from the build request because
         * OutputSchemaIdentities() and Identity() are consulted before any content is written, and
         * the incremental manifest records both. Binding the options to the writer instance is
         * what makes a change of target platform or container version invalidate stale artifacts.
         */
        class XnbPipelineWriter : public ContentTypeWriter
        {
        public:
            XnbPipelineWriter(std::string name, XnbOutputAssetId asset,
                              Xnb::XnbFileOptions options)
                : name_(std::move(name)), asset_(asset), options_(std::move(options))
            {
            }

            [[nodiscard]] ContentComponentIdentity Identity() const final
            {
                return {name_, std::string(kXnbCodecVersion) + "+" +
                                   XnbOutputOptionsDigest(options_)};
            }

            [[nodiscard]] ContentOutputFormat OutputFormat() const final
            {
                return ContentOutputFormat::Xnb;
            }

            [[nodiscard]] std::vector<ContentWriterSchemaIdentity>
            OutputSchemaIdentities() const override
            {
                ContentWriterSchemaIdentity schema;
                schema.assetTypeId = static_cast<std::uint32_t>(asset_);
                schema.assetSchemaVersion =
                    Xnb::XnbContainerVersionByte(options_.version);
                schema.assetTypeName = XnbOutputAssetTypeName(asset_);
                schema.codec = {kXnbCodecName,
                                std::string(kXnbCodecVersion) + "+" +
                                    XnbOutputOptionsDigest(options_)};
                return {schema};
            }

        protected:
            /** @brief Returns the container options every write of this writer uses. */
            [[nodiscard]] const Xnb::XnbFileOptions& Options() const noexcept { return options_; }

            /**
             * @brief Fills in the primary output identity shared by every XNB writer.
             *
             * @param bytes The complete file image.
             * @return A populated primary write result.
             */
            [[nodiscard]] ContentWriteResult MakeResult(std::vector<std::uint8_t> bytes) const
            {
                ContentWriteResult result;
                result.bytes = std::move(bytes);
                result.assetTypeId = static_cast<std::uint32_t>(asset_);
                result.assetSchemaVersion = Xnb::XnbContainerVersionByte(options_.version);
                result.assetTypeName = XnbOutputAssetTypeName(asset_);
                return result;
            }

        private:
            std::string name_;
            XnbOutputAssetId asset_ = XnbOutputAssetId::Texture2D;
            Xnb::XnbFileOptions options_;
        };

        /** @brief Writes a Texture2D, Texture3D or TextureCube from canonical CNB texture data. */
        class XnbTextureWriter final : public XnbPipelineWriter
        {
        public:
            XnbTextureWriter(std::string name, const XnbOutputAssetId asset,
                             std::string inputType, const Xnb::XnbTextureKind kind,
                             Xnb::XnbFileOptions options)
                : XnbPipelineWriter(std::move(name), asset, std::move(options))
                , inputType_(std::move(inputType))
                , kind_(kind)
            {
            }

            [[nodiscard]] std::string InputType() const override { return inputType_; }

            [[nodiscard]] ContentWriteResult Write(
                const ContentValue& input, const std::string& logicalName) const override
            {
                const Cnb::CnbTextureData& texture = input.Get<Cnb::CnbTextureData>();
                const Xnb::XnbTextureData converted =
                    ConvertCnbTextureToXnb(texture, kind_, Options(), logicalName);
                switch (kind_)
                {
                    case Xnb::XnbTextureKind::Texture3D:
                        return MakeResult(Xnb::WriteXnbAsset(
                            Xnb::XnbTexture3DContent{converted}, Options(), logicalName));
                    case Xnb::XnbTextureKind::TextureCube:
                        return MakeResult(Xnb::WriteXnbAsset(
                            Xnb::XnbTextureCubeContent{converted}, Options(), logicalName));
                    case Xnb::XnbTextureKind::Texture2D:
                    default:
                        return MakeResult(Xnb::WriteXnbAsset(
                            Xnb::XnbTexture2DContent{converted}, Options(), logicalName));
                }
            }

        private:
            std::string inputType_;
            Xnb::XnbTextureKind kind_ = Xnb::XnbTextureKind::Texture2D;
        };

        /** @brief Writes a SpriteFont, including its embedded glyph atlas. */
        class XnbSpriteFontWriter final : public XnbPipelineWriter
        {
        public:
            explicit XnbSpriteFontWriter(Xnb::XnbFileOptions options)
                : XnbPipelineWriter("CNA.XnbSpriteFontWriter", XnbOutputAssetId::SpriteFont,
                                    std::move(options))
            {
            }

            [[nodiscard]] std::string InputType() const override
            {
                return ProcessedSpriteFontType;
            }

            [[nodiscard]] ContentWriteResult Write(
                const ContentValue& input, const std::string& logicalName) const override
            {
                const Cnb::CnbSpriteFontData& font = input.Get<Cnb::CnbSpriteFontData>();
                Xnb::XnbSpriteFontData converted;
                converted.atlas = ConvertCnbTextureToXnb(
                    font.atlas, Xnb::XnbTextureKind::Texture2D, Options(),
                    logicalName + " (glyph atlas)");
                converted.glyphs = font.glyphBounds;
                converted.cropping = font.cropping;
                converted.characters = font.characters;
                converted.lineSpacing = font.lineSpacing;
                converted.spacing = font.spacing;
                converted.kerning = font.kerning;
                converted.defaultCharacter = font.defaultCharacter;
                return MakeResult(Xnb::WriteXnbAsset(converted, Options(), logicalName));
            }
        };

        /** @brief Writes a SoundEffect from canonical CNB PCM data. */
        class XnbSoundEffectWriter final : public XnbPipelineWriter
        {
        public:
            explicit XnbSoundEffectWriter(Xnb::XnbFileOptions options)
                : XnbPipelineWriter("CNA.XnbSoundEffectWriter", XnbOutputAssetId::SoundEffect,
                                    std::move(options))
            {
            }

            [[nodiscard]] std::string InputType() const override
            {
                return ProcessedSoundEffectType;
            }

            [[nodiscard]] ContentWriteResult Write(
                const ContentValue& input, const std::string& logicalName) const override
            {
                const Cnb::CnbSoundEffectData& sound = input.Get<Cnb::CnbSoundEffectData>();
                if (sound.format != Cnb::CnbAudioFormat::Pcm16)
                {
                    throw XnbWriteException(
                        "'" + logicalName + "': the XNB SoundEffect writer emits 16-bit PCM "
                        "only, but the processed audio is '" +
                        Cnb::CnbAudioFormatToString(sound.format) +
                        "'. Configure the SoundEffect processor to produce PCM16.");
                }
                if (sound.channels == 0u || sound.channels > 2u)
                {
                    throw XnbWriteException(
                        "'" + logicalName + "': XNA's SoundEffect supports mono and stereo only, "
                        "but the processed audio declares " + std::to_string(sound.channels) +
                        " channels.");
                }

                Xnb::XnbSoundEffectData converted;
                converted.formatTag = 1u;
                converted.channels = static_cast<std::uint16_t>(sound.channels);
                converted.sampleRate = sound.sampleRate;
                converted.bitsPerSample = 16u;
                converted.blockAlign = static_cast<std::uint16_t>(2u * sound.channels);
                converted.averageBytesPerSecond = sound.sampleRate * converted.blockAlign;
                converted.samples = sound.samples;
                converted.loopStart = static_cast<std::int32_t>(sound.loopStart);
                converted.loopLength = static_cast<std::int32_t>(sound.loopLength);
                converted.storedDurationMs =
                    sound.sampleRate == 0u
                        ? 0u
                        : static_cast<std::uint32_t>(
                              (static_cast<std::uint64_t>(sound.frameCount) * 1000ull) /
                              sound.sampleRate);
                return MakeResult(Xnb::WriteXnbAsset(converted, Options(), logicalName));
            }
        };

        /** @brief Writes Song metadata naming its external streaming media. */
        class XnbSongWriter final : public XnbPipelineWriter
        {
        public:
            explicit XnbSongWriter(Xnb::XnbFileOptions options)
                : XnbPipelineWriter("CNA.XnbSongWriter", XnbOutputAssetId::Song,
                                    std::move(options))
            {
            }

            [[nodiscard]] std::string InputType() const override { return ProcessedSongType; }

            [[nodiscard]] ContentWriteResult Write(
                const ContentValue& input, const std::string& logicalName) const override
            {
                const Cnb::CnbSongData& song = input.Get<Cnb::CnbSongData>();
                Xnb::XnbSongData converted;
                converted.mediaPath = song.streamReference;
                converted.durationMs = static_cast<std::int32_t>(song.durationMs);
                return MakeResult(Xnb::WriteXnbAsset(converted, Options(), logicalName));
            }
        };

        /** @brief Writes Video metadata naming its external streaming media. */
        class XnbVideoWriter final : public XnbPipelineWriter
        {
        public:
            explicit XnbVideoWriter(Xnb::XnbFileOptions options)
                : XnbPipelineWriter("CNA.XnbVideoWriter", XnbOutputAssetId::Video,
                                    std::move(options))
            {
            }

            [[nodiscard]] std::string InputType() const override { return ProcessedVideoType; }

            [[nodiscard]] ContentWriteResult Write(
                const ContentValue& input, const std::string& logicalName) const override
            {
                const Cnb::CnbVideoData& video = input.Get<Cnb::CnbVideoData>();
                Xnb::XnbVideoData converted;
                converted.mediaPath = video.streamReference;
                converted.durationMs = static_cast<std::int32_t>(video.durationMs);
                converted.width = static_cast<std::int32_t>(video.width);
                converted.height = static_cast<std::int32_t>(video.height);
                converted.framesPerSecond = video.framesPerSecond;
                converted.soundtrackType = static_cast<std::int32_t>(video.soundtrackType);
                return MakeResult(Xnb::WriteXnbAsset(converted, Options(), logicalName));
            }
        };

        /** @brief Writes a Curve. */
        class XnbCurveWriter final : public XnbPipelineWriter
        {
        public:
            explicit XnbCurveWriter(Xnb::XnbFileOptions options)
                : XnbPipelineWriter("CNA.XnbCurveWriter", XnbOutputAssetId::Curve,
                                    std::move(options))
            {
            }

            [[nodiscard]] std::string InputType() const override { return ProcessedCurveType; }

            [[nodiscard]] ContentWriteResult Write(
                const ContentValue& input, const std::string& logicalName) const override
            {
                const ProcessedCurve& curve = input.Get<ProcessedCurve>();
                return MakeResult(Xnb::WriteXnbAsset(curve.value, Options(), logicalName));
            }
        };

        /** @brief Writes a Model from the exact schema-2 canonical representation. */
        class XnbModelWriter final : public XnbPipelineWriter
        {
        public:
            explicit XnbModelWriter(Xnb::XnbFileOptions options)
                : XnbPipelineWriter("CNA.XnbModelWriter", XnbOutputAssetId::Model,
                                    std::move(options))
            {
            }

            [[nodiscard]] std::string InputType() const override { return ProcessedModelType; }

            [[nodiscard]] ContentWriteResult Write(
                const ContentValue& input, const std::string& logicalName) const override
            {
                const ProcessedModelBundle& bundle = input.Get<ProcessedModelBundle>();
                if (!bundle.children.empty())
                {
                    throw XnbWriteException(
                        "'" + logicalName + "': the XNB Model writer cannot emit generated child "
                        "assets. Build with 'generateChildAssets' disabled, or build the child "
                        "assets as their own sources.");
                }
                const auto* schema2 = std::get_if<Cnb::CnbModelV2Data>(&bundle.primary);
                if (schema2 == nullptr)
                {
                    throw XnbWriteException(
                        "'" + logicalName +
                        "': this Model was processed into CNB Model schema 1, which stores a "
                        "vertex stride but no VertexDeclaration. An XNA Model requires one per "
                        "vertex buffer, so the asset cannot be emitted as XNB. See "
                        "plans/plan_xnapipeline.md XNAP-56/XNAP-57 for the canonical model "
                        "representation this needs.");
                }
                return MakeResult(Xnb::WriteXnbAsset(
                    ConvertModelV2ToXnb(*schema2, logicalName), Options(), logicalName));
            }

        private:
            [[nodiscard]] static Xnb::XnbVertexDeclarationData ConvertDeclaration(
                const Cnb::CnbModelV2VertexDeclaration& declaration);

            [[nodiscard]] static Xnb::XnbModelData ConvertModelV2ToXnb(
                const Cnb::CnbModelV2Data& model, const std::string& logicalName);
        };
    }


    namespace
    {
        /**
         * @brief Maps a schema-2 vertex-element format onto its XNA `VertexElementFormat`.
         *
         * @param format The schema-2 format.
         * @return The runtime element format.
         * @throws XnbWriteException for a format XNA has no element for.
         */
        [[nodiscard]] Microsoft::Xna::Framework::Graphics::VertexElementFormat
        ConvertVertexFormat(const Cnb::CnbModelV2VertexFormat format)
        {
            using Microsoft::Xna::Framework::Graphics::VertexElementFormat;
            switch (format)
            {
                case Cnb::CnbModelV2VertexFormat::Single: return VertexElementFormat::Single;
                case Cnb::CnbModelV2VertexFormat::Vector2: return VertexElementFormat::Vector2;
                case Cnb::CnbModelV2VertexFormat::Vector3: return VertexElementFormat::Vector3;
                case Cnb::CnbModelV2VertexFormat::Vector4: return VertexElementFormat::Vector4;
                case Cnb::CnbModelV2VertexFormat::Color: return VertexElementFormat::Color;
                case Cnb::CnbModelV2VertexFormat::Byte4: return VertexElementFormat::Byte4;
                case Cnb::CnbModelV2VertexFormat::Short2: return VertexElementFormat::Short2;
                case Cnb::CnbModelV2VertexFormat::Short4: return VertexElementFormat::Short4;
                case Cnb::CnbModelV2VertexFormat::NormalizedShort2:
                    return VertexElementFormat::NormalizedShort2;
                case Cnb::CnbModelV2VertexFormat::NormalizedShort4:
                    return VertexElementFormat::NormalizedShort4;
                case Cnb::CnbModelV2VertexFormat::HalfVector2:
                    return VertexElementFormat::HalfVector2;
                case Cnb::CnbModelV2VertexFormat::HalfVector4:
                    return VertexElementFormat::HalfVector4;
            }
            throw XnbWriteException(
                "XNB Model: vertex element format " +
                std::to_string(static_cast<std::uint32_t>(format)) +
                " has no XNA VertexElementFormat.");
        }

        /**
         * @brief Maps a schema-2 vertex-element usage onto its XNA `VertexElementUsage`.
         *
         * @param usage The schema-2 usage.
         * @return The runtime element usage.
         * @throws XnbWriteException for a usage XNA has no element for.
         */
        [[nodiscard]] Microsoft::Xna::Framework::Graphics::VertexElementUsage
        ConvertVertexUsage(const Cnb::CnbModelV2VertexUsage usage)
        {
            using Microsoft::Xna::Framework::Graphics::VertexElementUsage;
            switch (usage)
            {
                case Cnb::CnbModelV2VertexUsage::Position: return VertexElementUsage::Position;
                case Cnb::CnbModelV2VertexUsage::Color: return VertexElementUsage::Color;
                case Cnb::CnbModelV2VertexUsage::TextureCoordinate:
                    return VertexElementUsage::TextureCoordinate;
                case Cnb::CnbModelV2VertexUsage::Normal: return VertexElementUsage::Normal;
                case Cnb::CnbModelV2VertexUsage::Binormal: return VertexElementUsage::Binormal;
                case Cnb::CnbModelV2VertexUsage::Tangent: return VertexElementUsage::Tangent;
                case Cnb::CnbModelV2VertexUsage::BlendIndices:
                    return VertexElementUsage::BlendIndices;
                case Cnb::CnbModelV2VertexUsage::BlendWeight:
                    return VertexElementUsage::BlendWeight;
                case Cnb::CnbModelV2VertexUsage::Depth: return VertexElementUsage::Depth;
                case Cnb::CnbModelV2VertexUsage::Fog: return VertexElementUsage::Fog;
                case Cnb::CnbModelV2VertexUsage::PointSize: return VertexElementUsage::PointSize;
                case Cnb::CnbModelV2VertexUsage::Sample: return VertexElementUsage::Sample;
                case Cnb::CnbModelV2VertexUsage::TessellateFactor:
                    return VertexElementUsage::TessellateFactor;
            }
            throw XnbWriteException(
                "XNB Model: vertex element usage " +
                std::to_string(static_cast<std::uint32_t>(usage)) +
                " has no XNA VertexElementUsage.");
        }

        /**
         * @brief Rebuilds an XNA row-major matrix from a schema-2 sixteen-float transform.
         *
         * @param values The transform in M11..M44 order.
         * @return The equivalent runtime matrix.
         */
        [[nodiscard]] Microsoft::Xna::Framework::Matrix ConvertTransform(
            const std::array<float, 16>& values)
        {
            return Microsoft::Xna::Framework::Matrix(
                values[0], values[1], values[2], values[3],
                values[4], values[5], values[6], values[7],
                values[8], values[9], values[10], values[11],
                values[12], values[13], values[14], values[15]);
        }

        /**
         * @brief Converts one schema-2 stock effect into its canonical XNB shared resource.
         *
         * @param effect The schema-2 effect resource.
         * @param logicalName Logical asset name used in the failure message.
         * @return The shared resource ready for serialization.
         * @throws XnbWriteException for an effect kind with no XNB writer.
         */
        [[nodiscard]] Xnb::XnbModelSharedResourceData ConvertEffect(
            const Cnb::CnbModelV2Effect& effect, const std::string& logicalName)
        {
            const auto vector = [](const std::array<float, 3>& values)
            {
                return Microsoft::Xna::Framework::Vector3(values[0], values[1], values[2]);
            };

            Xnb::XnbModelSharedResourceData resource;
            switch (effect.kind)
            {
                case Cnb::CnbModelV2EffectKind::BasicEffect:
                {
                    Xnb::XnbBasicEffectData value;
                    value.textureReference = effect.primaryTexture;
                    value.diffuseColor = vector(effect.diffuse);
                    value.emissiveColor = vector(effect.emissive);
                    value.specularColor = vector(effect.specular);
                    value.specularPower = effect.specularPower;
                    value.alpha = effect.alpha;
                    value.vertexColorEnabled = effect.vertexColorEnabled;
                    resource.reader = "Microsoft.Xna.Framework.Content.BasicEffectReader";
                    resource.value = value;
                    return resource;
                }
                case Cnb::CnbModelV2EffectKind::SkinnedEffect:
                {
                    Xnb::XnbSkinnedEffectData value;
                    value.textureReference = effect.primaryTexture;
                    value.weightsPerVertex = static_cast<std::int32_t>(effect.weightsPerVertex);
                    value.diffuseColor = vector(effect.diffuse);
                    value.emissiveColor = vector(effect.emissive);
                    value.specularColor = vector(effect.specular);
                    value.specularPower = effect.specularPower;
                    value.alpha = effect.alpha;
                    resource.reader = "Microsoft.Xna.Framework.Content.SkinnedEffectReader";
                    resource.value = value;
                    return resource;
                }
                case Cnb::CnbModelV2EffectKind::DualTextureEffect:
                {
                    Xnb::XnbDualTextureEffectData value;
                    value.textureReference = effect.primaryTexture;
                    value.texture2Reference = effect.secondaryTexture;
                    value.diffuseColor = vector(effect.diffuse);
                    value.alpha = effect.alpha;
                    value.vertexColorEnabled = effect.vertexColorEnabled;
                    resource.reader = "Microsoft.Xna.Framework.Content.DualTextureEffectReader";
                    resource.value = value;
                    return resource;
                }
                case Cnb::CnbModelV2EffectKind::AlphaTestEffect:
                {
                    Xnb::XnbAlphaTestEffectData value;
                    value.textureReference = effect.primaryTexture;
                    value.alphaFunction = static_cast<std::int32_t>(effect.alphaFunction);
                    value.referenceAlpha = effect.referenceAlpha;
                    value.diffuseColor = vector(effect.diffuse);
                    value.alpha = effect.alpha;
                    value.vertexColorEnabled = effect.vertexColorEnabled;
                    resource.reader = "Microsoft.Xna.Framework.Content.AlphaTestEffectReader";
                    resource.value = value;
                    return resource;
                }
                case Cnb::CnbModelV2EffectKind::EnvironmentMapEffect:
                {
                    Xnb::XnbEnvironmentMapEffectData value;
                    value.textureReference = effect.primaryTexture;
                    value.environmentMapReference = effect.cubeTexture;
                    value.environmentMapAmount = effect.environmentMapAmount;
                    value.environmentMapSpecular = vector(effect.specular);
                    value.fresnelFactor = effect.fresnelFactor;
                    value.diffuseColor = vector(effect.diffuse);
                    value.emissiveColor = vector(effect.emissive);
                    value.alpha = effect.alpha;
                    resource.reader = "Microsoft.Xna.Framework.Content.EnvironmentMapEffectReader";
                    resource.value = value;
                    return resource;
                }
            }
            throw XnbWriteException(
                "'" + logicalName + "': stock effect kind " +
                std::to_string(static_cast<std::uint32_t>(effect.kind)) +
                " has no XNB stock-effect writer.");
        }
    }

    std::string XnbOutputOptionsDigest(const Xnb::XnbFileOptions& options)
    {
        std::string digest;
        digest += Xnb::XnbPlatformByte(options.platform);
        digest += std::to_string(Xnb::XnbContainerVersionByte(options.version));
        digest += options.graphicsProfile == Xnb::XnbGraphicsProfile::HiDef ? 'h' : 'r';
        switch (options.compression)
        {
            case Xnb::XnbOutputCompression::Lzx: digest += 'x'; break;
            case Xnb::XnbOutputCompression::Lz4: digest += '4'; break;
            case Xnb::XnbOutputCompression::None: digest += 'u'; break;
        }
        digest += options.readerNameStyle == Xnb::XnbReaderNameStyle::Portable ? 'p' : 'n';
        return digest;
    }

    Xnb::XnbTextureData ConvertCnbTextureToXnb(
        const Cnb::CnbTextureData& texture, const Xnb::XnbTextureKind kind,
        const Xnb::XnbFileOptions& options, const std::string& diagnosticName)
    {
        const std::uint32_t expectedFaces =
            kind == Xnb::XnbTextureKind::TextureCube ? 6u : 1u;
        if (texture.faceCount != expectedFaces)
        {
            throw XnbWriteException(
                "'" + diagnosticName + "': the processed texture declares " +
                std::to_string(texture.faceCount) + " face(s), but this XNB texture shape needs " +
                std::to_string(expectedFaces) + ".");
        }
        if (kind != Xnb::XnbTextureKind::Texture3D && texture.depth != 1u)
        {
            throw XnbWriteException(
                "'" + diagnosticName + "': a two-dimensional or cube XNB texture cannot carry " +
                std::to_string(texture.depth) + " depth slices.");
        }
        if (kind == Xnb::XnbTextureKind::TextureCube && texture.width != texture.height)
        {
            throw XnbWriteException(
                "'" + diagnosticName + "': an XNB TextureCube needs square faces, but the "
                "processed texture is " + std::to_string(texture.width) + "x" +
                std::to_string(texture.height) + ".");
        }
        if (texture.representations.empty())
        {
            throw XnbWriteException(
                "'" + diagnosticName + "': the processed texture carries no pixel data.");
        }

        Xnb::XnbTextureData result;
        result.kind = kind;
        result.width = texture.width;
        result.height = texture.height;
        result.depth = texture.depth;
        result.faceCount = texture.faceCount;
        result.mipCount = texture.mipCount;
        result.platform = Xnb::XnbPlatformByte(options.platform);

        std::string rejected;
        for (const Cnb::CnbTextureRepresentation& representation : texture.representations)
        {
            SurfaceFormat surfaceFormat = SurfaceFormat::Color;
            try
            {
                surfaceFormat = Cnb::CnbTextureFormatToSurfaceFormat(representation.format);
            }
            catch (const std::exception&)
            {
                // A CNB format with no runtime SurfaceFormat at all cannot be a candidate; the
                // aggregate diagnostic below names it along with the expressible-but-rejected ones.
                if (!rejected.empty()) { rejected += ", "; }
                rejected += Cnb::CnbTextureFormatToString(representation.format);
                continue;
            }
            if (!Xnb::IsXnbWritableSurfaceFormat(surfaceFormat, options.version))
            {
                if (!rejected.empty()) { rejected += ", "; }
                rejected += Cnb::CnbTextureFormatToString(representation.format);
                continue;
            }
            result.surfaceFormat = surfaceFormat;
            result.levels = representation.levels;
            return result;
        }

        throw XnbWriteException(
            "'" + diagnosticName + "': none of the processed texture's representations (" +
            (rejected.empty() ? std::string("no XNA-compatible format") : rejected) +
            ") can be written into a container-version-" +
            std::to_string(Xnb::XnbContainerVersionByte(options.version)) +
            " XNB. Configure the texture processor to produce an XNA 4.0 surface format such as "
            "Color, Dxt1, Dxt3 or Dxt5.");
    }

    void RegisterXnbOutputContentPipeline(ContentPipelineRegistry& registry,
                                          const Xnb::XnbFileOptions& options)
    {
        Xnb::ValidateXnbFileOptions(options);
        registry.RegisterWriter(std::make_shared<const XnbTextureWriter>(
            "CNA.XnbTexture2DWriter", XnbOutputAssetId::Texture2D, ProcessedTexture2DType,
            Xnb::XnbTextureKind::Texture2D, options));
        registry.RegisterWriter(std::make_shared<const XnbTextureWriter>(
            "CNA.XnbTexture3DWriter", XnbOutputAssetId::Texture3D, ProcessedTexture3DType,
            Xnb::XnbTextureKind::Texture3D, options));
        registry.RegisterWriter(std::make_shared<const XnbTextureWriter>(
            "CNA.XnbTextureCubeWriter", XnbOutputAssetId::TextureCube, ProcessedTextureCubeType,
            Xnb::XnbTextureKind::TextureCube, options));
        registry.RegisterWriter(std::make_shared<const XnbSpriteFontWriter>(options));
        registry.RegisterWriter(std::make_shared<const XnbSoundEffectWriter>(options));
        registry.RegisterWriter(std::make_shared<const XnbSongWriter>(options));
        registry.RegisterWriter(std::make_shared<const XnbVideoWriter>(options));
        registry.RegisterWriter(std::make_shared<const XnbCurveWriter>(options));
        registry.RegisterWriter(std::make_shared<const XnbModelWriter>(options));
    }

    namespace
    {
        Xnb::XnbVertexDeclarationData XnbModelWriter::ConvertDeclaration(
            const Cnb::CnbModelV2VertexDeclaration& declaration)
        {
            Xnb::XnbVertexDeclarationData result;
            result.stride = static_cast<std::int32_t>(declaration.vertexStride);
            result.elements.reserve(declaration.elements.size());
            for (const Cnb::CnbModelV2VertexElement& element : declaration.elements)
            {
                result.elements.emplace_back(
                    static_cast<int>(element.offset), ConvertVertexFormat(element.format),
                    ConvertVertexUsage(element.usage), static_cast<int>(element.usageIndex));
            }
            return result;
        }

        Xnb::XnbModelData XnbModelWriter::ConvertModelV2ToXnb(
            const Cnb::CnbModelV2Data& model, const std::string& logicalName)
        {
            if (model.bones.empty())
            {
                throw XnbWriteException(
                    "'" + logicalName +
                    "': an XNA Model needs at least one bone, and this Model has none.");
            }
            if (model.rootBone >= model.bones.size())
            {
                throw XnbWriteException(
                    "'" + logicalName + "': the Model names bone " +
                    std::to_string(model.rootBone) + " as its root, outside its " +
                    std::to_string(model.bones.size()) + "-bone table.");
            }

            Xnb::XnbModelData result;
            result.bones.reserve(model.bones.size());
            for (const Cnb::CnbModelV2Bone& bone : model.bones)
            {
                Xnb::XnbModelBoneData converted;
                converted.name = bone.name;
                converted.transform = ConvertTransform(bone.transform);
                converted.parent = bone.parent;
                result.bones.push_back(std::move(converted));
            }
            // XNA serializes each bone's children explicitly; schema 2 stores only parents, so the
            // child lists are rebuilt here in bone order, which keeps the output deterministic.
            for (std::size_t index = 0u; index < model.bones.size(); ++index)
            {
                const std::int32_t parent = model.bones[index].parent;
                if (parent < 0) { continue; }
                if (static_cast<std::size_t>(parent) >= result.bones.size())
                {
                    throw XnbWriteException(
                        "'" + logicalName + "': bone " + std::to_string(index) +
                        " names parent " + std::to_string(parent) +
                        ", outside the bone table.");
                }
                result.bones[static_cast<std::size_t>(parent)].children.push_back(
                    static_cast<std::int32_t>(index));
            }
            result.rootBone = static_cast<std::int32_t>(model.rootBone);

            // Schema 2 stores vertex buffers, index buffers and effects as three separate tables;
            // XNB stores one flat shared-resource list, so the three are concatenated in that
            // order and each part's references are rebased onto the flat numbering.
            const std::size_t vertexBase = 0u;
            const std::size_t indexBase = model.vertexBuffers.size();
            const std::size_t effectBase = indexBase + model.indexBuffers.size();

            for (const Cnb::CnbModelV2VertexBuffer& buffer : model.vertexBuffers)
            {
                if (buffer.declaration >= model.vertexDeclarations.size())
                {
                    throw XnbWriteException(
                        "'" + logicalName + "': a vertex buffer names declaration " +
                        std::to_string(buffer.declaration) + ", outside the " +
                        std::to_string(model.vertexDeclarations.size()) +
                        "-entry declaration table.");
                }
                Xnb::XnbVertexBufferData converted;
                converted.declaration =
                    ConvertDeclaration(model.vertexDeclarations[buffer.declaration]);
                converted.vertexCount = buffer.vertexCount;
                converted.bytes = buffer.bytes;
                result.sharedResources.push_back(
                    {"Microsoft.Xna.Framework.Content.VertexBufferReader", std::move(converted)});
            }
            for (const Cnb::CnbModelV2IndexBuffer& buffer : model.indexBuffers)
            {
                Xnb::XnbIndexBufferData converted;
                converted.indexElementSize = buffer.indexElementSize;
                converted.bytes = buffer.bytes;
                result.sharedResources.push_back(
                    {"Microsoft.Xna.Framework.Content.IndexBufferReader", std::move(converted)});
            }
            for (const Cnb::CnbModelV2Effect& effect : model.effects)
            {
                result.sharedResources.push_back(ConvertEffect(effect, logicalName));
            }

            result.meshes.reserve(model.meshes.size());
            for (const Cnb::CnbModelV2Mesh& mesh : model.meshes)
            {
                Xnb::XnbModelMeshData converted;
                converted.name = mesh.name;
                converted.parentBone = mesh.parentBone;
                converted.boundingSphere = Microsoft::Xna::Framework::BoundingSphere(
                    Microsoft::Xna::Framework::Vector3(mesh.boundingSphere[0],
                                                       mesh.boundingSphere[1],
                                                       mesh.boundingSphere[2]),
                    mesh.boundingSphere[3]);
                converted.parts.reserve(mesh.parts.size());
                for (const Cnb::CnbModelV2Part& part : mesh.parts)
                {
                    if (part.vertexBuffer >= model.vertexBuffers.size() ||
                        part.indexBuffer >= model.indexBuffers.size() ||
                        part.effect >= model.effects.size())
                    {
                        throw XnbWriteException(
                            "'" + logicalName + "': mesh '" + mesh.name +
                            "' has a part referencing a vertex buffer, index buffer or effect "
                            "outside the Model's own resource tables.");
                    }
                    Xnb::XnbModelPartData convertedPart;
                    convertedPart.vertexOffset = static_cast<std::int32_t>(part.vertexOffset);
                    convertedPart.vertexCount = static_cast<std::int32_t>(part.numVertices);
                    convertedPart.startIndex = static_cast<std::int32_t>(part.startIndex);
                    convertedPart.primitiveCount = static_cast<std::int32_t>(part.primitiveCount);
                    convertedPart.vertexBufferResource =
                        static_cast<std::int32_t>(vertexBase + part.vertexBuffer);
                    convertedPart.indexBufferResource =
                        static_cast<std::int32_t>(indexBase + part.indexBuffer);
                    convertedPart.effectResource =
                        static_cast<std::int32_t>(effectBase + part.effect);
                    converted.parts.push_back(convertedPart);
                }
                result.meshes.push_back(std::move(converted));
            }
            return result;
        }
    }
}
