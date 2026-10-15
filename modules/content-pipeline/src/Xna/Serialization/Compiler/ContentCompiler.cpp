// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Content/Pipeline/Serialization/Compiler/ContentCompiler.hpp"

#include <algorithm>

#include "CNA/Content/Pipeline/XnaModelBridge.hpp"
#include "CNA/Content/Cnb/CnbTextureFormat.hpp"
#include "CNA/Internal/PathUtf8.hpp"
#include "CNA/Internal/Xnb/XnbAssetTypeWriters.hpp"
#include "CNA/Internal/Xnb/XnbBuiltInWriters.hpp"
#include "Microsoft/Xna/Framework/BoundingBox.hpp"
#include "Microsoft/Xna/Framework/BoundingFrustum.hpp"
#include "Microsoft/Xna/Framework/BoundingSphere.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Graphics/BitmapContent.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Graphics/MipmapChain.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Graphics/TextureContent.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/PipelineException.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Processors/AudioProcessors.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Processors/CompiledEffectContent.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Processors/SpriteFontContent.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/VideoContent.hpp"
#include "Microsoft/Xna/Framework/Curve.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Plane.hpp"
#include "Microsoft/Xna/Framework/Point.hpp"
#include "Microsoft/Xna/Framework/Quaternion.hpp"
#include "Microsoft/Xna/Framework/Ray.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "System/DateTime.hpp"
#include "System/TimeSpan.hpp"
#if SHARP_RUNTIME_HAS_NATIVE_INT128
#include "System/Decimal.hpp"
#endif

namespace Microsoft::Xna::Framework::Content::Pipeline::Serialization::Compiler
{
    namespace
    {
        namespace Xnb = CNA::Internal::Xnb;
        namespace Processors = Microsoft::Xna::Framework::Content::Pipeline::Processors;

        /// The container's spelling of a target platform. `GetRuntimeReader` answers a per-platform
        /// name because one part of it is per-platform: the Compact Framework's `mscorlib` identity.
        [[nodiscard]] Xnb::XnbTargetPlatform ContainerPlatform(const TargetPlatform platform) noexcept
        {
            switch (platform)
            {
                case TargetPlatform::Xbox360: return Xnb::XnbTargetPlatform::Xbox360;
                case TargetPlatform::WindowsPhone: return Xnb::XnbTargetPlatform::WindowsPhone;
                case TargetPlatform::Windows: break;
            }
            return Xnb::XnbTargetPlatform::Windows;
        }

        /// The façade view of a built-in canonical type writer, so GetTypeWriter answers for
        /// primitives and framework value types as it does for user writers.
        template<typename T>
        class BuiltInTypeWriter final : public ContentTypeWriter<T>
        {
        public:
            explicit BuiltInTypeWriter(const Xnb::XnbTypeWriterBase& canonical) : canonical_(&canonical) {}

            [[nodiscard]] std::int32_t getTypeVersionProperty() const override
            {
                return canonical_->ReaderIdentity().readerVersion;
            }

            [[nodiscard]] std::string GetRuntimeReader(TargetPlatform targetPlatform) const override
            {
                return Xnb::FormatXnbReaderName(canonical_->ReaderIdentity(),
                                                Xnb::XnbReaderNameStyle::Xna40,
                                                ContainerPlatform(targetPlatform));
            }

        protected:
            void Write(ContentWriter& output, const Carrier<T>& value) override
            {
                output.Output().WriteRawObject(*canonical_, &value);
            }

        private:
            const Xnb::XnbTypeWriterBase* canonical_;
        };

        /// The one route from the XNA-shaped model graph to an `.xnb`: the graph becomes the
        /// canonical model and goes through the canonical writer, which is the only model writer
        /// there is. XNA's own ModelWriter is internal, so this one is too.
        class ModelContentTypeWriter final : public ContentTypeWriter<Processors::ModelContent>
        {
        public:
            explicit ModelContentTypeWriter(const Xnb::XnbTypeWriterBase& canonical) : canonical_(&canonical) {}

            [[nodiscard]] std::int32_t getTypeVersionProperty() const override
            {
                return canonical_->ReaderIdentity().readerVersion;
            }

            [[nodiscard]] std::string GetRuntimeReader(TargetPlatform targetPlatform) const override
            {
                return Xnb::FormatXnbReaderName(canonical_->ReaderIdentity(),
                                                Xnb::XnbReaderNameStyle::Xna40,
                                                ContainerPlatform(targetPlatform));
            }

            [[nodiscard]] std::string GetRuntimeType(TargetPlatform targetPlatform) const override
            {
                (void)targetPlatform;
                return canonical_->ReaderIdentity().targetBaseName;
            }

        protected:
            void Write(ContentWriter& output, const Carrier<Processors::ModelContent>& value) override
            {
                if (value == nullptr)
                {
                    throw PipelineException("ContentCompiler: a null model cannot be compiled.");
                }
                const Xnb::XnbModelData data = CNA::Content::Pipeline::ToCanonicalModel(*value);
                output.Output().WriteRawObject(*canonical_, &data);
            }

        private:
            const Xnb::XnbTypeWriterBase* canonical_;
        };

        /// A processed `Texture2DContent` is written the way XNA's Texture2DWriter writes it: the mip chain's surface format and
        /// every level's bytes go to the canonical Texture2D writer, which is the only one there is.
        class Texture2DContentTypeWriter final : public ContentTypeWriter<Graphics::Texture2DContent>
        {
        public:
            explicit Texture2DContentTypeWriter(const Xnb::XnbTypeWriterBase& canonical) : canonical_(&canonical) {}

            [[nodiscard]] std::int32_t getTypeVersionProperty() const override
            {
                return canonical_->ReaderIdentity().readerVersion;
            }

            [[nodiscard]] std::string GetRuntimeReader(TargetPlatform targetPlatform) const override
            {
                return Xnb::FormatXnbReaderName(canonical_->ReaderIdentity(),
                                                Xnb::XnbReaderNameStyle::Xna40,
                                                ContainerPlatform(targetPlatform));
            }

            [[nodiscard]] std::string GetRuntimeType(TargetPlatform targetPlatform) const override
            {
                (void)targetPlatform;
                return canonical_->ReaderIdentity().targetBaseName;
            }

        protected:
            void Write(ContentWriter& output, const Carrier<Graphics::Texture2DContent>& value) override
            {
                if (value == nullptr)
                {
                    throw PipelineException("ContentCompiler: a null texture cannot be compiled.");
                }
                const Graphics::MipmapChain& chain = value->getMipmapsProperty();
                if (chain.getCountProperty() == 0)
                {
                    throw PipelineException("ContentCompiler: a texture with no mipmap levels cannot be compiled.");
                }
                Xnb::XnbTexture2DContent wrapper;
                Xnb::XnbTextureData& texture = wrapper.texture;
                texture.kind = Xnb::XnbTextureKind::Texture2D;
                texture.width = static_cast<std::uint32_t>(chain[0]->getWidthProperty());
                texture.height = static_cast<std::uint32_t>(chain[0]->getHeightProperty());
                texture.mipCount = static_cast<std::uint32_t>(chain.getCountProperty());
                texture.platform = 'w';
                if (!chain[0]->TryGetFormat(texture.surfaceFormat))
                {
                    throw PipelineException("ContentCompiler: the texture's bitmap type has no surface format to write.");
                }
                for (std::int32_t level = 0; level < chain.getCountProperty(); ++level)
                {
                    Microsoft::Xna::Framework::Graphics::SurfaceFormat format{};
                    if (!chain[level]->TryGetFormat(format) || format != texture.surfaceFormat)
                    {
                        throw PipelineException("ContentCompiler: every level of a texture must have the same surface format.");
                    }
                    const auto data = chain[level]->GetPixelData();
                    texture.levels.emplace_back(data.begin(), data.end());
                }
                output.Output().WriteRawObject(*canonical_, &wrapper);
            }

        private:
            const Xnb::XnbTypeWriterBase* canonical_;
        };

        /// Resolves the runtime surface format carried by a processed bitmap.
        [[nodiscard]] Microsoft::Xna::Framework::Graphics::SurfaceFormat TextureFormat(
            const Graphics::BitmapContent& bitmap)
        {
            Microsoft::Xna::Framework::Graphics::SurfaceFormat format{};
            if (!bitmap.TryGetFormat(format))
            {
                throw PipelineException(
                    "ContentCompiler: a texture's bitmap type has no surface format to write.");
            }
            return format;
        }

        /// Flattens six pipeline face chains into the XNB writer's face-major level order.
        [[nodiscard]] Xnb::XnbTextureCubeContent ConvertTextureCube(
            const Graphics::TextureCubeContent& source)
        {
            const Graphics::MipmapChainCollection& faces = source.getFacesProperty();
            if (faces.getCountProperty() != 6)
            {
                throw PipelineException("ContentCompiler: a cube texture must have six faces.");
            }
            const Graphics::MipmapChain& first = *faces[0];
            if (first.getCountProperty() == 0)
            {
                throw PipelineException("ContentCompiler: a cube texture face has no mipmap levels.");
            }
            Xnb::XnbTextureCubeContent wrapper;
            Xnb::XnbTextureData& texture = wrapper.texture;
            texture.kind = Xnb::XnbTextureKind::TextureCube;
            texture.surfaceFormat = TextureFormat(*first[0]);
            texture.width = static_cast<std::uint32_t>(first[0]->getWidthProperty());
            texture.height = static_cast<std::uint32_t>(first[0]->getHeightProperty());
            texture.depth = 1u;
            texture.faceCount = 6u;
            texture.mipCount = static_cast<std::uint32_t>(first.getCountProperty());
            texture.platform = 'w';
            for (SharpRuntime::intcs face = 0; face < faces.getCountProperty(); ++face)
            {
                const Graphics::MipmapChain& chain = *faces[face];
                if (chain.getCountProperty() != first.getCountProperty())
                {
                    throw PipelineException(
                        "ContentCompiler: every cube face must have the same mipmap count.");
                }
                for (SharpRuntime::intcs level = 0; level < chain.getCountProperty(); ++level)
                {
                    const SharpRuntime::intcs expectedWidth =
                        std::max<SharpRuntime::intcs>(1, first[0]->getWidthProperty() >> level);
                    const SharpRuntime::intcs expectedHeight =
                        std::max<SharpRuntime::intcs>(1, first[0]->getHeightProperty() >> level);
                    if (chain[level]->getWidthProperty() != expectedWidth ||
                        chain[level]->getHeightProperty() != expectedHeight)
                    {
                        throw PipelineException(
                            "ContentCompiler: every cube face must have the same mipmap dimensions.");
                    }
                    if (TextureFormat(*chain[level]) != texture.surfaceFormat)
                    {
                        throw PipelineException(
                            "ContentCompiler: every level of a cube texture must have the same surface format.");
                    }
                    const auto bytes = chain[level]->GetPixelData();
                    texture.levels.emplace_back(bytes.begin(), bytes.end());
                }
            }
            return wrapper;
        }

        /// Packs each volume mip into one payload, with its surviving slices placed consecutively.
        [[nodiscard]] Xnb::XnbTexture3DContent ConvertTexture3D(
            const Graphics::Texture3DContent& source)
        {
            const Graphics::MipmapChainCollection& slices = source.getFacesProperty();
            if (slices.getCountProperty() == 0 || slices[0]->getCountProperty() == 0)
            {
                throw PipelineException(
                    "ContentCompiler: a volume texture must have a depth and a mipmap level.");
            }
            const Graphics::MipmapChain& first = *slices[0];
            Xnb::XnbTexture3DContent wrapper;
            Xnb::XnbTextureData& texture = wrapper.texture;
            texture.kind = Xnb::XnbTextureKind::Texture3D;
            texture.surfaceFormat = TextureFormat(*first[0]);
            texture.width = static_cast<std::uint32_t>(first[0]->getWidthProperty());
            texture.height = static_cast<std::uint32_t>(first[0]->getHeightProperty());
            texture.depth = static_cast<std::uint32_t>(slices.getCountProperty());
            texture.faceCount = 1u;
            texture.mipCount = static_cast<std::uint32_t>(first.getCountProperty());
            texture.platform = 'w';
            for (SharpRuntime::intcs slice = 0; slice < slices.getCountProperty(); ++slice)
            {
                SharpRuntime::intcs expectedLevels = 0;
                for (SharpRuntime::intcs level = 0; level < first.getCountProperty(); ++level)
                {
                    if (slice < std::max<SharpRuntime::intcs>(
                                    1, slices.getCountProperty() >> level))
                    {
                        ++expectedLevels;
                    }
                }
                if (slices[slice]->getCountProperty() != expectedLevels)
                {
                    throw PipelineException(
                        "ContentCompiler: a volume texture slice has the wrong mipmap count for its depth.");
                }
            }
            for (SharpRuntime::intcs level = 0; level < first.getCountProperty(); ++level)
            {
                const SharpRuntime::intcs levelDepth =
                    std::max<SharpRuntime::intcs>(1, slices.getCountProperty() >> level);
                std::vector<std::uint8_t> combined;
                for (SharpRuntime::intcs slice = 0; slice < levelDepth; ++slice)
                {
                    const Graphics::MipmapChain& chain = *slices[slice];
                    if (chain.getCountProperty() <= level)
                    {
                        throw PipelineException(
                            "ContentCompiler: a volume texture slice is missing a mipmap level.");
                    }
                    const SharpRuntime::intcs expectedWidth =
                        std::max<SharpRuntime::intcs>(1, first[0]->getWidthProperty() >> level);
                    const SharpRuntime::intcs expectedHeight =
                        std::max<SharpRuntime::intcs>(1, first[0]->getHeightProperty() >> level);
                    if (chain[level]->getWidthProperty() != expectedWidth ||
                        chain[level]->getHeightProperty() != expectedHeight)
                    {
                        throw PipelineException(
                            "ContentCompiler: every volume slice must have the same mipmap dimensions.");
                    }
                    if (TextureFormat(*chain[level]) != texture.surfaceFormat)
                    {
                        throw PipelineException(
                            "ContentCompiler: every level of a volume texture must have the same surface format.");
                    }
                    const auto bytes = chain[level]->GetPixelData();
                    combined.insert(combined.end(), bytes.begin(), bytes.end());
                }
                texture.levels.push_back(std::move(combined));
            }
            return wrapper;
        }

        /// A processed sound or song reaches the canonical writer for it the same way: the pipeline's content object becomes the
        /// canonical data (what the .xnb holds), and the canonical writer writes it. `Convert` does the one conversion that differs.
        template<typename TContent, typename TCanonical, typename TConvert>
        class CanonicalContentTypeWriter final : public ContentTypeWriter<TContent>
        {
        public:
            CanonicalContentTypeWriter(const Xnb::XnbTypeWriterBase& canonical, TConvert convert)
                : canonical_(&canonical), convert_(std::move(convert)) {}

            [[nodiscard]] std::int32_t getTypeVersionProperty() const override
            {
                return canonical_->ReaderIdentity().readerVersion;
            }

            [[nodiscard]] std::string GetRuntimeReader(TargetPlatform targetPlatform) const override
            {
                return Xnb::FormatXnbReaderName(canonical_->ReaderIdentity(),
                                                Xnb::XnbReaderNameStyle::Xna40,
                                                ContainerPlatform(targetPlatform));
            }

            [[nodiscard]] std::string GetRuntimeType(TargetPlatform targetPlatform) const override
            {
                (void)targetPlatform;
                return canonical_->ReaderIdentity().targetBaseName;
            }

        protected:
            void Write(ContentWriter& output, const Carrier<TContent>& value) override
            {
                if (value == nullptr)
                {
                    throw PipelineException("ContentCompiler: a null asset cannot be compiled.");
                }
                const TCanonical data = convert_(*value);
                output.Output().WriteRawObject(*canonical_, &data);
            }

        private:
            const Xnb::XnbTypeWriterBase* canonical_;
            TConvert convert_;
        };

        /// The worker view of a built-in canonical type writer: the registry already holds the
        /// canonical writer, so this bundle is never registered, only consulted.
        template<typename T>
        class BuiltInBundle final : public Xnb::XnbTypeWriterBase, public XnaTypeWorker
        {
        public:
            explicit BuiltInBundle(const Xnb::XnbTypeWriterBase& canonical) : canonical_(&canonical) {}
            [[nodiscard]] Xnb::XnbTypeId TargetTypeId() const noexcept override { return canonical_->TargetTypeId(); }
            [[nodiscard]] Xnb::XnbReaderIdentity ReaderIdentity() const override { return canonical_->ReaderIdentity(); }
            [[nodiscard]] bool IsSerializedByReference() const noexcept override { return canonical_->IsSerializedByReference(); }
            void WriteUntyped(Xnb::XnbWriter& output, const void* value) const override { canonical_->WriteUntyped(output, value); }
            [[nodiscard]] const Xnb::XnbTypeWriterBase& Canonical() const noexcept override { return *canonical_; }
            [[nodiscard]] std::type_index CarrierType() const noexcept override { return typeid(Carrier<T>); }
            [[nodiscard]] std::string TypeName() const override { return ContentTypeName<T>::Name(); }
            void WriteFromObject(Xnb::XnbWriter&, const std::shared_ptr<System::Object>&, bool) const override
            {
                throw System::InvalidCastException("'" + ContentTypeName<T>::Name() + "' is not a reference type.");
            }
            [[nodiscard]] std::int32_t AddSharedFromObject(Xnb::XnbWriter&, const std::shared_ptr<System::Object>&) const override
            {
                throw System::InvalidCastException("'" + ContentTypeName<T>::Name() + "' is not a reference type.");
            }

        private:
            const Xnb::XnbTypeWriterBase* canonical_;
        };
    }

    ContentCompiler::ContentCompiler()
    {
        RegisterBuiltIns();
    }

    ContentCompiler::~ContentCompiler() = default;

    void ContentCompiler::AddWriter(std::shared_ptr<ContentTypeWriterBase> facade, const std::type_index type,
                                    const std::type_index carrier, const bool isReference, std::string name,
                                    WorkerFactory factory)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (frozen_)
        {
            throw PipelineException("ContentCompiler: type writers cannot be added after the compiler has compiled.");
        }
        for (const Known& known : known_)
        {
            if (known.type == type)
            {
                throw PipelineException("ContentCompiler: a type writer for '{0}' is already registered.", name);
            }
        }
        facade->Initialize(*this);
        known_.push_back(Known{type, carrier, isReference, std::move(name), std::move(facade), std::move(factory)});
    }

    void ContentCompiler::RegisterBuiltIns()
    {
        const Xnb::XnbTypeWriterRegistry& builtIns = Xnb::BuiltInXnbWriterRegistry();
        const auto add = [this, &builtIns]<typename T>() {
            const Xnb::XnbTypeWriterBase* canonical = builtIns.Find(Xnb::XnbTypeKey<T>::Id());
            if (canonical == nullptr) { return; }
            auto facade = std::make_shared<BuiltInTypeWriter<T>>(*canonical);
            known_.push_back(Known{std::type_index(typeid(T)), std::type_index(typeid(Carrier<T>)), false,
                                   ContentTypeName<T>::Name(), facade,
                                   [canonical](TargetPlatform) -> std::shared_ptr<Xnb::XnbTypeWriterBase> {
                                       return std::make_shared<BuiltInBundle<T>>(*canonical);
                                   }});
        };
        add.template operator()<bool>();
        add.template operator()<std::int8_t>();
        add.template operator()<std::uint8_t>();
        add.template operator()<std::int16_t>();
        add.template operator()<std::uint16_t>();
        add.template operator()<std::int32_t>();
        add.template operator()<std::uint32_t>();
        add.template operator()<std::int64_t>();
        add.template operator()<std::uint64_t>();
        add.template operator()<float>();
        add.template operator()<double>();
        add.template operator()<char16_t>();
        add.template operator()<std::string>();
        add.template operator()<System::TimeSpan>();
        add.template operator()<System::DateTime>();
#if SHARP_RUNTIME_HAS_NATIVE_INT128
        add.template operator()<System::Decimal>();
#endif
        add.template operator()<Microsoft::Xna::Framework::Vector2>();
        add.template operator()<Microsoft::Xna::Framework::Vector3>();
        add.template operator()<Microsoft::Xna::Framework::Vector4>();
        add.template operator()<Microsoft::Xna::Framework::Matrix>();
        add.template operator()<Microsoft::Xna::Framework::Quaternion>();
        add.template operator()<Microsoft::Xna::Framework::Color>();
        add.template operator()<Microsoft::Xna::Framework::Point>();
        add.template operator()<Microsoft::Xna::Framework::Rectangle>();
        add.template operator()<Microsoft::Xna::Framework::Plane>();
        add.template operator()<Microsoft::Xna::Framework::BoundingBox>();
        add.template operator()<Microsoft::Xna::Framework::BoundingSphere>();
        add.template operator()<Microsoft::Xna::Framework::BoundingFrustum>();
        add.template operator()<Microsoft::Xna::Framework::Ray>();
        add.template operator()<Microsoft::Xna::Framework::Curve>();
        add.template operator()<std::vector<std::string>>();
        add.template operator()<std::vector<std::int32_t>>();
        add.template operator()<std::vector<char16_t>>();
        add.template operator()<std::vector<Microsoft::Xna::Framework::Rectangle>>();
        add.template operator()<std::vector<Microsoft::Xna::Framework::Vector3>>();
        add.template operator()<std::vector<Microsoft::Xna::Framework::Matrix>>();
        add.template operator()<System::Collections::Generic::OrderedDictionary<std::string,
                                                                                std::int32_t>>();
        add.template operator()<System::Collections::Generic::OrderedDictionary<std::string,
                                                                                std::string>>();

        // The processed model graph is a built-in too: it reaches the same canonical model writer
        // the rest of the engine writes through (plans/plan_xnapipeline_parity.md XNAPP-152).
        if (const Xnb::XnbTypeWriterBase* model = builtIns.Find(Xnb::XnbTypeKey<Xnb::XnbModelData>::Id()))
        {
            auto facade = std::make_shared<ModelContentTypeWriter>(*model);
            facade->Initialize(*this);
            known_.push_back(Known{std::type_index(typeid(Processors::ModelContent)),
                                   std::type_index(typeid(Carrier<Processors::ModelContent>)), true,
                                   ContentTypeName<Processors::ModelContent>::Name(), facade,
                                   [facade, this](TargetPlatform platform)
                                       -> std::shared_ptr<Xnb::XnbTypeWriterBase>
                                   { return std::make_shared<Adapter<Processors::ModelContent>>(
                                         facade, platform, this); }});
        }
        // A processed texture likewise reaches the canonical Texture2D writer (XNA's ContentCompiler writes Texture2DContent too).
        if (const Xnb::XnbTypeWriterBase* texture = builtIns.Find(Xnb::XnbTypeKey<Xnb::XnbTexture2DContent>::Id()))
        {
            auto facade = std::make_shared<Texture2DContentTypeWriter>(*texture);
            facade->Initialize(*this);
            known_.push_back(Known{std::type_index(typeid(Graphics::Texture2DContent)),
                                   std::type_index(typeid(Carrier<Graphics::Texture2DContent>)), true,
                                   ContentTypeName<Graphics::Texture2DContent>::Name(), facade,
                                   [facade, this](TargetPlatform platform)
                                       -> std::shared_ptr<Xnb::XnbTypeWriterBase>
                                   { return std::make_shared<Adapter<Graphics::Texture2DContent>>(
                                         facade, platform, this); }});
        }
        // A processed sound effect and a processed song, through the same canonical writers a runtime asset uses.
        const auto addCanonical = [this, &builtIns]<typename TContent, typename TCanonical, typename TConvert>(TConvert convert) {
            const Xnb::XnbTypeWriterBase* canonical = builtIns.Find(Xnb::XnbTypeKey<TCanonical>::Id());
            if (canonical == nullptr) { return; }
            auto facade = std::make_shared<CanonicalContentTypeWriter<TContent, TCanonical, TConvert>>(*canonical, std::move(convert));
            facade->Initialize(*this);
            known_.push_back(Known{std::type_index(typeid(TContent)), std::type_index(typeid(Carrier<TContent>)), true,
                                   ContentTypeName<TContent>::Name(), facade,
                                   [facade, this](TargetPlatform platform) -> std::shared_ptr<Xnb::XnbTypeWriterBase>
                                   { return std::make_shared<Adapter<TContent>>(facade, platform, this); }});
        };
        addCanonical.template operator()<Graphics::TextureCubeContent, Xnb::XnbTextureCubeContent>(
            ConvertTextureCube);
        addCanonical.template operator()<Graphics::Texture3DContent, Xnb::XnbTexture3DContent>(
            ConvertTexture3D);
        addCanonical.template operator()<Processors::SoundEffectContent, Xnb::XnbSoundEffectData>(
            [](const Processors::SoundEffectContent& sound) {
                Xnb::XnbSoundEffectData data;
                data.platform = 'w';
                const Audio::AudioFormat& format = *sound.Format();
                data.formatTag = static_cast<std::uint16_t>(format.getFormatProperty());
                data.channels = static_cast<std::uint16_t>(format.getChannelCountProperty());
                data.sampleRate = static_cast<std::uint32_t>(format.getSampleRateProperty());
                data.averageBytesPerSecond = static_cast<std::uint32_t>(format.getAverageBytesPerSecondProperty());
                data.blockAlign = static_cast<std::uint16_t>(format.getBlockAlignProperty());
                data.bitsPerSample = static_cast<std::uint16_t>(format.getBitsPerSampleProperty());
                const auto& native = format.getNativeWaveFormatProperty();
                // The native WAVEFORMATEX is 18 bytes (through cbSize); what follows is the codec's extension data.
                if (native.size() > 18u) { data.extensionData.assign(native.begin() + 18, native.end()); }
                data.samples.assign(sound.Data().begin(), sound.Data().end());
                data.loopStart = sound.LoopStart();
                data.loopLength = sound.LoopLength();
                data.storedDurationMs = static_cast<std::uint32_t>(sound.Duration().getTotalMillisecondsProperty());
                return data;
            });
        addCanonical.template operator()<Processors::SongContent, Xnb::XnbSongData>(
            [](const Processors::SongContent& song) {
                Xnb::XnbSongData data;
                data.mediaPath = song.FileName();
                data.durationMs = static_cast<std::int32_t>(song.Duration().getTotalMillisecondsProperty());
                return data;
            });
        addCanonical.template operator()<VideoContent, Xnb::XnbVideoData>(
            [](const VideoContent& video) {
                Xnb::XnbVideoData data;
                data.mediaPath = CNA::Internal::PathToUtf8(
                    CNA::Internal::PathFromUtf8(video.getFilenameProperty()).filename());
                data.durationMs = static_cast<std::int32_t>(
                    video.getDurationProperty().getTotalMillisecondsProperty());
                data.width = video.getWidthProperty();
                data.height = video.getHeightProperty();
                data.framesPerSecond = video.getFramesPerSecondProperty();
                data.soundtrackType = static_cast<std::int32_t>(
                    video.getVideoSoundtrackTypeProperty());
                return data;
            });
        addCanonical.template operator()<Processors::SpriteFontContent, Xnb::XnbSpriteFontData>(
            [](const Processors::SpriteFontContent& font) {
                const CNA::Content::Cnb::CnbSpriteFontData& source = font.Data();
                if (source.atlas.representations.empty())
                {
                    throw PipelineException(
                        "ContentCompiler: a sprite font atlas carries no pixel representation.");
                }
                const CNA::Content::Cnb::CnbTextureRepresentation& representation =
                    source.atlas.representations.front();
                Xnb::XnbSpriteFontData data;
                data.atlas.kind = Xnb::XnbTextureKind::Texture2D;
                data.atlas.surfaceFormat =
                    CNA::Content::Cnb::CnbTextureFormatToSurfaceFormat(representation.format);
                data.atlas.width = source.atlas.width;
                data.atlas.height = source.atlas.height;
                data.atlas.depth = source.atlas.depth;
                data.atlas.faceCount = source.atlas.faceCount;
                data.atlas.mipCount = source.atlas.mipCount;
                data.atlas.platform = 'w';
                data.atlas.levels = representation.levels;
                data.glyphs = source.glyphBounds;
                data.cropping = source.cropping;
                data.characters = source.characters;
                data.lineSpacing = source.lineSpacing;
                data.spacing = source.spacing;
                data.kerning = source.kerning;
                data.defaultCharacter = source.defaultCharacter;
                return data;
            });
        addCanonical.template operator()<Processors::CompiledEffectContent,
                                         Xnb::XnbCompiledEffectContent>(
            [](const Processors::CompiledEffectContent& effect) {
                Xnb::XnbCompiledEffectContent data;
                data.bytecode.assign(effect.GetEffectCode().begin(), effect.GetEffectCode().end());
                return data;
            });
    }

    const ContentCompiler::PlatformRegistry& ContentCompiler::Platform(const TargetPlatform platform) const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        frozen_ = true;
        auto found = platforms_.find(platform);
        if (found != platforms_.end()) { return *found->second; }
        auto built = std::make_unique<PlatformRegistry>();
        // The built-in canonical writers come first, exactly as the built-in registry has them; a
        // user writer for a built-in type was already refused by AddWriter's duplicate rule.
        Xnb::RegisterBuiltInXnbWriters(built->registry);
        for (const Known& known : known_)
        {
            std::shared_ptr<Xnb::XnbTypeWriterBase> worker = known.factory(platform);
            const auto* view = dynamic_cast<const XnaTypeWorker*>(worker.get());
            if (view == nullptr) { continue; }
            if (built->registry.Find(worker->TargetTypeId()) == nullptr)
            {
                built->registry.Register(worker);
            }
            built->workers.push_back(worker);
            built->byCarrier.emplace(known.carrier, view);
            built->byType.emplace(known.type, view);
            built->byFacade.emplace(known.facade.get(), view);
        }
        built->registry.Freeze();
        const PlatformRegistry& result = *built;
        platforms_.emplace(platform, std::move(built));
        return result;
    }

    std::shared_ptr<ContentTypeWriterBase> ContentCompiler::GetTypeWriter(const System::Type type) const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (type.getTypeInfo() != nullptr)
        {
            for (const Known& known : known_)
            {
                if (known.type == std::type_index(*type.getTypeInfo())) { return known.facade; }
            }
        }
        throw PipelineException("ContentCompiler: no type writer is registered for '{0}'.", type.getNameProperty());
    }

    std::vector<std::string> ContentCompiler::KnownTypeNames() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<std::string> names;
        for (const Known& known : known_) { names.push_back(known.name); }
        std::sort(names.begin(), names.end());
        return names;
    }

    const Xnb::XnbTypeWriterRegistry& ContentCompiler::TypeWriterRegistry(const TargetPlatform platform) const
    {
        return Platform(platform).registry;
    }

    const XnaTypeWorker* ContentCompiler::FindWorker(const std::type_index carrier, const TargetPlatform platform) const
    {
        const PlatformRegistry& registry = Platform(platform);
        const auto found = registry.byCarrier.find(carrier);
        return found == registry.byCarrier.end() ? nullptr : found->second;
    }

    const XnaTypeWorker* ContentCompiler::FindWorkerForObject(const System::Object& object,
                                                              const TargetPlatform platform) const
    {
        const PlatformRegistry& registry = Platform(platform);
        const auto found = registry.byType.find(std::type_index(typeid(object)));
        return found == registry.byType.end() ? nullptr : found->second;
    }

    const XnaTypeWorker* ContentCompiler::FindWorkerFor(const ContentTypeWriterBase& writer,
                                                        const TargetPlatform platform) const
    {
        const PlatformRegistry& registry = Platform(platform);
        const auto found = registry.byFacade.find(&writer);
        return found == registry.byFacade.end() ? nullptr : found->second;
    }

    ContentWriter* ContentCompiler::ActiveWriter(const Xnb::XnbWriter& output) const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto found = active_.find(&output);
        return found == active_.end() ? nullptr : found->second;
    }

    void ContentCompiler::RegisterActive(const Xnb::XnbWriter& output, ContentWriter& facade) const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        active_[&output] = &facade;
    }

    void ContentCompiler::UnregisterActive(const Xnb::XnbWriter& output) const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        active_.erase(&output);
    }

    Xnb::XnbAssetWriteResult ContentCompiler::CompileObject(const ContentObject& value,
                                                            const CompileOptions& options) const
    {
        if (value.Empty())
        {
            throw PipelineException("ContentCompiler: cannot compile an empty object.");
        }
        const XnaTypeWorker* worker = FindWorker(value.CppType(), options.targetPlatform);
        if (worker == nullptr)
        {
            throw PipelineException("ContentCompiler: no type writer is registered for '{0}'.", value.StableType());
        }
        Xnb::XnbFileOptions fileOptions = options.container;
        switch (options.targetPlatform)
        {
            case TargetPlatform::Windows: fileOptions.platform = Xnb::XnbTargetPlatform::Windows; break;
            case TargetPlatform::Xbox360: fileOptions.platform = Xnb::XnbTargetPlatform::Xbox360; break;
            case TargetPlatform::WindowsPhone: fileOptions.platform = Xnb::XnbTargetPlatform::WindowsPhone; break;
        }
        fileOptions.graphicsProfile = options.targetProfile == Microsoft::Xna::Framework::Graphics::GraphicsProfile::HiDef
                                          ? Xnb::XnbGraphicsProfile::HiDef
                                          : Xnb::XnbGraphicsProfile::Reach;
        bool compress = options.compressContent;
        if (compress)
        {
            // The root writer may decline compression for content that does not compress well.
            std::lock_guard<std::mutex> lock(mutex_);
            for (const Known& known : known_)
            {
                if (known.carrier == value.CppType())
                {
                    compress = known.facade->InvokeShouldCompressContent(options.targetPlatform, value);
                    break;
                }
            }
        }
        fileOptions.compression = compress ? Xnb::XnbOutputCompression::Lzx : Xnb::XnbOutputCompression::None;

        Xnb::XnbWriter output(TypeWriterRegistry(options.targetPlatform), fileOptions, options.assetName);
        Xnb::XnbAssetWriteResult result;
        {
            // The façade must outlive Finish(): shared resources are serialized there, and their
            // writers reach this file's façade through the compiler.
            ContentWriter facade(*this, output, options.targetPlatform,
                                 options.targetProfile, options.outputDirectory, options.assetName);
            output.WriteObject(worker->Canonical(), value.RawData());
            result.bytes = output.Finish();
        }
        result.rootReaderName = output.RootReaderName();
        return result;
    }
}
