// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Net/AvailableNetworkSession.hpp"
#include "CNA/Internal/GamerServices/ServiceSessionDirectory.hpp"
#include "../Internal/OnlineQosResult.hpp"

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
        uint16_t hostPort,
        NetworkSessionType sessionType
    )
        : currentGamerCount_(numGamers)
        , hostGamertag_(host)
        , openPrivateGamerSlots_(privateSlots)
        , openPublicGamerSlots_(publicSlots)
        , sessionProperties_(std::move(properties))
        , qualityOfService_(std::move(qos))
        , hostAddress_(hostAddress)
        , hostPort_(hostPort)
        , sessionType_(sessionType)
    {
        sessionProperties_.makeReadOnly();
    }

    AvailableNetworkSession AvailableNetworkSession::CreateInternal(
        int numGamers,
        const std::string& host,
        int privateSlots,
        int publicSlots,
        NetworkSessionProperties properties,
        QualityOfService qos,
        const std::string& hostAddress,
        uint16_t hostPort,
        NetworkSessionType sessionType
    ) {
        return AvailableNetworkSession(
            numGamers, host, privateSlots, publicSlots, std::move(properties), std::move(qos),
            hostAddress, hostPort, sessionType
        );
    }

    int AvailableNetworkSession::getCurrentGamerCountProperty() const     { return currentGamerCount_; }
    const std::string& AvailableNetworkSession::getHostGamertagProperty() const { return hostGamertag_; }
    int AvailableNetworkSession::getOpenPrivateGamerSlotsProperty() const { return openPrivateGamerSlots_; }
    int AvailableNetworkSession::getOpenPublicGamerSlotsProperty() const  { return openPublicGamerSlots_; }

    const QualityOfService& AvailableNetworkSession::getQualityOfServiceProperty() const
    {
        // Refresh AvailableNetworkSession.get_QualityOfService IL_0000-002e:
        // adopt a new snapshot only while the parent is live; old snapshots remain immutable.
        if (onlineQos_) {
            const bool live = !collectionDisposed_ || !*collectionDisposed_;
            if (const auto* measured = onlineQos_->poll(live)) return *measured;
        }
        return qualityOfService_;
    }

    const NetworkSessionProperties& AvailableNetworkSession::getSessionPropertiesProperty() const
    {
        return sessionProperties_;
    }

    const std::string& AvailableNetworkSession::GetConnectAddress() const { return hostAddress_; }
    uint16_t AvailableNetworkSession::GetConnectPort() const { return hostPort_; }
    NetworkSessionType AvailableNetworkSession::GetSessionType() const { return sessionType_; }

    bool AvailableNetworkSession::operator==(const AvailableNetworkSession& other) const
    {
        return currentGamerCount_ == other.currentGamerCount_
            && hostGamertag_ == other.hostGamertag_
            && openPrivateGamerSlots_ == other.openPrivateGamerSlots_
            && openPublicGamerSlots_ == other.openPublicGamerSlots_
            && hostAddress_ == other.hostAddress_
            && hostPort_ == other.hostPort_
            && sessionType_ == other.sessionType_
            && ((!serviceSnapshot_ && !other.serviceSnapshot_)
                || (serviceSnapshot_ && other.serviceSnapshot_ && serviceSnapshot_->session==other.serviceSnapshot_->session));
    }

    bool AvailableNetworkSession::operator!=(const AvailableNetworkSession& other) const
    {
        return !(*this == other);
    }
}
