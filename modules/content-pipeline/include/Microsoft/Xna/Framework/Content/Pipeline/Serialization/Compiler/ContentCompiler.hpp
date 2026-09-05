// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <type_traits>
#include <typeindex>
#include <utility>
#include <vector>

#include "CNA/CNAHelper.hpp"
#include "CNA/Internal/Xnb/XnbAssetWriter.hpp"
#include "CNA/Internal/Xnb/XnbFileOptions.hpp"
#include "CNA/Internal/Xnb/XnbReaderIdentity.hpp"
#include "CNA/Internal/Xnb/XnbTypeWriter.hpp"
#include "CNA/Internal/Xnb/XnbWriter.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/ContentObject.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/ContentTypeName.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Serialization/Compiler/ContentTypeWriter.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Serialization/Compiler/ContentTypeWriterAttribute.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/PipelineException.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Serialization/Compiler/ContentWriter.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/TargetPlatform.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "System/InvalidCastException.hpp"
#include "System/Object.hpp"
#include "System/Type.hpp"

namespace Microsoft::Xna::Framework::Content::Pipeline::Serialization::Compiler
{
    /**
     * @brief Everything one `.xnb` compilation is told beyond the value itself (CNA-owned; XNA's
     *        `ContentCompiler.Compile` receives these as arguments).
     */
    struct CNAEXT CompileOptions
    {
        /** @brief The content build target platform. */
        TargetPlatform targetPlatform = TargetPlatform::Windows;

        /** @brief The target graphics profile. */
        Microsoft::Xna::Framework::Graphics::GraphicsProfile targetProfile =
            Microsoft::Xna::Framework::Graphics::GraphicsProfile::Reach;

        /** @brief Whether to LZX-compress the payload when the root writer allows it. */
        bool compressContent = false;

        /** @brief Directory the compiled asset is published to; resolves external references. */
        std::filesystem::path outputDirectory;

        /** @brief Logical name of the asset being compiled. */
        std::string assetName;

        /** @brief Container version, reader-name style and write limits; platform, profile and
         *         compression are taken from the fields above. */
        CNA::Internal::Xnb::XnbFileOptions container;
    };

    /**
     * @brief Provides methods for writing compiled binary format.
     *
     * XNA's compiler is created by the build host and reached through `ContentTypeWriter::Initialize`;
     * here it is constructible, owns the canonical `XnbTypeWriterRegistry` it compiles with (the
     * built-in writers plus every `ContentTypeWriter<T>` added), and resolves a writer by type for
     * `GetTypeWriter`. Sealed in XNA, `final` here.
     */
    class ContentCompiler final
    {
    public:
        /** @brief .NET full name of this type. */
        CNAEXT static constexpr std::string_view XnaTypeName =
            "Microsoft.Xna.Framework.Content.Pipeline.Serialization.Compiler.ContentCompiler";

        /** @brief Creates a compiler knowing every built-in type writer. */
        CNAEXT ContentCompiler();

        /** @brief Destroys the compiler. */
        ~ContentCompiler();

        ContentCompiler(const ContentCompiler&) = delete;
        ContentCompiler& operator=(const ContentCompiler&) = delete;

        /**
         * @brief Retrieves the worker writer for the specified type.
         *
         * @param type The type handled by the writer.
         * @return The writer.
         * @throws PipelineException when no writer handles @p type.
         */
        [[nodiscard]] std::shared_ptr<ContentTypeWriterBase> GetTypeWriter(System::Type type) const;

        /**
         * @brief Adds a user type writer class, the C++ form of applying
         *        `[ContentTypeWriter]` to it.
         *
         * @tparam TWriter A class deriving `ContentTypeWriter<T>`, default-constructible.
         * @param attribute The marker descriptor.
         * @return The added writer.
         * @throws PipelineException when a writer for the same type exists or the compiler has
         *         already compiled (its registries are frozen).
         */
        template<typename TWriter>
        CNAEXT std::shared_ptr<TWriter> AddTypeWriter(ContentTypeWriterAttribute attribute = {})
        {
            using T = typename TWriter::TargetContentType;
            static_assert(std::is_base_of_v<ContentTypeWriter<T>, TWriter>,
                          "AddTypeWriter needs a class deriving ContentTypeWriter<T>.");
            (void)attribute;
            auto writer = std::make_shared<TWriter>();
            AddWriter(writer, std::type_index(typeid(T)), std::type_index(typeid(Carrier<T>)),
                      std::is_base_of_v<System::Object, T>, ContentTypeName<T>::Name(),
                      [writer, this](TargetPlatform platform) -> std::shared_ptr<CNA::Internal::Xnb::XnbTypeWriterBase> {
                          return std::make_shared<Adapter<T>>(writer, platform, this);
                      });
            return writer;
        }

        /**
         * @brief Compiles a value into a complete `.xnb` file image.
         *
         * @tparam T The content type of the root.
         * @param value The root value.
         * @param options Platform, profile, compression, directories and container options.
         * @return The file bytes and the root reader name the type table dispatches to.
         */
        template<typename T>
        [[nodiscard]] CNAEXT CNA::Internal::Xnb::XnbAssetWriteResult Compile(const Carrier<T>& value,
                                                                            const CompileOptions& options) const
        {
            return CompileObject(Box<T>(value), options);
        }

        /**
         * @brief Compiles a boxed value into a complete `.xnb` file image, dispatching on the
         *        box's C++ type.
         *
         * @param value The boxed root.
         * @param options Platform, profile, compression, directories and container options.
         * @return The file bytes and the root reader name.
         * @throws PipelineException when no writer handles the boxed type.
         */
        [[nodiscard]] CNAEXT CNA::Internal::Xnb::XnbAssetWriteResult CompileObject(const ContentObject& value,
                                                                                  const CompileOptions& options) const;

        /**
         * @brief Returns the .NET names of every type this compiler can write, sorted.
         *
         * @return Type names (built-ins and added writers).
         */
        [[nodiscard]] CNAEXT std::vector<std::string> KnownTypeNames() const;

        /**
         * @brief Returns the canonical type-writer registry for a platform, freezing it.
         *
         * @param platform The target platform whose reader names the writers spell.
         * @return The frozen registry.
         */
        [[nodiscard]] CNAEXT const CNA::Internal::Xnb::XnbTypeWriterRegistry& TypeWriterRegistry(TargetPlatform platform) const;

        /**
         * @brief Finds the worker for a C++ carrier type on a platform.
         *
         * @param carrier `typeid(Carrier<T>)`.
         * @param platform The target platform.
         * @return The worker, or null.
         */
        [[nodiscard]] CNAEXT const XnaTypeWorker* FindWorker(std::type_index carrier, TargetPlatform platform) const;

        /**
         * @brief Finds the worker for a reference-typed value by its dynamic type.
         *
         * @param object The value.
         * @param platform The target platform.
         * @return The worker, or null.
         */
        [[nodiscard]] CNAEXT const XnaTypeWorker* FindWorkerForObject(const System::Object& object, TargetPlatform platform) const;

        /**
         * @brief Finds the worker that wraps a specific façade writer.
         *
         * @param writer A writer returned by `GetTypeWriter` or added with `AddTypeWriter`.
         * @param platform The target platform.
         * @return The worker, or null when @p writer is not this compiler's.
         */
        [[nodiscard]] CNAEXT const XnaTypeWorker* FindWorkerFor(const ContentTypeWriterBase& writer, TargetPlatform platform) const;

        /**
         * @brief Returns the façade writer active for a canonical writer, or null.
         *
         * @param output A canonical writer a `ContentWriter` was created over.
         * @return The façade.
         */
        [[nodiscard]] CNAEXT ContentWriter* ActiveWriter(const CNA::Internal::Xnb::XnbWriter& output) const;

    private:
        friend class ContentWriter;

        using WorkerFactory = std::function<std::shared_ptr<CNA::Internal::Xnb::XnbTypeWriterBase>(TargetPlatform)>;

        struct Known
        {
            std::type_index type;
            std::type_index carrier;
            bool isReference = false;
            std::string name;
            std::shared_ptr<ContentTypeWriterBase> facade;
            WorkerFactory factory;
        };

        struct PlatformRegistry
        {
            CNA::Internal::Xnb::XnbTypeWriterRegistry registry;
            std::vector<std::shared_ptr<CNA::Internal::Xnb::XnbTypeWriterBase>> workers;
            std::map<std::type_index, const XnaTypeWorker*> byCarrier;
            std::map<std::type_index, const XnaTypeWorker*> byType;
            std::map<const ContentTypeWriterBase*, const XnaTypeWorker*> byFacade;
        };

        /** @brief Adapts a `ContentTypeWriter<T>` into a canonical type writer for one platform. */
        template<typename T>
        class Adapter final : public CNA::Internal::Xnb::XnbTypeWriterBase, public XnaTypeWorker
        {
        public:
            Adapter(std::shared_ptr<ContentTypeWriter<T>> writer, TargetPlatform platform,
                    const ContentCompiler* compiler)
                : writer_(std::move(writer)), platform_(platform), compiler_(compiler)
            {
            }

            [[nodiscard]] CNA::Internal::Xnb::XnbTypeId TargetTypeId() const noexcept override
            {
                return CNA::Internal::Xnb::XnbTypeKey<Carrier<T>>::Id();
            }

            [[nodiscard]] CNA::Internal::Xnb::XnbReaderIdentity ReaderIdentity() const override
            {
                CNA::Internal::Xnb::XnbReaderIdentity identity;
                identity.readerBaseName = writer_->GetRuntimeReader(platform_);
                identity.readerAssembly = CNA::Internal::Xnb::XnbAssembly::None;
                identity.targetBaseName = writer_->GetRuntimeType(platform_);
                identity.targetAssembly = CNA::Internal::Xnb::XnbAssembly::None;
                identity.readerVersion = writer_->getTypeVersionProperty();
                identity.targetSharesGenericArguments = false;
                identity.evidence = CNA::Internal::Xnb::XnbNameEvidence::DerivedRule;
                return identity;
            }

            [[nodiscard]] bool IsSerializedByReference() const noexcept override
            {
                return std::is_base_of_v<System::Object, T>;
            }

            void WriteUntyped(CNA::Internal::Xnb::XnbWriter& output, const void* value) const override
            {
                ContentWriter& facade = RequireFacade(output);
                writer_->InvokeWrite(facade, Box<T>(*static_cast<const Carrier<T>*>(value)));
            }

            [[nodiscard]] const CNA::Internal::Xnb::XnbTypeWriterBase& Canonical() const noexcept override { return *this; }
            [[nodiscard]] std::type_index CarrierType() const noexcept override { return typeid(Carrier<T>); }
            [[nodiscard]] std::string TypeName() const override { return ContentTypeName<T>::Name(); }

            void WriteFromObject(CNA::Internal::Xnb::XnbWriter& output, const std::shared_ptr<System::Object>& object,
                                 const bool raw) const override
            {
                const Carrier<T> typed = Downcast(object);
                if (raw) { output.WriteRawObject(*this, &typed); }
                else { output.WriteObject(*this, &typed); }
            }

            [[nodiscard]] std::int32_t AddSharedFromObject(CNA::Internal::Xnb::XnbWriter& output,
                                                           const std::shared_ptr<System::Object>& object) const override
            {
                return output.AddSharedResource(*this, std::make_shared<const Carrier<T>>(Downcast(object)));
            }

        private:
            [[nodiscard]] Carrier<T> Downcast(const std::shared_ptr<System::Object>& object) const
            {
                if constexpr (std::is_base_of_v<System::Object, T>)
                {
                    T* typed = dynamic_cast<T*>(object.get());
                    if (typed == nullptr)
                    {
                        throw System::InvalidCastException("The writer for '" + ContentTypeName<T>::Name() +
                                                           "' was given a '" + object->GetTypeName() + "'.");
                    }
                    return std::shared_ptr<T>(object, typed);
                }
                else
                {
                    throw System::InvalidCastException("'" + ContentTypeName<T>::Name() + "' is not a reference type.");
                }
            }

            [[nodiscard]] ContentWriter& RequireFacade(CNA::Internal::Xnb::XnbWriter& output) const
            {
                ContentWriter* facade = compiler_->ActiveWriter(output);
                if (facade == nullptr)
                {
                    throw PipelineException(
                        "A ContentTypeWriter was invoked by a file writer that no ContentCompiler is compiling with.");
                }
                return *facade;
            }

            std::shared_ptr<ContentTypeWriter<T>> writer_;
            TargetPlatform platform_;
            const ContentCompiler* compiler_;
        };

        void AddWriter(std::shared_ptr<ContentTypeWriterBase> facade, std::type_index type, std::type_index carrier,
                       bool isReference, std::string name, WorkerFactory factory);
        void RegisterBuiltIns();
        [[nodiscard]] const PlatformRegistry& Platform(TargetPlatform platform) const;
        void RegisterActive(const CNA::Internal::Xnb::XnbWriter& output, ContentWriter& facade) const;
        void UnregisterActive(const CNA::Internal::Xnb::XnbWriter& output) const;

        std::vector<Known> known_;
        mutable std::map<TargetPlatform, std::unique_ptr<PlatformRegistry>> platforms_;
        mutable std::mutex mutex_;
        mutable std::map<const CNA::Internal::Xnb::XnbWriter*, ContentWriter*> active_;
        mutable bool frozen_ = false;
    };
}
