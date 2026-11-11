// SPDX-License-Identifier: MS-PL
// Copyright (c) Robert Vokac and contributors
#include "CNA/Internal/Net/SearchAddress.hpp"

#include <algorithm>
#include <cctype>

namespace CNA::Internal::Net
{
    std::optional<SearchAddress> ParseSearchAddress(std::string_view entry, std::uint16_t defaultPort)
    {
        // A NUL would end the name the resolver receives (Host.c_str()) early, so the entry would
        // reach another host than it names.
        if (entry.empty()
            || std::any_of(entry.begin(), entry.end(),
                           [](char c) { return c == '\0' || std::isspace(static_cast<unsigned char>(c)) != 0; }))
            return std::nullopt;
        const auto colon = entry.find(':');
        if (colon == std::string_view::npos) return SearchAddress{std::string(entry), defaultPort};
        // IPv6 literals are not supported: the ENet transport is IPv4 only.
        if (entry.find(':', colon + 1) != std::string_view::npos || colon == 0) return std::nullopt;
        const std::string_view digits = entry.substr(colon + 1);
        if (digits.empty() || digits.size() > 5
            || !std::all_of(digits.begin(), digits.end(), [](char c) { return c >= '0' && c <= '9'; }))
            return std::nullopt;
        unsigned value = 0;
        for (const char c : digits) value = value * 10 + static_cast<unsigned>(c - '0');
        if (value == 0 || value > 65535) return std::nullopt;
        return SearchAddress{std::string(entry.substr(0, colon)), static_cast<std::uint16_t>(value)};
    }
}
