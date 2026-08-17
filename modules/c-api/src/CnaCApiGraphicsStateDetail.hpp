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

void ToCBlendState(
    const Microsoft::Xna::Framework::Graphics::BlendState& source,
    CNA_BlendState* destination) noexcept;

void ToCDepthStencilState(
    const Microsoft::Xna::Framework::Graphics::DepthStencilState& source,
    CNA_DepthStencilState* destination) noexcept;

void ToCRasterizerState(
    const Microsoft::Xna::Framework::Graphics::RasterizerState& source,
    CNA_RasterizerState* destination) noexcept;

void ToCSamplerState(
    const Microsoft::Xna::Framework::Graphics::SamplerState& source,
    CNA_SamplerState* destination) noexcept;

} // namespace CNA::C::Detail

#endif
