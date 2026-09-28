// SPDX-License-Identifier: MS-PL
#pragma once
#include "ServiceRoster.hpp"
#include <variant>

namespace CNA::Internal::Net {
/** @brief Owned control data admitted by an authenticated service machine, never an XNA object. */
using ServiceGameControl=std::variant<ClientHelloMessage,ServerWelcomeMessage,GamerJoinBroadcastMessage,GamerLeaveBroadcastMessage,
    StateChangeBroadcastMessage,SessionPropertiesBroadcastMessage>;
/** @brief Immutable directory authority applied before any online peer packet mutates game state. */
class ServiceGamePacketPolicy {
public:
    /** @brief Indexes a full authenticated snapshot. @param snapshot Directory membership. */
    explicit ServiceGamePacketPolicy(const GamerServices::ServiceSessionSnapshot& snapshot);
    /** @brief Validates control direction, source, complete groups and directory-owned metadata.
     * @param source Authenticated relay source machine, never a packet-supplied identity.
     * @param bytes Complete bounded control message. @param channel ENet channel, zero for control.
     * @param established Whether welcome has completed on the requesting machine.
     * @param localNames Ordered exact local gamer claims for welcome validation.
     * @param admitted Remote groups already connected, required for a leave broadcast.
     * @return Owned validated control value. */
    ServiceGameControl control(const std::string& source,std::span<const unsigned char> bytes,
        unsigned char channel,bool established,const std::vector<std::string>& localNames,
        const std::vector<std::string>& admitted={}) const;
    /** @brief Validates game sender/target before allocating the bounded payload copy.
     * @param source Authenticated relay source machine. @param bytes Complete game message.
     * @param channel One of the two configured ENet channels. @param established Welcome completed.
     * @param admitted Remote machines whose full groups completed their realtime handshake.
     * @return Owned application message; throws before mutation for unauthorized IDs. */
    AppDataMessage application(const std::string& source,std::span<const unsigned char> bytes,
        unsigned char channel,bool established,const std::vector<std::string>& admitted) const;
private:
    void sourceGuard(const std::string& source) const;
    ServiceRoster roster_;
    GamerServices::ServiceSessionSnapshot snapshot_;
    std::map<unsigned char,std::string> machinesById_;
    std::map<std::string,std::size_t> groupSizes_;
};
}
