// SPDX-License-Identifier: MS-PL

#ifndef CNA_C_API_NET_DETAIL_HPP
#define CNA_C_API_NET_DETAIL_HPP

#include "CnaCApiDetail.hpp"

namespace Microsoft::Xna::Framework::Net {
class NetworkSessionProperties;
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

} // namespace CNA::C::Detail

#endif
