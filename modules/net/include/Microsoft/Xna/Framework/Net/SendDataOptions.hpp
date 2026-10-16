// SPDX-License-Identifier: MS-PL
#pragma once

#include <type_traits>

namespace Microsoft::Xna::Framework::Net
{
    /**
     * @brief Defines options for network packet transmission.
     *
     * A flags enumeration, as in XNA: `ReliableInOrder` is `Reliable | InOrder`, and `Chat` can be
     * combined with either or both of `Reliable` and `InOrder`. In-order chat packets are ordered
     * relative to other chat packets; they may arrive out of order with respect to non-chat data.
     */
    enum class SendDataOptions : int
    {
        /** @brief No delivery guarantees; packets may be dropped or arrive in any order. */
        None = 0,
        /** @brief Reliable delivery without special ordering; packets are resent until they arrive. */
        Reliable = 1,
        /** @brief Guaranteed ordering without reliable delivery; delivered packets arrive in order. */
        InOrder = 2,
        /** @brief Reliable delivery in the order the packets were sent. */
        ReliableInOrder = 3,
        /** @brief Marks chat data, which is sent without packet encryption. */
        Chat = 4
    };

    /** @brief Combines two SendDataOptions flags with bitwise OR. */
    [[nodiscard]] constexpr SendDataOptions operator|(SendDataOptions left, SendDataOptions right)
    {
        using Underlying = std::underlying_type_t<SendDataOptions>;
        return static_cast<SendDataOptions>(
            static_cast<Underlying>(left) | static_cast<Underlying>(right)
        );
    }

    /** @brief Masks two SendDataOptions flags with bitwise AND. */
    [[nodiscard]] constexpr SendDataOptions operator&(SendDataOptions left, SendDataOptions right)
    {
        using Underlying = std::underlying_type_t<SendDataOptions>;
        return static_cast<SendDataOptions>(
            static_cast<Underlying>(left) & static_cast<Underlying>(right)
        );
    }

    /** @brief Inverts a SendDataOptions flag mask. */
    [[nodiscard]] constexpr SendDataOptions operator~(SendDataOptions value)
    {
        using Underlying = std::underlying_type_t<SendDataOptions>;
        return static_cast<SendDataOptions>(~static_cast<Underlying>(value));
    }

    /** @brief Combines flags into @p left with bitwise OR-assignment. */
    constexpr SendDataOptions& operator|=(SendDataOptions& left, SendDataOptions right)
    {
        left = left | right;
        return left;
    }

    /** @brief Masks @p left with @p right using bitwise AND-assignment. */
    constexpr SendDataOptions& operator&=(SendDataOptions& left, SendDataOptions right)
    {
        left = left & right;
        return left;
    }
}
