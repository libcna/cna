// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/ContentObject.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/ContentTypeName.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/TargetPlatform.hpp"
#include "System/Object.hpp"
#include "System/Type.hpp"

namespace Microsoft::Xna::Framework::Content::Pipeline::Serialization::Compiler
{
    class ContentCompiler;
    class ContentWriter;

    /**
     * @brief Provides methods and properties for compiling a specific managed type into a binary
     *        format.
     *
     * XNA spells the abstract base and the generic base with one name, `ContentTypeWriter` and
     * `ContentTypeWriter<T>`; C++ cannot give a class and a class template the same name, so the
     * non-generic base is `ContentTypeWriterBase`, exactly as CNA's runtime spells
     * `ContentTypeReaderBase` beside `ContentTypeReader<T>`. The `protected internal` members of
     * XNA are `protected` here and reached by the compiler through a friend relationship.
     */
    class ContentTypeWriterBase
    {
    public:
        /** @brief .NET full name of this type. */
        CNAEXT static constexpr std::string_view XnaTypeName =
            "Microsoft.Xna.Framework.Content.Pipeline.Serialization.Compiler.ContentTypeWriter";

        /** @brief Destroys the writer. */
        virtual ~ContentTypeWriterBase() = default;

        /**
         * @brief Determines if deserialization into an existing object is possible.
         *
         * @return True when the runtime reader can fill an existing instance; false by default.
         */
        [[nodiscard]] virtual bool getCanDeserializeIntoExistingObjectProperty() const;

        /**
         * @brief Gets the type handled by this compiler component.
         *
         * @return The target type.
         */
        [[nodiscard]] System::Type getTargetTypeProperty() const noexcept;

        /**
         * @brief Gets a format version number for this type.
         *
         * @return The version written beside the reader name in the type-reader table; 0 by default.
         */
        [[nodiscard]] virtual std::int32_t getTypeVersionProperty() const;

        /**
         * @brief Gets the assembly qualified name of the runtime loader for this type.
         *
         * @param targetPlatform Name of the platform.
         * @return The reader type name, exactly as it is written into the `.xnb` type-reader table.
         */
        [[nodiscard]] virtual std::string GetRuntimeReader(TargetPlatform targetPlatform) const = 0;

        /**
         * @brief Gets the assembly qualified name of the runtime target type.
         *
         * @param targetPlatform Name of the platform.
         * @return The .NET name of the runtime type; `ContentTypeWriter<T>` answers
         *         `ContentTypeName<T>::Name()`.
         */
        [[nodiscard]] virtual std::string GetRuntimeType(TargetPlatform targetPlatform) const;

        /**
         * @brief Invokes the protected `Write` -- the C++ form of XNA's `protected internal`
         *        access, which lets the compiler in the same assembly drive a writer.
         *
         * @param output The content writer serializing the value.
         * @param value The boxed value to write.
         */
        CNAEXT void InvokeWrite(ContentWriter& output, const ContentObject& value) { Write(output, value); }

        /**
         * @brief Invokes the protected `ShouldCompressContent`, for the same reason as `InvokeWrite`.
         *
         * @param targetPlatform The target platform of the content build.
         * @param value The object about to be serialized.
         * @return The writer's answer.
         */
        CNAEXT [[nodiscard]] bool InvokeShouldCompressContent(TargetPlatform targetPlatform, const ContentObject& value) const
        {
            return ShouldCompressContent(targetPlatform, value);
        }

    protected:
        /**
         * @brief Initializes a new instance of the ContentTypeWriter class.
         *
         * @param targetType The type handled by this writer.
         */
        explicit ContentTypeWriterBase(System::Type targetType);

        /**
         * @brief Initializes a writer that also knows its target's .NET name.
         *
         * @param targetType The type handled by this writer.
         * @param runtimeTypeName The .NET full name `GetRuntimeType` answers by default.
         */
        CNAEXT ContentTypeWriterBase(System::Type targetType, std::string runtimeTypeName);

        /**
         * @brief Retrieves and caches any nested type writers; called once by the compiler when
         *        the writer is added.
         *
         * @param compiler The content compiler.
         */
        virtual void Initialize(ContentCompiler& compiler);

        /**
         * @brief Indicates whether a given type of content should be compressed.
         *
         * @param targetPlatform The target platform of the content build.
         * @param value The object about to be serialized.
         * @return True (the default) to allow compression; a writer returns false for content that
         *         does not compress well.
         */
        [[nodiscard]] virtual bool ShouldCompressContent(TargetPlatform targetPlatform,
                                                         const ContentObject& value) const;

        /**
         * @brief Compiles an object into binary format.
         *
         * @param output The content writer serializing the value.
         * @param value The boxed value to write.
         */
        virtual void Write(ContentWriter& output, const ContentObject& value) = 0;

    private:
        friend class ContentCompiler;
        friend class ContentWriter;
        System::Type targetType_;
        std::string runtimeTypeName_;
    };

    /**
     * @brief Provides a generic implementation of ContentTypeWriter methods and properties for
     *        compiling a specific managed type into a binary format.
     *
     * Derive from this class, override `GetRuntimeReader` and the typed `Write`, and add the class
     * to a `ContentCompiler` with `AddTypeWriter<TWriter>()`. A reference-typed @p T arrives as
     * `std::shared_ptr<T>`, a value type by value (docs/xna-content-pipeline-compat-api.md §2).
     *
     * @tparam T The type handled by this writer.
     */
    template<typename T>
    class ContentTypeWriter : public ContentTypeWriterBase
    {
    public:
        /** @brief The content type this writer handles, for the compiler's registration template. */
        using TargetContentType = T;

        /** @brief The carrier the typed `Write` receives. */
        using TargetCarrier = Carrier<T>;

        /**
         * @brief Gets the assembly qualified name of the runtime target type: `ContentTypeName<T>`.
         *
         * @param targetPlatform Name of the platform.
         * @return The .NET full name of @p T.
         */
        [[nodiscard]] std::string GetRuntimeType(TargetPlatform targetPlatform) const override
        {
            (void)targetPlatform;
            return ContentTypeName<T>::Name();
        }

    protected:
        /** @brief Initializes a new instance of the ContentTypeWriter class. */
        ContentTypeWriter() : ContentTypeWriterBase(System::Type::From<T>(), ContentTypeName<T>::Name()) {}

        /**
         * @brief Compiles a strongly typed object into binary format.
         *
         * @param output The content writer serializing the value.
         * @param value The value to write.
         */
        virtual void Write(ContentWriter& output, const TargetCarrier& value) = 0;

        /**
         * @brief Unboxes the object and forwards to the typed `Write`.
         *
         * @param output The content writer serializing the value.
         * @param value The boxed value.
         */
        void Write(ContentWriter& output, const ContentObject& value) override
        {
            Write(output, Unbox<T>(value));
        }
    };
}
