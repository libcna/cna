// SPDX-License-Identifier: MS-PL

#ifndef CNA_C_API_GAMER_SERVICES_DETAIL_HPP
#define CNA_C_API_GAMER_SERVICES_DETAIL_HPP

#include "CnaCApiDetail.hpp"

namespace Microsoft::Xna::Framework::GamerServices {
class SignedInGamer;
}

namespace CNA::C::Detail {

// A network session cannot be created without at least one signed-in gamer, so the session adapter
// borrows one through this entry point rather than learning the gamer-services resource layout.
[[nodiscard]] CNA_Result BorrowSignedInGamer(
    CNA_Handle handle,
    Microsoft::Xna::Framework::GamerServices::SignedInGamer** outGamer);

} // namespace CNA::C::Detail

#endif
