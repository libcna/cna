// SPDX-License-Identifier: MS-PL
#pragma once

#include <string>
#include <vector>

#include "CNA/Internal/Xnb/XnbReadLimits.hpp"
#include "CNA/Internal/Xnb/XnbTypeName.hpp"
#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"
#include "System/IO/BinaryReader.hpp"

namespace CNA::Internal::Xnb
{
    /**
     * @brief One entry of a `.xnb` file's type-reader table (plan_xnb.md XNB-12).
     */
    struct XnbTypeReaderTableEntry
    {
        /** @brief The type-reader name exactly as it appears in the file (assembly-qualified). */
        std::string rawName;

        /** @brief @ref rawName normalized to the XNB-5 canonical registry key (see NormalizeXnbTypeReaderName()). */
        std::string normalizedName;

        /** @brief The serialized reader version (plan_xnb.md XNB-16B consults this against a reader's supported versions). */
        int32_t version = 0;
    };

    /**
     * @brief Parses a `.xnb` type-reader table: a 7-bit-encoded count, then per entry a
     *        7-bit-length-prefixed string (the reader's assembly-qualified name) and a 4-byte
     *        reader version (plan_xnb.md XNB-12), matching FNA's
     *        `ContentTypeReaderManager.LoadAssetReaders`.
     *
     * @param reader Binary reader positioned immediately after the `.xnb` container header.
     * @param path   File path, used only to build exception messages.
     * @param limits Bounds applied to the parsed count (plan_xnb.md XNB-10A).
     * @return The parsed table, in file order (index 0 is table entry 1 in the 1-based dispatch
     *         protocol used by XNB-16's root/object dispatch).
     * @throws Microsoft::Xna::Framework::Content::ContentLoadException if the encoded count is
     *         negative or exceeds @p limits.maxTypeReaderCount.
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
            entry.normalizedName = NormalizeXnbTypeReaderName(entry.rawName);
            entry.version = reader.ReadInt32();
            table.push_back(std::move(entry));
        }

        return table;
    }
}
