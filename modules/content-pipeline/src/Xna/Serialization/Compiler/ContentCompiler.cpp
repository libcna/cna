// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Content/Pipeline/Serialization/Compiler/ContentCompiler.hpp"

#include <algorithm>

#include "CNA/Content/Pipeline/XnaModelBridge.hpp"
#include "CNA/Internal/Xnb/XnbBuiltInWriters.hpp"
#include "Microsoft/Xna/Framework/BoundingBox.hpp"
#include "Microsoft/Xna/Framework/BoundingFrustum.hpp"
#include "Microsoft/Xna/Framework/BoundingSphere.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/PipelineException.hpp"
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
                (void)targetPlatform;
                return Xnb::FormatXnbReaderName(canonical_->ReaderIdentity(), Xnb::XnbReaderNameStyle::Xna40);
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
                (void)targetPlatform;
                return Xnb::FormatXnbReaderName(canonical_->ReaderIdentity(), Xnb::XnbReaderNameStyle::Xna40);
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
        add.template operator()<std::map<std::string, std::int32_t>>();

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
