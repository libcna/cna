// SPDX-License-Identifier: MS-PL
#pragma once
#include "RelayTransport.hpp"
#include "CNA/Internal/Net/ENetHostHandle.hpp"
#include "CNA/Internal/GamerServices/IGamerServicesBackend.hpp"
#include <functional>

namespace CNA::Internal::Net {
/** @brief Owned native preparation resources; private fakes implement the same lifetime boundary. */
class IPreparedOnlineTransport {
public:
    /** @brief Releases sockets and joins owned transport workers. */
    virtual ~IPreparedOnlineTransport()=default;
    /** @brief Gets the loopback ENet host. @return Owned host. */
    virtual ENetHostHandle& host()=0;
    /** @brief Gets the native relay, absent only for explicit deterministic fixtures. @return Bridge. */
    virtual RelayTransport* relay()=0;
    /** @brief Observes transport readiness without publishing XNA events. @return Safe status. */
    virtual RelayTransportStatus status() const=0;
    /** @brief Re-establishes a failed relay with fresh authority, keeping local routes.
     * @param ticket New one-use authority for this machine. */
    virtual void reconnect(GamerServices::ServiceRelayTicket ticket)=0;
};
/** @brief Creates bounded native loopback ENet and verified relay resources.
 * @param configuration Immutable deployment authority. @param ticket One-use bearer authority.
 * @param machines Authorized remote routes. @return Owned transport. */
std::unique_ptr<IPreparedOnlineTransport> makeNativePreparedTransport(
    const CNA::GamerServices::Configuration& configuration,GamerServices::ServiceRelayTicket ticket,
    const std::vector<std::string>& machines);
/** @brief Logical request containing identities, never public gamer pointers or bearer credentials. */
struct OnlineSessionRequest {
    /** @brief Distinguishes new hosting from joining existing membership. */
    enum class Operation {
        /** @brief Creates host membership. */ Create,
        /** @brief Acquires membership in an existing session. */ Join
    };
    /** @brief Membership acquisition operation. */
    Operation operation=Operation::Create;
    /** @brief Ordered local identities; owner must be the first of one through four. */
    std::string owner;
    std::vector<std::string> users;
    /** @brief Directory category and hosting settings. */
    GamerServices::ServiceSessionKind kind=GamerServices::ServiceSessionKind::PlayerMatch;
    GamerServices::ServiceSessionSettings settings;
    /** @brief Existing session and optional accepted invitation for joining. */
    std::string session,invite;
};
/** @brief Private dependency boundary for deterministic cancellation/failure tests. */
struct OnlinePreparationDependencies {
    /** @brief Resolves immutable origin deployment authority; defaults to the actual online backend. */
    std::function<CNA::GamerServices::Configuration(const GamerServices::IGamerServicesBackend&)> configuration;
    /** @brief Creates owned transport; defaults to bounded native ENet and verified WSS. */
    std::function<std::unique_ptr<IPreparedOnlineTransport>(const CNA::GamerServices::Configuration&,
        GamerServices::ServiceRelayTicket,const std::vector<std::string>&)> transport;
};
/** @brief Consumed membership and transport ownership, retained independently of an APM result. */
class PreparedOnlineSession {
public:
    /** @brief Closes transport, then best-effort releases membership while origin remains alive. */
    ~PreparedOnlineSession();
    /** @brief Prevents duplicated membership ownership. */
    PreparedOnlineSession(const PreparedOnlineSession&)=delete;
    /** @brief Prevents duplicated membership ownership. */
    PreparedOnlineSession& operator=(const PreparedOnlineSession&)=delete;
    /** @brief Gets authenticated membership. @return Immutable snapshot. */
    const GamerServices::ServiceSessionSnapshot& snapshot() const;
    /** @brief Gets the origin retained for control/lease operations. @return Origin backend. */
    const std::shared_ptr<GamerServices::IGamerServicesBackend>& backend() const;
    /** @brief Gets owned prepared transport. @return Resources. */
    IPreparedOnlineTransport& transport();
    /** @brief Closes sockets and releases membership at most once. @return Leave succeeded. */
    bool release() noexcept;
private:
    friend class OnlineSessionPreparation;
    PreparedOnlineSession(std::shared_ptr<GamerServices::IGamerServicesBackend> backend,
        std::string owner,GamerServices::ServiceSessionSnapshot snapshot,
        std::unique_ptr<IPreparedOnlineTransport> transport);
    std::shared_ptr<GamerServices::IGamerServicesBackend> backend_;
    std::string owner_;
    GamerServices::ServiceSessionSnapshot snapshot_;
    std::unique_ptr<IPreparedOnlineTransport> transport_;
    bool released_=false,releaseSucceeded_=false;
};
/** @brief Caller-owned asynchronous preparation with no queued backend ownership cycle.
 * Construction, cancellation, consumption and completion dispatch share one owner thread;
 * only the separate synchronized work state is accessed by the service worker. */
class OnlineSessionPreparation {
public:
    /** @brief Queues acquisition and relay readiness on the originating service executor.
     * @param backend Retained executor/origin. @param request Owned logical request.
     * @param completion Owner-pump notification, suppressed after cancellation.
     * @param dependencies Explicit private test providers, empty for native service mode. */
    OnlineSessionPreparation(std::shared_ptr<GamerServices::IGamerServicesBackend> backend,
        OnlineSessionRequest request,std::function<void()> completion={},
        OnlinePreparationDependencies dependencies={});
    /** @brief Cancels and releases any unconsumed prepared value. */
    ~OnlineSessionPreparation();
    /** @brief Prevents duplicated preparation ownership. */
    OnlineSessionPreparation(const OnlineSessionPreparation&)=delete;
    /** @brief Prevents duplicated preparation ownership. */
    OnlineSessionPreparation& operator=(const OnlineSessionPreparation&)=delete;
    /** @brief Checks owner-pump completion. @return Whether completion has been published. */
    bool complete() const;
    /** @brief Claims prepared ownership once on the owner thread. @return Independent lease. */
    std::unique_ptr<PreparedOnlineSession> take();
    /** @brief Suppresses callbacks and rolls back ready resources; active work observes cancellation. */
    void cancel() noexcept;
private:
    struct State;
    std::shared_ptr<GamerServices::IGamerServicesBackend> backend_;
    std::shared_ptr<State> state_;
};
}
