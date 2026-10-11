// SPDX-License-Identifier: MS-PL

#include "CNA/C/packed_vectors.h"
#include "CnaCApiDetail.hpp"
#include "CnaCApiMarshal.hpp"
#include "CnaCApiOutput.hpp"

#include "Microsoft/Xna/Framework/Graphics/PackedVector/Alpha8.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/Bgr565.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/Bgra4444.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/Bgra5551.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/Byte4.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/HalfSingle.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/HalfTypeHelper.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/HalfVector2.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/HalfVector4.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/NormalizedByte2.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/NormalizedByte4.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/NormalizedShort2.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/NormalizedShort4.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/Rg32.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/Rgba1010102.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/Rgba64.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/Short2.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/Short4.hpp"

#include <cmath>
#include <cstdint>
#include <utility>

namespace {

using CNA::C::Detail::CallWithExceptionBarrier;
using CNA::C::Detail::Fail;
using CNA::C::Detail::InvalidArgument;
using CNA::C::Detail::ToC;
using CNA::C::Detail::ToCBool;
using CNA::C::Detail::ToNative;
using Microsoft::Xna::Framework::Vector4;
using namespace Microsoft::Xna::Framework::Graphics::PackedVector;

[[nodiscard]] bool IsValidFormat(const CNA_PackedVectorFormat format) noexcept
{
    return format <= CNA_PACKED_VECTOR_FORMAT_SHORT4;
}

[[nodiscard]] uint32_t StorageBits(const CNA_PackedVectorFormat format) noexcept
{
    switch (format) {
        case CNA_PACKED_VECTOR_FORMAT_ALPHA8:
            return 8U;
        case CNA_PACKED_VECTOR_FORMAT_BGR565:
        case CNA_PACKED_VECTOR_FORMAT_BGRA4444:
        case CNA_PACKED_VECTOR_FORMAT_BGRA5551:
        case CNA_PACKED_VECTOR_FORMAT_HALF_SINGLE:
        case CNA_PACKED_VECTOR_FORMAT_NORMALIZED_BYTE2:
            return 16U;
        case CNA_PACKED_VECTOR_FORMAT_BYTE4:
        case CNA_PACKED_VECTOR_FORMAT_HALF_VECTOR2:
        case CNA_PACKED_VECTOR_FORMAT_NORMALIZED_BYTE4:
        case CNA_PACKED_VECTOR_FORMAT_NORMALIZED_SHORT2:
        case CNA_PACKED_VECTOR_FORMAT_RG32:
        case CNA_PACKED_VECTOR_FORMAT_RGBA1010102:
        case CNA_PACKED_VECTOR_FORMAT_SHORT2:
            return 32U;
        default:
            return 64U;
    }
}

[[nodiscard]] bool PackedValueFits(
    const CNA_PackedVectorFormat format,
    const uint64_t packed) noexcept
{
    const uint32_t bits = StorageBits(format);
    return bits == 64U || (packed >> bits) == 0U;
}

[[nodiscard]] bool IsHalfFormat(const CNA_PackedVectorFormat format) noexcept
{
    return format == CNA_PACKED_VECTOR_FORMAT_HALF_SINGLE ||
        format == CNA_PACKED_VECTOR_FORMAT_HALF_VECTOR2 ||
        format == CNA_PACKED_VECTOR_FORMAT_HALF_VECTOR4;
}

[[nodiscard]] bool ConsumedComponentsAreFinite(
    const CNA_PackedVectorFormat format,
    const CNA_Vector4 value) noexcept
{
    if (IsHalfFormat(format)) {
        return true;
    }
    if (format == CNA_PACKED_VECTOR_FORMAT_ALPHA8) {
        return std::isfinite(value.w);
    }
    if (format == CNA_PACKED_VECTOR_FORMAT_BGR565) {
        return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
    }
    if (format == CNA_PACKED_VECTOR_FORMAT_NORMALIZED_BYTE2 ||
        format == CNA_PACKED_VECTOR_FORMAT_NORMALIZED_SHORT2 ||
        format == CNA_PACKED_VECTOR_FORMAT_RG32 ||
        format == CNA_PACKED_VECTOR_FORMAT_SHORT2) {
        return std::isfinite(value.x) && std::isfinite(value.y);
    }
    return std::isfinite(value.x) && std::isfinite(value.y) &&
        std::isfinite(value.z) && std::isfinite(value.w);
}

template<typename TPackedVector>
[[nodiscard]] uint64_t PackNative(const Vector4& value)
{
    TPackedVector packed;
    packed.PackFromVector4(value);
    return static_cast<uint64_t>(packed.getPackedValueProperty());
}

template<typename TPackedVector>
[[nodiscard]] Vector4 UnpackNative(const uint64_t value)
{
    using TPacked = decltype(std::declval<const TPackedVector&>().getPackedValueProperty());
    TPackedVector packed;
    packed.setPackedValueProperty(static_cast<TPacked>(value));
    return packed.ToVector4();
}

template<typename TPackedVector>
[[nodiscard]] bool CompareNative(
    const uint64_t left,
    const uint64_t right,
    const bool inequality)
{
    using TPacked = decltype(std::declval<const TPackedVector&>().getPackedValueProperty());
    TPackedVector leftValue;
    TPackedVector rightValue;
    leftValue.setPackedValueProperty(static_cast<TPacked>(left));
    rightValue.setPackedValueProperty(static_cast<TPacked>(right));
    return inequality ? leftValue != rightValue : leftValue == rightValue;
}

[[nodiscard]] uint64_t PackForFormat(
    const CNA_PackedVectorFormat format,
    const Vector4& value)
{
    switch (format) {
        case CNA_PACKED_VECTOR_FORMAT_ALPHA8: return PackNative<Alpha8>(value);
        case CNA_PACKED_VECTOR_FORMAT_BGR565: return PackNative<Bgr565>(value);
        case CNA_PACKED_VECTOR_FORMAT_BGRA4444: return PackNative<Bgra4444>(value);
        case CNA_PACKED_VECTOR_FORMAT_BGRA5551: return PackNative<Bgra5551>(value);
        case CNA_PACKED_VECTOR_FORMAT_BYTE4: return PackNative<Byte4>(value);
        case CNA_PACKED_VECTOR_FORMAT_HALF_SINGLE: return PackNative<HalfSingle>(value);
        case CNA_PACKED_VECTOR_FORMAT_HALF_VECTOR2: return PackNative<HalfVector2>(value);
        case CNA_PACKED_VECTOR_FORMAT_HALF_VECTOR4: return PackNative<HalfVector4>(value);
        case CNA_PACKED_VECTOR_FORMAT_NORMALIZED_BYTE2:
            return PackNative<NormalizedByte2>(value);
        case CNA_PACKED_VECTOR_FORMAT_NORMALIZED_BYTE4:
            return PackNative<NormalizedByte4>(value);
        case CNA_PACKED_VECTOR_FORMAT_NORMALIZED_SHORT2:
            return PackNative<NormalizedShort2>(value);
        case CNA_PACKED_VECTOR_FORMAT_NORMALIZED_SHORT4:
            return PackNative<NormalizedShort4>(value);
        case CNA_PACKED_VECTOR_FORMAT_RG32: return PackNative<Rg32>(value);
        case CNA_PACKED_VECTOR_FORMAT_RGBA1010102: return PackNative<Rgba1010102>(value);
        case CNA_PACKED_VECTOR_FORMAT_RGBA64: return PackNative<Rgba64>(value);
        case CNA_PACKED_VECTOR_FORMAT_SHORT2: return PackNative<Short2>(value);
        default: return PackNative<Short4>(value);
    }
}

[[nodiscard]] Vector4 UnpackForFormat(
    const CNA_PackedVectorFormat format,
    const uint64_t value)
{
    switch (format) {
        case CNA_PACKED_VECTOR_FORMAT_ALPHA8: return UnpackNative<Alpha8>(value);
        case CNA_PACKED_VECTOR_FORMAT_BGR565: return UnpackNative<Bgr565>(value);
        case CNA_PACKED_VECTOR_FORMAT_BGRA4444: return UnpackNative<Bgra4444>(value);
        case CNA_PACKED_VECTOR_FORMAT_BGRA5551: return UnpackNative<Bgra5551>(value);
        case CNA_PACKED_VECTOR_FORMAT_BYTE4: return UnpackNative<Byte4>(value);
        case CNA_PACKED_VECTOR_FORMAT_HALF_SINGLE: return UnpackNative<HalfSingle>(value);
        case CNA_PACKED_VECTOR_FORMAT_HALF_VECTOR2: return UnpackNative<HalfVector2>(value);
        case CNA_PACKED_VECTOR_FORMAT_HALF_VECTOR4: return UnpackNative<HalfVector4>(value);
        case CNA_PACKED_VECTOR_FORMAT_NORMALIZED_BYTE2:
            return UnpackNative<NormalizedByte2>(value);
        case CNA_PACKED_VECTOR_FORMAT_NORMALIZED_BYTE4:
            return UnpackNative<NormalizedByte4>(value);
        case CNA_PACKED_VECTOR_FORMAT_NORMALIZED_SHORT2:
            return UnpackNative<NormalizedShort2>(value);
        case CNA_PACKED_VECTOR_FORMAT_NORMALIZED_SHORT4:
            return UnpackNative<NormalizedShort4>(value);
        case CNA_PACKED_VECTOR_FORMAT_RG32: return UnpackNative<Rg32>(value);
        case CNA_PACKED_VECTOR_FORMAT_RGBA1010102: return UnpackNative<Rgba1010102>(value);
        case CNA_PACKED_VECTOR_FORMAT_RGBA64: return UnpackNative<Rgba64>(value);
        case CNA_PACKED_VECTOR_FORMAT_SHORT2: return UnpackNative<Short2>(value);
        default: return UnpackNative<Short4>(value);
    }
}

[[nodiscard]] bool CompareForFormat(
    const CNA_PackedVectorFormat format,
    const uint64_t left,
    const uint64_t right,
    const bool inequality)
{
    switch (format) {
        case CNA_PACKED_VECTOR_FORMAT_ALPHA8:
            return CompareNative<Alpha8>(left, right, inequality);
        case CNA_PACKED_VECTOR_FORMAT_BGR565:
            return CompareNative<Bgr565>(left, right, inequality);
        case CNA_PACKED_VECTOR_FORMAT_BGRA4444:
            return CompareNative<Bgra4444>(left, right, inequality);
        case CNA_PACKED_VECTOR_FORMAT_BGRA5551:
            return CompareNative<Bgra5551>(left, right, inequality);
        case CNA_PACKED_VECTOR_FORMAT_BYTE4:
            return CompareNative<Byte4>(left, right, inequality);
        case CNA_PACKED_VECTOR_FORMAT_HALF_SINGLE:
            return CompareNative<HalfSingle>(left, right, inequality);
        case CNA_PACKED_VECTOR_FORMAT_HALF_VECTOR2:
            return CompareNative<HalfVector2>(left, right, inequality);
        case CNA_PACKED_VECTOR_FORMAT_HALF_VECTOR4:
            return CompareNative<HalfVector4>(left, right, inequality);
        case CNA_PACKED_VECTOR_FORMAT_NORMALIZED_BYTE2:
            return CompareNative<NormalizedByte2>(left, right, inequality);
        case CNA_PACKED_VECTOR_FORMAT_NORMALIZED_BYTE4:
            return CompareNative<NormalizedByte4>(left, right, inequality);
        case CNA_PACKED_VECTOR_FORMAT_NORMALIZED_SHORT2:
            return CompareNative<NormalizedShort2>(left, right, inequality);
        case CNA_PACKED_VECTOR_FORMAT_NORMALIZED_SHORT4:
            return CompareNative<NormalizedShort4>(left, right, inequality);
        case CNA_PACKED_VECTOR_FORMAT_RG32:
            return CompareNative<Rg32>(left, right, inequality);
        case CNA_PACKED_VECTOR_FORMAT_RGBA1010102:
            return CompareNative<Rgba1010102>(left, right, inequality);
        case CNA_PACKED_VECTOR_FORMAT_RGBA64:
            return CompareNative<Rgba64>(left, right, inequality);
        case CNA_PACKED_VECTOR_FORMAT_SHORT2:
            return CompareNative<Short2>(left, right, inequality);
        default:
            return CompareNative<Short4>(left, right, inequality);
    }
}

[[nodiscard]] CNA_Result ValidateFormat(const CNA_PackedVectorFormat format) noexcept
{
    if (!IsValidFormat(format)) {
        return InvalidArgument("The packed-vector format is invalid.");
    }
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result ValidatePacked(
    const CNA_PackedVectorFormat format,
    const uint64_t packed) noexcept
{
    if (!PackedValueFits(format, packed)) {
        return Fail(
            CNA_RESULT_INVALID_ARGUMENT,
            CNA_ERROR_CATEGORY_RANGE,
            "The packed value exceeds the selected format's storage width.");
    }
    return CNA_RESULT_SUCCESS;
}

} // namespace

CNA_Result cna_packed_vector_pack(
    const CNA_PackedVectorFormat format,
    const CNA_Vector4 vector,
    uint64_t* const outPacked)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outPacked == nullptr) {
            return InvalidArgument("The packed-value output is null.");
        }
        CNA_Result result = ValidateFormat(format);
        if (result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (!ConsumedComponentsAreFinite(format, vector)) {
            return InvalidArgument("A consumed integer packed-vector component is not finite.");
        }
        const uint64_t packed = PackForFormat(format, ToNative(vector));
        *outPacked = packed;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_packed_vector_unpack(
    const CNA_PackedVectorFormat format,
    const uint64_t packed,
    CNA_Vector4* const outVector)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outVector == nullptr) {
            return InvalidArgument("The Vector4 output is null.");
        }
        CNA_Result result = ValidateFormat(format);
        if (result != CNA_RESULT_SUCCESS) {
            return result;
        }
        result = ValidatePacked(format, packed);
        if (result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const CNA_Vector4 vector = ToC(UnpackForFormat(format, packed));
        *outVector = vector;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_packed_vector_equals(
    const CNA_PackedVectorFormat format,
    const uint64_t left,
    const uint64_t right,
    CNA_Bool* const outEqual)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outEqual == nullptr) {
            return InvalidArgument("The Boolean output is null.");
        }
        CNA_Result result = ValidateFormat(format);
        if (result != CNA_RESULT_SUCCESS) {
            return result;
        }
        result = ValidatePacked(format, left);
        if (result != CNA_RESULT_SUCCESS) {
            return result;
        }
        result = ValidatePacked(format, right);
        if (result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const CNA_Bool equal = ToCBool(CompareForFormat(format, left, right, false));
        *outEqual = equal;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_packed_vector_not_equals(
    const CNA_PackedVectorFormat format,
    const uint64_t left,
    const uint64_t right,
    CNA_Bool* const outNotEqual)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outNotEqual == nullptr) {
            return InvalidArgument("The Boolean output is null.");
        }
        CNA_Result result = ValidateFormat(format);
        if (result != CNA_RESULT_SUCCESS) {
            return result;
        }
        result = ValidatePacked(format, left);
        if (result != CNA_RESULT_SUCCESS) {
            return result;
        }
        result = ValidatePacked(format, right);
        if (result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const CNA_Bool notEqual = ToCBool(CompareForFormat(format, left, right, true));
        *outNotEqual = notEqual;
        return CNA_RESULT_SUCCESS;
    });
}

// The mechanically regular routes of this family, generated from modules/c-api/spec/PackedVectors.toml.
#include "generated/PackedVectors.inc"
