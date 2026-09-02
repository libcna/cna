// SPDX-License-Identifier: MS-PL
#include "CNA/Content/Xnb/XnbWriter.hpp"

#include <limits>
#include <utility>

namespace CNA::Content::Xnb
{
    /**
     * @brief Increments the writer's nesting depth for the lifetime of one dispatched payload.
     *
     * A content graph is author-supplied and a build tool is routinely pointed at generated
     * content, so an accidentally cyclic non-shared reference must terminate as a diagnosable
     * refusal rather than as a stack overflow.
     */
    class XnbWriter::DepthGuard
    {
    public:
        explicit DepthGuard(XnbWriter& writer)
            : writer_(writer)
        {
            if (writer_.depth_ >= writer_.limits_.maxObjectNestingDepth)
            {
                throw XnbWriteException(
                    "XnbWriter: the object graph nests deeper than the maximum of " +
                    std::to_string(writer_.limits_.maxObjectNestingDepth) +
                    " levels; a cycle that is not expressed as a shared resource cannot be "
                    "serialized.");
            }
            ++writer_.depth_;
        }

        ~DepthGuard() { --writer_.depth_; }

        DepthGuard(const DepthGuard&) = delete;
        DepthGuard& operator=(const DepthGuard&) = delete;

    private:
        XnbWriter& writer_;
    };

    XnbWriter::XnbWriter(const XnbTypeWriterRegistry& registry, const XnbFileOptions& options,
                         const XnbWriteLimits& limits)
        : registry_(registry), options_(options), limits_(limits), body_(limits)
    {
        ValidateXnbFileOptions(options_);
        registry_.Freeze();
    }

    void XnbWriter::WriteByte(const std::uint8_t value) { body_.WriteByte(value); }
    void XnbWriter::WriteSByte(const std::int8_t value) { body_.WriteSByte(value); }
    void XnbWriter::WriteInt16(const std::int16_t value) { body_.WriteInt16(value); }
    void XnbWriter::WriteUInt16(const std::uint16_t value) { body_.WriteUInt16(value); }
    void XnbWriter::WriteInt32(const std::int32_t value) { body_.WriteInt32(value); }
    void XnbWriter::WriteUInt32(const std::uint32_t value) { body_.WriteUInt32(value); }
    void XnbWriter::WriteInt64(const std::int64_t value) { body_.WriteInt64(value); }
    void XnbWriter::WriteUInt64(const std::uint64_t value) { body_.WriteUInt64(value); }
    void XnbWriter::WriteSingle(const float value) { body_.WriteSingle(value); }
    void XnbWriter::WriteDouble(const double value) { body_.WriteDouble(value); }
    void XnbWriter::WriteBoolean(const bool value) { body_.WriteBoolean(value); }
    void XnbWriter::Write7BitEncodedInt(const std::int32_t value) { body_.Write7BitEncodedInt(value); }
    void XnbWriter::WriteChar(const char16_t value) { body_.WriteChar(value); }
    void XnbWriter::WriteString(const std::string& value) { body_.WriteString(value); }
    void XnbWriter::WriteBytes(const std::span<const std::uint8_t> bytes) { body_.WriteBytes(bytes); }

    void XnbWriter::WriteCollectionCount(const std::size_t count, const std::string& context)
    {
        if (count > static_cast<std::size_t>(limits_.maxCollectionElementCount))
        {
            throw XnbWriteException(
                context + ": a collection of " + std::to_string(count) +
                " elements exceeds the maximum of " +
                std::to_string(limits_.maxCollectionElementCount) + ".");
        }
        body_.WriteUInt32(static_cast<std::uint32_t>(count));
    }

    std::shared_ptr<const XnbTypeWriter> XnbWriter::ResolveWriter(
        const std::string& targetTypeName) const
    {
        return registry_.Resolve(targetTypeName);
    }

    std::int32_t XnbWriter::TypeIdFor(const XnbTypeWriter& writer)
    {
        const std::string readerName = writer.RuntimeReaderName();
        const auto existing = typeTableIndex_.find(readerName);
        if (existing != typeTableIndex_.end()) { return existing->second; }

        if (static_cast<std::int64_t>(typeTable_.size()) >= limits_.maxTypeWriterCount)
        {
            throw XnbWriteException(
                "XnbWriter: the type-reader table would exceed the maximum of " +
                std::to_string(limits_.maxTypeWriterCount) + " entries.");
        }

        typeTable_.push_back(TypeTableEntry{readerName, writer.TypeVersion()});
        typeReaderNames_.push_back(readerName);
        const auto id = static_cast<std::int32_t>(typeTable_.size());
        typeTableIndex_.emplace(readerName, id);
        return id;
    }

    void XnbWriter::WritePayload(const XnbTypeWriter& writer, const std::any& value)
    {
        const DepthGuard guard(*this);
        writer.Write(*this, value);
    }

    void XnbWriter::WriteNullObject()
    {
        body_.Write7BitEncodedInt(0);
    }

    void XnbWriter::WriteObject(const std::string& targetTypeName, const std::any& value)
    {
        const auto writer = ResolveWriter(targetTypeName);
        if (!value.has_value())
        {
            // An empty std::any is this API's spelling of .NET null. Only a reference type can be
            // null, so a value type says so rather than silently writing a zero identifier the
            // reader would then interpret as a missing required field.
            if (writer->IsValueType())
            {
                throw XnbWriteException(
                    "'" + targetTypeName + "' is a value type and cannot be written as null.");
            }
            (void)TypeIdFor(*writer);
            WriteNullObject();
            return;
        }
        body_.Write7BitEncodedInt(TypeIdFor(*writer));
        WritePayload(*writer, value);
    }

    void XnbWriter::WriteRawObject(const std::string& targetTypeName, const std::any& value)
    {
        const auto writer = ResolveWriter(targetTypeName);
        // A raw value carries no type identifier, but its reader must still be resolvable when
        // the file is loaded, because a nested field may dispatch to it. Registering the table
        // entry here keeps the file self-consistent, exactly as XNA's own writer does.
        (void)TypeIdFor(*writer);
        WritePayload(*writer, value);
    }

    void XnbWriter::WriteValueOrObject(const std::string& targetTypeName, const std::any& value)
    {
        const auto writer = ResolveWriter(targetTypeName);
        if (!value.has_value())
        {
            if (writer->IsValueType())
            {
                throw XnbWriteException(
                    "'" + targetTypeName + "' is a value type and cannot be written as null.");
            }
            (void)TypeIdFor(*writer);
            WriteNullObject();
            return;
        }
        if (writer->IsValueType())
        {
            (void)TypeIdFor(*writer);
            WritePayload(*writer, value);
            return;
        }
        body_.Write7BitEncodedInt(TypeIdFor(*writer));
        WritePayload(*writer, value);
    }

    std::int32_t XnbWriter::RegisterSharedResource(const std::string& key,
                                                   const std::string& targetTypeName,
                                                   const std::any& value)
    {
        if (key.empty())
        {
            throw XnbWriteException(
                "XnbWriter::RegisterSharedResource(): the resource key must not be empty.");
        }

        const auto existing = sharedResourceIndex_.find(key);
        if (existing != sharedResourceIndex_.end())
        {
            const auto& registered =
                sharedResources_[static_cast<std::size_t>(existing->second - 1)];
            if (registered.targetTypeName != targetTypeName)
            {
                throw XnbWriteException(
                    "XnbWriter::RegisterSharedResource(): key '" + key + "' was registered as '" +
                    registered.targetTypeName + "' and cannot be reused for '" + targetTypeName +
                    "'.");
            }
            return existing->second;
        }

        if (static_cast<std::int64_t>(sharedResources_.size()) >= limits_.maxSharedResourceCount)
        {
            throw XnbWriteException(
                "XnbWriter: the shared-resource table would exceed the maximum of " +
                std::to_string(limits_.maxSharedResourceCount) + " entries.");
        }

        // Resolve now rather than at drain time, so an unregistered type fails while the caller's
        // own context is still on the stack.
        (void)ResolveWriter(targetTypeName);
        sharedResources_.push_back(PendingSharedResource{targetTypeName, value});
        const auto id = static_cast<std::int32_t>(sharedResources_.size());
        sharedResourceIndex_.emplace(key, id);
        return id;
    }

    void XnbWriter::WriteSharedResourceReference(const std::int32_t resourceId)
    {
        if (resourceId < 0 ||
            static_cast<std::size_t>(resourceId) > sharedResources_.size())
        {
            throw XnbWriteException(
                "XnbWriter::WriteSharedResourceReference(): " + std::to_string(resourceId) +
                " is not a registered shared-resource identifier.");
        }
        body_.Write7BitEncodedInt(resourceId);
    }

    void XnbWriter::WriteSharedResource(const std::string& key, const std::string& targetTypeName,
                                        const std::any& value)
    {
        WriteSharedResourceReference(RegisterSharedResource(key, targetTypeName, value));
    }

    void XnbWriter::WriteNullSharedResource()
    {
        body_.Write7BitEncodedInt(0);
    }

    void XnbWriter::WriteExternalReference(const std::string& assetName)
    {
        if (assetName.find('\\') != std::string::npos)
        {
            throw XnbWriteException(
                "XnbWriter::WriteExternalReference(): '" + assetName +
                "' must use '/' separators, not '\\'.");
        }
        if (assetName.size() >= 4u &&
            assetName.compare(assetName.size() - 4u, 4u, ".xnb") == 0)
        {
            throw XnbWriteException(
                "XnbWriter::WriteExternalReference(): '" + assetName +
                "' must not carry the .xnb extension.");
        }
        body_.WriteString(assetName);
    }

    const std::vector<std::string>& XnbWriter::TypeReaderNames() const noexcept
    {
        return typeReaderNames_;
    }

    std::vector<std::uint8_t> XnbWriter::WriteAsset(const std::string& rootTypeName,
                                                     const std::any& rootValue)
    {
        if (finished_)
        {
            throw XnbWriteException("XnbWriter::WriteAsset(): this writer has already produced a file.");
        }
        finished_ = true;

        WriteObject(rootTypeName, rootValue);

        // Shared resources are serialized after the root, and one may register another while it
        // is being written, so the queue is drained by index rather than by iterator.
        for (std::size_t index = 0u; index < sharedResources_.size(); ++index)
        {
            const std::string targetTypeName = sharedResources_[index].targetTypeName;
            const std::any value = sharedResources_[index].value;
            WriteObject(targetTypeName, value);
        }

        XnbByteWriter file(limits_);
        file.WriteByte(static_cast<std::uint8_t>('X'));
        file.WriteByte(static_cast<std::uint8_t>('N'));
        file.WriteByte(static_cast<std::uint8_t>('B'));
        file.WriteByte(static_cast<std::uint8_t>(XnbPlatformByte(options_.platform)));
        file.WriteByte(static_cast<std::uint8_t>(options_.version));

        std::uint8_t flags = 0u;
        if (options_.profile == XnbGraphicsProfile::HiDef) { flags |= 0x01u; }
        file.WriteByte(flags);

        const std::size_t totalSizeOffset = file.Size();
        file.WriteUInt32(0u);   // patched below, once the real length is known

        XnbByteWriter table(limits_);
        table.Write7BitEncodedInt(static_cast<std::int32_t>(typeTable_.size()));
        for (const auto& entry : typeTable_)
        {
            table.WriteString(entry.readerName);
            table.WriteInt32(entry.version);
        }
        table.Write7BitEncodedInt(static_cast<std::int32_t>(sharedResources_.size()));

        file.WriteBytes(table.View());
        file.WriteBytes(body_.View());

        const std::size_t total = file.Size();
        if (total > static_cast<std::size_t>(limits_.maxFileSize) ||
            total > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max()))
        {
            throw XnbWriteException(
                "XnbWriter: the produced file is " + std::to_string(total) +
                " bytes, past the maximum of " + std::to_string(limits_.maxFileSize) + ".");
        }
        file.PatchUInt32(totalSizeOffset, static_cast<std::uint32_t>(total));
        return file.Take();
    }

    std::vector<std::uint8_t> WriteXnbFile(const XnbTypeWriterRegistry& registry,
                                           const XnbFileOptions& options,
                                           const std::string& rootTypeName,
                                           const std::any& rootValue,
                                           const XnbWriteLimits& limits)
    {
        XnbWriter writer(registry, options, limits);
        return writer.WriteAsset(rootTypeName, rootValue);
    }
}
