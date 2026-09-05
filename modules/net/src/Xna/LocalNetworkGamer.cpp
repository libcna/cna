// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Net/LocalNetworkGamer.hpp"
#include "System/ArgumentException.hpp"
#include "System/IO/MemoryStream.hpp"
#include <algorithm>

namespace Microsoft::Xna::Framework::Net
{
    namespace
    {
        std::vector<SharpRuntime::bytecs> TakePacket(PacketWriter& data)
        {
            auto* stream = dynamic_cast<System::IO::MemoryStream*>(data.getBaseStreamProperty());
            std::vector<SharpRuntime::bytecs> packet =
                stream != nullptr ? stream->ToArray() : std::vector<SharpRuntime::bytecs>{};

            // PacketWriter is deliberately reusable: XNA sends only the bytes before its current
            // Position and then rewinds it. MemoryStream::ToArray() reflects Length, which may
            // still include a previous, longer packet after the writer has been rewound.
            packet.resize(static_cast<std::size_t>(data.getPositionProperty()));
            data.setPositionProperty(0);
            return packet;
        }
    }

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
        // Task 2.8: FNA's own Array.Copy(packet.Packet, 0, data, offset, len) validates offset+len
        // against data.Length and throws ArgumentException on overflow (len itself is computed
        // the same offset-oblivious way in FNA, so it can legitimately exceed data.size() - offset
        // for a non-zero offset) - preserved here instead of std::copy's undefined behavior for an
        // out-of-bounds destination range. The packet is still consumed from the queue either way,
        // matching FNA's Dequeue()-before-Array.Copy ordering.
        if (offset < 0 || offset + len > static_cast<int>(data.size()))
        {
            throw System::ArgumentException("offset");
        }
        std::copy(packet.Packet.begin(), packet.Packet.begin() + len, data.begin() + offset);

        for (NetworkGamer* gamer : getSessionProperty()->getAllGamersProperty())
        {
            // Task 6.2: FIXME upstream ("This is a bad equality check!" in FNA's own source) -
            // pointer identity is a weak equality check for "same gamer", preserved as-is rather
            // than "fixed" to a value-based comparison FNA itself doesn't have. Re-evaluated after
            // Task 3.1 gave NetworkSession/ENetBackend real ownership of every gamer they create
            // (previously nothing was ever freed, so no address could be coincidentally reused):
            // still safe, because neither ever frees a gamer individually - only in bulk, at
            // whole-session Dispose()/TeardownSession - and every NetworkEvent that could carry a
            // stale Gamer* lives inside a per-gamer packetQueue_ (or NetworkSession's own event
            // queue), which is destroyed together with that same session's gamers at that same
            // Dispose() call. No stale pointer from a torn-down session can outlive the objects
            // it would need to be compared against.
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
            // Task 6.2: same pointer-identity FIXME and same re-evaluated-safe reasoning as the
            // other ReceiveData overload above.
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
        // Task 2.9: FNA's own Array.Copy(data, offset, mem, 0, mem.Length) validates
        // offset/count against data.Length and throws on overflow - preserved here instead of
        // constructing a vector from an out-of-bounds iterator range (undefined behavior).
        if (offset < 0 || count < 0 || offset + count > static_cast<int>(data.size()))
        {
            throw System::ArgumentException("offset");
        }
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
        // Task 2.9: see the non-recipient overload above for why this check exists.
        if (offset < 0 || count < 0 || offset + count > static_cast<int>(data.size()))
        {
            throw System::ArgumentException("offset");
        }
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
        std::vector<SharpRuntime::bytecs> packet = TakePacket(data);

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
        std::vector<SharpRuntime::bytecs> packet = TakePacket(data);

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
