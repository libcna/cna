// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "CNA/Internal/Xnb/XnbReadLimits.hpp"
#include "CNA/Internal/Xnb/XnbTypeName.hpp"
#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"
#include "System/IO/BinaryReader.hpp"

namespace CNA::Internal::Xnb
{
    /**
     * @brief One entry of a `.xnb` file's type-reader table (plans/plan_xnb.md XNB-12).
     */
    struct XnbTypeReaderTableEntry
    {
        /** @brief The type-reader name exactly as it appears in the file (assembly-qualified). */
        std::string rawName;

        /** @brief @ref rawName normalized to the XNB-5 canonical registry key (see NormalizeXnbTypeReaderName()). */
        std::string normalizedName;

        /** @brief The serialized reader version (plans/plan_xnb.md XNB-16B consults this against a reader's supported versions). */
        int32_t version = 0;
    };

    /**
     * @brief Parses a `.xnb` type-reader table: a 7-bit-encoded count, then per entry a
     *        7-bit-length-prefixed string (the reader's assembly-qualified name) and a 4-byte
     *        reader version (plans/plan_xnb.md XNB-12), matching FNA's
     *        `ContentTypeReaderManager.LoadAssetReaders`.
     *
     * @param reader Binary reader positioned immediately after the `.xnb` container header.
     * @param path   File path, used only to build exception messages.
     * @param limits Bounds applied to the parsed count (plans/plan_xnb.md XNB-10A).
     * @return The parsed table, in file order (index 0 is table entry 1 in the 1-based dispatch
     *         protocol used by XNB-16's root/object dispatch).
     * @throws Microsoft::Xna::Framework::Content::ContentLoadException if the encoded count is
     *         negative or exceeds @p limits.maxTypeReaderCount, an entry's raw name exceeds
     *         @p limits.maxStringBytes (REMED-CONTENT-006), or an entry's name is not a
     *         well-formed (possibly generic) .NET type name (plans/plan_xnb.md XNB-43) -- matching every
     *         other malformed-input case in this pipeline, a caller only ever needs to catch this
     *         one exception type for "this .xnb file is malformed", never
     *         `XnbTypeName`'s own lower-level `std::invalid_argument`.
     */
    inline std::vector<XnbTypeReaderTableEntry> ParseXnbTypeReaderTable(
        System::IO::BinaryReader& reader,
        const std::string& path,
        const XnbReadLimits& limits = DefaultXnbReadLimits())
    {
        using Microsoft::Xna::Framework::Content::ContentLoadException;

        const int32_t count = reader.Read7BitEncodedInt();
        if (count < 0 || count > limits.maxTypeReaderCount)
        {
            throw ContentLoadException(
                "'" + path + "' has an invalid type-reader count (" + std::to_string(count) + ").");
        }

        std::vector<XnbTypeReaderTableEntry> table;
        table.reserve(static_cast<std::size_t>(count));

        for (int32_t i = 0; i < count; ++i)
        {
            XnbTypeReaderTableEntry entry;
            entry.rawName = reader.ReadString();
            // REMED-CONTENT-006: BinaryReader::ReadString() has no cap of its own -- only its
            // coarse whole-stream limit applies (up to limits.maxFileSize), not the much tighter
            // limits.maxStringBytes this field is specifically meant to enforce. Checked after the
            // read (BinaryReader::ReadString() has no way to pass a cap in), which still rejects
            // the file with a clean exception rather than silently accepting an oversized name.
            if (static_cast<int64_t>(entry.rawName.size()) > limits.maxStringBytes)
            {
                throw ContentLoadException(
                    "'" + path + "' has a type-reader name of " + std::to_string(entry.rawName.size()) +
                    " bytes, exceeding the maximum of " + std::to_string(limits.maxStringBytes) + ".");
            }
            try
            {
                entry.normalizedName = NormalizeXnbTypeReaderName(entry.rawName, limits);
            }
            catch (const std::invalid_argument& ex)
            {
                throw ContentLoadException(
                    "'" + path + "' has a malformed type-reader name '" + entry.rawName + "': " +
                    ex.what());
            }
            entry.version = reader.ReadInt32();
            table.push_back(std::move(entry));
        }

        return table;
    }
}
