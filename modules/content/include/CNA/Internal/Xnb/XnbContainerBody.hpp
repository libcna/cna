// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <variant>
#include <vector>

#include "CNA/Internal/Xnb/XnbHeader.hpp"
#include "CNA/Internal/Xnb/XnbReadLimits.hpp"

namespace CNA::Internal::Xnb
{
    /**
     * @brief A validated XNB header and its uncompressed content-reader body.
     *
     * Uncompressed containers borrow their body from the input span. Compressed containers own
     * the decoded body, so callers must keep this object alive while using @ref Bytes.
     */
    struct XnbContainerBody
    {
        /** @brief The parsed and validated XNB container header. */
        XnbHeader header;

        /** @brief Borrowed uncompressed bytes or owned decompressed bytes. */
        std::variant<std::span<const std::uint8_t>, std::vector<std::uint8_t>> storage;

        /**
         * @brief Gets the uncompressed content-reader body.
         * @return A view valid while this object and, for an uncompressed container, its input
         *         bytes remain alive.
         */
        [[nodiscard]] std::span<const std::uint8_t> Bytes() const;
    };

    /**
     * @brief Validates an XNB container and exposes its uncompressed content-reader body.
     *
     * The declared container range is honored even when the input span has trailing bytes,
     * matching ContentManager's existing/FNA runtime behavior. Callers that require a file to
     * end exactly at `totalLength` must enforce that separately.
     *
     * @param file Complete bytes available for the XNB container.
     * @param origin File or asset name used in diagnostics.
     * @param limits File, compressed-payload, and decompressed-payload limits.
     * @return The validated header and uncompressed body.
     * @throws Microsoft::Xna::Framework::Content::ContentLoadException if the header, declared
     *         range, compression flags, sizes, or compressed payload are invalid.
     */
    [[nodiscard]] XnbContainerBody DecodeXnbContainerBody(
        std::span<const std::uint8_t> file,
        const std::string& origin,
        const XnbReadLimits& limits = DefaultXnbReadLimits());
}
