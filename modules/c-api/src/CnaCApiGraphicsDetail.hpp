// SPDX-License-Identifier: MS-PL

#ifndef CNA_C_API_GRAPHICS_DETAIL_HPP
#define CNA_C_API_GRAPHICS_DETAIL_HPP

#include "CNA/C/abi.h"

#include <cstdint>
#include <memory>

namespace Microsoft::Xna::Framework::Graphics {
class Texture2D;
}

namespace CNA::C::Detail {

struct Texture2DResource final {
    std::shared_ptr<Microsoft::Xna::Framework::Graphics::Texture2D> value;
    CNA_Handle parentGame;
    uint64_t activeBatchReferenceCount;
    uint64_t activeFontReferenceCount;
};

[[nodiscard]] CNA_Result CreateOwnedTexture2D(
    std::shared_ptr<Microsoft::Xna::Framework::Graphics::Texture2D> texture,
    CNA_Handle parentGame,
    CNA_Handle* outTexture);

[[nodiscard]] CNA_Result CreateOwnedRenderTarget2D(
    std::shared_ptr<Microsoft::Xna::Framework::Graphics::Texture2D> texture,
    CNA_Handle parentGame,
    CNA_Handle* outTexture);

[[nodiscard]] CNA_Result GetOwnedTexture2D(
    CNA_Handle handle,
    std::shared_ptr<Texture2DResource>* outTexture);

} // namespace CNA::C::Detail

#endif
