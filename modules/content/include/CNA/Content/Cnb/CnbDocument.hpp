// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "CNA/Content/Cnb/CnbByteReader.hpp"
#include "CNA/Content/Cnb/CnbFormat.hpp"
#include "CNA/Content/Cnb/CnbReadLimits.hpp"

namespace CNA::Content::Cnb
{
    /** @brief One parsed table-of-contents entry (plans/plan_cnb.md `CNBF-008`). */
    struct CnbChunkEntry
    {
        /** @brief The chunk's four-character identifier. */
        CnbChunkId type{};

        /** @brief Per-chunk flags; see CnbChunkFlags. */
        std::uint32_t flags = 0u;

        /** @brief Absolute byte offset of the chunk's stored bytes within the file. */
        std::uint64_t offset = 0u;

        /** @brief Number of bytes the chunk occupies in the file. */
        std::uint64_t storedSize = 0u;

        /**
         * @brief Number of bytes the chunk expands to once decompressed.
         *
         * Equal to @ref storedSize for every CNB v1 chunk, because the only codec v1 defines is
         * `CnbCompression::None`.
         */
        std::uint64_t uncompressedSize = 0u;

        /** @brief CRC-32C of the chunk's stored bytes. */
        std::uint32_t checksum = 0u;

        /** @brief The codec used to store the chunk. */
        CnbCompression compression = CnbCompression::None;

        /** @brief Power-of-two alignment, in bytes, that @ref offset satisfies. */
        std::uint32_t alignment = 1u;

        /** @brief Whether this chunk carries the CnbChunkFlags::Mandatory bit. */
        [[nodiscard]] bool IsMandatory() const
        {
            return (flags & CnbChunkFlags::Mandatory) != 0u;
        }
    };

    /** @brief One entry of a `.cnb` file's optional `XREF` external-reference table. */
    struct CnbExternalReference
    {
        /** @brief Reserved for future use; must be zero in CNB v1. */
        std::uint32_t flags = 0u;

        /**
         * @brief The asset type the referring schema expects at this logical name, or
         *        CnbAssetTypeId::Invalid when the schema does not constrain it.
         */
        std::uint32_t expectedAssetTypeId = CnbAssetTypeId::Invalid;

        /**
         * @brief The referenced asset's logical name, exactly as it would be passed to
         *        `ContentManager::Load<T>()`.
         *
         * Always relative, always `/`-separated, never containing a `..` segment -- the reader
         * enforces all three before the name can reach any path-resolution code.
         */
        std::string logicalName;
    };

    /** @brief The optional `CMET` chunk's contents: debug-only provenance strings. */
    struct CnbMetadata
    {
        /** @brief Whether a `CMET` chunk was present at all. */
        bool present = false;

        /** @brief Reserved for future use; must be zero in CNB v1. */
        std::uint32_t flags = 0u;

        /**
         * @brief Human-readable name of the asset type, e.g. `"Microsoft.Xna.Framework.Curve"`.
         *
         * Diagnostic only. Dispatch is driven by the header's numeric asset type identifier; this
         * string exists so a custom-type identifier collision can be reported rather than
         * silently mis-loaded.
         */
        std::string assetTypeName;

        /** @brief The logical content name the compiler was given, or empty. Diagnostic only. */
        std::string contentName;
    };

    /**
     * @brief A parsed, fully validated `.cnb` container (plans/plan_cnb.md `CNBF-008`).
     *
     * Parse() applies every container invariant listed in `plans/plan_cnb.md` §4 -- magic, versions,
     * reserved-field zeroing, both structural checksums, every chunk checksum, overflow-safe
     * offset arithmetic, alignment, table-of-contents ordering, exact non-overlapping coverage of
     * the file, and zeroed alignment padding -- before any accessor can hand out a byte. A
     * CnbDocument that exists is therefore a container that is structurally sound; a schema
     * decoder only has to worry about its own contents.
     *
     * The document owns its bytes, so every span it hands out stays valid for its lifetime.
     */
    class CnbDocument
    {
    public:
        /**
         * @brief Parses and validates a complete `.cnb` byte image.
         *
         * @param bytes  The whole file. Moved into the document.
         * @param origin Name used in exception messages, normally the file path.
         * @param limits Sanity bounds to apply.
         * @return The parsed document.
         * @throws Microsoft::Xna::Framework::Content::ContentLoadException if any container
         *         invariant is violated.
         */
        [[nodiscard]] static CnbDocument Parse(std::vector<std::uint8_t> bytes,
                                               const std::string& origin,
                                               const CnbReadLimits& limits = DefaultCnbReadLimits());

        /**
         * @brief Reads a `.cnb` file from disk and parses it.
         *
         * The file's size is checked against `CnbReadLimits::maxFileSize` before it is read, so an
         * oversized file is refused without ever being allocated.
         *
         * @param path   Filesystem path to read.
         * @param limits Sanity bounds to apply.
         * @return The parsed document.
         * @throws Microsoft::Xna::Framework::Content::ContentLoadException if the file cannot be
         *         opened, is too large, or violates any container invariant.
         */
        [[nodiscard]] static CnbDocument ParseFile(const std::string& path,
                                                    const CnbReadLimits& limits = DefaultCnbReadLimits());

        /**
         * @brief Whether @p bytes begins with the `.cnb` magic.
         *
         * A cheap pre-check for tools that inspect files by content rather than extension. It says
         * nothing about whether the rest of the file is valid.
         *
         * @param bytes Bytes to inspect.
         * @return True when the first four bytes are the CNB magic.
         */
        [[nodiscard]] static bool HasMagic(std::span<const std::uint8_t> bytes);

        /** @brief The name this document reports in exception messages. @return The origin string. */
        [[nodiscard]] const std::string& Origin() const noexcept;

        /** @brief The container major version the file declares. @return The major version. */
        [[nodiscard]] std::uint16_t ContainerMajor() const noexcept;

        /** @brief The container minor version the file declares. @return The minor version. */
        [[nodiscard]] std::uint16_t ContainerMinor() const noexcept;

        /** @brief The asset type the file holds. @return The asset type identifier. */
        [[nodiscard]] std::uint32_t AssetTypeId() const noexcept;

        /** @brief The asset schema version the file was written to. @return The schema version. */
        [[nodiscard]] std::uint32_t AssetSchemaVersion() const noexcept;

        /** @brief Number of chunks in the table of contents. @return The chunk count. */
        [[nodiscard]] std::size_t ChunkCount() const noexcept;

        /**
         * @brief The table-of-contents entry at @p index.
         *
         * @param index Zero-based table-of-contents index; must be less than ChunkCount().
         * @return The entry.
         * @throws Microsoft::Xna::Framework::Content::ContentLoadException if @p index is out of
         *         range.
         */
        [[nodiscard]] const CnbChunkEntry& ChunkAt(std::size_t index) const;

        /**
         * @brief The stored bytes of the chunk at @p index.
         *
         * @param index Zero-based table-of-contents index.
         * @return A view of the chunk's bytes, valid for this document's lifetime.
         * @throws Microsoft::Xna::Framework::Content::ContentLoadException if @p index is out of
         *         range.
         */
        [[nodiscard]] std::span<const std::uint8_t> ChunkData(std::size_t index) const;

        /**
         * @brief A bounded reader positioned at the start of the chunk at @p index.
         *
         * @param index Zero-based table-of-contents index.
         * @return A cursor over that chunk, with an exception context naming the file and chunk.
         * @throws Microsoft::Xna::Framework::Content::ContentLoadException if @p index is out of
         *         range.
         */
        [[nodiscard]] CnbByteReader OpenChunk(std::size_t index) const;

        /**
         * @brief Every table-of-contents index whose entry has type @p type, in file order.
         *
         * @param type The chunk identifier to look for.
         * @return The matching indices, ascending.
         */
        [[nodiscard]] std::vector<std::size_t> FindAll(CnbChunkId type) const;

        /**
         * @brief The single chunk of type @p type, if the file has exactly one.
         *
         * @param type The chunk identifier to look for.
         * @return Its index, or `std::nullopt` when the file has none.
         * @throws Microsoft::Xna::Framework::Content::ContentLoadException if the file has more
         *         than one chunk of that type.
         */
        [[nodiscard]] std::optional<std::size_t> FindSingle(CnbChunkId type) const;

        /**
         * @brief The single chunk of type @p type, which must be present exactly once.
         *
         * @param type The chunk identifier to look for.
         * @return Its index.
         * @throws Microsoft::Xna::Framework::Content::ContentLoadException if the file has zero or
         *         more than one chunk of that type.
         */
        [[nodiscard]] std::size_t RequireSingle(CnbChunkId type) const;

        /**
         * @brief Enforces the mandatory-chunk rule for a schema decoder.
         *
         * A chunk carrying CnbChunkFlags::Mandatory whose identifier is neither container-defined
         * nor listed in @p knownTypes means the file relies on something this build cannot honour,
         * so the whole file is refused. A mandatory chunk that *is* known, and any non-mandatory
         * chunk, passes -- an unknown optional chunk is simply ignored, which is what lets a newer
         * writer add data an older reader can safely skip.
         *
         * @param knownTypes Every chunk identifier this schema decoder understands.
         * @throws Microsoft::Xna::Framework::Content::ContentLoadException on an unknown mandatory
         *         chunk.
         */
        void RequireMandatoryChunksUnderstood(std::span<const CnbChunkId> knownTypes) const;

        /**
         * @brief The file's `CMET` metadata.
         *
         * Decoded during Parse(), not on first use: a document is fully immutable once it exists,
         * which is what makes it safe to hand the same document to two threads and what lets the
         * loader registry consult the type name from a `const` context without a data race
         * (plans/plan_cnb.md `CNBF-H004`).
         *
         * @return The metadata; `CnbMetadata::present` is false when the file has no `CMET` chunk.
         */
        [[nodiscard]] const CnbMetadata& Metadata() const noexcept;

        /**
         * @brief The file's `XREF` external-reference table.
         *
         * Decoded and validated during Parse() for the same reason as Metadata(), which also means
         * a file naming an unsafe path is refused at parse time rather than at whichever later
         * moment happened to touch the table first.
         *
         * @return The table; empty when the file has no `XREF` chunk.
         */
        [[nodiscard]] const std::vector<CnbExternalReference>& ExternalReferences() const noexcept;

        /**
         * @brief Looks up one external reference by index, with a schema-friendly error message.
         *
         * @param index      Index into the `XREF` table.
         * @param whatForDiagnostics Noun naming what referred to it, used in the error message.
         * @return The referenced entry.
         * @throws Microsoft::Xna::Framework::Content::ContentLoadException if @p index is out of
         *         range.
         */
        [[nodiscard]] const CnbExternalReference& ExternalReferenceAt(
            std::uint32_t index, const char* whatForDiagnostics) const;

        /**
         * @brief Requires the file's asset type and schema version to be what a decoder expects.
         *
         * @param expectedAssetTypeId The asset type the caller is prepared to decode.
         * @param maxSchemaVersion    The highest schema version the caller understands; version 1
         *                            is always the lowest accepted.
         * @throws Microsoft::Xna::Framework::Content::ContentLoadException on a mismatch or an
         *         out-of-range schema version.
         */
        void RequireAsset(std::uint32_t expectedAssetTypeId, std::uint32_t maxSchemaVersion) const;

        /**
         * @brief The limits this document was parsed with.
         *
         * @return A reference to this document's own copy, valid for the document's lifetime --
         *         not to whatever the caller passed to Parse().
         */
        [[nodiscard]] const CnbReadLimits& Limits() const noexcept;

    private:
        CnbDocument() = default;

        /// Decodes the optional container-level chunks. Called by Parse() once every structural
        /// invariant holds, which is what lets them use the ordinary bounds-checked accessors.
        void DecodeMetadata();
        void DecodeExternalReferences();

        std::vector<std::uint8_t> bytes_;
        std::string origin_;
        std::uint16_t containerMajor_ = 0u;
        std::uint16_t containerMinor_ = 0u;
        std::uint32_t assetTypeId_ = 0u;
        std::uint32_t assetSchemaVersion_ = 0u;
        std::vector<CnbChunkEntry> chunks_;
        /**
         * @brief The limits this document was parsed with, held **by value**.
         *
         * Same reasoning as CnbByteReader::limits_: `Parse(bytes, "foo", CnbReadLimits{})` is the
         * natural call, and its argument is a temporary. A document that outlived it while holding
         * its address would hand a dangling reference to every chunk cursor it opens.
         */
        CnbReadLimits limits_{};

        CnbMetadata metadata_;
        std::vector<CnbExternalReference> externalReferences_;
    };
}
