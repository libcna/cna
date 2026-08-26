// SPDX-License-Identifier: MS-PL

#include "CNA/Content/Cnb/CnbWriter.hpp"

#include <filesystem>
#include <fstream>
#include <limits>
#include <utility>

#include "CNA/Content/Cnb/CnbArithmetic.hpp"
#include "CNA/Content/Cnb/CnbByteWriter.hpp"
#include "CNA/Content/Cnb/CnbCrc32c.hpp"
#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"

using Microsoft::Xna::Framework::Content::ContentLoadException;

namespace CNA::Content::Cnb
{
    namespace
    {
        [[nodiscard]] bool IsPowerOfTwo(std::uint32_t value)
        {
            return value != 0u && (value & (value - 1u)) == 0u;
        }

        [[nodiscard]] std::uint64_t AlignUp(std::uint64_t value, std::uint32_t alignment)
        {
            const std::uint64_t remainder = value % alignment;
            return remainder == 0u ? value : value + (alignment - remainder);
        }
    }

    CnbWriter::CnbWriter(std::uint32_t assetTypeId, std::uint32_t assetSchemaVersion)
        : assetTypeId_(assetTypeId), assetSchemaVersion_(assetSchemaVersion)
    {
        if (assetTypeId_ == CnbAssetTypeId::Invalid)
        {
            throw ContentLoadException("CnbWriter: asset type 0 is not a valid asset type.");
        }
        if (assetSchemaVersion_ == 0u)
        {
            throw ContentLoadException("CnbWriter: asset schema versions start at 1.");
        }
    }

    void CnbWriter::SetMetadata(std::string assetTypeName, std::string contentName)
    {
        hasMetadata_ = true;
        assetTypeName_ = std::move(assetTypeName);
        contentName_ = std::move(contentName);
    }

    void CnbWriter::SetExternalReferences(std::vector<CnbExternalReference> references)
    {
        hasExternalReferences_ = true;
        externalReferences_ = std::move(references);
    }

    void CnbWriter::AddChunk(CnbChunkId type, std::vector<std::uint8_t> data,
                             std::uint32_t flags, std::uint32_t alignment)
    {
        if (!IsWellFormedChunkId(type))
        {
            throw ContentLoadException(
                "CnbWriter: a chunk identifier must be four printable ASCII bytes.");
        }
        if ((flags & ~CnbChunkFlags::KnownMask) != 0u)
        {
            throw ContentLoadException("CnbWriter: unknown chunk flag bits requested.");
        }
        if (!IsPowerOfTwo(alignment) || alignment > DefaultCnbReadLimits().maxChunkAlignment)
        {
            throw ContentLoadException(
                "CnbWriter: chunk alignment must be a power of two no greater than " +
                std::to_string(DefaultCnbReadLimits().maxChunkAlignment) + ".");
        }
        chunks_.push_back(PendingChunk{type, flags, alignment, std::move(data)});
    }

    std::size_t CnbWriter::SchemaChunkCount() const noexcept { return chunks_.size(); }

    std::vector<CnbWriter::PendingChunk> CnbWriter::AssembleChunkList() const
    {
        std::vector<PendingChunk> all;
        all.reserve(chunks_.size() + 2u);

        if (hasMetadata_)
        {
            CnbByteWriter w;
            w.WriteU32(0u); // flags, reserved
            w.WriteString(assetTypeName_);
            w.WriteString(contentName_);
            all.push_back(PendingChunk{CnbContainerChunk::Metadata, CnbChunkFlags::None, 4u,
                                       w.Take()});
        }
        if (hasExternalReferences_)
        {
            if (externalReferences_.size() >
                static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()))
            {
                throw ContentLoadException("CnbWriter: too many external references to encode.");
            }
            CnbByteWriter w;
            w.WriteU32(static_cast<std::uint32_t>(externalReferences_.size()));
            for (const CnbExternalReference& ref : externalReferences_)
            {
                if (ref.flags != 0u)
                {
                    throw ContentLoadException(
                        "CnbWriter: external reference flags are reserved and must be zero.");
                }
                w.WriteU32(ref.flags);
                w.WriteU32(ref.expectedAssetTypeId);
                w.WriteString(ref.logicalName);
            }
            // The reference table is marked mandatory: a reader that cannot see the names an
            // asset depends on would load a visibly incomplete asset and say nothing.
            all.push_back(PendingChunk{CnbContainerChunk::ExternalReferences,
                                       CnbChunkFlags::Mandatory, 4u, w.Take()});
        }

        for (const PendingChunk& chunk : chunks_) { all.push_back(chunk); }
        return all;
    }

    std::vector<std::uint8_t> CnbWriter::Build() const
    {
        const std::vector<PendingChunk> all = AssembleChunkList();
        if (all.size() > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()))
        {
            throw ContentLoadException("CnbWriter: too many chunks to encode.");
        }
        const auto chunkCount = static_cast<std::uint32_t>(all.size());

        const std::uint64_t tocOffset = Format::DefaultTocOffset;
        const std::uint64_t tocSize =
            CheckedMultiply(chunkCount, Format::TocEntrySize, "CnbWriter table of contents");
        std::uint64_t cursor = CheckedAdd(tocOffset, tocSize, "CnbWriter table of contents");

        std::vector<std::uint64_t> offsets(all.size(), 0u);
        for (std::size_t i = 0; i < all.size(); ++i)
        {
            cursor = AlignUp(cursor, all[i].alignment);
            offsets[i] = cursor;
            cursor = CheckedAdd(cursor, all[i].data.size(),
                                "CnbWriter chunk " + std::to_string(i));
        }
        const std::uint64_t fileSize = cursor;
        if (fileSize > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max()))
        {
            throw ContentLoadException("CnbWriter: the resulting .cnb file would be too large.");
        }

        std::vector<std::uint8_t> out(static_cast<std::size_t>(fileSize), 0u);

        CnbByteWriter header;
        header.WriteBytes(Format::Magic);
        header.WriteU16(Format::ContainerMajor);
        header.WriteU16(Format::ContainerMinor);
        header.WriteU32(0u); // header flags, reserved
        header.WriteU32(assetTypeId_);
        header.WriteU32(assetSchemaVersion_);
        header.WriteU32(chunkCount);
        header.WriteU64(fileSize);
        header.WriteU64(tocOffset);
        const std::vector<std::uint8_t> headerPrefix = header.Take();

        CnbByteWriter toc;
        for (std::size_t i = 0; i < all.size(); ++i)
        {
            const PendingChunk& chunk = all[i];
            toc.WriteU32(chunk.type.value);
            toc.WriteU32(chunk.flags);
            toc.WriteU64(offsets[i]);
            toc.WriteU64(chunk.data.size());
            toc.WriteU64(chunk.data.size());
            toc.WriteU32(Crc32c(chunk.data));
            toc.WriteU32(static_cast<std::uint32_t>(CnbCompression::None));
            toc.WriteU32(chunk.alignment);
            toc.WriteU32(0u); // reserved
        }
        const std::vector<std::uint8_t> tocBytes = toc.Take();

        std::copy(headerPrefix.begin(), headerPrefix.end(), out.begin());
        std::copy(tocBytes.begin(), tocBytes.end(),
                  out.begin() + static_cast<std::ptrdiff_t>(tocOffset));
        for (std::size_t i = 0; i < all.size(); ++i)
        {
            std::copy(all[i].data.begin(), all[i].data.end(),
                      out.begin() + static_cast<std::ptrdiff_t>(offsets[i]));
        }

        // The table-of-contents checksum lives inside the header's checksummed range, so it has to
        // be in place before the header checksum is computed.
        CnbByteWriter tocChecksumField;
        tocChecksumField.WriteU32(Crc32c(tocBytes));
        const std::vector<std::uint8_t> tocChecksumBytes = tocChecksumField.Take();
        std::copy(tocChecksumBytes.begin(), tocChecksumBytes.end(), out.begin() + 40);

        CnbByteWriter headerChecksumField;
        headerChecksumField.WriteU32(Crc32c(
            std::span<const std::uint8_t>(out).first(Format::HeaderChecksumCoverage)));
        const std::vector<std::uint8_t> headerChecksumBytes = headerChecksumField.Take();
        std::copy(headerChecksumBytes.begin(), headerChecksumBytes.end(),
                  out.begin() + static_cast<std::ptrdiff_t>(Format::HeaderChecksumOffset));

        return out;
    }

    void CnbWriter::WriteToFile(const std::string& path) const
    {
        const std::vector<std::uint8_t> bytes = Build();
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        if (!file.is_open())
        {
            throw ContentLoadException("CnbWriter: cannot open '" + path + "' for writing.");
        }
        file.write(reinterpret_cast<const char*>(bytes.data()),
                   static_cast<std::streamsize>(bytes.size()));
        if (!file)
        {
            throw ContentLoadException("CnbWriter: failed while writing '" + path + "'.");
        }
    }
}
