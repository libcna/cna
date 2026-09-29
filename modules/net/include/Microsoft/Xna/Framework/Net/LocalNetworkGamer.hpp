// SPDX-License-Identifier: MS-PL
#pragma once
#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Net/NetworkGamer.hpp"
#include "Microsoft/Xna/Framework/Net/NetworkSession.hpp"
#include "Microsoft/Xna/Framework/Net/PacketReader.hpp"
#include "Microsoft/Xna/Framework/Net/PacketWriter.hpp"
#include "Microsoft/Xna/Framework/Net/SendDataOptions.hpp"
#include "SharpRuntime/SharpRuntimeHelper.hpp"
#include <queue>
#include <vector>

namespace Microsoft::Xna::Framework::GamerServices
{
    class SignedInGamer;
}

namespace Microsoft::Xna::Framework::Net
{
    /**
     * @brief Represents a local gamer participating in a network session.
     */
    class LocalNetworkGamer final : public NetworkGamer
    {
    public:
        /** @brief .NET type name. @return Name. */
        GetTypeNameHPP()

        /**
         * @brief Gets whether an incoming packet is queued and ready to receive.
         *
         * @return true if a packet is available.
         */
        [[nodiscard]] bool getIsDataAvailableProperty() const;

        /**
         * @brief Gets the signed-in gamer backing this local gamer.
         *
         * @return Pointer to the SignedInGamer.
         */
        [[nodiscard]] GamerServices::SignedInGamer* getSignedInGamerProperty() const;

        /**
         * @brief Gets whether this gamer is local.
         *
         * Overrides NetworkGamer::getIsLocalProperty() to return true, matching FNA's
         * `this is LocalNetworkGamer` check on the base class.
         *
         * @return Always true.
         */
        [[nodiscard]] bool getIsLocalProperty() const override;

        /**
         * @brief Enables or disables sending this gamer's voice to a remote gamer; voice is enabled
         * for every gamer at first. Only the gamer who owns the machine's microphone has voice.
         *
         * @param remoteGamer The remote gamer to configure.
         * @param enable Whether voice sending should be enabled.
         * @throws System::InvalidOperationException if either gamer has left the session.
         * @throws System::ArgumentNullException if remoteGamer is null.
         * @throws System::ArgumentException if remoteGamer belongs to another session.
         */
        void EnableSendVoice(NetworkGamer* remoteGamer, bool enable);

        /**
         * @brief Sends game invitations to the gamer's party members.
         *
         * @throws System::InvalidOperationException if the gamer has left the session or is alone in
         *         its party, which is always the case: CNA has no party service.
         */
        void SendPartyInvites();

        /**
         * @brief Receives the next queued packet into data, starting at offset 0.
         *
         * @param data The buffer to receive into.
         * @param sender Receives the gamer that sent the packet.
         * @return The packet size, or 0 if no packet was available.
         * @throws System::ArgumentOutOfRangeException if data is empty.
         * @throws System::ArgumentException if the packet does not fit; it stays queued.
         */
        int ReceiveData(std::vector<SharpRuntime::bytecs>& data, NetworkGamer*& sender);

        /**
         * @brief Receives the next queued packet into data, starting at the given offset.
         *
         * @param data The buffer to receive into.
         * @param offset The offset within data to start writing at.
         * @param sender Receives the gamer that sent the packet.
         * @return The packet size, or 0 if no packet was available.
         * @throws System::ArgumentOutOfRangeException if offset is not inside data.
         * @throws System::ArgumentException if the packet does not fit after offset; it stays queued.
         */
        int ReceiveData(std::vector<SharpRuntime::bytecs>& data, int offset, NetworkGamer*& sender);

        /**
         * @brief Receives the next queued packet into a PacketReader, which is sized to the packet
         * and rewound (emptied when no packet was queued).
         *
         * @param data The PacketReader to receive into.
         * @param sender Receives the gamer that sent the packet.
         * @return The packet size, or 0 if no packet was available.
         */
        int ReceiveData(PacketReader& data, NetworkGamer*& sender);

        /**
         * @brief Sends data to every gamer in the session.
         *
         * @param data The payload to send.
         * @param options The delivery guarantees to request.
         */
        void SendData(const std::vector<SharpRuntime::bytecs>& data, SendDataOptions options);

        /**
         * @brief Sends a sub-range of data to every gamer in the session.
         *
         * @param data The source buffer.
         * @param offset The offset within data to start reading at.
         * @param count The number of bytes to send.
         * @param options The delivery guarantees to request.
         */
        void SendData(const std::vector<SharpRuntime::bytecs>& data, int offset, int count, SendDataOptions options);

        /**
         * @brief Sends data to a single recipient.
         *
         * @param data The payload to send.
         * @param options The delivery guarantees to request.
         * @param recipient The gamer to send to.
         */
        void SendData(const std::vector<SharpRuntime::bytecs>& data, SendDataOptions options, NetworkGamer* recipient);

        /**
         * @brief Sends a sub-range of data to a single recipient.
         *
         * @param data The source buffer.
         * @param offset The offset within data to start reading at.
         * @param count The number of bytes to send.
         * @param options The delivery guarantees to request.
         * @param recipient The gamer to send to.
         */
        void SendData(const std::vector<SharpRuntime::bytecs>& data, int offset, int count, SendDataOptions options, NetworkGamer* recipient);

        /**
         * @brief Sends the contents of a PacketWriter to every gamer in the session.
         *
         * @param data The PacketWriter whose buffer contents are sent.
         * @param options The delivery guarantees to request.
         */
        void SendData(PacketWriter& data, SendDataOptions options);

        /**
         * @brief Sends the contents of a PacketWriter to a single recipient.
         *
         * @param data The PacketWriter whose buffer contents are sent.
         * @param options The delivery guarantees to request.
         * @param recipient The gamer to send to.
         */
        void SendData(PacketWriter& data, SendDataOptions options, NetworkGamer* recipient);

        /**
         * @brief Clears all queued incoming packets.
         *
         * FNA exposes the backing queue as an `internal` field that NetworkSession::Dispose()
         * clears directly; this method restores that same-library access.
         */
        CNAEXT void ClearPacketQueue();

        /**
         * @brief Queues an incoming packet for later retrieval via ReceiveData.
         *
         * Not part of FNA's original design (see NetworkSession.cpp's Update(), which never had
         * anywhere to route a PacketSend event to before Phase 5): used by NetworkSession::
         * Update() to deliver a PacketSend event addressed to this gamer, whether the packet
         * originated locally (same-machine SendData) or arrived over the real ENet transport.
         *
         * @param evt The event to queue; its Gamer field should already be set to the sender
         *            (see NetworkEvent::Sender's doc comment for why the two differ).
         */
        CNAEXT void EnqueuePacket(NetworkSession::NetworkEvent evt);

        /** @brief Creates a LocalNetworkGamer for CNA internal use. */
        CNAEXT static LocalNetworkGamer CreateInternal(GamerServices::SignedInGamer* gamer, NetworkSession* session);

    private:
        // The session gamer (current or previous) that sent packet, or nullptr.
        NetworkGamer* SenderOf(const NetworkSession::NetworkEvent& packet) const;
        LocalNetworkGamer(GamerServices::SignedInGamer* gamer, NetworkSession* session);

        GamerServices::SignedInGamer* signedInGamer_;
        std::queue<NetworkSession::NetworkEvent> packetQueue_;
    };
}
