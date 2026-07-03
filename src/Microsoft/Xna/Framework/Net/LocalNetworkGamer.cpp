// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Net/LocalNetworkGamer.hpp"
#include "System/IO/MemoryStream.hpp"
#include <algorithm>

namespace Microsoft::Xna::Framework::Net
{
    LocalNetworkGamer::LocalNetworkGamer(GamerServices::SignedInGamer* gamer, NetworkSession* session)
        : NetworkGamer(session)
        , signedInGamer_(gamer)
    {
    }

    LocalNetworkGamer LocalNetworkGamer::CreateInternal(GamerServices::SignedInGamer* gamer, NetworkSession* session)
    {
        return LocalNetworkGamer(gamer, session);
    }

    bool LocalNetworkGamer::getIsDataAvailableProperty() const { return !packetQueue_.empty(); }
    GamerServices::SignedInGamer* LocalNetworkGamer::getSignedInGamerProperty() const { return signedInGamer_; }
    bool LocalNetworkGamer::getIsLocalProperty() const { return true; }

    void LocalNetworkGamer::EnableSendVoice(NetworkGamer* /*remoteGamer*/, bool /*enable*/)
    {
    }

    void LocalNetworkGamer::SendPartyInvites()
    {
    }

    int LocalNetworkGamer::ReceiveData(std::vector<SharpRuntime::bytecs>& data, NetworkGamer*& sender)
    {
        return ReceiveData(data, 0, sender);
    }

    int LocalNetworkGamer::ReceiveData(std::vector<SharpRuntime::bytecs>& data, int offset, NetworkGamer*& sender)
    {
        sender = nullptr;
        if (!getIsDataAvailableProperty())
        {
            return 0;
        }

        NetworkSession::NetworkEvent packet = std::move(packetQueue_.front());
        packetQueue_.pop();
        int len = std::min(static_cast<int>(packet.Packet.size()), static_cast<int>(data.size()));
        std::copy(packet.Packet.begin(), packet.Packet.begin() + len, data.begin() + offset);

        for (NetworkGamer* gamer : getSessionProperty()->getAllGamersProperty())
        {
            // FIXME upstream: pointer identity is a weak equality check for "same gamer".
            if (gamer == packet.Gamer)
            {
                sender = gamer;
                return len;
            }
        }

        return len;
    }

    int LocalNetworkGamer::ReceiveData(PacketReader& data, NetworkGamer*& sender)
    {
        sender = nullptr;
        if (!getIsDataAvailableProperty())
        {
            return 0;
        }

        // FNA declares `uint len = 0` here and never updates it before returning it — the
        // written data is real, but the reported length is always 0. Preserved as-is.
        uint32_t len = 0;
        NetworkSession::NetworkEvent packet = std::move(packetQueue_.front());
        packetQueue_.pop();
        data.setPositionProperty(0);
        data.getBaseStreamProperty()->Write(packet.Packet.data(), 0, static_cast<int>(packet.Packet.size()));
        data.setPositionProperty(0);

        for (NetworkGamer* gamer : getSessionProperty()->getAllGamersProperty())
        {
            if (gamer == packet.Gamer)
            {
                sender = gamer;
                return static_cast<int>(len);
            }
        }

        return static_cast<int>(len);
    }

    void LocalNetworkGamer::SendData(const std::vector<SharpRuntime::bytecs>& data, SendDataOptions options)
    {
        SendData(data, 0, static_cast<int>(data.size()), options);
    }

    void LocalNetworkGamer::SendData(const std::vector<SharpRuntime::bytecs>& data, int offset, int count, SendDataOptions options)
    {
        std::vector<SharpRuntime::bytecs> mem(data.begin() + offset, data.begin() + offset + count);
        for (NetworkGamer* gamer : getSessionProperty()->getAllGamersProperty())
        {
            NetworkSession::NetworkEvent evt;
            evt.Type = NetworkSession::NetworkEventType::PacketSend;
            evt.Gamer = gamer;
            evt.Sender = this;
            evt.Packet = mem;
            evt.Reliable = options;
            getSessionProperty()->SendNetworkEvent(std::move(evt));
        }
    }

    void LocalNetworkGamer::SendData(const std::vector<SharpRuntime::bytecs>& data, SendDataOptions options, NetworkGamer* recipient)
    {
        SendData(data, 0, static_cast<int>(data.size()), options, recipient);
    }

    void LocalNetworkGamer::SendData(const std::vector<SharpRuntime::bytecs>& data, int offset, int count, SendDataOptions options, NetworkGamer* recipient)
    {
        std::vector<SharpRuntime::bytecs> mem(data.begin() + offset, data.begin() + offset + count);
        NetworkSession::NetworkEvent evt;
        evt.Type = NetworkSession::NetworkEventType::PacketSend;
        evt.Gamer = recipient;
        evt.Sender = this;
        evt.Packet = std::move(mem);
        evt.Reliable = options;
        getSessionProperty()->SendNetworkEvent(std::move(evt));
    }

    void LocalNetworkGamer::SendData(PacketWriter& data, SendDataOptions options)
    {
        auto* mem = dynamic_cast<System::IO::MemoryStream*>(data.getBaseStreamProperty());
        std::vector<SharpRuntime::bytecs> packet = mem ? mem->ToArray() : std::vector<SharpRuntime::bytecs>{};
        data.setPositionProperty(0);

        for (NetworkGamer* gamer : getSessionProperty()->getAllGamersProperty())
        {
            NetworkSession::NetworkEvent evt;
            evt.Type = NetworkSession::NetworkEventType::PacketSend;
            evt.Gamer = gamer;
            evt.Sender = this;
            evt.Packet = packet;
            evt.Reliable = options;
            getSessionProperty()->SendNetworkEvent(std::move(evt));
        }
    }

    void LocalNetworkGamer::SendData(PacketWriter& data, SendDataOptions options, NetworkGamer* recipient)
    {
        auto* mem = dynamic_cast<System::IO::MemoryStream*>(data.getBaseStreamProperty());
        std::vector<SharpRuntime::bytecs> packet = mem ? mem->ToArray() : std::vector<SharpRuntime::bytecs>{};
        data.setPositionProperty(0);

        NetworkSession::NetworkEvent evt;
        evt.Type = NetworkSession::NetworkEventType::PacketSend;
        evt.Gamer = recipient;
        evt.Sender = this;
        evt.Packet = std::move(packet);
        evt.Reliable = options;
        getSessionProperty()->SendNetworkEvent(std::move(evt));
    }

    void LocalNetworkGamer::ClearPacketQueue()
    {
        packetQueue_ = {};
    }

    void LocalNetworkGamer::EnqueuePacket(NetworkSession::NetworkEvent evt)
    {
        packetQueue_.push(std::move(evt));
    }
}
