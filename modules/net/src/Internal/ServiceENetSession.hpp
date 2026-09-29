// SPDX-License-Identifier: MS-PL
#pragma once
#include "OnlineSessionPreparation.hpp"
#include "CNA/Internal/Net/NetPacketCodec.hpp"
#include <chrono>
#include <map>
#include <optional>
#include <utility>

namespace CNA::Internal::Net {
/** @brief Owned logical realtime observation for conversion at the XNA owner's update boundary. */
struct ServiceENetObservation {
    /** @brief Logical event category. */
    enum class Type {
        /** @brief Host resources or client welcome are ready. */ Ready,
        /** @brief A complete authenticated remote group connected. */ Joined,
        /** @brief An admitted remote group disconnected or lost authority. */ Left,
        /** @brief An admitted game packet targets this local group. */ Data,
        /** @brief Authenticated directory metadata changed. */ Snapshot,
        /** @brief Gamers' lobby ready state changed. */ Readiness,
        /** @brief The directory handed the host role to another machine (snapshot names it). */ HostChanged,
        /** @brief Local gamers added to this machine's group (ids and gamers name them). */ LocalAdded,
        /** @brief The service refused adding local gamers (failure names why). */ AddFailed,
        /** @brief Authority or transport became unavailable. */ Failed
    };
    /** @brief Observation category. */
    Type type=Type::Failed;
    /** @brief Authenticated joined/ready remote identities. */
    std::vector<RosterEntry> gamers;
    /** @brief Ordered assigned local IDs, or departed remote IDs. */
    std::vector<unsigned char> ids;
    /** @brief Owned local-target game data. */
    std::optional<AppDataMessage> data;
    /** @brief Owned directory metadata. */
    std::optional<GamerServices::ServiceSessionSnapshot> snapshot;
    /** @brief Lobby ready states for a Readiness observation. */
    std::vector<GamerReadyEntry> readiness;
    /** @brief Fixed safe failure code, never raw network text. */
    std::string failure;
};
/** @brief Explicit private route/clock providers for deterministic loopback fixtures. */
struct ServiceENetDependencies {
    /** @brief Replaces authorized remote routes; default uses the prepared native relay. */
    std::function<void(const std::vector<std::string>&)> setRoutes;
    /** @brief Resolves a source-preserving loopback port; default uses the prepared native relay. */
    std::function<std::uint16_t(const std::string&)> routePort;
    /** @brief Supplies time for bounded handshake/recovery; default is steady_clock. */
    std::function<std::chrono::steady_clock::time_point()> clock;
};
/** @brief Owns an authenticated lease, relay ENet, handshake and roster on one update thread. */
class ServiceENetSession {
public:
    /** @brief Consumes prepared resources and exact ordered local gamer names.
     * @param prepared Owned service membership/transport. @param localNames Exact local group.
     * @param dependencies Private fixture injection, empty for native online mode. */
    ServiceENetSession(std::unique_ptr<PreparedOnlineSession> prepared,std::vector<std::string> localNames,
        ServiceENetDependencies dependencies={});
    /** @brief Cancels control work, closes peers and best-effort releases the owned lease. */
    ~ServiceENetSession();
    /** @brief Prevents duplicated session resource ownership. */
    ServiceENetSession(const ServiceENetSession&)=delete;
    /** @brief Prevents duplicated session resource ownership. */
    ServiceENetSession& operator=(const ServiceENetSession&)=delete;
    /** @brief Pumps bounded control/ENet work without constructing XNA objects or raising events.
     * @return Owned logical observations on the caller thread. */
    std::vector<ServiceENetObservation> update();
    /** @brief Sends from one local ID to an admitted local/remote target.
     * @param sender Local source. @param target Known connected target. @param payload Game bytes.
     * @param options Delivery semantics. */
    void send(unsigned char sender,unsigned char target,const std::vector<unsigned char>& payload,SendDataOptions options);
    /** @brief Sends local gamers' lobby ready state (a client to the host, the host to every
     * client); the host also reports any gamer, as ResetReady does.
     * @param entries Changed ready states. */
    void publishReady(const std::vector<GamerReadyEntry>& entries);
    /** @brief Host only: removes another machine from the session (XNA NetworkMachine.RemoveFromSession).
     * The directory removes it; the host then disconnects it with DisconnectRemovedByHost.
     * @param machine The machine to remove. */
    void removeMachine(const std::string& machine);
    /** @brief Publishes host-owned directory settings; clients observe them through authority.
     * @param settings Complete desired state, capacity, join-in-progress and properties. */
    void publish(const GamerServices::ServiceSessionSettings& settings);
    /** @brief Asks the service to add signed-in local accounts to this machine's group
     * (XNA AddLocalGamer); the outcome is a later LocalAdded or AddFailed observation.
     * @param names Gamertags, in order. @param users Their accounts. */
    void addLocal(std::vector<std::string> names,std::vector<std::string> users);
    /** @brief Gets whether host resources/client welcome are established. @return Ready flag. */
    bool ready() const;
    /** @brief Gets current authenticated metadata. @return Owner-thread snapshot. */
    const GamerServices::ServiceSessionSnapshot& snapshot() const;
    /** @brief Gets the backend that owns this membership. @return Retained origin. */
    const std::shared_ptr<GamerServices::IGamerServicesBackend>& origin() const;
    /** @brief Gets rejected source/control/application packet count. @return Cumulative drops. */
    std::uint64_t rejected() const;
    /** @brief Gets the round trip to each remote gamer: the direct peer's on the host; on a client the
     * round trip to the host, plus the host's reported round trip for a gamer on another client.
     * @return Milliseconds by gamer ID; empty before a client is connected. */
    std::map<unsigned char,std::uint32_t> roundTrips() const;
    /** @brief Gets this session's ENet wire totals, protocol overhead included.
     * @return Cumulative bytes sent and received, each wrapping at 2^32. */
    std::pair<std::uint32_t,std::uint32_t> traffic() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
