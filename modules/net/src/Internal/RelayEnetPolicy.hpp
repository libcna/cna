// SPDX-License-Identifier: MS-PL
#pragma once
#include <cstddef>
#include <span>

namespace CNA::Internal::Net {
/** @brief Maximum logical game packet for the private relay ENet host. */
inline constexpr std::size_t MaxRelayGamePacketBytes=1024*1024;
/** @brief Maximum ENet waiting data per relay peer. */
inline constexpr std::size_t MaxRelayWaitingBytes=4*1024*1024;
/** @brief Maximum fragment bitmap slots per relayed game packet. */
inline constexpr std::size_t MaxRelayGameFragments=2048;
/** @brief Checks resource bounds before an untrusted relay datagram reaches ENet.
 * @param bytes Uncompressed ENet datagram without an optional checksum.
 * @return Whether the datagram satisfies the private relay host's allocation policy. */
bool validateRelayEnetDatagram(std::span<const unsigned char> bytes) noexcept;
}
