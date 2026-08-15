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

// The accepted-invite event carries a signed-in gamer, so the session adapter needs a handle for
// one it does not own; the view lives only as long as the callback that receives it.
[[nodiscard]] CNA_Result CreateBorrowedSignedInGamer(
    Microsoft::Xna::Framework::GamerServices::SignedInGamer* value,
    CNA_Handle* outGamer);

[[nodiscard]] CNA_Result ReleaseBorrowedSignedInGamer(CNA_Handle handle);

} // namespace CNA::C::Detail

#endif
