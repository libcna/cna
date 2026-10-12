// SPDX-License-Identifier: MS-PL

#ifndef CNA_C_API_GRAPHICS_STATE_DETAIL_HPP
#define CNA_C_API_GRAPHICS_STATE_DETAIL_HPP

#include "CNA/C/graphics_state.h"

namespace Microsoft::Xna::Framework::Graphics {
class BlendState;
class DepthStencilState;
class RasterizerState;
class SamplerState;
}

namespace CNA::C::Detail {

[[nodiscard]] CNA_Result ToNativeBlendState(
    const CNA_BlendState* source,
    Microsoft::Xna::Framework::Graphics::BlendState* destination);

[[nodiscard]] CNA_Result ToNativeDepthStencilState(
    const CNA_DepthStencilState* source,
    Microsoft::Xna::Framework::Graphics::DepthStencilState* destination);

[[nodiscard]] CNA_Result ToNativeRasterizerState(
    const CNA_RasterizerState* source,
    Microsoft::Xna::Framework::Graphics::RasterizerState* destination);

[[nodiscard]] CNA_Result ToNativeSamplerState(
    const CNA_SamplerState* source,
    Microsoft::Xna::Framework::Graphics::SamplerState* destination);

[[nodiscard]] CNA_BlendState ToCBlendState(
    const Microsoft::Xna::Framework::Graphics::BlendState& source) noexcept;

[[nodiscard]] CNA_DepthStencilState ToCDepthStencilState(
    const Microsoft::Xna::Framework::Graphics::DepthStencilState& source) noexcept;

[[nodiscard]] CNA_RasterizerState ToCRasterizerState(
    const Microsoft::Xna::Framework::Graphics::RasterizerState& source) noexcept;

[[nodiscard]] CNA_SamplerState ToCSamplerState(
    const Microsoft::Xna::Framework::Graphics::SamplerState& source) noexcept;

} // namespace CNA::C::Detail

#endif
