// SPDX-License-Identifier: MS-PL
// Copyright (c) Robert Vokac and contributors
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace CNA::Net
{
    /**
     * @brief Process-wide SystemLink settings for play beyond one broadcast domain.
     *
     * CNA extension, no XNA equivalent: XNA finds SystemLink sessions by LAN broadcast only.
     * NetworkSession::Find and BeginFind also send their discovery query, by unicast, to each
     * extra search address, so a host reached through a port forward or a virtual network (where
     * broadcasts take another interface) is listed like a LAN host, with the address its reply
     * came from; NetworkSession::Join then works unchanged. A host can also bind its game port
     * to a fixed number, so that a router can forward it together with the discovery port.
     *
     * All members are static and may be called from any thread. A search reads the settings
     * when it starts; a session reads the host port when it is created.
     */
    class SystemLinkDiscovery
    {
    public:
        /** @brief The UDP port on which every SystemLink host answers discovery queries. */
        static constexpr std::uint16_t DiscoveryPort = 61190;

        /**
         * @brief Sets the hosts that each SystemLink search also queries by unicast.
         *
         * Each entry is a host name or an IPv4 address, optionally followed by `:port` for a
         * discovery port other than DiscoveryPort (a forward from another external port). A name
         * is resolved at every search; a name that does not resolve is skipped with a logged
         * warning, never thrown out of Find. Duplicate replies are merged, so an address that is
         * also reached by broadcast lists its host once. An empty list (the default) restores
         * broadcast-only discovery. The browser build has no SystemLink discovery at all, so the
         * list has no effect there.
         *
         * @param addresses The host names or addresses, each optionally with `:port`.
         * @throws System::ArgumentException An entry is empty, contains whitespace, or has a
         *         port that is not a number from 1 to 65535; the previous list is kept.
         */
        static void setExtraSearchAddressesProperty(const std::vector<std::string>& addresses);

        /**
         * @brief Gets the hosts that each SystemLink search also queries by unicast.
         *
         * @return The entries as last set, in their original order.
         */
        [[nodiscard]] static std::vector<std::string> getExtraSearchAddressesProperty();

        /**
         * @brief Sets the UDP port a SystemLink session created on this machine binds its game
         * traffic to.
         *
         * 0 (the default) binds an ephemeral port chosen by the operating system. The setting is
         * read by NetworkSession::Create for SystemLink; a session that joins another machine's
         * session always uses an ephemeral port. Create fails with NetworkNotAvailableException
         * when the port cannot be bound (for example while another program uses it).
         *
         * @param port The fixed game port, or 0 for an ephemeral one.
         */
        static void setHostPortProperty(std::uint16_t port);

        /**
         * @brief Gets the UDP port a SystemLink session created on this machine binds its game
         * traffic to.
         *
         * @return The fixed game port, or 0 for an ephemeral one.
         */
        [[nodiscard]] static std::uint16_t getHostPortProperty();

    private:
        /** @brief Not instantiable: every member is static. */
        SystemLinkDiscovery() = delete;
    };
}
