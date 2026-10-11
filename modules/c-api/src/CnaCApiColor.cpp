// SPDX-License-Identifier: MS-PL

#include "CNA/C/color.h"
#include "CnaCApiDetail.hpp"
#include "CnaCApiMarshal.hpp"
#include "CnaCApiOutput.hpp"

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"

#include <bit>
#include <cstring>
#include <string>
#include <utility>

namespace {

using CNA::C::Detail::CallWithExceptionBarrier;
using CNA::C::Detail::Fail;
using CNA::C::Detail::ToC;
using CNA::C::Detail::ToNative;
using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Vector4;

[[nodiscard]] int32_t PremultiplyUnchecked(
    const int32_t component,
    const int32_t alpha) noexcept
{
    // Preserve FNA's unchecked Int32 product without invoking signed-overflow UB.
    const uint32_t product =
        std::bit_cast<uint32_t>(component) * std::bit_cast<uint32_t>(alpha);
    return std::bit_cast<int32_t>(product) / 255;
}

} // namespace

CNA_Result cna_color_set_packed_value(
    CNA_Color* const color,
    const uint32_t packedValue)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (color == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The Color pointer is null.");
        }
        Color native(UINT8_C(0), UINT8_C(0), UINT8_C(0), UINT8_C(0));
        native.setPackedValueProperty(packedValue);
        const CNA_Color result = ToC(native);
        *color = result;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_color_pack_from_vector4(
    CNA_Color* const color,
    const CNA_Vector4 vector)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (color == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The Color pointer is null.");
        }
        Color native = ToNative(*color);
        native.PackFromVector4(ToNative(vector));
        const CNA_Color result = ToC(native);
        *color = result;
        return CNA_RESULT_SUCCESS;
    });
}

// The mechanically regular routes of this family, generated from modules/c-api/spec/Color.toml.
#include "generated/Color.inc"
