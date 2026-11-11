// SPDX-License-Identifier: MS-PL
// Copyright (c) Robert Vokac and contributors
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace CNA::Internal::Net
{
    /** @brief One extra SystemLink search address: a host name or IPv4 address and its discovery port. */
    struct SearchAddress
    {
        std::string Host;
        std::uint16_t Port{0};
    };

    /**
     * @brief Splits `host` or `host:port` into a SearchAddress.
     *
     * @param entry The entry as given to CNA::Net::SystemLinkDiscovery.
     * @param defaultPort The port of an entry without `:port`.
     * @return The address, or empty when the entry is empty, contains whitespace or a second
     *         colon, or has a port that is not a number from 1 to 65535.
     */
    std::optional<SearchAddress> ParseSearchAddress(std::string_view entry, std::uint16_t defaultPort);
}
