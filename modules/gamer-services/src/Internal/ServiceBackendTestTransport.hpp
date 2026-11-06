// SPDX-License-Identifier: MS-PL
#pragma once
#include "CNA/Internal/GamerServices/IGamerServicesBackend.hpp"
#include "CNA/GamerServices/Configuration.hpp"
#include <string_view>

namespace CNA::Internal::GamerServices {
/** @brief Runs the real service backend synchronously over an injected control transport.
 * Only the byte exchange is substituted; authentication, negotiation, credential repair,
 * participant serialization and directory decoding remain production code. No event socket
 * or binary download is allowed. Tests must disable credential persistence.
 * @param configuration Captured title/endpoint authority.
 * @param exchange Request bytes to response bytes, e.g. an in-process Service::handle.
 * @return Backend with its normal queued owner-publication boundary. */
std::shared_ptr<IGamerServicesBackend> makeServiceBackendWithTransportForTesting(
    CNA::GamerServices::Configuration configuration,
    std::function<std::string(std::string_view)> exchange);
}
