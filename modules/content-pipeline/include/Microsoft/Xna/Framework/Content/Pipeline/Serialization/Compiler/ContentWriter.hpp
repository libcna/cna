// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <typeindex>
#include <utility>

#include "CNA/CNAHelper.hpp"
#include "CNA/Internal/Xnb/XnbWriter.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/ContentObject.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/ContentTypeName.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/ExternalReference.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/PipelineException.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Serialization/Compiler/ContentTypeWriter.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/TargetPlatform.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "System/IO/BinaryWriter.hpp"
#include "System/IO/MemoryStream.hpp"
#include "System/Object.hpp"

namespace Microsoft::Xna::Framework
{
    struct Color;
    struct Matrix;
    struct Quaternion;
    struct Vector2;
    struct Vector3;
    struct Vector4;
}

namespace Microsoft::Xna::Framework::Content::Pipeline::Serialization::Compiler
{
    class ContentCompiler;

    /**
     * @brief The type-writer worker the compiler hands a `ContentWriter`: the canonical
     *        `XnbTypeWriterBase` plus the routes a polymorphic reference needs.
     *
     * Not XNA API; the shape every writer the compiler knows -- a user `ContentTypeWriter<T>` or
     * a built-in -- presents to the writer façade.
     */
    class CNAEXT XnaTypeWorker
    {
    public:
        virtual ~XnaTypeWorker() = default;

        /** @brief The canonical type writer that serializes the carrier. */
        [[nodiscard]] virtual const CNA::Internal::Xnb::XnbTypeWriterBase& Canonical() const noexcept = 0;

        /** @brief The C++ carrier type the canonical writer expects a pointer to. */
        [[nodiscard]] virtual std::type_index CarrierType() const noexcept = 0;

        /** @brief The .NET name of the content type. */
        [[nodiscard]] virtual std::string TypeName() const = 0;

        /**
         * @brief Writes a reference-typed value through its dynamic type.
         *
         * @param output The file being written.
         * @param object The value, as the `System::Object` every reference type derives.
         * @param raw True to omit the dispatch index.
         * @throws System::InvalidCastException when @p object is not the worker's type.
         */
        virtual void WriteFromObject(CNA::Internal::Xnb::XnbWriter& output,
                                     const std::shared_ptr<System::Object>& object, bool raw) const = 0;

        /**
         * @brief Queues a reference-typed value as a shared resource.
         *
         * @param output The file being written.
         * @param object The value.
         * @return The 1-based shared-resource identifier.
         */
        [[nodiscard]] virtual std::int32_t AddSharedFromObject(CNA::Internal::Xnb::XnbWriter& output,
                                                               const std::shared_ptr<System::Object>& object) const = 0;
    };

    /** @brief Holds the placeholder stream a `ContentWriter`'s `BinaryWriter` base is constructed over. */
    struct CNAEXT ContentWriterPlaceholderStream
    {
        /** @brief Stays empty: every byte goes to the canonical writer. */
        System::IO::MemoryStream stream;
    };

    /**
     * @brief Provides an implementation for many of the ContentCompiler methods including
     *        compilation, state tracking for shared resources and creation of the header type
     *        manifest.
     *
     * Derives sharp-runtime's `System::IO::BinaryWriter`, as XNA's derives .NET's, so a type
     * writer's `output.Write(value)` calls are the `BinaryWriter` overloads; every one of them is
     * overridden to write through the one canonical `XnbWriter`, which owns the body buffer, the
     * type-reader table and the shared-resource queue. The base stream is a placeholder that stays
     * empty. Sealed in XNA, `final` here; created by the `ContentCompiler`.
     */
    class ContentWriter final : private ContentWriterPlaceholderStream, public System::IO::BinaryWriter
    {
    public:
        /** @brief .NET full name of this type. */
        CNAEXT static constexpr std::string_view XnaTypeName =
            "Microsoft.Xna.Framework.Content.Pipeline.Serialization.Compiler.ContentWriter";

        /**
         * @brief Creates the writer façade over a canonical file writer.
         *
         * @param compiler The compiler whose type writers resolve nested objects.
         * @param output The canonical writer for the file being produced; must outlive this object.
         * @param targetPlatform The content build target platform.
         * @param targetProfile The target graphics profile.
         * @param outputDirectory Directory the compiled asset is published to, or empty.
         * @param assetName Logical name of the asset being written.
         */
        CNAEXT ContentWriter(const ContentCompiler& compiler, CNA::Internal::Xnb::XnbWriter& output,
                             TargetPlatform targetPlatform,
                             Microsoft::Xna::Framework::Graphics::GraphicsProfile targetProfile,
                             std::filesystem::path outputDirectory, std::string assetName);

        /** @brief Unregisters the façade from its compiler. */
        ~ContentWriter() override;

        ContentWriter(const ContentWriter&) = delete;
        ContentWriter& operator=(const ContentWriter&) = delete;

        /**
         * @brief Gets the content build target platform.
         *
         * @return The platform.
         */
        [[nodiscard]] TargetPlatform getTargetPlatformProperty() const noexcept;

        /**
         * @brief Gets the target graphics profile.
         *
         * @return `Reach` or `HiDef`.
         */
        [[nodiscard]] Microsoft::Xna::Framework::Graphics::GraphicsProfile getTargetProfileProperty() const noexcept;

        void Write(SharpRuntime::bytecs value) override;
        void Write(std::int8_t value) override;
        void Write(SharpRuntime::shortcs value) override;
        void Write(SharpRuntime::ushortcs value) override;
        void Write(SharpRuntime::intcs value) override;
        void Write(std::uint32_t value) override;
        void Write(SharpRuntime::longcs value) override;
        void Write(std::uint64_t value) override;
        void Write(SharpRuntime::Single value) override;
        void Write(double value) override;
        void Write(bool value) override;
        void Write(const std::string& value) override;
        void Write(const SharpRuntime::bytecs* buffer, SharpRuntime::intcs offset, SharpRuntime::intcs count) override;

        /**
         * @brief Writes a single UTF-16 code unit as the .NET `BinaryWriter.Write(char)` does.
         *
         * @param value The character.
         */
        void Write(SharpRuntime::charcs value);

        /**
         * @brief Writes a 32-bit integer in the 7-bit encoded form the format uses for counts.
         *
         * @param value The value.
         */
        void Write7BitEncodedInt(SharpRuntime::intcs value);

        /**
         * @brief Writes a Color value.
         * @param value The value to write.
         */
        void Write(const Microsoft::Xna::Framework::Color& value);

        /**
         * @brief Writes a Matrix value.
         * @param value The value to write.
         */
        void Write(const Microsoft::Xna::Framework::Matrix& value);

        /**
         * @brief Writes a Quaternion value.
         * @param value The value to write.
         */
        void Write(const Microsoft::Xna::Framework::Quaternion& value);

        /**
         * @brief Writes a Vector2 value.
         * @param value The value to write.
         */
        void Write(const Microsoft::Xna::Framework::Vector2& value);

        /**
         * @brief Writes a Vector3 value.
         * @param value The value to write.
         */
        void Write(const Microsoft::Xna::Framework::Vector3& value);

        /**
         * @brief Writes a Vector4 value.
         * @param value The value to write.
         */
        void Write(const Microsoft::Xna::Framework::Vector4& value);

        /** @brief Flushes nothing: the canonical writer buffers the whole file. */
        void Flush() override;

        /** @brief Closes nothing: the canonical writer finishes the file. */
        void Close() override;

        /**
         * @brief Writes the name of an external file to the output binary, relative to the
         *        directory of the asset being written and without the `.xnb` extension.
         *
         * @tparam T The type of the referenced content.
         * @param reference The reference; an empty one writes an empty string.
         */
        template<typename T>
        void WriteExternalReference(const ExternalReference<T>& reference)
        {
            WriteExternalReferenceName(reference.getFilenameProperty());
        }

        /**
         * @brief Writes a single object preceded by a type identifier to the output binary.
         *
         * A reference-typed value is written by the writer of its dynamic type, as in .NET; a
         * null reference writes the null type identifier.
         *
         * @tparam T The type of the value.
         * @param value The value to write.
         */
        template<typename T>
        void WriteObject(const Carrier<T>& value)
        {
            WriteObjectImpl<T>(value, nullptr, false);
        }

        /**
         * @brief Writes a single object to the output binary, using the specified type hint and
         *        writer worker.
         *
         * @tparam T The type of the value.
         * @param value The value to write.
         * @param typeWriter The writer to use.
         */
        template<typename T>
        void WriteObject(const Carrier<T>& value, ContentTypeWriterBase& typeWriter)
        {
            WriteObjectImpl<T>(value, &typeWriter, false);
        }

        /**
         * @brief Writes a single object to the output binary as an instance of the specified type,
         *        without a type identifier.
         *
         * @tparam T The type of the value.
         * @param value The value to write.
         */
        template<typename T>
        void WriteRawObject(const Carrier<T>& value)
        {
            WriteObjectImpl<T>(value, nullptr, true);
        }

        /**
         * @brief Writes a single object to the output binary using the specified writer worker,
         *        without a type identifier.
         *
         * @tparam T The type of the value.
         * @param value The value to write.
         * @param typeWriter The writer to use.
         */
        template<typename T>
        void WriteRawObject(const Carrier<T>& value, ContentTypeWriterBase& typeWriter)
        {
            WriteObjectImpl<T>(value, &typeWriter, true);
        }

        /**
         * @brief Adds a shared reference to the output binary and records the object to be
         *        serialized later, once, however many references name it.
         *
         * @tparam T The type of the value.
         * @param value The value to share; a null reference writes the null reference.
         */
        template<typename T>
        void WriteSharedResource(const Carrier<T>& value)
        {
            if constexpr (std::is_base_of_v<System::Object, T>)
            {
                if (value == nullptr)
                {
                    output_->WriteSharedResourceReference(0);
                    return;
                }
                const auto known = sharedResources_.find(value.get());
                if (known != sharedResources_.end())
                {
                    output_->WriteSharedResourceReference(known->second);
                    return;
                }
                const XnaTypeWorker& worker = RequireWorkerForObject(*value, ContentTypeName<T>::Name());
                const std::int32_t id = worker.AddSharedFromObject(*output_, std::static_pointer_cast<System::Object>(value));
                sharedResources_.emplace(value.get(), id);
                output_->WriteSharedResourceReference(id);
            }
            else
            {
                const XnaTypeWorker* worker = FindWorker(typeid(Carrier<T>));
                std::int32_t id = 0;
                if (worker != nullptr)
                {
                    id = output_->AddSharedResource(worker->Canonical(), std::make_shared<const Carrier<T>>(value));
                }
                else
                {
                    id = output_->AddSharedResource<Carrier<T>>(value);
                }
                output_->WriteSharedResourceReference(id);
            }
        }

        /**
         * @brief Returns the canonical writer this façade writes through.
         *
         * @return The canonical writer.
         */
        CNAEXT [[nodiscard]] CNA::Internal::Xnb::XnbWriter& Output() const noexcept;

    protected:
        /**
         * @brief Releases the façade; the canonical writer is owned by the compiler.
         *
         * @param disposing True when called from Dispose rather than a finalizer.
         */
        void Dispose(bool disposing);

    private:
        template<typename T>
        void WriteObjectImpl(const Carrier<T>& value, ContentTypeWriterBase* hint, const bool raw)
        {
            if (hint != nullptr)
            {
                const XnaTypeWorker& worker = WorkerFor(*hint);
                if constexpr (std::is_base_of_v<System::Object, T>)
                {
                    if (value == nullptr)
                    {
                        if (raw) { throw PipelineException("WriteRawObject: a null reference has no raw form."); }
                        output_->WriteNullObject();
                        return;
                    }
                    worker.WriteFromObject(*output_, std::static_pointer_cast<System::Object>(value), raw);
                }
                else
                {
                    RequireCarrier(worker, typeid(Carrier<T>), ContentTypeName<T>::Name());
                    if (raw) { output_->WriteRawObject(worker.Canonical(), &value); }
                    else { output_->WriteObject(worker.Canonical(), &value); }
                }
                return;
            }
            if constexpr (std::is_base_of_v<System::Object, T>)
            {
                if (value == nullptr)
                {
                    if (raw) { throw PipelineException("WriteRawObject: a null reference has no raw form."); }
                    output_->WriteNullObject();
                    return;
                }
                const XnaTypeWorker& worker = RequireWorkerForObject(*value, ContentTypeName<T>::Name());
                worker.WriteFromObject(*output_, std::static_pointer_cast<System::Object>(value), raw);
            }
            else
            {
                const XnaTypeWorker* worker = FindWorker(typeid(Carrier<T>));
                if (worker != nullptr)
                {
                    if (raw) { output_->WriteRawObject(worker->Canonical(), &value); }
                    else { output_->WriteObject(worker->Canonical(), &value); }
                    return;
                }
                if (raw) { output_->WriteRawObject<Carrier<T>>(value); }
                else { output_->WriteObject<Carrier<T>>(value); }
            }
        }

        [[nodiscard]] const XnaTypeWorker* FindWorker(std::type_index carrier) const;
        [[nodiscard]] const XnaTypeWorker& RequireWorkerForObject(const System::Object& object,
                                                                  const std::string& staticTypeName) const;
        [[nodiscard]] const XnaTypeWorker& WorkerFor(const ContentTypeWriterBase& writer) const;
        static void RequireCarrier(const XnaTypeWorker& worker, std::type_index carrier,
                                   const std::string& typeName);
        void WriteExternalReferenceName(const std::string& filename);

        const ContentCompiler* compiler_;
        CNA::Internal::Xnb::XnbWriter* output_;
        TargetPlatform targetPlatform_;
        Microsoft::Xna::Framework::Graphics::GraphicsProfile targetProfile_;
        std::filesystem::path outputDirectory_;
        std::string assetName_;
        std::map<const void*, std::int32_t> sharedResources_;
        bool disposed_ = false;
    };
}
