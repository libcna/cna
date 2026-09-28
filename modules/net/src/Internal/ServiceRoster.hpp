// SPDX-License-Identifier: MS-PL
#pragma once
#include "CNA/Internal/GamerServices/ServiceSessionDirectory.hpp"
#include "CNA/Internal/Net/NetPacketCodec.hpp"
#include <map>
#include <span>

namespace CNA::Internal::Net {
/** @brief Preflights bounded service control bytes before the existing decoder allocates fields.
 * @param bytes Complete connected-channel control message. @return Validated control tag. */
MessageTag validateServiceControlPacket(std::span<const unsigned char> bytes);
/** @brief Immutable service authority used before online handshake/gamer mutation. */
class ServiceRoster {
public:
    /** @brief Validates and indexes an authenticated membership snapshot.
     * @param snapshot Full requesting-machine roster, never an advertisement. */
    explicit ServiceRoster(const GamerServices::ServiceSessionSnapshot& snapshot);
    /** @brief Assigns deterministic service wire IDs only for an exact machine account group.
     * @param machine Authenticated transport source. @param gamertags Ordered local claims.
     * @return Ordinal-plus-one IDs in the caller's local order. */
    std::vector<unsigned char> idsFor(const std::string& machine,const std::vector<std::string>& gamertags) const;
    /** @brief Gets unique remote machine routes from authority. @return Remote identities. */
    std::vector<std::string> remoteMachines() const;
    /** @brief Builds a host welcome using authority and complete connected machine groups.
     * @param machine Joining transport source. @param gamertags Exact local claims.
     * @param connected Other currently connected machines; host is included automatically.
     * @return Owned validated welcome, with no credentials. */
    ServerWelcomeMessage welcomeFor(const std::string& machine,const std::vector<std::string>& gamertags,
        const std::vector<std::string>& connected={}) const;
    /** @brief Verifies assignments, host identity, complete represented groups and properties.
     * @param welcome Decoded host response. @param locals Ordered local account names. */
    void validateWelcome(const ServerWelcomeMessage& welcome,const std::vector<std::string>& locals) const;
    /** @brief Verifies a roster broadcast before any remote gamer creation.
     * @param entries Known service identities and host flags. */
    void validateEntries(const std::vector<RosterEntry>& entries) const;
private:
    struct Member {std::string user,tag,machine;bool host=false;};
    GamerServices::ServiceSessionSnapshot snapshot_;
    std::map<unsigned char,Member> members_;
    std::map<std::string,unsigned char> tags_;
    std::map<std::string,std::vector<unsigned char>> machines_;
};
}
