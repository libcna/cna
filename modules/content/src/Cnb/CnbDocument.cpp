// SPDX-License-Identifier: MS-PL

#include "CNA/Content/Cnb/CnbChunkCompression.hpp"
#include "CNA/Content/Cnb/CnbDocument.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <system_error>
#include <utility>

#include "CNA/Content/Cnb/CnbArithmetic.hpp"
#include "CNA/Content/Cnb/CnbCrc32c.hpp"
#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"

using Microsoft::Xna::Framework::Content::ContentLoadException;

namespace CNA::Content::Cnb
{
    namespace
    {
        [[nodiscard]] std::string Hex32(std::uint32_t value)
        {
            static constexpr char kDigits[] = "0123456789ABCDEF";
            std::string out = "0x";
            for (int shift = 28; shift >= 0; shift -= 4)
            {
                out.push_back(kDigits[(value >> shift) & 0xFu]);
            }
            return out;
        }

        [[nodiscard]] bool IsPowerOfTwo(std::uint32_t value)
        {
            return value != 0u && (value & (value - 1u)) == 0u;
        }

        /// One half-open byte range the file must account for, used by the coverage/overlap check.
        struct Region
        {
            std::uint64_t begin = 0u;
            std::uint64_t end = 0u;
            std::string what;
        };
    }

    bool CnbDocument::HasMagic(std::span<const std::uint8_t> bytes)
    {
        return bytes.size() >= Format::Magic.size() &&
               std::equal(Format::Magic.begin(), Format::Magic.end(), bytes.begin());
    }

    CnbDocument CnbDocument::Parse(std::vector<std::uint8_t> bytes, const std::string& origin,
                                    const CnbReadLimits& limits)
    {
        CnbDocument doc;
        doc.origin_ = origin;
        doc.limits_ = limits;
        doc.bytes_ = std::move(bytes);

        const std::uint64_t fileSize = doc.bytes_.size();
        const std::string where = "'" + origin + "'";

        if (fileSize > limits.maxFileSize)
        {
            throw ContentLoadException(
                where + " is " + std::to_string(fileSize) +
                " bytes, above the configured .cnb size limit of " +
                std::to_string(limits.maxFileSize) + " bytes.");
        }
        if (fileSize < Format::HeaderSize)
        {
            throw ContentLoadException(
                where + " is only " + std::to_string(fileSize) +
                " bytes; a .cnb file needs at least " + std::to_string(Format::HeaderSize) +
                " bytes for its header.");
        }
        if (!HasMagic(doc.bytes_))
        {
            throw ContentLoadException(where + " is not a .cnb file (bad magic bytes).");
        }

        CnbByteReader header(std::span<const std::uint8_t>(doc.bytes_).first(Format::HeaderSize),
                              where + " header", limits);
        header.Skip(Format::Magic.size());

        doc.containerMajor_ = header.ReadU16();
        doc.containerMinor_ = header.ReadU16();
        if (doc.containerMajor_ != Format::ContainerMajor)
        {
            throw ContentLoadException(
                where + " declares CNB container version " + std::to_string(doc.containerMajor_) +
                "." + std::to_string(doc.containerMinor_) +
                "; this build reads major version " + std::to_string(Format::ContainerMajor) +
                " only.");
        }
        // A higher MINOR version is accepted on purpose: minor bumps are additive-only by
        // definition, and anything a newer writer added that actually matters travels in a chunk
        // carrying the Mandatory flag, which RequireMandatoryChunksUnderstood() will reject by
        // name. Rejecting on the number alone would make every additive change a breaking one.

        const std::uint32_t headerFlags = header.ReadU32();
        if (headerFlags != 0u)
        {
            throw ContentLoadException(
                where + " sets container header flags " + Hex32(headerFlags) +
                ", which this build does not define; refusing to guess what they mean.");
        }

        doc.assetTypeId_ = header.ReadU32();
        doc.assetSchemaVersion_ = header.ReadU32();
        const std::uint32_t chunkCount = header.ReadU32();
        const std::uint64_t declaredFileSize = header.ReadU64();
        const std::uint64_t tocOffset = header.ReadU64();
        const std::uint32_t tocChecksum = header.ReadU32();
        const std::uint32_t headerChecksum = header.ReadU32();
        for (std::uint32_t i = 0; i < Format::HeaderReservedSize; ++i)
        {
            if (header.ReadU8() != 0u)
            {
                throw ContentLoadException(
                    where + " has a non-zero reserved header byte at offset " +
                    std::to_string(Format::HeaderChecksumOffset + 4u + i) +
                    "; this build cannot know what it would be agreeing to.");
            }
        }
        header.RequireExhausted();

        // Checked before ANY of the offsets above are trusted for arithmetic: a corrupt header is
        // exactly the case where offset/size fields cannot be believed.
        const std::uint32_t actualHeaderChecksum = Crc32c(
            std::span<const std::uint8_t>(doc.bytes_).first(Format::HeaderChecksumCoverage));
        if (headerChecksum != actualHeaderChecksum)
        {
            throw ContentLoadException(
                where + " has a corrupt header: checksum " + Hex32(headerChecksum) +
                " does not match the computed " + Hex32(actualHeaderChecksum) + ".");
        }

        if (declaredFileSize != fileSize)
        {
            throw ContentLoadException(
                where + " declares a file size of " + std::to_string(declaredFileSize) +
                " bytes but is actually " + std::to_string(fileSize) + " bytes.");
        }
        if (doc.assetTypeId_ == CnbAssetTypeId::Invalid)
        {
            throw ContentLoadException(where + " declares asset type 0, which is not a valid type.");
        }
        if (doc.assetSchemaVersion_ == 0u)
        {
            throw ContentLoadException(
                where + " declares asset schema version 0; schema versions start at 1.");
        }
        if (chunkCount > limits.maxChunkCount)
        {
            throw ContentLoadException(
                where + " declares " + std::to_string(chunkCount) +
                " chunks, above the configured limit of " + std::to_string(limits.maxChunkCount) +
                ".");
        }
        if (tocOffset < Format::HeaderSize)
        {
            throw ContentLoadException(
                where + " places its table of contents at offset " + std::to_string(tocOffset) +
                ", which overlaps the header.");
        }

        const std::string tocWhere = where + " table of contents";
        const std::uint64_t tocSize = CheckedMultiply(chunkCount, Format::TocEntrySize, tocWhere);
        const std::uint64_t tocEnd = CheckedAdd(tocOffset, tocSize, tocWhere);
        if (tocEnd > fileSize)
        {
            throw ContentLoadException(
                tocWhere + " ends at byte " + std::to_string(tocEnd) +
                ", past the end of the " + std::to_string(fileSize) + "-byte file.");
        }

        const auto tocBytes = std::span<const std::uint8_t>(doc.bytes_)
                                  .subspan(static_cast<std::size_t>(tocOffset),
                                           static_cast<std::size_t>(tocSize));
        const std::uint32_t actualTocChecksum = Crc32c(tocBytes);
        if (tocChecksum != actualTocChecksum)
        {
            throw ContentLoadException(
                tocWhere + " is corrupt: checksum " + Hex32(tocChecksum) +
                " does not match the computed " + Hex32(actualTocChecksum) + ".");
        }

        CnbByteReader toc(tocBytes, tocWhere, limits);
        doc.chunks_.reserve(chunkCount);
        std::uint64_t previousOffset = 0u;
        // plans/plan_cnb.md CNBF-114. Accumulated as the entries are read, so the aggregate is known
        // before a single byte is allocated for any chunk's contents. Every entry counts, whatever
        // its codec: for an uncompressed file the sum is bounded by the file's own size anyway
        // (chunks do not overlap), and keeping the rule uniform means there is one invariant to
        // state rather than one per codec.
        std::uint64_t totalUncompressed = 0u;
        for (std::uint32_t i = 0; i < chunkCount; ++i)
        {
            const std::string entryWhere = where + " chunk " + std::to_string(i);

            CnbChunkEntry entry;
            entry.type = CnbChunkId{toc.ReadU32()};
            entry.flags = toc.ReadU32();
            entry.offset = toc.ReadU64();
            entry.storedSize = toc.ReadU64();
            entry.uncompressedSize = toc.ReadU64();
            entry.checksum = toc.ReadU32();
            const std::uint32_t compression = toc.ReadU32();
            entry.alignment = toc.ReadU32();
            const std::uint32_t reserved = toc.ReadU32();

            if (!IsWellFormedChunkId(entry.type))
            {
                throw ContentLoadException(
                    entryWhere + " has a chunk identifier containing non-printable bytes (" +
                    Hex32(entry.type.value) + ").");
            }
            if ((entry.flags & ~CnbChunkFlags::KnownMask) != 0u)
            {
                throw ContentLoadException(
                    entryWhere + " (" + ChunkIdToString(entry.type) + ") sets chunk flags " +
                    Hex32(entry.flags) + ", which this build does not define.");
            }
            if (reserved != 0u)
            {
                throw ContentLoadException(
                    entryWhere + " (" + ChunkIdToString(entry.type) +
                    ") has a non-zero reserved field.");
            }
            // plans/plan_cnb.md CNBF-105. A codec this build does not implement is refused here,
            // by name, exactly as every codec was before one landed -- so a build without the
            // library behaves as it always did rather than failing somewhere further in.
            entry.compression = static_cast<CnbCompression>(compression);
            if (!IsCnbCompressionSupported(entry.compression))
            {
                throw ContentLoadException(
                    entryWhere + " (" + ChunkIdToString(entry.type) +
                    ") uses compression codec " + std::to_string(compression) + " (" +
                    CnbCompressionToString(entry.compression) +
                    "), which this build does not implement.");
            }
            if (entry.compression == CnbCompression::None &&
                entry.uncompressedSize != entry.storedSize)
            {
                throw ContentLoadException(
                    entryWhere + " (" + ChunkIdToString(entry.type) +
                    ") is stored uncompressed but declares a different unpacked size (" +
                    std::to_string(entry.uncompressedSize) + " vs " +
                    std::to_string(entry.storedSize) + ").");
            }
            if (entry.compression != CnbCompression::None &&
                entry.uncompressedSize > limits.maxChunkSize)
            {
                // Checked here as well as inside the codec, because this is the earliest point at
                // which the number is known and nothing has been allocated for it yet.
                throw ContentLoadException(
                    entryWhere + " (" + ChunkIdToString(entry.type) + ") declares an unpacked size of " +
                    std::to_string(entry.uncompressedSize) + " bytes, above the configured limit of " +
                    std::to_string(limits.maxChunkSize) + ".");
            }
            if (!IsPowerOfTwo(entry.alignment) || entry.alignment > limits.maxChunkAlignment)
            {
                throw ContentLoadException(
                    entryWhere + " (" + ChunkIdToString(entry.type) +
                    ") declares an invalid alignment of " + std::to_string(entry.alignment) +
                    "; it must be a power of two no greater than " +
                    std::to_string(limits.maxChunkAlignment) + ".");
            }
            if (entry.storedSize > limits.maxChunkSize)
            {
                throw ContentLoadException(
                    entryWhere + " (" + ChunkIdToString(entry.type) + ") declares " +
                    std::to_string(entry.storedSize) +
                    " bytes, above the configured per-chunk limit of " +
                    std::to_string(limits.maxChunkSize) + ".");
            }
            if (entry.offset % entry.alignment != 0u)
            {
                throw ContentLoadException(
                    entryWhere + " (" + ChunkIdToString(entry.type) + ") starts at offset " +
                    std::to_string(entry.offset) + ", which is not a multiple of its declared " +
                    std::to_string(entry.alignment) + "-byte alignment.");
            }
            const std::uint64_t chunkEnd = CheckedAdd(entry.offset, entry.storedSize, entryWhere);
            if (chunkEnd > fileSize)
            {
                throw ContentLoadException(
                    entryWhere + " (" + ChunkIdToString(entry.type) + ") ends at byte " +
                    std::to_string(chunkEnd) + ", past the end of the " +
                    std::to_string(fileSize) + "-byte file.");
            }
            if (i > 0 && entry.offset < previousOffset)
            {
                throw ContentLoadException(
                    entryWhere + " (" + ChunkIdToString(entry.type) + ") starts at offset " +
                    std::to_string(entry.offset) +
                    ", before the previous entry's offset " + std::to_string(previousOffset) +
                    "; a .cnb table of contents must be ordered by ascending offset.");
            }
            previousOffset = entry.offset;

            // Through CheckedAdd, and against a configured ceiling, because neither of the
            // per-entry limits bounds this: `maxChunkCount * maxChunkSize` is 24 PiB at the
            // defaults, which is what a few kilobytes of individually legal compressed frames
            // could otherwise ask a reader to allocate (plans/plan_cnb.md CNBF-114).
            totalUncompressed =
                CheckedAdd(totalUncompressed, entry.uncompressedSize,
                           where + " total unpacked chunk size");
            if (totalUncompressed > limits.maxTotalUncompressedSize)
            {
                throw ContentLoadException(
                    where + " declares chunks unpacking to at least " +
                    std::to_string(totalUncompressed) +
                    " bytes in total, above the configured aggregate limit of " +
                    std::to_string(limits.maxTotalUncompressedSize) +
                    " bytes. Refused before allocating anything.");
            }

            doc.chunks_.push_back(entry);
        }
        toc.RequireExhausted();

        // --- Region partition: header + table of contents + every non-empty chunk must tile the
        // whole file with nothing overlapping, nothing missing and nothing hidden in the gaps.
        // One invariant covering "no overlap", "no chunk past EOF", "no trailing junk" and "no
        // stowaway bytes in alignment padding" at once.
        std::vector<Region> regions;
        regions.reserve(doc.chunks_.size() + 2u);
        regions.push_back({0u, Format::HeaderSize, "the header"});
        if (tocSize != 0u)
        {
            regions.push_back({tocOffset, tocEnd, "the table of contents"});
        }
        for (std::size_t i = 0; i < doc.chunks_.size(); ++i)
        {
            const CnbChunkEntry& entry = doc.chunks_[i];
            if (entry.storedSize == 0u)
            {
                // A zero-length chunk is legitimate (an empty index buffer, a clip with no
                // tracks). It occupies no bytes, so it takes no part in the partition; its offset
                // has already been range- and alignment-checked above.
                continue;
            }
            regions.push_back({entry.offset, entry.offset + entry.storedSize,
                               "chunk " + std::to_string(i) + " (" + ChunkIdToString(entry.type) + ")"});
        }
        std::stable_sort(regions.begin(), regions.end(),
                         [](const Region& a, const Region& b) { return a.begin < b.begin; });

        std::uint64_t cursor = 0u;
        for (const Region& region : regions)
        {
            if (region.begin < cursor)
            {
                throw ContentLoadException(
                    where + ": " + region.what + " overlaps an earlier region of the file.");
            }
            for (std::uint64_t p = cursor; p < region.begin; ++p)
            {
                if (doc.bytes_[static_cast<std::size_t>(p)] != 0u)
                {
                    throw ContentLoadException(
                        where + " has a non-zero byte at offset " + std::to_string(p) +
                        ", which belongs to no chunk; alignment padding must be zero-filled.");
                }
            }
            cursor = region.end;
        }
        if (cursor != fileSize)
        {
            for (std::uint64_t p = cursor; p < fileSize; ++p)
            {
                if (doc.bytes_[static_cast<std::size_t>(p)] != 0u)
                {
                    throw ContentLoadException(
                        where + " has " + std::to_string(fileSize - cursor) +
                        " trailing byte(s) after its last chunk, the first non-zero one at offset " +
                        std::to_string(p) + ".");
                }
            }
        }

        for (std::size_t i = 0; i < doc.chunks_.size(); ++i)
        {
            const CnbChunkEntry& entry = doc.chunks_[i];
            const auto data = std::span<const std::uint8_t>(doc.bytes_)
                                  .subspan(static_cast<std::size_t>(entry.offset),
                                           static_cast<std::size_t>(entry.storedSize));
            const std::uint32_t actual = Crc32c(data);
            if (actual != entry.checksum)
            {
                throw ContentLoadException(
                    where + " chunk " + std::to_string(i) + " (" + ChunkIdToString(entry.type) +
                    ") is corrupt: checksum " + Hex32(entry.checksum) +
                    " does not match the computed " + Hex32(actual) + ".");
            }
        }

        // Decoded here rather than on first use, so a parsed document is immutable and every
        // accessor is a plain const read. It also means a malformed CMET or an unsafe XREF path is
        // a parse failure rather than a surprise from whichever call happened to touch it first.
        doc.DecompressChunks();
        doc.DecodeMetadata();
        doc.DecodeExternalReferences();

        return doc;
    }

    CnbDocument CnbDocument::ParseFile(const std::string& path, const CnbReadLimits& limits)
    {
        namespace fs = std::filesystem;

        std::error_code ec;
        const std::uintmax_t size = fs::file_size(path, ec);
        if (ec)
        {
            throw ContentLoadException("CNB: cannot stat '" + path + "': " + ec.message() + ".");
        }
        if (static_cast<std::uint64_t>(size) > limits.maxFileSize)
        {
            throw ContentLoadException(
                "'" + path + "' is " + std::to_string(size) +
                " bytes, above the configured .cnb size limit of " +
                std::to_string(limits.maxFileSize) + " bytes.");
        }

        std::ifstream file(path, std::ios::binary);
        if (!file.is_open())
        {
            throw ContentLoadException("CNB: cannot open '" + path + "'.");
        }
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
        if (size != 0u)
        {
            file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size));
            if (static_cast<std::uintmax_t>(file.gcount()) != size)
            {
                throw ContentLoadException(
                    "CNB: '" + path + "' ended after " + std::to_string(file.gcount()) +
                    " of " + std::to_string(size) + " expected bytes.");
            }
        }
        return Parse(std::move(bytes), path, limits);
    }

    const std::string& CnbDocument::Origin() const noexcept { return origin_; }
    std::uint16_t CnbDocument::ContainerMajor() const noexcept { return containerMajor_; }
    std::uint16_t CnbDocument::ContainerMinor() const noexcept { return containerMinor_; }
    std::uint32_t CnbDocument::AssetTypeId() const noexcept { return assetTypeId_; }
    std::uint32_t CnbDocument::AssetSchemaVersion() const noexcept { return assetSchemaVersion_; }
    std::size_t CnbDocument::ChunkCount() const noexcept { return chunks_.size(); }
    const CnbReadLimits& CnbDocument::Limits() const noexcept { return limits_; }

    const CnbChunkEntry& CnbDocument::ChunkAt(std::size_t index) const
    {
        if (index >= chunks_.size())
        {
            throw ContentLoadException(
                "'" + origin_ + "': chunk index " + std::to_string(index) +
                " is out of range (" + std::to_string(chunks_.size()) + " chunk(s)).");
        }
        return chunks_[index];
    }

    std::span<const std::uint8_t> CnbDocument::ChunkData(std::size_t index) const
    {
        const CnbChunkEntry& entry = ChunkAt(index);
        // CNBF-105: a compressed chunk was expanded once at parse time, so this returns the
        // chunk's LOGICAL bytes either way and every caller is unaffected by the codec.
        //
        // CNBF-114: the test is whether this chunk HAS an expansion, not whether that expansion
        // happens to be non-empty. A compressed chunk whose logical size is zero expands to no
        // bytes at all, and an emptiness test would then have fallen through to the branch below
        // and handed out the stored compressed frame -- non-empty bytes, in a chunk the file says
        // is empty.
        if (index < expanded_.size() && expanded_[index].has_value())
        {
            return std::span<const std::uint8_t>(*expanded_[index]);
        }
        return std::span<const std::uint8_t>(bytes_).subspan(
            static_cast<std::size_t>(entry.offset), static_cast<std::size_t>(entry.storedSize));
    }

    void CnbDocument::DecompressChunks()
    {
        bool any = false;
        for (const CnbChunkEntry& entry : chunks_)
        {
            if (entry.compression != CnbCompression::None) { any = true; break; }
        }
        if (!any) { return; }

        expanded_.resize(chunks_.size());
        for (std::size_t i = 0; i < chunks_.size(); ++i)
        {
            const CnbChunkEntry& entry = chunks_[i];
            if (entry.compression == CnbCompression::None) { continue; }
            const std::span<const std::uint8_t> stored =
                std::span<const std::uint8_t>(bytes_).subspan(
                    static_cast<std::size_t>(entry.offset),
                    static_cast<std::size_t>(entry.storedSize));
            expanded_[i] = DecompressCnbChunk(
                stored, entry.compression, entry.uncompressedSize, limits_.maxChunkSize,
                "'" + origin_ + "' chunk " + ChunkIdToString(entry.type));
        }
    }

    CnbByteReader CnbDocument::OpenChunk(std::size_t index) const
    {
        const CnbChunkEntry& entry = ChunkAt(index);
        return CnbByteReader(ChunkData(index),
                             "'" + origin_ + "' chunk " + ChunkIdToString(entry.type), limits_);
    }

    std::vector<std::size_t> CnbDocument::FindAll(CnbChunkId type) const
    {
        std::vector<std::size_t> found;
        for (std::size_t i = 0; i < chunks_.size(); ++i)
        {
            if (chunks_[i].type == type) { found.push_back(i); }
        }
        return found;
    }

    std::optional<std::size_t> CnbDocument::FindSingle(CnbChunkId type) const
    {
        const std::vector<std::size_t> found = FindAll(type);
        if (found.empty()) { return std::nullopt; }
        if (found.size() > 1u)
        {
            throw ContentLoadException(
                "'" + origin_ + "' has " + std::to_string(found.size()) + " '" +
                ChunkIdToString(type) + "' chunks, but this schema allows at most one.");
        }
        return found.front();
    }

    std::size_t CnbDocument::RequireSingle(CnbChunkId type) const
    {
        const std::optional<std::size_t> found = FindSingle(type);
        if (!found.has_value())
        {
            throw ContentLoadException(
                "'" + origin_ + "' is missing its required '" + ChunkIdToString(type) +
                "' chunk.");
        }
        return *found;
    }

    void CnbDocument::RequireMandatoryChunksUnderstood(std::span<const CnbChunkId> knownTypes) const
    {
        for (std::size_t i = 0; i < chunks_.size(); ++i)
        {
            const CnbChunkEntry& entry = chunks_[i];
            if (!entry.IsMandatory()) { continue; }
            if (entry.type == CnbContainerChunk::Metadata ||
                entry.type == CnbContainerChunk::ExternalReferences)
            {
                continue;
            }
            if (std::find(knownTypes.begin(), knownTypes.end(), entry.type) != knownTypes.end())
            {
                continue;
            }
            throw ContentLoadException(
                "'" + origin_ + "' chunk " + std::to_string(i) + " ('" +
                ChunkIdToString(entry.type) +
                "') is marked mandatory but is not understood by this build of CNA; the file "
                "requires a newer reader.");
        }
    }

    void CnbDocument::RequireAsset(std::uint32_t expectedAssetTypeId,
                                    std::uint32_t maxSchemaVersion) const
    {
        if (assetTypeId_ != expectedAssetTypeId)
        {
            throw ContentLoadException(
                "'" + origin_ + "' holds a " + AssetTypeIdToString(assetTypeId_) +
                " asset, but was requested as " + AssetTypeIdToString(expectedAssetTypeId) + ".");
        }
        if (assetSchemaVersion_ < 1u || assetSchemaVersion_ > maxSchemaVersion)
        {
            throw ContentLoadException(
                "'" + origin_ + "' uses " + AssetTypeIdToString(assetTypeId_) +
                " schema version " + std::to_string(assetSchemaVersion_) +
                "; this build supports version" +
                (maxSchemaVersion > 1u ? "s 1 to " + std::to_string(maxSchemaVersion)
                                        : std::string(" 1")) + ".");
        }
    }

    const CnbMetadata& CnbDocument::Metadata() const noexcept { return metadata_; }

    const std::vector<CnbExternalReference>& CnbDocument::ExternalReferences() const noexcept
    {
        return externalReferences_;
    }

    void CnbDocument::DecodeMetadata()
    {
        const std::optional<std::size_t> index = FindSingle(CnbContainerChunk::Metadata);
        if (index.has_value())
        {
            CnbByteReader reader = OpenChunk(*index);
            metadata_.present = true;
            metadata_.flags = reader.ReadU32();
            if (metadata_.flags != 0u)
            {
                reader.Fail("the CMET chunk sets flags this build does not define.");
            }
            metadata_.assetTypeName = reader.ReadString();
            metadata_.contentName = reader.ReadString();
            reader.RequireExhausted();
        }
    }

    void CnbDocument::DecodeExternalReferences()
    {
        const std::optional<std::size_t> index = FindSingle(CnbContainerChunk::ExternalReferences);
        if (index.has_value())
        {
            CnbByteReader reader = OpenChunk(*index);
            // Element size 0: entries are variable-length (they carry a string), so the fit check
            // cannot apply -- the per-entry reads bound themselves.
            const std::uint32_t count = reader.ReadCount(0u, "external references");
            externalReferences_.reserve(std::min<std::uint32_t>(count, 4096u));
            for (std::uint32_t i = 0; i < count; ++i)
            {
                CnbExternalReference ref;
                ref.flags = reader.ReadU32();
                if (ref.flags != 0u)
                {
                    reader.Fail("external reference " + std::to_string(i) +
                                " sets flags this build does not define.");
                }
                ref.expectedAssetTypeId = reader.ReadU32();
                ref.logicalName = reader.ReadString();

                // A logical name goes straight into ContentManager's path resolution. It is
                // checked here as well as there, because a compiled file should never be able to
                // hand path-traversal input to the resolver in the first place -- and through the
                // shared rule (CNBF-115), so the writer cannot come to disagree with this about
                // what a legal reference is.
                if (const std::string problem = CnbLogicalNameProblem(ref.logicalName);
                    !problem.empty())
                {
                    reader.Fail("external reference " + std::to_string(i) + " ('" +
                                ref.logicalName + "') " + problem + ".");
                }

                externalReferences_.push_back(std::move(ref));
            }
            reader.RequireExhausted();
        }
    }

    const CnbExternalReference& CnbDocument::ExternalReferenceAt(
        std::uint32_t index, const char* whatForDiagnostics) const
    {
        const std::vector<CnbExternalReference>& refs = ExternalReferences();
        if (static_cast<std::size_t>(index) >= refs.size())
        {
            throw ContentLoadException(
                "'" + origin_ + "': " + whatForDiagnostics + " names external reference " +
                std::to_string(index) + ", but the file declares only " +
                std::to_string(refs.size()) + ".");
        }
        return refs[index];
    }
}
