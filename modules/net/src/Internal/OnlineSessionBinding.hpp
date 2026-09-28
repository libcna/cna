// SPDX-License-Identifier: MS-PL
#pragma once
#include "OnlineSessionOperation.hpp"
#include "Microsoft/Xna/Framework/Net/NetworkSession.hpp"
#include "Microsoft/Xna/Framework/Net/NetworkMachine.hpp"
#include <map>

namespace CNA::Internal::Net {
/** @brief Explicit private providers for deterministic public online session tests. */
struct OnlineSessionFixture {
    /** @brief Supplies preparation fixtures; empty for secure native operation. */
    std::function<OnlinePreparationDependencies()> preparation;
    /** @brief Supplies route/clock fixtures; empty for secure native operation. */
    std::function<ServiceENetDependencies()> realtime;
};
/** @brief Installs fixtures used by later public Begin calls; an empty value restores native transport.
 * @param fixture Test-only providers; never reachable from the public XNA or C API. */
void setOnlineSessionFixtureForTesting(OnlineSessionFixture fixture);
/** @brief Gets the providers for a new public online operation. @return Current fixture. */
OnlineSessionFixture onlineSessionFixture();
/** @brief Rethrows a failed online Create/Join as the exception family XNA reports.
 * @param error Deferred operation failure. @param joining Whether the operation was a join. */
[[noreturn]] void throwOnlineEndFailure(std::exception_ptr error,bool joining);

/** @brief Owner-thread projection of one authenticated online engine onto its public NetworkSession.
 * Observations are converted only inside NetworkSession::Update; no XNA object is touched elsewhere. */
class OnlineSessionBinding {
public:
    /** @brief Projects the initial authenticated roster without queued join events.
     * @param session Owning public session, already holding the frozen local gamers.
     * @param established Consumed engine and observations accumulated before End.
     * @param localUsers Service account IDs of the local gamers, in session order. */
    OnlineSessionBinding(Microsoft::Xna::Framework::Net::NetworkSession& session,
        EstablishedOnlineSession established,std::vector<std::string> localUsers);
    /** @brief Releases the engine (disconnect and membership leave) and owned remote gamers. */
    ~OnlineSessionBinding();
    /** @brief Prevents duplicated ownership. */
    OnlineSessionBinding(const OnlineSessionBinding&)=delete;
    /** @brief Prevents duplicated ownership. */
    OnlineSessionBinding& operator=(const OnlineSessionBinding&)=delete;
    /** @brief Publishes host settings and converts engine observations into queued session events. */
    void pump();
    /** @brief Sends one application packet to an admitted remote gamer.
     * @param sender Local sender. @param target Remote recipient. @param payload Bytes.
     * @param options Delivery semantics. */
    void send(Microsoft::Xna::Framework::Net::NetworkGamer* sender,Microsoft::Xna::Framework::Net::NetworkGamer* target,
        const std::vector<SharpRuntime::bytecs>& payload,SendDataOptions options);
    /** @brief Records the host's requested gameplay state for directory publication.
     * @param state Lobby or Playing. */
    void requestState(NetworkSessionState state);
    /** @brief Closes realtime/membership resources before the session frees its gamers. */
    void close() noexcept;
    /** @brief Gets whether the session ended through service or transport loss. @return Ended flag. */
    bool ended() const {return ended_;}
    /** @brief Gets whether this machine hosts the session. @return Host flag. */
    bool host() const {return host_;}
    /** @brief Gets the opaque directory session. @return Session identifier. */
    const std::string& session() const {return snapshot_.session;}
private:
    using NetworkGamer=Microsoft::Xna::Framework::Net::NetworkGamer;
    void project(const ServiceENetObservation& ready);
    void convert(ServiceENetObservation observation);
    NetworkGamer* addRemote(const RosterEntry& entry);
    void apply(const GamerServices::ServiceSessionSnapshot& snapshot,bool initial);
    void end(const std::string& failure);
    std::shared_ptr<Microsoft::Xna::Framework::Net::NetworkMachine> machine(const std::string& id);
    GamerServices::ServiceSessionSettings desired() const;
    Microsoft::Xna::Framework::Net::NetworkSession& session_;
    std::unique_ptr<ServiceENetSession> engine_;
    std::vector<std::string> users_;
    std::vector<std::unique_ptr<NetworkGamer>> remote_;
    std::map<unsigned char,NetworkGamer*> gamers_;
    std::map<std::string,std::shared_ptr<Microsoft::Xna::Framework::Net::NetworkMachine>> machines_;
    GamerServices::ServiceSessionSnapshot snapshot_;
    std::optional<GamerServices::ServiceSessionSettings> requested_;
    GamerServices::ServiceSessionState desiredState_=GamerServices::ServiceSessionState::Lobby;
    GamerServices::ServiceSessionState observedState_=GamerServices::ServiceSessionState::Lobby;
    bool host_=false,ended_=false;
};
}
