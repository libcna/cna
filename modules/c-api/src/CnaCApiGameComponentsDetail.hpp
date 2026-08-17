// SPDX-License-Identifier: MS-PL

#ifndef CNA_C_API_GAME_COMPONENTS_DETAIL_HPP
#define CNA_C_API_GAME_COMPONENTS_DETAIL_HPP

#include "CnaCApiDetail.hpp"

#include <memory>

namespace Microsoft::Xna::Framework {
class GameComponent;
}

namespace CNA::C::Detail {

// Most game components this ABI publishes are ones it derived itself from a caller's callback set.
// A canonical component -- one the runtime already implements, like the gamer-services component --
// still needs the same handle, the same collection registry entry and the same ownership
// bookkeeping, so it goes through the components adapter rather than around it.
[[nodiscard]] CNA_Result CreateOwnedCanonicalGameComponent(
    CNA_Handle game,
    std::unique_ptr<Microsoft::Xna::Framework::GameComponent> component,
    CNA_Handle* outComponent);

} // namespace CNA::C::Detail

#endif
