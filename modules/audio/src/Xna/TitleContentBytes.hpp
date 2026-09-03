// SPDX-License-Identifier: MS-PL

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace CNA::Internal::Audio
{
    [[nodiscard]] std::vector<std::uint8_t> ReadTitleContentBytes(
        const std::string& filename);
}
