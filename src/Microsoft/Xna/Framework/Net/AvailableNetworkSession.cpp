// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Net/AvailableNetworkSession.hpp"

namespace Microsoft::Xna::Framework::Net
{
    AvailableNetworkSession::AvailableNetworkSession(
        int numGamers,
        const std::string& host,
        int privateSlots,
        int publicSlots,
        NetworkSessionProperties properties,
        QualityOfService qos,
        const std::string& hostAddress,
        uint16_t hostPort
    )
        : currentGamerCount_(numGamers)
        , hostGamertag_(host)
        , openPrivateGamerSlots_(privateSlots)
        , openPublicGamerSlots_(publicSlots)
        , sessionProperties_(std::move(properties))
        , qualityOfService_(std::move(qos))
        , hostAddress_(hostAddress)
        , hostPort_(hostPort)
    {
    }

    AvailableNetworkSession AvailableNetworkSession::CreateInternal(
        int numGamers,
        const std::string& host,
        int privateSlots,
        int publicSlots,
        NetworkSessionProperties properties,
        QualityOfService qos,
        const std::string& hostAddress,
        uint16_t hostPort
    ) {
        return AvailableNetworkSession(
            numGamers, host, privateSlots, publicSlots, std::move(properties), std::move(qos), hostAddress, hostPort
        );
    }

    int AvailableNetworkSession::getCurrentGamerCountProperty() const     { return currentGamerCount_; }
    const std::string& AvailableNetworkSession::getHostGamertagProperty() const { return hostGamertag_; }
    int AvailableNetworkSession::getOpenPrivateGamerSlotsProperty() const { return openPrivateGamerSlots_; }
    int AvailableNetworkSession::getOpenPublicGamerSlotsProperty() const  { return openPublicGamerSlots_; }

    const QualityOfService& AvailableNetworkSession::getQualityOfServiceProperty() const
    {
        return qualityOfService_;
    }

    const NetworkSessionProperties& AvailableNetworkSession::getSessionPropertiesProperty() const
    {
        return sessionProperties_;
    }

    const std::string& AvailableNetworkSession::GetConnectAddress() const { return hostAddress_; }
    uint16_t AvailableNetworkSession::GetConnectPort() const { return hostPort_; }

    bool AvailableNetworkSession::operator==(const AvailableNetworkSession& other) const
    {
        return currentGamerCount_ == other.currentGamerCount_
            && hostGamertag_ == other.hostGamertag_
            && openPrivateGamerSlots_ == other.openPrivateGamerSlots_
            && openPublicGamerSlots_ == other.openPublicGamerSlots_
            && hostAddress_ == other.hostAddress_
            && hostPort_ == other.hostPort_;
    }

    bool AvailableNetworkSession::operator!=(const AvailableNetworkSession& other) const
    {
        return !(*this == other);
    }
}
