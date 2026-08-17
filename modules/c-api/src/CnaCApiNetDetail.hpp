// SPDX-License-Identifier: MS-PL

#ifndef CNA_C_API_NET_DETAIL_HPP
#define CNA_C_API_NET_DETAIL_HPP

#include "CnaCApiDetail.hpp"

#include <cstddef>
#include <memory>

namespace Microsoft::Xna::Framework::Net {
class NetworkGamer;
class NetworkSession;
class NetworkSessionProperties;
class PacketReader;
class PacketWriter;
}

namespace Microsoft::Xna::Framework::GamerServices {
class SignedInGamer;
}

namespace CNA::C::Detail {

// Session properties are owned by the networking adapter, but the session-facing adapters need to
// read one out of a handle and to publish a copy back as a new handle. Both go through these two
// entry points so no adapter learns another one's resource layout.
[[nodiscard]] CNA_Result BorrowNetworkSessionProperties(
    CNA_Handle handle,
    Microsoft::Xna::Framework::Net::NetworkSessionProperties** outProperties);

[[nodiscard]] CNA_Result CreateOwnedNetworkSessionProperties(
    const Microsoft::Xna::Framework::Net::NetworkSessionProperties& value,
    CNA_Handle* outProperties);

// A gamer handle is either an owner or a view onto a gamer some other object owns. A view keeps its
// owner alive through the shared pointer and counts itself in the owner's own view counter, so the
// owner cannot be released while a view still points into it. Sessions and machines both use it.
[[nodiscard]] CNA_Result BorrowNetworkGamer(
    CNA_Handle handle,
    Microsoft::Xna::Framework::Net::NetworkGamer** outGamer);

[[nodiscard]] CNA_Result RetainNetworkGamer(CNA_Handle handle, std::shared_ptr<void>* outOwner);

[[nodiscard]] CNA_Result CreateBorrowedNetworkGamer(
    Microsoft::Xna::Framework::Net::NetworkGamer* value,
    std::shared_ptr<void> viewOwner,
    std::size_t* viewCounter,
    CNA_Handle session,
    CNA_Handle* outGamer);

// The session adapter owns network sessions; the gamer adapter needs to accept one as a creation
// argument, so the lookup is shared rather than duplicated.
[[nodiscard]] CNA_Result BorrowNetworkSession(
    CNA_Handle handle,
    Microsoft::Xna::Framework::Net::NetworkSession** outSession);

// The packet buffers are owned by the networking adapter; the local-gamer routes hand them to the
// canonical send and receive calls without learning that adapter's resource layout.
[[nodiscard]] CNA_Result BorrowPacketReader(
    CNA_Handle handle,
    Microsoft::Xna::Framework::Net::PacketReader** outReader);

[[nodiscard]] CNA_Result BorrowPacketWriter(
    CNA_Handle handle,
    Microsoft::Xna::Framework::Net::PacketWriter** outWriter);

// A local gamer is a network gamer, so it shares the gamer handle kind; only the gamer adapter
// knows how to build an owning record for one.
[[nodiscard]] CNA_Result CreateOwnedLocalNetworkGamer(
    Microsoft::Xna::Framework::GamerServices::SignedInGamer* signedInGamer,
    Microsoft::Xna::Framework::Net::NetworkSession* session,
    CNA_Handle sessionHandle,
    CNA_Handle* outGamer);

} // namespace CNA::C::Detail

#endif
