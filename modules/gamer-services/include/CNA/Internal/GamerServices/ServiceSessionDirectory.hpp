// SPDX-License-Identifier: MS-PL
#pragma once
#include "Microsoft/Xna/Framework/GamerServices/GamerServicesNotAvailableException.hpp"
#include <array>
#include <optional>
#include <string>
#include <vector>

namespace CNA::Internal::GamerServices {
/** @brief Logical service failure carrying a safe, stable reason for internal exception mapping. */
class ServiceOperationError : public Microsoft::Xna::Framework::GamerServices::GamerServicesNotAvailableException {
public:
    /** @brief Constructs a logical failure. @param reason Stable credential-free code. */
    explicit ServiceOperationError(std::string reason);
    /** @brief Stable service error, never a transport payload or credential. */
    const std::string code;
};
/** @brief CNA directory category; independent of public XNA enum encoding. */
enum class ServiceSessionKind { PlayerMatch, Ranked };
/** @brief Logical lobby or active gameplay state. */
enum class ServiceSessionState { Lobby, Playing };
/** @brief Fixed nullable matchmaking properties. */
using ServiceSessionProperties = std::array<std::optional<int>,8>;
/** @brief Authenticated participant snapshot. */
struct ServiceSessionMember {
    /** @brief Stable account, public gamertag and opaque machine identity. */
    std::string userId, gamertag, machine;
    /** @brief Whether membership occupies a private slot. */
    bool privateSlot=false;
    /** @brief Unique session ordinal, zero through thirty. */
    int ordinal=0;
};
/** @brief Directory snapshot; membership alone does not establish realtime connectivity. */
struct ServiceSessionSnapshot {
    /** @brief Opaque session, host account/name/machine, and optional requesting machine. */
    std::string session, hostId, hostGamertag, hostMachine, machine;
    /** @brief Service category. */
    ServiceSessionKind kind=ServiceSessionKind::PlayerMatch;
    /** @brief Gameplay state. */
    ServiceSessionState state=ServiceSessionState::Lobby;
    /** @brief Capacity and occupied/free slot counts. */
    int maxGamers=2, privateSlots=0, currentGamers=0, openPublicSlots=0, openPrivateSlots=0;
    /** @brief Monotonic host-controlled revision. */
    int revision=1;
    /** @brief Whether a playing session permits joins. */
    bool allowJoinInProgress=false;
    /** @brief Whether the service hands the session to another machine when its host leaves
     * (member snapshots only; false in advertisements). */
    bool allowHostMigration=false;
    /** @brief Search metadata. */
    ServiceSessionProperties properties{};
    /** @brief Roster for authenticated membership reads; absent from advertisements. */
    std::vector<ServiceSessionMember> members;
};
/** @brief Host-controlled session configuration. */
struct ServiceSessionSettings {
    /** @brief Capacity. */
    int maxGamers=2, privateSlots=0;
    /** @brief Gameplay state. */
    ServiceSessionState state=ServiceSessionState::Lobby;
    /** @brief Join-in-progress policy. */
    bool allowJoinInProgress=false;
    /** @brief Host migration policy (sent only to a service with the host-migration capability). */
    bool allowHostMigration=false;
    /** @brief Fixed metadata. */
    ServiceSessionProperties properties{};
};
/** @brief Bounded directory page. */
struct ServiceSessionPage {
    /** @brief Zero-based result offset. */
    int start=0;
    /** @brief Whether another page exists. */
    bool more=false;
    /** @brief Advertisements without member/credential data. */
    std::vector<ServiceSessionSnapshot> sessions;
};
/** @brief Explicit invitation lifecycle. */
enum class ServiceInvitationState { Pending, Accepted, Dismissed, Used };
/** @brief Recipient-bound service invitation, without bearer authority. */
struct ServiceInvitation {
    /** @brief Opaque invitation/session and public sender identity. */
    std::string invite, session, senderId, senderGamertag;
    /** @brief Target directory category. */
    ServiceSessionKind kind=ServiceSessionKind::PlayerMatch;
    /** @brief Invitation state. */
    ServiceInvitationState state=ServiceInvitationState::Pending;
    /** @brief Unix-second timestamps; acceptedAt is zero before acceptance. */
    long long created=0, expires=0, acceptedAt=0;
};
/** @brief Bounded authenticated inbox page. */
struct ServiceInvitationPage {
    /** @brief Zero-based offset. */
    int start=0;
    /** @brief Whether another page exists. */
    bool more=false;
    /** @brief Pending or accepted live invitations. */
    std::vector<ServiceInvitation> invites;
};
/** @brief Ephemeral relay authority for an internal secure transport; never game-facing. */
struct ServiceRelayTicket {
    /** @brief One-use bearer secret; do not log or persist. */
    std::string ticket;
    /** @brief Bound session and machine. */
    std::string session, machine;
    /** @brief Server-issued Unix-second time and short expiration. */
    long long issuedAt=0, expires=0;
};
/** @brief Private logical control boundary; callers retain the owning backend during use. */
class IServiceSessionDirectory {
public:
    /** @brief Releases control resources. */
    virtual ~IServiceSessionDirectory()=default;
    /** @brief Creates leased membership. @param owner Local host identity. @param users Local accounts.
     * @param kind Directory category. @param settings Initial settings. @return Authenticated roster. */
    virtual ServiceSessionSnapshot create(const std::string& owner,const std::vector<std::string>& users,
        ServiceSessionKind kind,const ServiceSessionSettings& settings)=0;
    /** @brief Searches public slots. @param actor Authenticated local identity. @param kind Category.
     * @param locals Required local capacity. @param properties Nullable filters. @param start Offset.
     * @param limit Page size. @return Bounded advertisements. */
    virtual ServiceSessionPage find(const std::string& actor,ServiceSessionKind kind,int locals,
        const ServiceSessionProperties& properties,int start,int limit)=0;
    /** @brief Joins ordinary or explicitly accepted invited membership. @param actor Machine owner.
     * @param users Local identities. @param session Session ID. @param invite Optional accepted invite.
     * @return Authenticated roster. */
    virtual ServiceSessionSnapshot join(const std::string& actor,const std::vector<std::string>& users,
        const std::string& session,const std::string& invite={})=0;
    /** @brief Reads a member roster. @param actor Member identity. @param session ID. @return Snapshot. */
    virtual ServiceSessionSnapshot get(const std::string& actor,const std::string& session)=0;
    /** @brief Renews a machine lease. @param actor Machine owner. @param session ID. @return Snapshot. */
    virtual ServiceSessionSnapshot touch(const std::string& actor,const std::string& session)=0;
    /** @brief Updates host settings conditionally. @param owner Host identity. @param session ID.
     * @param revision Expected revision. @param settings New settings. @return Updated snapshot. */
    virtual ServiceSessionSnapshot update(const std::string& owner,const std::string& session,int revision,
        const ServiceSessionSettings& settings)=0;
    /** @brief Removes the owning machine's group. @param actor Machine owner. @param session ID.
     * @return Whether host departure closed the session. */
    virtual bool leave(const std::string& actor,const std::string& session)=0;
    /** @brief Removes another machine and all its users (XNA NetworkMachine.RemoveFromSession).
     * Its users are then answered REMOVED_BY_HOST. @param owner Host identity. @param session ID.
     * @param machine The machine to remove. @return The host's updated snapshot. */
    virtual ServiceSessionSnapshot remove(const std::string& owner,const std::string& session,const std::string& machine)=0;
    /** @brief Issues one-use secure relay authority for the exact authenticated local group.
     * @param actor Machine owner. @param users Local identities. @param session ID.
     * @return Ephemeral authority; issuance alone does not establish a data connection. */
    virtual ServiceRelayTicket issueRelayTicket(const std::string& actor,const std::vector<std::string>& users,
        const std::string& session)=0;
    /** @brief Sends an invitation. @param actor Member identity. @param session ID.
     * @param gamertag Recipient. @return Pending or duplicate accepted invitation. */
    virtual ServiceInvitation sendInvite(const std::string& actor,const std::string& session,const std::string& gamertag)=0;
    /** @brief Reads a recipient inbox. @param actor Recipient identity. @param start Offset.
     * @param limit Page size. @return Live pending/accepted invitations. */
    virtual ServiceInvitationPage listInvites(const std::string& actor,int start,int limit)=0;
    /** @brief Reads recipient-bound invitation state. @param actor Recipient. @param invite ID.
     * @return Invitation. */
    virtual ServiceInvitation getInvite(const std::string& actor,const std::string& invite)=0;
    /** @brief Records explicit recipient confirmation. @param actor Recipient. @param invite ID.
     * @return Accepted invitation. */
    virtual ServiceInvitation acceptInvite(const std::string& actor,const std::string& invite)=0;
    /** @brief Dismisses an unconsumed invite. @param actor Recipient. @param invite ID.
     * @return Dismissed invitation. */
    virtual ServiceInvitation dismissInvite(const std::string& actor,const std::string& invite)=0;
};
}
