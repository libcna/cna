// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "CNA/Internal/Xnb/XnbByteWriter.hpp"
#include "CNA/Internal/Xnb/XnbFileOptions.hpp"
#include "CNA/Internal/Xnb/XnbTypeWriter.hpp"

namespace Microsoft::Xna::Framework
{
    struct BoundingSphere;
    struct Color;
    struct Matrix;
    struct Quaternion;
    struct Rectangle;
    struct Vector2;
    struct Vector3;
    struct Vector4;
}

namespace CNA::Internal::Xnb
{
    /**
     * @brief The object-graph writer passed to every @ref XnbTypeWriter (the exact counterpart of
     *        `Microsoft::Xna::Framework::Content::ContentReader`, plans/plan_xnapipeline.md
     *        `XNAP-14`).
     *
     * Owns the three pieces of per-file state a `.xnb` object graph needs:
     *
     * 1. The **type-reader table**, interned in first-use order. `WriteObject()` emits a
     *    one-based index into it; `WriteRawObject()` interns without emitting one, which is how a
     *    reader such as `VertexDeclarationReader` legitimately appears in the table of a file that
     *    never dispatches to it.
     * 2. The **shared-resource list**. `AddSharedResource()` enqueues a value and returns its
     *    one-based identifier; the values are serialized after the root object, and a resource may
     *    itself enqueue more.
     * 3. The **body buffer**, so the header and the table can be prepended once their contents are
     *    final.
     *
     * One instance serves exactly one file and is not thread-safe; the registry it borrows is.
     */
    class XnbWriter
    {
    public:
        /**
         * @brief Creates a writer for one file.
         *
         * @param registry Frozen type-writer registry that must outlive this writer.
         * @param options Container-level configuration, already validated by the caller or
         *                validated here.
         * @param assetName Logical asset name used in diagnostics only; never serialized.
         * @throws XnbWriteException for an unsupported option combination.
         */
        XnbWriter(const XnbTypeWriterRegistry& registry, XnbFileOptions options,
                  std::string assetName = {});

        /** @brief The writer owns per-file state and cannot be copied. */
        XnbWriter(const XnbWriter&) = delete;

        /** @brief The writer owns per-file state and cannot be assigned. */
        XnbWriter& operator=(const XnbWriter&) = delete;

        /** @brief Returns the container-level configuration in force for this file. */
        [[nodiscard]] const XnbFileOptions& Options() const noexcept;

        /** @brief Returns the logical asset name used in diagnostics. */
        [[nodiscard]] const std::string& AssetName() const noexcept;

        /** @brief Returns the container version byte, so a writer can emit legacy field forms. */
        [[nodiscard]] int Version() const noexcept;

        /** @brief Returns the target platform byte, so a writer can apply platform field rules. */
        [[nodiscard]] char Platform() const noexcept;

        // -- payload primitives, mirroring System::IO::BinaryReader as the XNB readers use it --

        /** @brief Appends one unsigned byte. @param value The byte to append. */
        void WriteByte(std::uint8_t value);

        /** @brief Appends one signed byte. @param value The value to append. */
        void WriteSByte(std::int8_t value);

        /** @brief Appends a one-byte boolean. @param value The value to append. */
        void WriteBoolean(bool value);

        /** @brief Appends a little-endian `Int16`. @param value The value to append. */
        void WriteInt16(std::int16_t value);

        /** @brief Appends a little-endian `UInt16`. @param value The value to append. */
        void WriteUInt16(std::uint16_t value);

        /** @brief Appends a little-endian `Int32`. @param value The value to append. */
        void WriteInt32(std::int32_t value);

        /** @brief Appends a little-endian `UInt32`. @param value The value to append. */
        void WriteUInt32(std::uint32_t value);

        /** @brief Appends a little-endian `Int64`. @param value The value to append. */
        void WriteInt64(std::int64_t value);

        /** @brief Appends a little-endian `UInt64`. @param value The value to append. */
        void WriteUInt64(std::uint64_t value);

        /** @brief Appends a little-endian `Single`. @param value The value to append. */
        void WriteSingle(float value);

        /** @brief Appends a little-endian `Double`. @param value The value to append. */
        void WriteDouble(double value);

        /** @brief Appends a 7-bit-encoded integer. @param value The value to append. */
        void Write7BitEncodedInt(std::int32_t value);

        /**
         * @brief Appends a length-prefixed UTF-8 string.
         *
         * @param value Well-formed UTF-8 text within the configured string limit.
         */
        void WriteString(const std::string& value);

        /** @brief Appends one UTF-8-encoded `System.Char`. @param value The code unit. */
        void WriteChar(SharpRuntime::charcs value);

        /** @brief Appends raw bytes verbatim. @param bytes The bytes to append. */
        void WriteBytes(std::span<const std::uint8_t> bytes);

        /**
         * @brief Appends an `Int32` byte count followed by exactly that many bytes.
         *
         * This is the shape every length-prefixed blob in the format uses (texture level bytes,
         * audio samples, vertex/index payloads, effect bytecode).
         *
         * @param bytes The payload to append.
         * @throws XnbWriteException if the payload exceeds `Int32` length or the payload limit.
         */
        void WriteLengthPrefixedBytes(std::span<const std::uint8_t> bytes);

        // -- framework value types, mirroring ContentReader's own convenience readers --

        /** @brief Appends a `Vector2` as two `Single` values. @param value The value to append. */
        void WriteVector2(const Microsoft::Xna::Framework::Vector2& value);

        /** @brief Appends a `Vector3` as three `Single` values. @param value The value to append. */
        void WriteVector3(const Microsoft::Xna::Framework::Vector3& value);

        /** @brief Appends a `Vector4` as four `Single` values. @param value The value to append. */
        void WriteVector4(const Microsoft::Xna::Framework::Vector4& value);

        /** @brief Appends a `Matrix` in M11..M44 order. @param value The value to append. */
        void WriteMatrix(const Microsoft::Xna::Framework::Matrix& value);

        /** @brief Appends a `Quaternion` as X, Y, Z, W. @param value The value to append. */
        void WriteQuaternion(const Microsoft::Xna::Framework::Quaternion& value);

        /** @brief Appends a `Color` as four packed bytes in R, G, B, A order. @param value The value. */
        void WriteColor(const Microsoft::Xna::Framework::Color& value);

        /** @brief Appends a `Rectangle` as X, Y, Width, Height. @param value The value to append. */
        void WriteRectangle(const Microsoft::Xna::Framework::Rectangle& value);

        /** @brief Appends a `BoundingSphere` as centre XYZ then radius. @param value The value. */
        void WriteBoundingSphere(const Microsoft::Xna::Framework::BoundingSphere& value);

        /**
         * @brief Appends an external asset reference: a relative logical path, or the empty string
         *        for "no reference".
         *
         * @param relativePath Path relative to this asset, using `/` separators, or empty.
         * @throws XnbWriteException for an absolute path or one that escapes the content root.
         */
        void WriteExternalReference(const std::string& relativePath);

        // -- object graph --

        /**
         * @brief Rejects a collection element count above the configured maximum.
         *
         * @param count The element count about to be written.
         * @param readerName Writer name used in the failure message.
         * @throws XnbWriteException when @p count exceeds the configured maximum or `Int32`.
         */
        void RequireCollectionCount(std::size_t count, const std::string& readerName) const;

        /**
         * @brief Refuses to write a payload whose byte order this build does not produce
         *        (plans/plan_xnapipeline.md `XNAP-82`).
         *
         * The Xbox 360 is big-endian. CNA has one piece of Xbox-specific payload handling -- the
         * `SoundEffect` WAVEFORMATEX byte swap -- and nothing else, so an `x`-targeted file is
         * Windows-layout bytes under an Xbox header unless a writer has explicitly done the work.
         * Every asset writer calls this, and the one that has done the work says so by passing
         * `true`. `XnbFileOptions::allowUnverifiedXboxPayloads` overrides it for somebody with
         * real hardware to test against.
         *
         * @param readerName Writer name used in the failure message.
         * @param handlesXboxByteOrder Whether this writer implements the target's byte order.
         * @throws XnbWriteException when the target is Xbox 360 and neither condition holds.
         */
        void RequireVerifiedPlatformPayload(const std::string& readerName,
                                            bool handlesXboxByteOrder = false) const;

        /**
         * @brief Interns one reader identity into the type-reader table.
         *
         * @param writer The type writer whose reader must appear in the table.
         * @return The one-based table index.
         * @throws XnbWriteException if the table would exceed the configured maximum.
         */
        std::int32_t InternTypeWriter(const XnbTypeWriterBase& writer);

        /**
         * @brief Writes a one-based dispatch index followed by the value's payload.
         *
         * @tparam T The exact type to serialize; a writer for it must be registered.
         * @param value The value to serialize.
         * @throws XnbWriteException for an unregistered type or excessive nesting.
         */
        template<typename T>
        void WriteObject(const T& value)
        {
            const XnbTypeWriterBase& writer = RequireWriter<T>();
            Write7BitEncodedInt(InternTypeWriter(writer));
            WriteNested(writer, &value);
        }

        /**
         * @brief Writes a value's payload with no dispatch index, still interning its reader.
         *
         * This is how a nested value whose reader the consuming reader already knows is written
         * (a `VertexDeclaration` inside a `VertexBuffer`, a value-typed collection element).
         *
         * @tparam T The exact type to serialize; a writer for it must be registered.
         * @param value The value to serialize.
         * @throws XnbWriteException for an unregistered type or excessive nesting.
         */
        template<typename T>
        void WriteRawObject(const T& value)
        {
            const XnbTypeWriterBase& writer = RequireWriter<T>();
            InternTypeWriter(writer);
            WriteNested(writer, &value);
        }

        /** @brief Writes dispatch index zero, the encoding of a null reference. */
        void WriteNullObject();

        /**
         * @brief Enqueues one value as a shared resource, serialized after the root object.
         *
         * @tparam T The exact type to serialize; a writer for it must be registered.
         * @param value The value to enqueue; it is copied and kept alive until the file is final.
         * @return The one-based shared-resource identifier.
         * @throws XnbWriteException for an unregistered type or an excessive resource count.
         */
        template<typename T>
        [[nodiscard]] std::int32_t AddSharedResource(T value)
        {
            const XnbTypeWriterBase& writer = RequireWriter<T>();
            auto owned = std::make_shared<const T>(std::move(value));
            const void* pointer = owned.get();
            return EnqueueSharedResource(writer, pointer, std::move(owned));
        }

        /**
         * @brief Writes a one-based shared-resource reference, or zero for "no reference".
         *
         * @param sharedResourceId Identifier returned by @ref AddSharedResource, or zero.
         * @throws XnbWriteException for a negative or not-yet-issued identifier.
         */
        void WriteSharedResourceReference(std::int32_t sharedResourceId);

        /**
         * @brief Serializes the root object, then every shared resource, and returns the file.
         *
         * @tparam T The root type; a writer for it must be registered.
         * @param root The root value.
         * @return The complete `.xnb` file image.
         * @throws XnbWriteException for an unregistered type, an exceeded limit, or an
         *         unimplemented compression selection.
         */
        template<typename T>
        [[nodiscard]] std::vector<std::uint8_t> WriteAsset(const T& root)
        {
            WriteObject(root);
            return Finish();
        }

        /**
         * @brief Serializes every pending shared resource and assembles the complete file.
         *
         * Call this after the root object has been written through @ref WriteObject. Separated
         * from @ref WriteAsset so a caller that must write the root through a specific writer can
         * still finish the file.
         *
         * @return The complete `.xnb` file image.
         * @throws XnbWriteException for an exceeded limit or an unimplemented compression.
         */
        [[nodiscard]] std::vector<std::uint8_t> Finish();

    private:
        struct SharedResourceEntry
        {
            const XnbTypeWriterBase* writer = nullptr;
            const void* value = nullptr;
            std::shared_ptr<const void> owner;
        };

        struct TypeTableEntry
        {
            std::string name;
            std::int32_t readerVersion = 0;
        };

        template<typename T>
        [[nodiscard]] const XnbTypeWriterBase& RequireWriter() const
        {
            // Deliberately not typeid(T).name(): the registry is RTTI-free by contract, and a
            // mangled name is worse guidance than naming where the unwritable value appeared.
            return registry_->Require(XnbTypeKey<T>::Id(), MissingWriterContext());
        }

        [[nodiscard]] std::string MissingWriterContext() const;

        void WriteNested(const XnbTypeWriterBase& writer, const void* value);

        std::int32_t EnqueueSharedResource(const XnbTypeWriterBase& writer, const void* value,
                                           std::shared_ptr<const void> owner);

        const XnbTypeWriterRegistry* registry_ = nullptr;
        XnbFileOptions options_{};
        std::string assetName_;
        XnbByteWriter body_;
        std::vector<TypeTableEntry> typeTable_;
        std::map<std::string, std::int32_t> typeTableIndices_;
        std::vector<SharedResourceEntry> sharedResources_;
        std::string currentContext_ = "the root object";
        std::int32_t nestingDepth_ = 0;
        bool finished_ = false;
    };
}
