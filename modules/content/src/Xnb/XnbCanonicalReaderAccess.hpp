// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstddef>
#include <cstdint>

#include "CNA/Internal/Xnb/XnbTypeReaderTable.hpp"
#include "Microsoft/Xna/Framework/Content/ContentReader.hpp"

namespace CNA::Internal::Xnb
{
    /** @brief Source-private friend shim from canonical decoders to ContentReader internals. */
    class XnbCanonicalReaderAccess
    {
    public:
        /**
         * @brief Parses the canonical type table without instantiating runtime readers.
         * @param input Reader positioned at the XNB type table.
         */
        static void Initialize(
            Microsoft::Xna::Framework::Content::ContentReader& input)
        {
            input.InitializeCanonicalTypeReadersEXT();
        }

        /**
         * @brief Reads one required canonical type-reader reference.
         * @param input Initialized XNB content reader.
         * @return Referenced validated table entry.
         */
        [[nodiscard]] static const XnbTypeReaderTableEntry& ReadReference(
            Microsoft::Xna::Framework::Content::ContentReader& input)
        {
            return input.ReadCanonicalTypeReaderReferenceEXT();
        }

        /**
         * @brief Reads one nullable canonical type-reader reference.
         * @param input Initialized XNB content reader.
         * @return Referenced table entry, or null for wire index zero.
         */
        [[nodiscard]] static const XnbTypeReaderTableEntry* ReadOptionalReference(
            Microsoft::Xna::Framework::Content::ContentReader& input)
        {
            return input.ReadOptionalCanonicalTypeReaderReferenceEXT();
        }

        /**
         * @brief Returns the parsed reader-table size.
         * @param input Initialized or raw runtime content reader.
         * @return Reader-table entry count.
         */
        [[nodiscard]] static std::size_t ReaderCount(
            const Microsoft::Xna::Framework::Content::ContentReader& input)
        {
            return input.getCanonicalTypeReaderCountEXT();
        }

        /**
         * @brief Returns the validated shared-resource count.
         * @param input Initialized XNB content reader.
         * @return Shared-resource count.
         */
        [[nodiscard]] static std::int32_t SharedResourceCount(
            const Microsoft::Xna::Framework::Content::ContentReader& input)
        {
            return input.getSharedResourceCountEXT();
        }
    };
}
