// SPDX-License-Identifier: MS-PL
#pragma once

namespace Microsoft::Xna::Framework::Net
{
    /**
     * @brief Describes how a network packet should be delivered.
     *
     * FNA marks this enum `[Flags]`, but its members use plain sequential values
     * (0-4), not power-of-two bit values — `ReliableInOrder` is its own discrete
     * member, not actually composed from `Reliable | InOrder` at runtime. Ported
     * as a plain enum with no bitwise operators to match that actual behavior.
     */
    enum class SendDataOptions
    {
        /** @brief No special delivery guarantees; packets may be dropped or reordered. */
        None,
        /** @brief The packet is guaranteed to arrive. */
        Reliable,
        /** @brief The packet is guaranteed to arrive in order relative to other in-order packets. */
        InOrder,
        /** @brief The packet is guaranteed to arrive, in order. */
        ReliableInOrder,
        /** @brief The packet contains chat data. */
        Chat
    };
}
