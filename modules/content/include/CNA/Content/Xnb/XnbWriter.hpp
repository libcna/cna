// SPDX-License-Identifier: MS-PL
#pragma once

#include <any>
#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <vector>

#include "CNA/Content/Xnb/XnbByteWriter.hpp"
#include "CNA/Content/Xnb/XnbFileOptions.hpp"
#include "CNA/Content/Xnb/XnbTypeWriter.hpp"

namespace CNA::Content::Xnb
{
    /**
     * @brief Serializes an object graph into a complete `.xnb` container
     *        (plans/plan_xnapipeline.md `XNAP-005`).
     *
     * The CNA equivalent of XNA's `ContentWriter`, and the exact counterpart of
     * `Microsoft::Xna::Framework::Content::ContentReader`. It owns the three pieces of
     * bookkeeping the format needs and a type writer must not have to think about:
     *
     * - the **type-reader table**, built in first-use order as writers are dispatched to, and
     *   emitted ahead of the body once the body is complete;
     * - the **shared-resource table**, whose entries are serialized after the root object and
     *   referenced by 1-based index, which is how the format expresses arbitrary graphs;
     * - the **container header**, whose declared total size is patched once everything else is
     *   known.
     *
     * Emission is two-pass by necessity: the type table precedes the body but is only fully known
     * after the body has been written, so the body goes into its own buffer first. That also
     * makes the table's order a pure function of the object graph, which is what lets this class
     * promise byte-identical output for an identical input.
     *
     * A writer instance is single-threaded and single-use. Concurrency lives one level up: a
     * frozen `XnbTypeWriterRegistry` may back any number of concurrent `XnbWriter` instances.
     */
    class XnbWriter
    {
    public:
        /**
         * @brief Creates a writer bound to a frozen registry and a validated container shape.
         *
         * @param registry Type writers to dispatch to; must outlive this writer and is frozen here.
         * @param options Container description, validated before construction completes.
         * @param limits Bounds every count-driven write consults.
         * @throws XnbWriteException for an invalid container description.
         */
        XnbWriter(const XnbTypeWriterRegistry& registry, const XnbFileOptions& options,
                  const XnbWriteLimits& limits = DefaultXnbWriteLimits());

        /** @brief Returns the container description this writer is producing. */
        [[nodiscard]] const XnbFileOptions& Options() const noexcept { return options_; }

        /** @brief Returns the bounds this writer enforces. */
        [[nodiscard]] const XnbWriteLimits& Limits() const noexcept { return limits_; }

        // -- Primitive payload writes, forwarded to the body buffer --

        /** @brief Appends one raw byte. */
        void WriteByte(std::uint8_t value);

        /** @brief Appends one signed byte. */
        void WriteSByte(std::int8_t value);

        /** @brief Appends a little-endian signed 16-bit integer. */
        void WriteInt16(std::int16_t value);

        /** @brief Appends a little-endian unsigned 16-bit integer. */
        void WriteUInt16(std::uint16_t value);

        /** @brief Appends a little-endian signed 32-bit integer. */
        void WriteInt32(std::int32_t value);

        /** @brief Appends a little-endian unsigned 32-bit integer. */
        void WriteUInt32(std::uint32_t value);

        /** @brief Appends a little-endian signed 64-bit integer. */
        void WriteInt64(std::int64_t value);

        /** @brief Appends a little-endian unsigned 64-bit integer. */
        void WriteUInt64(std::uint64_t value);

        /** @brief Appends a little-endian IEEE-754 binary32 value. */
        void WriteSingle(float value);

        /** @brief Appends a little-endian IEEE-754 binary64 value. */
        void WriteDouble(double value);

        /** @brief Appends one byte, `1` for true and `0` for false. */
        void WriteBoolean(bool value);

        /** @brief Appends a variable-length 7-bit encoded 32-bit integer. */
        void Write7BitEncodedInt(std::int32_t value);

        /** @brief Appends one UTF-16 code unit as a UTF-8 encoded character. */
        void WriteChar(char16_t value);

        /** @brief Appends a 7-bit encoded UTF-8 byte count followed by the string's bytes. */
        void WriteString(const std::string& value);

        /** @brief Appends raw bytes verbatim, with no length prefix. */
        void WriteBytes(std::span<const std::uint8_t> bytes);

        /**
         * @brief Appends a validated collection element count.
         *
         * The format stores array, list and dictionary counts as a `UInt32`, not a 7-bit encoded
         * integer. The count is checked against `XnbWriteLimits::maxCollectionElementCount` first,
         * so this writer never produces a collection its own reader would refuse.
         *
         * @param count Element count.
         * @param context Component name used verbatim in a failure message.
         * @throws XnbWriteException when @p count exceeds the configured maximum.
         */
        void WriteCollectionCount(std::size_t count, const std::string& context);

        // -- Object graph --

        /**
         * @brief Writes a null reference in the polymorphic form (type identifier zero).
         */
        void WriteNullObject();

        /**
         * @brief Writes a value in the polymorphic form: a type identifier, then the payload.
         *
         * An empty `std::any` is this API's spelling of .NET `null` and produces the format's null
         * reference; a value type refuses it, because a value type has no null form.
         *
         * @param targetTypeName Serialized .NET type selecting the writer.
         * @param value The value to serialize, or an empty `std::any` for null.
         * @throws XnbWriteException for an unregistered type, a null value type, or an over-deep
         *         graph.
         */
        void WriteObject(const std::string& targetTypeName, const std::any& value);

        /**
         * @brief Writes a value in the raw form: the payload alone, with no type identifier.
         *
         * @param targetTypeName Serialized .NET type selecting the writer.
         * @param value The value to serialize.
         * @throws XnbWriteException for an unregistered type or an over-deep graph.
         */
        void WriteRawObject(const std::string& targetTypeName, const std::any& value);

        /**
         * @brief Writes a value in the format's `Object? T` form.
         *
         * Raw when the selected writer reports a value type, polymorphic when it reports a
         * reference type. This is the form every collection element and most struct fields use.
         * An empty `std::any` writes the null reference, exactly as WriteObject() does.
         *
         * @param targetTypeName Serialized .NET type selecting the writer.
         * @param value The value to serialize, or an empty `std::any` for null.
         * @throws XnbWriteException for an unregistered type, a null value type, or an over-deep
         *         graph.
         */
        void WriteValueOrObject(const std::string& targetTypeName, const std::any& value);

        /** @brief Writes a typed value in the polymorphic form, resolving `XnbTypeKey<T>`. */
        template <typename T>
        void WriteObject(const T& value)
        {
            WriteObject(XnbTypeKey<T>::Name(), std::any(value));
        }

        /** @brief Writes a typed value in the raw form, resolving `XnbTypeKey<T>`. */
        template <typename T>
        void WriteRawObject(const T& value)
        {
            WriteRawObject(XnbTypeKey<T>::Name(), std::any(value));
        }

        /** @brief Writes a typed value in the `Object? T` form, resolving `XnbTypeKey<T>`. */
        template <typename T>
        void WriteValueOrObject(const T& value)
        {
            WriteValueOrObject(XnbTypeKey<T>::Name(), std::any(value));
        }

        /**
         * @brief Reserves a shared-resource slot for a value, or returns the existing slot.
         *
         * Shared resources are how the format expresses a graph rather than a tree: the value is
         * serialized once, after the root object, and referenced by index everywhere it appears.
         *
         * XNA deduplicates by .NET object identity. C++ values are copied rather than referenced,
         * so identity is supplied explicitly by the caller as @p key: two registrations sharing a
         * key are the same resource and are serialized once. A key is scoped to this writer, so
         * any stable spelling works (`"vertexBuffer:0"`, a content name, a hash).
         *
         * @param key Caller-chosen stable identity for the resource.
         * @param targetTypeName Serialized .NET type selecting the writer.
         * @param value The value to serialize once, later.
         * @return The 1-based shared-resource identifier written on the wire.
         * @throws XnbWriteException when the shared-resource limit is exceeded, or when @p key is
         *         reused for a different serialized type.
         */
        std::int32_t RegisterSharedResource(const std::string& key,
                                            const std::string& targetTypeName,
                                            const std::any& value);

        /**
         * @brief Writes a shared-resource reference: its 1-based identifier, or zero for null.
         *
         * @param resourceId Identifier from RegisterSharedResource(), or zero for a null reference.
         * @throws XnbWriteException for an identifier no registration produced.
         */
        void WriteSharedResourceReference(std::int32_t resourceId);

        /**
         * @brief Registers a shared resource and immediately writes its reference.
         *
         * @param key Caller-chosen stable identity for the resource.
         * @param targetTypeName Serialized .NET type selecting the writer.
         * @param value The value to serialize once, later.
         */
        void WriteSharedResource(const std::string& key, const std::string& targetTypeName,
                                 const std::any& value);

        /** @brief Writes a null shared-resource reference. */
        void WriteNullSharedResource();

        /**
         * @brief Writes an external reference: a sibling asset name with no `.xnb` extension.
         *
         * An empty name is the format's null external reference. The name is written verbatim as
         * a raw string, matching the `ExternalReferenceReader` payload, and must use `/`
         * separators and no extension.
         *
         * @param assetName Asset name relative to the referring asset.
         * @throws XnbWriteException when @p assetName carries an `.xnb` extension or a backslash.
         */
        void WriteExternalReference(const std::string& assetName);

        /**
         * @brief Serializes the root object and every shared resource, then returns the file.
         *
         * Single-use: the writer is exhausted afterwards and must not be reused.
         *
         * @param rootTypeName Serialized .NET type of the root asset.
         * @param rootValue The root asset value.
         * @return The complete `.xnb` file image.
         * @throws XnbWriteException for an unregistered type, an over-deep or over-large graph,
         *         or a file that would exceed the configured maximum size.
         */
        [[nodiscard]] std::vector<std::uint8_t> WriteAsset(const std::string& rootTypeName,
                                                           const std::any& rootValue);

        /**
         * @brief Returns the runtime reader names recorded so far, in table order.
         *
         * Exposed for diagnostics and conformance tests, which need to assert exactly which
         * readers a produced file demands.
         *
         * @return The reader names, index 0 being table entry 1.
         */
        [[nodiscard]] const std::vector<std::string>& TypeReaderNames() const noexcept;

    private:
        struct TypeTableEntry
        {
            std::string readerName;
            std::int32_t version = 0;
        };

        struct PendingSharedResource
        {
            std::string targetTypeName;
            std::any value;
        };

        class DepthGuard;

        [[nodiscard]] std::int32_t TypeIdFor(const XnbTypeWriter& writer);
        [[nodiscard]] std::shared_ptr<const XnbTypeWriter> ResolveWriter(
            const std::string& targetTypeName) const;
        void WritePayload(const XnbTypeWriter& writer, const std::any& value);

        const XnbTypeWriterRegistry& registry_;
        XnbFileOptions options_{};
        XnbWriteLimits limits_{};
        XnbByteWriter body_;
        std::vector<TypeTableEntry> typeTable_;
        std::vector<std::string> typeReaderNames_;
        std::map<std::string, std::int32_t> typeTableIndex_;
        std::vector<PendingSharedResource> sharedResources_;
        std::map<std::string, std::int32_t> sharedResourceIndex_;
        std::int32_t depth_ = 0;
        bool finished_ = false;
    };

    /**
     * @brief Serializes one root asset into a complete `.xnb` file image.
     *
     * The single call most producers need. It freezes @p registry, validates @p options, writes
     * the graph and returns the bytes.
     *
     * @param registry Type writers to dispatch to.
     * @param options Container description.
     * @param rootTypeName Serialized .NET type of the root asset.
     * @param rootValue The root asset value.
     * @param limits Bounds every count-driven write consults.
     * @return The complete `.xnb` file image.
     * @throws XnbWriteException on any write-side failure.
     */
    [[nodiscard]] std::vector<std::uint8_t> WriteXnbFile(
        const XnbTypeWriterRegistry& registry, const XnbFileOptions& options,
        const std::string& rootTypeName, const std::any& rootValue,
        const XnbWriteLimits& limits = DefaultXnbWriteLimits());

    /**
     * @brief Serializes one typed root asset into a complete `.xnb` file image.
     *
     * @tparam T Root asset type; `XnbTypeKey<T>` supplies its serialized type name.
     * @param registry Type writers to dispatch to.
     * @param options Container description.
     * @param rootValue The root asset value.
     * @param limits Bounds every count-driven write consults.
     * @return The complete `.xnb` file image.
     */
    template <typename T>
    [[nodiscard]] std::vector<std::uint8_t> WriteXnbFile(
        const XnbTypeWriterRegistry& registry, const XnbFileOptions& options, const T& rootValue,
        const XnbWriteLimits& limits = DefaultXnbWriteLimits())
    {
        return WriteXnbFile(registry, options, XnbTypeKey<T>::Name(), std::any(rootValue), limits);
    }
}
