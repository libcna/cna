// SPDX-License-Identifier: MS-PL
#pragma once
#include <array>
#include <functional>
#include <memory>
#include <map>
#include <string>
#include <vector>
#include <optional>
#include <variant>
#include "CNA/Internal/GamerServices/ServiceSessionDirectory.hpp"
namespace CNA::Internal::GamerServices {
/** @brief Service identity/profile data, independent of XNA objects and wire representation. */
struct ServiceIdentity {
    /** @brief Stable service user identifier. */
    std::string userId;
    /** @brief Immutable public picture hash. */
    std::string picture;
    /** @brief Public gamer name. */
    std::string gamertag;
    /** @brief Public display name. */
    std::string displayName;
    /** @brief User motto. */
    std::string motto;
    /** @brief Region code. */
    std::string region = "US";
    /** @brief Persistent aggregate score. */
    int gamerScore = 0;
    /** @brief Number of earned achievements. */
    int totalAchievements = 0;
    /** @brief Titles in which the account has presence, an earned achievement or a leaderboard row. */
    int titlesPlayed = 0;
    /** @brief Account online-session permission. */
    bool allowOnlineSessions = false;
    /** @brief Fixture-only avatar description (1021 bytes), empty for none. */
    std::vector<unsigned char> avatar;
    /** @brief The account's game defaults as a JSON object (a local profile's form), empty for none. */
    std::string gameDefaults;
};
/** @brief Title-scoped catalog entry and user-earned state. */
struct ServiceAchievement {
    /** @brief Catalog key. */
    std::string key;
    /** @brief Display metadata. */
    std::string name, description, howToEarn, picture;
    /** @brief Award score. */
    int score = 0;
    /** @brief Visibility before award. */
    bool displayBeforeEarned = true;
    /** @brief Earned timestamp in .NET ticks, or zero. */
    long long earnedTicks = 0;
};
/** @brief Snapshot of a subscribed friend's service state. */
struct ServiceFriend {
    /** @brief Public gamertag. */
    std::string gamertag;
    /** @brief Recent authenticated activity. */
    bool online = false;
    /** @brief Mutual friendship, or pending request flags. */
    bool accepted = false, requestSent = false, requestReceived = false;
    /** @brief In this title's live session that admits joiners now with a public slot free. */
    bool joinable = false;
    /** @brief This title's unexpired invitations between the two: pending either way, and the
     * friend's answer to the caller's. */
    bool inviteReceivedFrom = false, inviteSentTo = false, inviteAccepted = false, inviteRejected = false;
    /** @brief Friend's current title-scoped rich presence. */
    std::string presence;
    /** @brief The online friend's account status is away or busy (never both). */
    bool away = false, busy = false;
};
/** @brief Most bytes a leaderboard Stream column carries (CNA's bound; hex on the wire). */
inline constexpr std::size_t MaxLeaderboardStreamBytes = 256;
/** @brief Typed service column, independent of public dictionary objects. */
struct ServiceLeaderboardColumn {
    /** @brief Provisioned type name. */
    std::string type;
    /** @brief Scalar data preserving integer precision. */
    std::variant<long long,double,std::string> value;
    /** @brief Compares typed values. @param other Other value. @return Equality. */
    bool operator==(const ServiceLeaderboardColumn& other) const = default;
};
/** @brief One ranked entry returned by a bounded service read. */
struct ServiceLeaderboardEntry {
    /** @brief Stable identity and public name. */
    std::string userId, gamertag;
    /** @brief Rating and global one-based rank. */
    long long rating=0;
    int rank=0;
    /** @brief Provisioned typed columns. */
    std::map<std::string,ServiceLeaderboardColumn> columns;
};
/** @brief Remote page with total independent of its bounded entry count. */
struct ServiceLeaderboardPage {
    /** @brief Zero-based page start and filtered total. */
    int start=0, total=0;
    /** @brief Entries on this page. */
    std::vector<ServiceLeaderboardEntry> entries;
};
/** @brief Explicit deterministic test board; never installed in normal service mode. */
struct ServiceLeaderboardFixture {
    /** @brief Board identity. */
    std::string key;
    /** @brief Game mode. */
    int mode=0;
    /** @brief Sort direction. */
    bool ascending=false;
    /** @brief Whether rows are written only through Ranked arbitration. */
    bool arbitrated=false;
    /** @brief Supplied test data. */
    std::vector<ServiceLeaderboardEntry> entries;
};
/** @brief Transient gameplay row submitted through an authenticated game scope. */
struct ServiceLeaderboardWrite {
    /** @brief Member identity and board key. */
    std::string userId, key;
    /** @brief Board game mode. */
    int mode=0;
    /** @brief Final rating. */
    long long rating=0;
    /** @brief Final typed columns. */
    std::map<std::string,ServiceLeaderboardColumn> columns;
    /** @brief Compares final rows. @param other Other row. @return Equality. */
    bool operator==(const ServiceLeaderboardWrite& other) const = default;
};
/** @brief One account message in a recipient's inbox. */
struct ServiceMessage {
    /** @brief Opaque message identifier and the sender's gamertag. */
    std::string id, sender;
    /** @brief Message text, at most 256 UTF-8 bytes. */
    std::string text;
    /** @brief Unix-second creation time. */
    long long created=0;
    /** @brief Whether the recipient has read it. */
    bool read=false;
};
/** @brief Bounded inbox page, newest first. */
struct ServiceMessagePage {
    /** @brief Page start, total stored and unread counts. */
    int start=0, total=0, unread=0;
    /** @brief Messages on this page. */
    std::vector<ServiceMessage> messages;
};
/** @brief Ranked round a machine's arbitrated rows belong to. */
struct ServiceArbitration {
    /** @brief Directory session. */
    std::string session;
    /** @brief A directory revision observed while playing the round. */
    int revision=0;
};
/** @brief Work completion applied at the dispatcher's controlled update boundary. */
struct BackendEvent {
    /** @brief Event category. */
    enum class Type { SignedIn, SignedOut, Failed, Completion };
    /** @brief Event category. */
    Type type = Type::Failed;
    /** @brief Local player slot, zero through three. */
    int slot = 0;
    /** @brief Whether a SignedIn identity is a service account (false for a local offline profile). */
    bool signedInToLive = true;
    /** @brief Identity snapshot. */
    ServiceIdentity identity;
    /** @brief Safe diagnostic code, never credentials. */
    std::string error;
    /** @brief Main-thread asynchronous completion callback. */
    std::function<void()> completion;
};
/** @brief Private service boundary; public XNA classes never serialize protocol packets. */
class IGamerServicesBackend {
public:
    /** @brief Releases backend resources and joins its bounded executor. */
    virtual ~IGamerServicesBackend() = default;
    /** @brief Reports configured service/fake mode. @return Whether enabled. */
    virtual bool serviceEnabled() const = 0;
    /** @brief Gets typed session control, retaining this backend throughout its use.
     * @return Directory and invitation operations without wire/credential data. */
    virtual IServiceSessionDirectory& sessionDirectory() = 0;
    /** @brief Queues authentication. @param slot Local slot. @param username Account login.
     * @param password Secret, never logged. */
    virtual void signIn(int slot,std::string username,std::string password) = 0;
    /** @brief Signs a local offline profile into a slot at the next pump; no service is involved.
     * @param slot Local slot. @param gamertag Validated local profile name. */
    virtual void signInLocal(int slot,const std::string& gamertag) = 0;
    /** @brief Queues revocation. @param slot Local slot. */
    virtual void signOut(int slot) = 0;
    /** @brief Takes bounded ready events. @return Ordered events. */
    virtual std::vector<BackendEvent> pump() = 0;
    /** @brief Queues logical work on the backend executor. @param work Background work.
     * @param completion Completion at Update boundary. */
    virtual void submit(std::function<void()> work,std::function<void()> completion) = 0;
    /** @brief Retrieves public identity/profile. @param gamertag Public name. @return Snapshot. */
    virtual ServiceIdentity profile(const std::string& gamertag) = 0;
    /** @brief Gets achievements. @param userId Authenticated identity. @return Catalog/state. */
    virtual std::vector<ServiceAchievement> achievements(const std::string& userId) = 0;
    /** @brief Awards a title-defined achievement. @param userId Identity. @param key Catalog key.
     * @return The achievement's name when this call earned it, else empty (already earned, or a
     * service that does not say). */
    virtual std::string award(const std::string& userId,const std::string& key) = 0;
    /** @brief Gets friend snapshot. @param userId Identity. @return Friends. */
    virtual std::vector<ServiceFriend> friends(const std::string& userId) = 0;
    /** @brief Changes an account friendship. @param userId Actor. @param gamertag Target.
     * @param action Request, accept, or remove. */
    virtual void changeFriend(const std::string& userId,const std::string& gamertag,const std::string& action) = 0;
    /** @brief Sends an account message. @param userId Sender. @param gamertags 1..100 recipients.
     * @param text Message text. */
    virtual void sendMessage(const std::string& userId,const std::vector<std::string>& gamertags,const std::string& text) = 0;
    /** @brief Reads an inbox page. @param userId Recipient. @param start Offset. @param limit Page size.
     * @return Newest-first page. */
    virtual ServiceMessagePage messages(const std::string& userId,int start,int limit) = 0;
    /** @brief Marks read or deletes one of the recipient's messages. @param userId Recipient.
     * @param message Message ID. @param remove Delete instead of marking read. */
    virtual void updateMessage(const std::string& userId,const std::string& message,bool remove) = 0;
    /** @brief Records prefer/avoid feedback, or clears it. @param userId Reviewer.
     * @param gamertag Subject. @param rating "prefer", "avoid" or "clear". */
    virtual void reviewPlayer(const std::string& userId,const std::string& gamertag,const std::string& rating) = 0;
    /** @brief Publishes rich presence. @param userId Actor. @param mode Stable mode. @param text Display text. */
    virtual void setPresence(const std::string& userId,int mode,const std::string& text) = 0;
    /** @brief Sets the account-wide status friends see. @param userId Actor.
     * @param status "online", "away" or "busy". */
    virtual void setPresenceStatus(const std::string& userId,const std::string& status) = 0;
    /** @brief Reads accounts' avatar descriptions. @param userIds 1..16 service identities.
     * @return Description bytes per identity, in order; empty when the account has no avatar. */
    virtual std::vector<std::vector<unsigned char>> avatars(const std::vector<std::string>& userIds) = 0;
    /** @brief Stores a signed-in account's own avatar (the CNA avatar editor's save).
     * @param userId Signed-in account. @param description Valid 1021-byte CNA description.
     * @return The avatar's new revision. */
    virtual long long setAvatar(const std::string& userId,const std::vector<unsigned char>& description) = 0;
    /** @brief Reads an imported avatar catalog manifest. @param version Catalog version, 0 for the newest.
     * @return catalog.json text. */
    virtual std::string avatarCatalog(int version) = 0;
    /** @brief Retrieves immutable asset bytes, with verified local cache where configured.
     * @param hash SHA-256 identifier. @return Resource bytes. */
    virtual std::vector<unsigned char> asset(const std::string& hash) = 0;
    /** @brief Reads a remote page. @param key Board key. @param mode Game mode.
     * @param start Page start. @param size Page size. @param pivot Optional centered gamer.
     * @param gamers Optional restricted names. @return Ranked page. */
    virtual ServiceLeaderboardPage readLeaderboard(const std::string& key,int mode,int start,int size,
        const std::string& pivot,const std::optional<std::vector<std::string>>& gamers) = 0;
    /** @brief Begins a local gameplay write scope. @param users Authenticated local identities.
     * @return Opaque server-owned scope identifier. */
    virtual std::string beginLeaderboardGame(const std::vector<std::string>& users) = 0;
    /** @brief Cancels an uncommitted gameplay scope. @param gameplay Scope. @param owner Host identity. */
    virtual void abortLeaderboardGame(const std::string& gameplay,const std::string& owner) = 0;
    /** @brief Commits final rows atomically. @param gameplay Scope. @param owner Host identity.
     * @param rows Final writes. */
    virtual void commitLeaderboardGame(const std::string& gameplay,const std::string& owner,
        const std::vector<ServiceLeaderboardWrite>& rows,const std::optional<ServiceArbitration>& arbitration={}) = 0;
};
/** @brief Gets lazily configured backend. @return Shared backend lifetime. */
std::shared_ptr<IGamerServicesBackend> backend();
/**
 * @brief Publishes retained-operation completions from a superseded backend on the caller thread.
 *
 * Identity events from the superseded authority are discarded. Every completion in the batch is
 * dispatched before the first callback exception is rethrown.
 * @param executor Origin retained by the pending result; must differ from the active backend.
 */
void pumpRetainedCompletions(const std::shared_ptr<IGamerServicesBackend>& executor);
/** @brief Reports a final-write handler's restricted service context. @return Restricted flag. */
bool serviceCallsRestricted();
/** @brief Runs a final-write event without permitting other service calls.
 * @param callback Event delivery. */
void withRestrictedServiceCalls(const std::function<void()>& callback);
/** @brief Injects an explicit test backend. @param value Backend or null to reset configuration. */
void setBackendForTesting(std::shared_ptr<IGamerServicesBackend> value);
/** @brief Replaces an account's avatar on a fake backend. @param fake Backend from makeFakeBackend.
 * @param userId Account. @param description Bytes (empty = no avatar). */
void setFakeAvatar(IGamerServicesBackend& fake,const std::string& userId,std::vector<unsigned char> description);
/** @brief Makes a fake backend's avatar reads fail as an unreachable service would. @param fake Backend.
 * @param failing Whether reads fail. */
void setFakeAvatarsUnreachable(IGamerServicesBackend& fake,bool failing);
/** @brief Gives a fake backend an avatar catalog to serve. @param fake Backend from makeFakeBackend.
 * @param manifest catalog.json text. @param assets File bytes by SHA-256. */
void setFakeAvatarCatalog(IGamerServicesBackend& fake,std::string manifest,std::map<std::string,std::vector<unsigned char>> assets);
/** @brief Creates deterministic fake with explicitly supplied identities and catalog.
 * @param identities Accounts. @param catalog Title catalog. @param boards Explicit test boards.
 * @return Fake backend. */
std::shared_ptr<IGamerServicesBackend> makeFakeBackend(std::vector<ServiceIdentity> identities,
    std::vector<ServiceAchievement> catalog = {},std::vector<ServiceLeaderboardFixture> boards = {});
}
