// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Xnb/XnbWriter.hpp"

#include "CNA/Internal/Xnb/XnbCompressionWriter.hpp"

#include <algorithm>
#include <limits>
#include <utility>

#include "Microsoft/Xna/Framework/BoundingSphere.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Quaternion.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"

namespace CNA::Internal::Xnb
{
    namespace
    {
        /**
         * @brief Rejects an external reference that is absolute or climbs out of the content root.
         *
         * Both `/` and `\` are treated as separators because a reference authored on Windows and
         * one authored on a POSIX host must be judged by the same rule.
         *
         * @param reference The authored reference, which may be empty for "no reference".
         * @return Empty when the reference is acceptable, otherwise the reason it is not.
         */
        [[nodiscard]] std::string ExternalReferenceProblem(const std::string& reference)
        {
            if (reference.empty()) { return {}; }
            // A NUL or a control character in a path is never authored on purpose. It survives
            // this format's length-prefixed strings intact, and then truncates or misbehaves in
            // whatever consumes the path as a C string on the other side.
            for (const char character : reference)
            {
                if (static_cast<unsigned char>(character) < 0x20u ||
                    static_cast<unsigned char>(character) == 0x7Fu)
                {
                    return "it contains a control character";
                }
            }
            if (reference.front() == '/' || reference.front() == '\\')
            {
                return "it is an absolute path";
            }
            if (reference.size() >= 2u && reference[1] == ':')
            {
                return "it names a drive-qualified absolute path";
            }

            int depth = 0;
            std::size_t start = 0u;
            while (start <= reference.size())
            {
                std::size_t end = reference.find_first_of("/\\", start);
                if (end == std::string::npos) { end = reference.size(); }
                const std::string component = reference.substr(start, end - start);
                if (component == "..")
                {
                    if (--depth < 0) { return "it resolves outside the content root"; }
                }
                else if (!component.empty() && component != ".")
                {
                    ++depth;
                }
                if (end == reference.size()) { break; }
                start = end + 1u;
            }
            return {};
        }
    }

    namespace
    {
        /**
         * @brief Validates a file's limits and returns the set the body buffer should enforce.
         *
         * Validation happens here rather than at each check because the checks widen a signed
         * limit to `std::size_t`: a negative limit would silently become an enormous one, which
         * is exactly the failure a limit exists to prevent.
         */
        [[nodiscard]] XnbWriteLimits EffectiveWriteLimits(const XnbWriteLimits& limits)
        {
            ValidateXnbWriteLimits(limits);
            XnbWriteLimits effective = limits;
            effective.maxPayloadSize = EffectiveXnbPayloadCeiling(limits);
            return effective;
        }
    }

    XnbWriter::XnbWriter(const XnbTypeWriterRegistry& registry, XnbFileOptions options,
                         std::string assetName)
        : registry_(&registry)
        , options_(std::move(options))
        , assetName_(std::move(assetName))
        , body_(EffectiveWriteLimits(options_.limits))
    {
        ValidateXnbFileOptions(options_);
        registry_->Freeze();
    }

    const XnbFileOptions& XnbWriter::Options() const noexcept { return options_; }

    const std::string& XnbWriter::AssetName() const noexcept { return assetName_; }

    int XnbWriter::Version() const noexcept
    {
        return static_cast<int>(XnbContainerVersionByte(options_.version));
    }

    char XnbWriter::Platform() const noexcept { return XnbPlatformByte(options_.platform); }

    void XnbWriter::WriteByte(const std::uint8_t value) { body_.WriteByte(value); }

    void XnbWriter::WriteSByte(const std::int8_t value) { body_.WriteSByte(value); }

    void XnbWriter::WriteBoolean(const bool value) { body_.WriteBoolean(value); }

    void XnbWriter::WriteInt16(const std::int16_t value) { body_.WriteInt16(value); }

    void XnbWriter::WriteUInt16(const std::uint16_t value) { body_.WriteUInt16(value); }

    void XnbWriter::WriteInt32(const std::int32_t value) { body_.WriteInt32(value); }

    void XnbWriter::WriteUInt32(const std::uint32_t value) { body_.WriteUInt32(value); }

    void XnbWriter::WriteInt64(const std::int64_t value) { body_.WriteInt64(value); }

    void XnbWriter::WriteUInt64(const std::uint64_t value) { body_.WriteUInt64(value); }

    void XnbWriter::WriteSingle(const float value) { body_.WriteSingle(value); }

    void XnbWriter::WriteDouble(const double value) { body_.WriteDouble(value); }

    void XnbWriter::Write7BitEncodedInt(const std::int32_t value)
    {
        body_.Write7BitEncodedInt(value);
    }

    void XnbWriter::WriteString(const std::string& value) { body_.WriteString(value); }

    void XnbWriter::WriteChar(const SharpRuntime::charcs value) { body_.WriteChar(value); }

    void XnbWriter::WriteBytes(const std::span<const std::uint8_t> bytes)
    {
        body_.WriteBytes(bytes);
    }

    void XnbWriter::WriteLengthPrefixedBytes(const std::span<const std::uint8_t> bytes)
    {
        if (bytes.size() > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max()))
        {
            throw XnbWriteException(
                "XNB: a length-prefixed payload of " + std::to_string(bytes.size()) +
                " bytes cannot be described by the Int32 length field the format uses.");
        }
        body_.WriteInt32(static_cast<std::int32_t>(bytes.size()));
        body_.WriteBytes(bytes);
    }

    void XnbWriter::WriteVector2(const Microsoft::Xna::Framework::Vector2& value)
    {
        body_.WriteSingle(value.X);
        body_.WriteSingle(value.Y);
    }

    void XnbWriter::WriteVector3(const Microsoft::Xna::Framework::Vector3& value)
    {
        body_.WriteSingle(value.X);
        body_.WriteSingle(value.Y);
        body_.WriteSingle(value.Z);
    }

    void XnbWriter::WriteVector4(const Microsoft::Xna::Framework::Vector4& value)
    {
        body_.WriteSingle(value.X);
        body_.WriteSingle(value.Y);
        body_.WriteSingle(value.Z);
        body_.WriteSingle(value.W);
    }

    void XnbWriter::WriteMatrix(const Microsoft::Xna::Framework::Matrix& value)
    {
        body_.WriteSingle(value.M11); body_.WriteSingle(value.M12);
        body_.WriteSingle(value.M13); body_.WriteSingle(value.M14);
        body_.WriteSingle(value.M21); body_.WriteSingle(value.M22);
        body_.WriteSingle(value.M23); body_.WriteSingle(value.M24);
        body_.WriteSingle(value.M31); body_.WriteSingle(value.M32);
        body_.WriteSingle(value.M33); body_.WriteSingle(value.M34);
        body_.WriteSingle(value.M41); body_.WriteSingle(value.M42);
        body_.WriteSingle(value.M43); body_.WriteSingle(value.M44);
    }

    void XnbWriter::WriteQuaternion(const Microsoft::Xna::Framework::Quaternion& value)
    {
        body_.WriteSingle(value.X);
        body_.WriteSingle(value.Y);
        body_.WriteSingle(value.Z);
        body_.WriteSingle(value.W);
    }

    void XnbWriter::WriteColor(const Microsoft::Xna::Framework::Color& value)
    {
        body_.WriteByte(value.getRProperty());
        body_.WriteByte(value.getGProperty());
        body_.WriteByte(value.getBProperty());
        body_.WriteByte(value.getAProperty());
    }

    void XnbWriter::WriteRectangle(const Microsoft::Xna::Framework::Rectangle& value)
    {
        body_.WriteInt32(value.X);
        body_.WriteInt32(value.Y);
        body_.WriteInt32(value.Width);
        body_.WriteInt32(value.Height);
    }

    void XnbWriter::WriteBoundingSphere(const Microsoft::Xna::Framework::BoundingSphere& value)
    {
        WriteVector3(value.Center);
        body_.WriteSingle(value.Radius);
    }

    void XnbWriter::WriteExternalReference(const std::string& relativePath)
    {
        const std::string problem = ExternalReferenceProblem(relativePath);
        if (!problem.empty())
        {
            throw XnbWriteException(
                "'" + assetName_ + "': cannot write the external reference '" + relativePath +
                "' because " + problem + ".");
        }
        body_.WriteString(relativePath);
    }

    void XnbWriter::RequireCollectionCount(const std::size_t count,
                                            const std::string& readerName) const
    {
        // The format describes a collection's length with an Int32, so a count above that is not
        // merely large -- it is unrepresentable, and casting it would write a negative length.
        if (count > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max()))
        {
            throw XnbWriteException(
                "'" + assetName_ + "': " + readerName + " was given " + std::to_string(count) +
                " elements, more than the format's Int32 length field can describe.");
        }
        if (count > static_cast<std::size_t>(options_.limits.maxCollectionElementCount))
        {
            throw XnbWriteException(
                "'" + assetName_ + "': " + readerName + " was given " + std::to_string(count) +
                " elements, above the configured maximum of " +
                std::to_string(options_.limits.maxCollectionElementCount) + ".");
        }
    }

    void XnbWriter::RequireVerifiedPlatformPayload(const std::string& readerName,
                                                   const bool handlesXboxByteOrder) const
    {
        if (options_.platform != XnbTargetPlatform::Xbox360) { return; }
        if (options_.allowUnverifiedXboxPayloads) { return; }
        if (handlesXboxByteOrder)
        {
            // Even the one writer that swaps its header fields does not swap its sample bytes,
            // which is why this still refuses unless the caller opts in: a half-converted payload
            // is a worse outcome than a refusal, because it looks like it worked.
            throw XnbWriteException(
                "'" + assetName_ + "': " + readerName +
                " byte-swaps its WAVEFORMATEX fields for the big-endian Xbox 360 but not its "
                "sample bytes, so an 'x' file it writes is only half converted. Set "
                "XnbFileOptions::allowUnverifiedXboxPayloads to produce one anyway -- see "
                "plans/plan_xnapipeline.md XNAP-82.");
        }
        throw XnbWriteException(
            "'" + assetName_ + "': " + readerName +
            " has no Xbox 360 byte-order handling, and the Xbox 360 is big-endian, so writing "
            "the 'x' platform byte over these payloads would claim a compatibility this build "
            "cannot deliver. Target 'windows' instead, or set "
            "XnbFileOptions::allowUnverifiedXboxPayloads if you can test the result on real "
            "hardware -- see plans/plan_xnapipeline.md XNAP-82.");
    }

    std::int32_t XnbWriter::InternTypeWriter(const XnbTypeWriterBase& writer)
    {
        const XnbReaderIdentity identity = writer.ReaderIdentity();
        const std::string name = FormatXnbReaderName(identity, options_.readerNameStyle);
        const auto existing = typeTableIndices_.find(name);
        if (existing != typeTableIndices_.end()) { return existing->second; }

        if (static_cast<std::int32_t>(typeTable_.size()) >= options_.limits.maxTypeWriterCount)
        {
            throw XnbWriteException(
                "'" + assetName_ + "': the type-reader table would exceed the configured maximum "
                "of " + std::to_string(options_.limits.maxTypeWriterCount) + " entries.");
        }
        typeTable_.push_back({name, identity.readerVersion});
        const auto index = static_cast<std::int32_t>(typeTable_.size());
        typeTableIndices_.emplace(name, index);
        return index;
    }

    void XnbWriter::WriteNullObject() { body_.Write7BitEncodedInt(0); }

    std::string XnbWriter::MissingWriterContext() const
    {
        return "'" + assetName_ + "': " + currentContext_;
    }

    void XnbWriter::WriteNested(const XnbTypeWriterBase& writer, const void* value)
    {
        if (nestingDepth_ >= options_.limits.maxObjectNestingDepth)
        {
            throw XnbWriteException(
                "'" + assetName_ + "': the object graph nests deeper than the configured maximum "
                "of " + std::to_string(options_.limits.maxObjectNestingDepth) + " levels.");
        }
        std::string previousContext = std::move(currentContext_);
        const std::string canonicalName = XnbCanonicalReaderName(writer.ReaderIdentity());
        // Depth zero is the outermost object. The *first* one is the root; the shared resources
        // Finish() writes afterwards are at depth zero too, hence the emptiness check. Recorded
        // from the write itself, so the manifest identity cannot drift from the file's dispatch.
        if (nestingDepth_ == 0 && rootReaderName_.empty()) { rootReaderName_ = canonicalName; }
        currentContext_ = canonicalName + " wrote a value that";
        ++nestingDepth_;
        try
        {
            writer.WriteUntyped(*this, value);
        }
        catch (...)
        {
            --nestingDepth_;
            currentContext_ = std::move(previousContext);
            throw;
        }
        --nestingDepth_;
        currentContext_ = std::move(previousContext);
    }

    std::int32_t XnbWriter::EnqueueSharedResource(const XnbTypeWriterBase& writer,
                                                  const void* value,
                                                  std::shared_ptr<const void> owner)
    {
        if (static_cast<std::int32_t>(sharedResources_.size()) >=
            options_.limits.maxSharedResourceCount)
        {
            throw XnbWriteException(
                "'" + assetName_ + "': the shared-resource table would exceed the configured "
                "maximum of " + std::to_string(options_.limits.maxSharedResourceCount) +
                " entries.");
        }
        sharedResources_.push_back({&writer, value, std::move(owner)});
        return static_cast<std::int32_t>(sharedResources_.size());
    }

    void XnbWriter::WriteSharedResourceReference(const std::int32_t sharedResourceId)
    {
        if (sharedResourceId < 0 ||
            sharedResourceId > static_cast<std::int32_t>(sharedResources_.size()))
        {
            throw XnbWriteException(
                "'" + assetName_ + "': shared-resource reference " +
                std::to_string(sharedResourceId) +
                " was never issued by AddSharedResource(); valid identifiers are 0 (no reference) "
                "through " + std::to_string(sharedResources_.size()) + ".");
        }
        body_.Write7BitEncodedInt(sharedResourceId);
    }

    std::vector<std::uint8_t> XnbWriter::Finish()
    {
        if (finished_)
        {
            throw XnbWriteException(
                "'" + assetName_ + "': XnbWriter::Finish() was already called for this file.");
        }
        finished_ = true;

        // A shared resource may enqueue further shared resources, so this is an index-based loop
        // over a container that legitimately grows while it runs.
        for (std::size_t index = 0u; index < sharedResources_.size(); ++index)
        {
            const SharedResourceEntry entry = sharedResources_[index];
            Write7BitEncodedInt(InternTypeWriter(*entry.writer));
            WriteNested(*entry.writer, entry.value);
        }

        XnbByteWriter manifest(options_.limits);
        manifest.Write7BitEncodedInt(static_cast<std::int32_t>(typeTable_.size()));
        for (const TypeTableEntry& entry : typeTable_)
        {
            manifest.WriteString(entry.name);
            manifest.WriteInt32(entry.readerVersion);
        }
        manifest.Write7BitEncodedInt(static_cast<std::int32_t>(sharedResources_.size()));

        std::vector<std::uint8_t> payload = manifest.Take();
        const std::vector<std::uint8_t> body = body_.Take();
        payload.insert(payload.end(), body.begin(), body.end());

        if (options_.compression == XnbOutputCompression::Lzx)
        {
            throw XnbWriteException(
                "'" + assetName_ +
                "': LZX output is not implemented (plans/plan_xnapipeline.md XNAP-81). LZX is the "
                "scheme Microsoft XNA 4.0 itself produced, and writing one needs a bounded LZX "
                "*encoder*, which CNA does not have -- it has only the decoder. Write an "
                "uncompressed file, which every XNA 4.0 runtime also loads.");
        }

        constexpr std::size_t kHeaderBytes = 10u;
        const std::size_t decompressedSize = payload.size();
        if (options_.compression == XnbOutputCompression::Lz4)
        {
            // The container's own four-byte decompressed-size field takes the place of an LZ4
            // frame header, which is why this is a single raw block rather than a frame.
            std::vector<std::uint8_t> block = CompressXnbLz4Block(payload);
            std::vector<std::uint8_t> framed;
            framed.reserve(4u + block.size());
            for (int shift = 0; shift < 32; shift += 8)
            {
                framed.push_back(
                    static_cast<std::uint8_t>((decompressedSize >> shift) & 0xFFu));
            }
            framed.insert(framed.end(), block.begin(), block.end());
            payload = std::move(framed);
        }

        const std::size_t totalLength = kHeaderBytes + payload.size();
        if (totalLength > static_cast<std::size_t>(options_.limits.maxFileSize))
        {
            throw XnbWriteException(
                "'" + assetName_ + "': the complete file would be " +
                std::to_string(totalLength) + " bytes, above the configured maximum of " +
                std::to_string(options_.limits.maxFileSize) + " bytes.");
        }

        XnbByteWriter file(options_.limits);
        file.WriteByte('X');
        file.WriteByte('N');
        file.WriteByte('B');
        file.WriteByte(static_cast<std::uint8_t>(XnbPlatformByte(options_.platform)));
        file.WriteByte(XnbContainerVersionByte(options_.version));
        file.WriteByte(XnbHeaderFlagsByte(options_.graphicsProfile, options_.compression));
        file.WriteInt32(static_cast<std::int32_t>(totalLength));
        file.WriteBytes(payload);
        return file.Take();
    }
}
