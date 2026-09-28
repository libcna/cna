// SPDX-License-Identifier: MS-PL
#pragma once
#include "CNA/Internal/GamerServices/IGamerServicesBackend.hpp"
#include "Microsoft/Xna/Framework/GamerServices/Gamer.hpp"
#include <functional>
#include <optional>

namespace Microsoft::Xna::Framework::GamerServices {class SignedInGamer;}

namespace CNA::Internal::GamerServices {
/** @brief Internal read access to a gamer's service identity; never exposed to games. */
class GamerAccess {
public:
    /** @brief Gets the authenticated service account. @param gamer Gamer. @return Account ID, empty offline. */
    static const std::string& userId(const Microsoft::Xna::Framework::GamerServices::Gamer& gamer){return gamer.serviceUserId_;}
};
/** @brief The online session this process currently belongs to, registered by Net for Guide invitations. */
struct ActiveOnlineSession {
    /** @brief Opaque directory session. */
    std::string session;
    /** @brief Directory category. */
    ServiceSessionKind kind=ServiceSessionKind::PlayerMatch;
    /** @brief Service identities of the local members, in session order. */
    std::vector<std::string> users;
    /** @brief Backend that owns the membership; the session binding keeps it alive. */
    std::weak_ptr<IGamerServicesBackend> origin;
};
/** @brief Registers or clears the active online session. @param value Session or empty. */
void setActiveOnlineSession(std::optional<ActiveOnlineSession> value);
/** @brief Gets the active online session. @return Registered session, if any. */
const std::optional<ActiveOnlineSession>& activeOnlineSession();

/** @brief A recipient-accepted invitation waiting for NetworkSession.JoinInvited. */
struct AcceptedInvitation {
    /** @brief Accepted service invitation. */
    ServiceInvitation invitation;
    /** @brief Recipient service identity. */
    std::string user;
    /** @brief Recipient's published signed-in gamer. */
    Microsoft::Xna::Framework::GamerServices::SignedInGamer* gamer=nullptr;
    /** @brief Backend that accepted it. */
    std::weak_ptr<IGamerServicesBackend> origin;
};
/** @brief Gets the owner-thread accepted invitation slot. @return Mutable slot. */
std::optional<AcceptedInvitation>& acceptedInvitation();
/** @brief Installs the Net-side sink that raises the public InviteAccepted event.
 * @param sink Called on the dispatcher update owner after the Guide acceptance succeeded. */
void setInviteAcceptedSink(std::function<void(const AcceptedInvitation&)> sink);

/** @brief Polls recipient inboxes, prompts through the Guide and accepts or dismisses invitations.
 * Called by GamerServicesDispatcher.Update outside nested End pumping. */
void pumpInvitations();
/** @brief Queues invitations for the active online session at the dispatcher update boundary.
 * @param user Sending local service identity. @param gamertags Recipients.
 * @param done Owner-thread completion with the number of failed recipients. */
void sendInvitations(const std::string& user,const std::vector<std::string>& gamertags,std::function<void(int)> done);
/** @brief Makes the next pumpInvitations poll immediately; deterministic tests only. */
void pollInvitationsNowForTesting();
/** @brief Forgets seen/queued invitations and any accepted invitation; deterministic tests only. */
void resetInvitationsForTesting();
}
