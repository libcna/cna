// SPDX-License-Identifier: MS-PL

#include "CNA/C/curve.h"
#include "CnaCApiDetail.hpp"
#include "CnaCApiMarshal.hpp"
#include "CnaCApiOutput.hpp"
#include "CnaCApiRuntimeDetail.hpp"

#include "Microsoft/Xna/Framework/CurveContinuity.hpp"
#include "Microsoft/Xna/Framework/Curve.hpp"
#include "Microsoft/Xna/Framework/CurveKey.hpp"
#include "Microsoft/Xna/Framework/CurveKeyCollection.hpp"
#include "Microsoft/Xna/Framework/CurveLoopType.hpp"
#include "Microsoft/Xna/Framework/CurveTangent.hpp"

#include <memory>
#include <utility>

namespace {

using CNA::C::Detail::BufferTooSmall;
using CNA::C::Detail::CallWithExceptionBarrier;
using CNA::C::Detail::CreateHandle;
using CNA::C::Detail::InvalidArgument;
using CNA::C::Detail::LookupHandle;
using CNA::C::Detail::ObjectKind;
using CNA::C::Detail::OutOfRange;
using CNA::C::Detail::ToC;
using CNA::C::Detail::ToCBool;
using CNA::C::Detail::ToNative;
using Microsoft::Xna::Framework::Curve;
using Microsoft::Xna::Framework::CurveContinuity;
using Microsoft::Xna::Framework::CurveKey;
using Microsoft::Xna::Framework::CurveKeyCollection;
using Microsoft::Xna::Framework::CurveLoopType;
using Microsoft::Xna::Framework::CurveTangent;

[[nodiscard]] bool IsValidContinuity(const CNA_CurveContinuity value) noexcept
{
    return value == CNA_CURVE_CONTINUITY_SMOOTH || value == CNA_CURVE_CONTINUITY_STEP;
}

[[nodiscard]] CNA_Result ValidateContinuity(const CNA_CurveContinuity value) noexcept
{
    if (IsValidContinuity(value)) {
        return CNA_RESULT_SUCCESS;
    }
    return InvalidArgument("The curve continuity is invalid.");
}

[[nodiscard]] CNA_Result ValidateKey(const CNA_CurveKey value) noexcept
{
    return ValidateContinuity(value.continuity);
}

[[nodiscard]] bool IsValidLoopType(const CNA_CurveLoopType value) noexcept
{
    return value == CNA_CURVE_LOOP_CONSTANT || value == CNA_CURVE_LOOP_CYCLE ||
        value == CNA_CURVE_LOOP_CYCLE_OFFSET || value == CNA_CURVE_LOOP_OSCILLATE ||
        value == CNA_CURVE_LOOP_LINEAR;
}

[[nodiscard]] CNA_Result ValidateLoopType(const CNA_CurveLoopType value) noexcept
{
    if (IsValidLoopType(value)) {
        return CNA_RESULT_SUCCESS;
    }
    return InvalidArgument("The curve loop type is invalid.");
}

[[nodiscard]] bool IsValidTangent(const CNA_CurveTangent value) noexcept
{
    return value == CNA_CURVE_TANGENT_FLAT || value == CNA_CURVE_TANGENT_LINEAR ||
        value == CNA_CURVE_TANGENT_SMOOTH;
}

[[nodiscard]] CNA_Result ValidateTangent(const CNA_CurveTangent value) noexcept
{
    if (IsValidTangent(value)) {
        return CNA_RESULT_SUCCESS;
    }
    return InvalidArgument("The curve tangent type is invalid.");
}

template<typename TCallable>
[[nodiscard]] CNA_Result MutateValidatedKey(
    CNA_CurveKey* const key,
    TCallable&& callable) noexcept
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (key == nullptr) {
            return InvalidArgument("The CurveKey pointer is null.");
        }
        const CNA_Result validation = ValidateKey(*key);
        if (validation != CNA_RESULT_SUCCESS) {
            return validation;
        }
        CurveKey native = ToNative(*key);
        std::forward<TCallable>(callable)(native);
        const CNA_CurveKey result = ToC(native);
        *key = result;
        return CNA_RESULT_SUCCESS;
    });
}

[[nodiscard]] CNA_Result GetCollection(
    const CNA_CurveKeyCollectionHandle handle,
    std::shared_ptr<CurveKeyCollection>* const outCollection)
{
    return LookupHandle(handle, ObjectKind::CurveKeyCollection, outCollection);
}

[[nodiscard]] CNA_Result GetCurve(
    const CNA_CurveHandle handle,
    std::shared_ptr<Curve>* const outCurve)
{
    return LookupHandle(handle, ObjectKind::Curve, outCurve);
}

} // namespace

CNA_Result cna_curve_key_set_continuity(
    CNA_CurveKey* const key,
    const CNA_CurveContinuity continuity)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (key == nullptr) {
            return InvalidArgument("The CurveKey pointer is null.");
        }
        CNA_Result validation = ValidateKey(*key);
        if (validation == CNA_RESULT_SUCCESS) {
            validation = ValidateContinuity(continuity);
        }
        if (validation != CNA_RESULT_SUCCESS) {
            return validation;
        }
        CurveKey native = ToNative(*key);
        native.setContinuityProperty(static_cast<CurveContinuity>(continuity));
        const CNA_CurveKey result = ToC(native);
        *key = result;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_curve_key_set_tangent_in(
    CNA_CurveKey* const key,
    const float tangent)
{
    return MutateValidatedKey(key, [=](CurveKey& native) {
        native.setTangentInProperty(tangent);
    });
}

CNA_Result cna_curve_key_set_tangent_out(
    CNA_CurveKey* const key,
    const float tangent)
{
    return MutateValidatedKey(key, [=](CurveKey& native) {
        native.setTangentOutProperty(tangent);
    });
}

CNA_Result cna_curve_key_set_value(
    CNA_CurveKey* const key,
    const float value)
{
    return MutateValidatedKey(key, [=](CurveKey& native) {
        native.setValueProperty(value);
    });
}

CNA_Result cna_curve_key_compare_to(
    const CNA_CurveKey value,
    const CNA_CurveKey other,
    int32_t* const outComparison)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outComparison == nullptr) {
            return InvalidArgument("The comparison output is null.");
        }
        CNA_Result validation = ValidateKey(value);
        if (validation == CNA_RESULT_SUCCESS) {
            validation = ValidateKey(other);
        }
        if (validation != CNA_RESULT_SUCCESS) {
            return validation;
        }
        const int32_t result = static_cast<int32_t>(ToNative(value).CompareTo(ToNative(other)));
        *outComparison = result;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_curve_key_collection_copy_to(
    const CNA_CurveKeyCollectionHandle collectionHandle,
    CNA_CurveKey* const destination,
    const uint64_t capacity,
    const int32_t destinationIndex,
    uint64_t* const outCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outCount == nullptr || (destination == nullptr && capacity != 0U)) {
            return InvalidArgument("The key destination or count output is invalid.");
        }
        if (destinationIndex < 0 || static_cast<uint64_t>(destinationIndex) > capacity) {
            return OutOfRange("The destination index is out of range.");
        }
        std::shared_ptr<CurveKeyCollection> collection;
        if (const CNA_Result result = GetCollection(collectionHandle, &collection);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const uint64_t count = static_cast<uint64_t>(collection->getCountProperty());
        *outCount = count;
        const uint64_t start = static_cast<uint64_t>(destinationIndex);
        if (count > capacity - start) {
            return BufferTooSmall("The destination cannot hold all CurveKeyCollection items.");
        }
        for (uint64_t index = 0U; index < count; ++index) {
            destination[start + index] = ToC(
                collection->getItemProperty(static_cast<int32_t>(index)));
        }
        return CNA_RESULT_SUCCESS;
    });
}

// The routes generated from modules/c-api/spec/Curve.toml.
#include "generated/Curve.inc"
