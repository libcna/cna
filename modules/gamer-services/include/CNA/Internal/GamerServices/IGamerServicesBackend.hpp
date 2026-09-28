// SPDX-License-Identifier: MS-PL
#pragma once
#include <array>
#include <functional>
#include <memory>
#include <string>
#include <vector>
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
    /** @brief Account online-session permission. */
    bool allowOnlineSessions = false;
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
    /** @brief Friend's current title-scoped rich presence. */
    std::string presence;
};
/** @brief Work completion applied at the dispatcher's controlled update boundary. */
struct BackendEvent {
    /** @brief Event category. */
    enum class Type { SignedIn, SignedOut, Failed, Completion };
    /** @brief Event category. */
    Type type = Type::Failed;
    /** @brief Local player slot, zero through three. */
    int slot = 0;
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
    /** @brief Queues authentication. @param slot Local slot. @param username Account login.
     * @param password Secret, never logged. */
    virtual void signIn(int slot,std::string username,std::string password) = 0;
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
    /** @brief Awards a title-defined achievement. @param userId Identity. @param key Catalog key. */
    virtual void award(const std::string& userId,const std::string& key) = 0;
    /** @brief Gets friend snapshot. @param userId Identity. @return Friends. */
    virtual std::vector<ServiceFriend> friends(const std::string& userId) = 0;
    /** @brief Changes an account friendship. @param userId Actor. @param gamertag Target.
     * @param action Request, accept, or remove. */
    virtual void changeFriend(const std::string& userId,const std::string& gamertag,const std::string& action) = 0;
    /** @brief Publishes rich presence. @param userId Actor. @param mode Stable mode. @param text Display text. */
    virtual void setPresence(const std::string& userId,int mode,const std::string& text) = 0;
    /** @brief Retrieves immutable asset bytes, with verified local cache where configured.
     * @param hash SHA-256 identifier. @return Resource bytes. */
    virtual std::vector<unsigned char> asset(const std::string& hash) = 0;
};
/** @brief Gets lazily configured backend. @return Shared backend lifetime. */
std::shared_ptr<IGamerServicesBackend> backend();
/** @brief Injects an explicit test backend. @param value Backend or null to reset configuration. */
void setBackendForTesting(std::shared_ptr<IGamerServicesBackend> value);
/** @brief Creates deterministic fake with explicitly supplied identities and catalog.
 * @param identities Accounts. @param catalog Title catalog. @return Fake backend. */
std::shared_ptr<IGamerServicesBackend> makeFakeBackend(std::vector<ServiceIdentity> identities,
                                                     std::vector<ServiceAchievement> catalog = {});
}
