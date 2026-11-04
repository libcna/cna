// SPDX-License-Identifier: MS-PL

#include "CNA/C/color.h"
#include "CnaCApiDetail.hpp"
#include "CnaCApiMarshal.hpp"
#include "CnaCApiOutput.hpp"

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"

#include <algorithm>
#include <cstring>
#include <string>
#include <utility>

namespace {

using CNA::C::Detail::CallWithExceptionBarrier;
using CNA::C::Detail::InvalidArgument;
using CNA::C::Detail::ToC;
using CNA::C::Detail::ToNative;
using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Vector4;

[[nodiscard]] int32_t PremultiplyUnchecked(
    const int32_t component,
    const int32_t alpha) noexcept
{
    // Refresh Color.FromNonPremultiplied(int,int,int,int) IL_0000-003f:
    // unchecked Int64 product, division by 255, then clamp before narrowing.
    // Retain the helper name used by the generated route; no generator change.
    const int64_t quotient = static_cast<int64_t>(component) * alpha / 255;
    return static_cast<int32_t>(std::clamp(quotient, int64_t{0}, int64_t{255}));
}

[[nodiscard]] CNA_Result ColorSetPackedValue(
    CNA_Color* const color,
    const uint32_t packedValue)
{
    if (color == nullptr) {
        return InvalidArgument("The Color pointer is null.");
    }
    Color native(UINT8_C(0), UINT8_C(0), UINT8_C(0), UINT8_C(0));
    native.setPackedValueProperty(packedValue);
    const CNA_Color result = ToC(native);
    *color = result;
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result ColorPackFromVector4(
    CNA_Color* const color,
    const CNA_Vector4 vector)
{
    if (color == nullptr) {
        return InvalidArgument("The Color pointer is null.");
    }
    Color native = ToNative(*color);
    native.PackFromVector4(ToNative(vector));
    const CNA_Color result = ToC(native);
    *color = result;
    return CNA_RESULT_SUCCESS;
}

} // namespace

// The routes generated from modules/c-api/spec/Color.toml.
#include "generated/Color.inc"
