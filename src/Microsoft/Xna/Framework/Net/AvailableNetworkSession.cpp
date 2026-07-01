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
        QualityOfService qos
    )
        : currentGamerCount_(numGamers)
        , hostGamertag_(host)
        , openPrivateGamerSlots_(privateSlots)
        , openPublicGamerSlots_(publicSlots)
        , sessionProperties_(std::move(properties))
        , qualityOfService_(std::move(qos))
    {
    }

    AvailableNetworkSession AvailableNetworkSession::CreateInternal(
        int numGamers,
        const std::string& host,
        int privateSlots,
        int publicSlots,
        NetworkSessionProperties properties,
        QualityOfService qos
    ) {
        return AvailableNetworkSession(numGamers, host, privateSlots, publicSlots, std::move(properties), std::move(qos));
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

    bool AvailableNetworkSession::operator==(const AvailableNetworkSession& other) const
    {
        return currentGamerCount_ == other.currentGamerCount_
            && hostGamertag_ == other.hostGamertag_
            && openPrivateGamerSlots_ == other.openPrivateGamerSlots_
            && openPublicGamerSlots_ == other.openPublicGamerSlots_;
    }

    bool AvailableNetworkSession::operator!=(const AvailableNetworkSession& other) const
    {
        return !(*this == other);
    }
}
