// SPDX-License-Identifier: MS-PL
#pragma once
#include "CNA/GamerServices/Configuration.hpp"
#include "CNA/Internal/GamerServices/ServiceSessionDirectory.hpp"
#include <cstdint>
#include <memory>

namespace CNA::Internal::Net {
/** @brief Worker status, consumed by the owning Net update boundary. */
enum class RelayTransportState { Connecting,Ready,Failed,Stopped };
/** @brief Credential-free transport observations; never raises a game event. */
struct RelayTransportStatus {
    RelayTransportState state=RelayTransportState::Connecting;
    std::string error;
    std::uint64_t sent=0,received=0,dropped=0;
    std::size_t queued=0;
};
/** @brief Bounded worker-owned WSS/UDP bridge; ENet payload remains unchanged. */
class RelayTransport {
public:
    /** @brief Starts one owned worker and stable loopback routes.
     * @param configuration Validated deployment authority/trust.
     * @param ticket One-use full-machine authority, consumed in memory.
     * @param localPort Bound local ENet endpoint.
     * @param machines Service-authorized remote machines, at most thirty. */
    RelayTransport(const CNA::GamerServices::Configuration& configuration,
        GamerServices::ServiceRelayTicket ticket,std::uint16_t localPort,
        const std::vector<std::string>& machines);
    /** @brief Cancels and joins the worker, releasing sockets and authority. */
    ~RelayTransport();
    RelayTransport(const RelayTransport&)=delete;
    RelayTransport& operator=(const RelayTransport&)=delete;
    /** @brief Replaces authorized routes; unchanged identities retain their ports.
     * @param machines Latest authoritative directory machine set. */
    void setRoutes(const std::vector<std::string>& machines);
    /** @brief Gets a stable loopback destination for an authorized machine.
     * @param machine Service machine identity. @return Port, or zero if absent. */
    std::uint16_t routePort(const std::string& machine) const;
    /** @brief Copies safe worker observations at an update boundary. @return Status snapshot. */
    RelayTransportStatus status() const;
    /** @brief Cancels and joins without a detached callback or thread. */
    void stop() noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
