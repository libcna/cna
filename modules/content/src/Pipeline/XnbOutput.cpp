// SPDX-License-Identifier: MS-PL
#include "CNA/Content/Pipeline/XnbOutput.hpp"

#include <any>
#include <memory>
#include <stdexcept>
#include <utility>

#include "CNA/Content/Pipeline/CnjContentPipeline.hpp"
#include "CNA/Content/Pipeline/SongContentPipeline.hpp"
#include "CNA/Content/Pipeline/SoundEffectContentPipeline.hpp"
#include "CNA/Content/Pipeline/Texture2DContentPipeline.hpp"
#include "CNA/Content/Pipeline/VideoContentPipeline.hpp"
#include "CNA/Content/Xnb/XnbAssetTypeWriters.hpp"
#include "CNA/Content/Xnb/XnbBuiltInTypeWriters.hpp"
#include "CNA/Content/Xnb/XnbWriter.hpp"
#include "Microsoft/Xna/Framework/Curve.hpp"

namespace CNA::Content::Pipeline
{
    using Xnb::XnbFileOptions;
    using Xnb::XnbTextureAsset;
    using Xnb::XnbTypeWriterRegistry;
    using Xnb::XnbWriteException;

    namespace
    {
        /** @brief Stable build version for every writer this file registers. */
        constexpr const char* kXnbWriterVersion = "1";

        /**
         * @brief Serializes one canonical texture, closed over the XNA shape and processed type.
         *
         * The three texture shapes differ only in which processed pipeline type they accept and
         * which serialized XNA type they produce, so they share one adapter.
         */
        class TextureXnbAssetWriter final : public XnbAssetWriter
        {
        public:
            TextureXnbAssetWriter(std::string name, std::string inputType,
                                  const XnbTextureAsset::Shape shape, std::string readerName)
                : name_(std::move(name)), inputType_(std::move(inputType)), shape_(shape),
                  readerName_(std::move(readerName))
            {
            }

            [[nodiscard]] ContentComponentIdentity Identity() const override
            {
                return ContentComponentIdentity{name_, kXnbWriterVersion};
            }

            [[nodiscard]] std::string InputType() const override { return inputType_; }
            [[nodiscard]] std::string RootReaderName() const override { return readerName_; }

            [[nodiscard]] XnbWriteResult Write(const ContentValue& input,
                                                const XnbTypeWriterRegistry& registry,
                                                const XnbFileOptions& options,
                                                const std::string& logicalName) const override
            {
                (void)logicalName;
                XnbTextureAsset asset;
                asset.shape = shape_;
                asset.data = input.Get<Cnb::CnbTextureData>();
                asset.representation = 0u;

                XnbWriteResult result;
                result.options = options;
                result.rootReaderName = readerName_;
                result.bytes = Xnb::WriteXnbFile(registry, options,
                                                 Xnb::XnbTextureTypeName(shape_),
                                                 std::any(std::move(asset)));
                return result;
            }

        private:
            std::string name_;
            std::string inputType_;
            XnbTextureAsset::Shape shape_;
            std::string readerName_;
        };

        /** @brief Serializes one canonical value that maps directly onto a single XNB root type. */
        template <typename TValue, typename TAsset = TValue>
        class DirectXnbAssetWriter final : public XnbAssetWriter
        {
        public:
            using Adapt = TAsset (*)(const TValue&);

            DirectXnbAssetWriter(std::string name, std::string inputType, std::string targetType,
                                 std::string readerName, const Adapt adapt)
                : name_(std::move(name)), inputType_(std::move(inputType)),
                  targetType_(std::move(targetType)), readerName_(std::move(readerName)),
                  adapt_(adapt)
            {
            }

            [[nodiscard]] ContentComponentIdentity Identity() const override
            {
                return ContentComponentIdentity{name_, kXnbWriterVersion};
            }

            [[nodiscard]] std::string InputType() const override { return inputType_; }
            [[nodiscard]] std::string RootReaderName() const override { return readerName_; }

            [[nodiscard]] XnbWriteResult Write(const ContentValue& input,
                                                const XnbTypeWriterRegistry& registry,
                                                const XnbFileOptions& options,
                                                const std::string& logicalName) const override
            {
                (void)logicalName;
                XnbWriteResult result;
                result.options = options;
                result.rootReaderName = readerName_;
                result.bytes = Xnb::WriteXnbFile(
                    registry, options, targetType_,
                    std::any(adapt_(input.Get<TValue>())));
                return result;
            }

        private:
            std::string name_;
            std::string inputType_;
            std::string targetType_;
            std::string readerName_;
            Adapt adapt_ = nullptr;
        };

        [[nodiscard]] Xnb::XnbSpriteFontAsset AdaptSpriteFont(const Cnb::CnbSpriteFontData& font)
        {
            Xnb::XnbSpriteFontAsset asset;
            asset.data = font;
            asset.representation = 0u;
            return asset;
        }

        template <typename T>
        [[nodiscard]] T Identity(const T& value) { return value; }
    }

    const char* ContentOutputFormatName(const ContentOutputFormat format) noexcept
    {
        return format == ContentOutputFormat::Xnb ? "xnb" : "cnb";
    }

    const char* ContentOutputFormatExtension(const ContentOutputFormat format) noexcept
    {
        return format == ContentOutputFormat::Xnb ? ".xnb" : ".cnb";
    }

    ContentOutputFormat ParseContentOutputFormat(const std::string& name)
    {
        if (name == "cnb") { return ContentOutputFormat::Cnb; }
        if (name == "xnb") { return ContentOutputFormat::Xnb; }
        throw std::invalid_argument("'" + name +
                                    "' is not a content output format; expected cnb or xnb.");
    }

    std::shared_ptr<const XnbTypeWriterRegistry> CreateXnbTypeWriterRegistry()
    {
        auto registry = std::make_shared<XnbTypeWriterRegistry>();
        Xnb::RegisterBuiltInXnbTypeWriters(*registry);
        Xnb::RegisterXnbAssetTypeWriters(*registry);
        registry->Freeze();
        return registry;
    }

    void RegisterBuiltInXnbAssetWriters(ContentPipelineRegistry& registry)
    {
        registry.RegisterXnbWriter(std::make_shared<const TextureXnbAssetWriter>(
            "CNA.Xnb.Texture2DWriter", ProcessedTexture2DType,
            XnbTextureAsset::Shape::Texture2D,
            Xnb::XnbQualifiedReaderName("Microsoft.Xna.Framework.Content.Texture2DReader",
                                        Xnb::XnaGraphicsAssembly)));
        registry.RegisterXnbWriter(std::make_shared<const TextureXnbAssetWriter>(
            "CNA.Xnb.Texture3DWriter", ProcessedTexture3DType,
            XnbTextureAsset::Shape::Texture3D,
            Xnb::XnbQualifiedReaderName("Microsoft.Xna.Framework.Content.Texture3DReader",
                                        Xnb::XnaGraphicsAssembly)));
        registry.RegisterXnbWriter(std::make_shared<const TextureXnbAssetWriter>(
            "CNA.Xnb.TextureCubeWriter", ProcessedTextureCubeType,
            XnbTextureAsset::Shape::TextureCube,
            Xnb::XnbQualifiedReaderName("Microsoft.Xna.Framework.Content.TextureCubeReader",
                                        Xnb::XnaGraphicsAssembly)));

        registry.RegisterXnbWriter(
            std::make_shared<const DirectXnbAssetWriter<Cnb::CnbSpriteFontData,
                                                        Xnb::XnbSpriteFontAsset>>(
                "CNA.Xnb.SpriteFontWriter", ProcessedSpriteFontType,
                Xnb::XnbTypeKey<Xnb::XnbSpriteFontAsset>::Name(),
                Xnb::XnbQualifiedReaderName("Microsoft.Xna.Framework.Content.SpriteFontReader",
                                            Xnb::XnaGraphicsAssembly),
                &AdaptSpriteFont));

        registry.RegisterXnbWriter(
            std::make_shared<const DirectXnbAssetWriter<Cnb::CnbSoundEffectData>>(
                "CNA.Xnb.SoundEffectWriter", ProcessedSoundEffectType,
                Xnb::XnbTypeKey<Cnb::CnbSoundEffectData>::Name(),
                "Microsoft.Xna.Framework.Content.SoundEffectReader",
                &Identity<Cnb::CnbSoundEffectData>));

        registry.RegisterXnbWriter(
            std::make_shared<const DirectXnbAssetWriter<Microsoft::Xna::Framework::Curve>>(
                "CNA.Xnb.CurveWriter", ProcessedCurveType,
                Xnb::XnbTypeKey<Microsoft::Xna::Framework::Curve>::Name(),
                "Microsoft.Xna.Framework.Content.CurveReader",
                &Identity<Microsoft::Xna::Framework::Curve>));

        registry.RegisterXnbWriter(
            std::make_shared<const DirectXnbAssetWriter<Cnb::CnbSongData>>(
                "CNA.Xnb.SongWriter", ProcessedSongType,
                Xnb::XnbTypeKey<Cnb::CnbSongData>::Name(),
                "Microsoft.Xna.Framework.Content.SongReader", &Identity<Cnb::CnbSongData>));

        registry.RegisterXnbWriter(
            std::make_shared<const DirectXnbAssetWriter<Cnb::CnbVideoData>>(
                "CNA.Xnb.VideoWriter", ProcessedVideoType,
                Xnb::XnbTypeKey<Cnb::CnbVideoData>::Name(),
                "Microsoft.Xna.Framework.Content.VideoReader", &Identity<Cnb::CnbVideoData>));
    }
}
