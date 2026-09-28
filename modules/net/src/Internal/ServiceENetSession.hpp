// SPDX-License-Identifier: MS-PL
#pragma once
#include "OnlineSessionPreparation.hpp"
#include "CNA/Internal/Net/NetPacketCodec.hpp"
#include <chrono>
#include <optional>

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
    /** @brief Publishes host-owned directory settings; clients observe them through authority.
     * @param settings Complete desired state, capacity, join-in-progress and properties. */
    void publish(const GamerServices::ServiceSessionSettings& settings);
    /** @brief Gets whether host resources/client welcome are established. @return Ready flag. */
    bool ready() const;
    /** @brief Gets current authenticated metadata. @return Owner-thread snapshot. */
    const GamerServices::ServiceSessionSnapshot& snapshot() const;
    /** @brief Gets rejected source/control/application packet count. @return Cumulative drops. */
    std::uint64_t rejected() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
