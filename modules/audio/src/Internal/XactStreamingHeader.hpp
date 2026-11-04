// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Internal/Audio/XactTypes.hpp"

#include <cstdint>
#include <iosfwd>
#include <string>

namespace CNA::Internal::Audio
{
    /**
     * @brief Reads embedded wave bank metadata and resolves absolute wave entry offsets.
     * @param source Retained wave bank file stream.
     * @param path Source file name for diagnostics.
     * @param offset Byte offset of the bank's header within the file.
     * @return Streaming bank metadata with file-relative wave entry offsets.
     */
    [[nodiscard]] XwbData ParseXwbStreamingHeaderAtOffset(
        std::istream& source, const std::string& path, std::uint32_t offset);
}
