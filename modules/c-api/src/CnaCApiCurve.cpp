// SPDX-License-Identifier: MS-PL

#include "CNA/C/curve.h"
#include "CnaCApiDetail.hpp"

#include "Microsoft/Xna/Framework/CurveContinuity.hpp"
#include "Microsoft/Xna/Framework/CurveKey.hpp"

#include <utility>

namespace {

using CNA::C::Detail::CallWithExceptionBarrier;
using CNA::C::Detail::Fail;
using Microsoft::Xna::Framework::CurveContinuity;
using Microsoft::Xna::Framework::CurveKey;

[[nodiscard]] bool IsValidContinuity(const CNA_CurveContinuity value) noexcept
{
    return value == CNA_CURVE_CONTINUITY_SMOOTH || value == CNA_CURVE_CONTINUITY_STEP;
}

[[nodiscard]] CNA_Result ValidateContinuity(const CNA_CurveContinuity value) noexcept
{
    if (IsValidContinuity(value)) {
        return CNA_RESULT_SUCCESS;
    }
    return Fail(
        CNA_RESULT_INVALID_ARGUMENT,
        CNA_ERROR_CATEGORY_ARGUMENT,
        "The curve continuity is invalid.");
}

[[nodiscard]] CNA_Result ValidateKey(const CNA_CurveKey value) noexcept
{
    return ValidateContinuity(value.continuity);
}

[[nodiscard]] CurveKey ToNative(const CNA_CurveKey value)
{
    return CurveKey(
        value.position,
        value.value,
        value.tangent_in,
        value.tangent_out,
        static_cast<CurveContinuity>(value.continuity));
}

[[nodiscard]] CNA_CurveKey ToC(const CurveKey& value) noexcept
{
    return CNA_CurveKey{
        value.getPositionProperty(),
        value.getValueProperty(),
        value.getTangentInProperty(),
        value.getTangentOutProperty(),
        static_cast<CNA_CurveContinuity>(value.getContinuityProperty())};
}

template<typename TValue, typename TCallable>
[[nodiscard]] CNA_Result StoreOutput(
    TValue* const output,
    const char* const nullMessage,
    TCallable&& callable) noexcept
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (output == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                nullMessage);
        }
        const TValue result = std::forward<TCallable>(callable)();
        *output = result;
        return CNA_RESULT_SUCCESS;
    });
}

template<typename TValue, typename TCallable>
[[nodiscard]] CNA_Result StoreValidatedKeyOutput(
    const CNA_CurveKey key,
    TValue* const output,
    const char* const nullMessage,
    TCallable&& callable) noexcept
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (output == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                nullMessage);
        }
        const CNA_Result validation = ValidateKey(key);
        if (validation != CNA_RESULT_SUCCESS) {
            return validation;
        }
        const TValue result = std::forward<TCallable>(callable)(ToNative(key));
        *output = result;
        return CNA_RESULT_SUCCESS;
    });
}

template<typename TCallable>
[[nodiscard]] CNA_Result MutateValidatedKey(
    CNA_CurveKey* const key,
    TCallable&& callable) noexcept
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (key == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The CurveKey pointer is null.");
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

} // namespace

CNA_Result cna_curve_key_init_position_value(
    const float position,
    const float value,
    CNA_CurveKey* const outKey)
{
    return StoreOutput(outKey, "The CurveKey output is null.", [=] {
        return ToC(CurveKey(position, value));
    });
}

CNA_Result cna_curve_key_init_tangents(
    const float position,
    const float value,
    const float tangentIn,
    const float tangentOut,
    CNA_CurveKey* const outKey)
{
    return StoreOutput(outKey, "The CurveKey output is null.", [=] {
        return ToC(CurveKey(position, value, tangentIn, tangentOut));
    });
}

CNA_Result cna_curve_key_init_full(
    const float position,
    const float value,
    const float tangentIn,
    const float tangentOut,
    const CNA_CurveContinuity continuity,
    CNA_CurveKey* const outKey)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outKey == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The CurveKey output is null.");
        }
        const CNA_Result validation = ValidateContinuity(continuity);
        if (validation != CNA_RESULT_SUCCESS) {
            return validation;
        }
        const CNA_CurveKey result = ToC(CurveKey(
            position,
            value,
            tangentIn,
            tangentOut,
            static_cast<CurveContinuity>(continuity)));
        *outKey = result;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_curve_key_get_continuity(
    const CNA_CurveKey key,
    CNA_CurveContinuity* const outContinuity)
{
    return StoreValidatedKeyOutput(
        key,
        outContinuity,
        "The continuity output is null.",
        [](const CurveKey& native) {
            return static_cast<CNA_CurveContinuity>(native.getContinuityProperty());
        });
}

CNA_Result cna_curve_key_set_continuity(
    CNA_CurveKey* const key,
    const CNA_CurveContinuity continuity)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (key == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The CurveKey pointer is null.");
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

CNA_Result cna_curve_key_get_position(
    const CNA_CurveKey key,
    float* const outPosition)
{
    return StoreValidatedKeyOutput(
        key,
        outPosition,
        "The position output is null.",
        [](const CurveKey& native) { return native.getPositionProperty(); });
}

CNA_Result cna_curve_key_get_tangent_in(
    const CNA_CurveKey key,
    float* const outTangent)
{
    return StoreValidatedKeyOutput(
        key,
        outTangent,
        "The tangent output is null.",
        [](const CurveKey& native) { return native.getTangentInProperty(); });
}

CNA_Result cna_curve_key_set_tangent_in(
    CNA_CurveKey* const key,
    const float tangent)
{
    return MutateValidatedKey(key, [=](CurveKey& native) {
        native.setTangentInProperty(tangent);
    });
}

CNA_Result cna_curve_key_get_tangent_out(
    const CNA_CurveKey key,
    float* const outTangent)
{
    return StoreValidatedKeyOutput(
        key,
        outTangent,
        "The tangent output is null.",
        [](const CurveKey& native) { return native.getTangentOutProperty(); });
}

CNA_Result cna_curve_key_set_tangent_out(
    CNA_CurveKey* const key,
    const float tangent)
{
    return MutateValidatedKey(key, [=](CurveKey& native) {
        native.setTangentOutProperty(tangent);
    });
}

CNA_Result cna_curve_key_get_value(
    const CNA_CurveKey key,
    float* const outValue)
{
    return StoreValidatedKeyOutput(
        key,
        outValue,
        "The value output is null.",
        [](const CurveKey& native) { return native.getValueProperty(); });
}

CNA_Result cna_curve_key_set_value(
    CNA_CurveKey* const key,
    const float value)
{
    return MutateValidatedKey(key, [=](CurveKey& native) {
        native.setValueProperty(value);
    });
}

CNA_Result cna_curve_key_clone(
    const CNA_CurveKey key,
    CNA_CurveKey* const outKey)
{
    return StoreValidatedKeyOutput(
        key,
        outKey,
        "The CurveKey output is null.",
        [](const CurveKey& native) { return ToC(native.Clone()); });
}

CNA_Result cna_curve_key_compare_to(
    const CNA_CurveKey value,
    const CNA_CurveKey other,
    int32_t* const outComparison)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outComparison == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The comparison output is null.");
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

CNA_Result cna_curve_key_equals(
    const CNA_CurveKey left,
    const CNA_CurveKey right,
    CNA_Bool* const outEqual)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outEqual == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The Boolean output is null.");
        }
        CNA_Result validation = ValidateKey(left);
        if (validation == CNA_RESULT_SUCCESS) {
            validation = ValidateKey(right);
        }
        if (validation != CNA_RESULT_SUCCESS) {
            return validation;
        }
        const CNA_Bool result = ToNative(left).Equals(ToNative(right)) ? CNA_TRUE : CNA_FALSE;
        *outEqual = result;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_curve_key_not_equals(
    const CNA_CurveKey left,
    const CNA_CurveKey right,
    CNA_Bool* const outNotEqual)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outNotEqual == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The Boolean output is null.");
        }
        CNA_Result validation = ValidateKey(left);
        if (validation == CNA_RESULT_SUCCESS) {
            validation = ValidateKey(right);
        }
        if (validation != CNA_RESULT_SUCCESS) {
            return validation;
        }
        const CNA_Bool result = ToNative(left) != ToNative(right) ? CNA_TRUE : CNA_FALSE;
        *outNotEqual = result;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_curve_key_get_hash_code(
    const CNA_CurveKey key,
    int32_t* const outHash)
{
    return StoreValidatedKeyOutput(
        key,
        outHash,
        "The hash output is null.",
        [](const CurveKey& native) {
            return static_cast<int32_t>(native.GetHashCode());
        });
}
