// SPDX-License-Identifier: MS-PL
#pragma once
#include <cstdint>
#include <span>
#include <string>

namespace CNA::Internal::GamerServices {
/** @brief Lower-case hex SHA-256, the identity of every service asset, cached file and catalog file.
 * @param bytes Input. @return Digest. */
std::string sha256Hex(std::span<const std::uint8_t> bytes);
}
