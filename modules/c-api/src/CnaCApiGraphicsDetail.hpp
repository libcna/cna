// SPDX-License-Identifier: MS-PL

#ifndef CNA_C_API_GRAPHICS_DETAIL_HPP
#define CNA_C_API_GRAPHICS_DETAIL_HPP

#include "CNA/C/abi.h"

#include <memory>

namespace Microsoft::Xna::Framework::Graphics {
class Texture2D;
}

namespace CNA::C::Detail {

[[nodiscard]] CNA_Result CreateOwnedTexture2D(
    std::shared_ptr<Microsoft::Xna::Framework::Graphics::Texture2D> texture,
    CNA_Handle parentGame,
    CNA_Handle* outTexture);

} // namespace CNA::C::Detail

#endif
