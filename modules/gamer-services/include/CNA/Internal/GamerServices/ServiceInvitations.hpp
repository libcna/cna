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
    /** @brief Binds a remote session gamer to its authenticated service account.
     * @param gamer Gamer. @param userId Account ID from directory authority. */
    static void setUserId(Microsoft::Xna::Framework::GamerServices::Gamer& gamer,std::string userId){gamer.serviceUserId_=std::move(userId);}
    /** @brief Disposes a gamer, as signing it out does. @param gamer Gamer. */
    static void dispose(Microsoft::Xna::Framework::GamerServices::Gamer& gamer){gamer.isDisposed_=true;}
    /** @brief Sets a signed-in gamer's party size (XNA's internal setter). @param gamer Gamer.
     * @param size People in the party, 0 for none. */
    static void setPartySize(Microsoft::Xna::Framework::GamerServices::SignedInGamer& gamer,int size);
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
/** @brief Defers Guide notifications (invitation prompts); a delay already in force is kept.
 * @param milliseconds Requested delay, capped at 120 seconds like the reference. */
void delayNotifications(long long milliseconds);
/** @brief Records a gamer met in an online session for the Guide Players pane.
 * @param gamertag Remote gamer; the newest 30 distinct names are kept. */
void rememberRecentPlayer(const std::string& gamertag);
/** @brief Gets recently met players, newest first. @return Gamertags. */
std::vector<std::string> recentPlayers();
/** @brief Joins a friend's or party member's joinable game from the Guide: the service grants a join
 * request, it is accepted as an invitation, and the game hears InviteAccepted as for any invitation.
 * @param user Local service identity. @param gamertag Whose game. @param failed Owner-thread
 * completion with a reason when nothing could be joined. */
void joinFriendGame(const std::string& user,const std::string& gamertag,std::function<void(std::string)> failed={});

/** @brief Polls each signed-in account's party (PartySize follows it) and announces party
 * invitations once each. Called by GamerServicesDispatcher.Update beside pumpInvitations. */
void pumpParties();
/** @brief The last party read for an account, if one was. @param user Account. @return Party. */
std::optional<ServiceParty> knownParty(const std::string& user);
/** @brief Records a party the Guide just changed (PartySize follows at once). @param user Account.
 * @param party The service's answer. */
void applyParty(const std::string& user,ServiceParty party);
/** @brief Watches each signed-in account's inbox and friends and posts the console's social
 * notifications: a new message, a friend request, a friend coming online. The first read after
 * sign-in only learns what is already there. Called by GamerServicesDispatcher.Update. */
void pumpSocial();
/** @brief Asks the watchers to read now rather than at their next interval (a push hint from the
 * service); any thread. @param topic "invitations", "party", "messages" or "friends". */
void serviceHint(const std::string& topic);
/** @brief Makes the next pumpSocial poll immediately; deterministic tests only. */
void pollSocialNowForTesting();
/** @brief Makes the next pumpParties poll immediately; deterministic tests only. */
void pollPartiesNowForTesting();
/** @brief Makes the next pumpInvitations poll immediately; deterministic tests only. */
void pollInvitationsNowForTesting();
/** @brief Forgets seen/queued invitations and any accepted invitation; deterministic tests only. */
void resetInvitationsForTesting();
}
