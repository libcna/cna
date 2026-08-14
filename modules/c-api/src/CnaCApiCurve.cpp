// SPDX-License-Identifier: MS-PL

#include "CNA/C/curve.h"
#include "CnaCApiDetail.hpp"
#include "CnaCApiRuntimeDetail.hpp"

#include "Microsoft/Xna/Framework/CurveContinuity.hpp"
#include "Microsoft/Xna/Framework/CurveKey.hpp"
#include "Microsoft/Xna/Framework/CurveKeyCollection.hpp"

#include <memory>
#include <utility>

namespace {

using CNA::C::Detail::CallWithExceptionBarrier;
using CNA::C::Detail::ErrorCategoryForResult;
using CNA::C::Detail::Fail;
using CNA::C::Detail::GetRuntimeHandles;
using CNA::C::Detail::ObjectKind;
using Microsoft::Xna::Framework::CurveContinuity;
using Microsoft::Xna::Framework::CurveKey;
using Microsoft::Xna::Framework::CurveKeyCollection;

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

[[nodiscard]] CNA_Result GetCollection(
    const CNA_CurveKeyCollectionHandle handle,
    std::shared_ptr<CurveKeyCollection>* const outCollection)
{
    const CNA_Result result = GetRuntimeHandles().Get(
        handle,
        ObjectKind::CurveKeyCollection,
        outCollection);
    if (result == CNA_RESULT_SUCCESS) {
        return CNA_RESULT_SUCCESS;
    }
    return Fail(
        result,
        ErrorCategoryForResult(result),
        "The CurveKeyCollection handle is invalid for this call.");
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

CNA_Result cna_curve_key_collection_create(
    CNA_CurveKeyCollectionHandle* const outCollection)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outCollection == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The CurveKeyCollection output handle is null.");
        }
        *outCollection = CNA_INVALID_HANDLE;
        const auto collection = std::make_shared<CurveKeyCollection>();
        const CNA_Result result = GetRuntimeHandles().Create(
            ObjectKind::CurveKeyCollection,
            collection,
            outCollection);
        if (result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The CurveKeyCollection handle could not be created.");
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_curve_key_collection_destroy(
    const CNA_CurveKeyCollectionHandle collectionHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<CurveKeyCollection> collection;
        if (const CNA_Result result = GetCollection(collectionHandle, &collection);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const CNA_Result result = GetRuntimeHandles().Release(collectionHandle);
        if (result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The CurveKeyCollection handle could not be destroyed.");
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_curve_key_collection_get_count(
    const CNA_CurveKeyCollectionHandle collectionHandle,
    uint64_t* const outCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outCount == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The collection-count output is null.");
        }
        std::shared_ptr<CurveKeyCollection> collection;
        if (const CNA_Result result = GetCollection(collectionHandle, &collection);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const uint64_t count = static_cast<uint64_t>(collection->getCountProperty());
        *outCount = count;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_curve_key_collection_get_is_read_only(
    const CNA_CurveKeyCollectionHandle collectionHandle,
    CNA_Bool* const outIsReadOnly)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outIsReadOnly == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The read-only output is null.");
        }
        std::shared_ptr<CurveKeyCollection> collection;
        if (const CNA_Result result = GetCollection(collectionHandle, &collection);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const CNA_Bool result = collection->getIsReadOnlyProperty() ? CNA_TRUE : CNA_FALSE;
        *outIsReadOnly = result;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_curve_key_collection_get(
    const CNA_CurveKeyCollectionHandle collectionHandle,
    const int32_t index,
    CNA_CurveKey* const outKey)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outKey == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The CurveKey output is null.");
        }
        std::shared_ptr<CurveKeyCollection> collection;
        if (const CNA_Result result = GetCollection(collectionHandle, &collection);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const CNA_CurveKey result = ToC(collection->getItemProperty(index));
        *outKey = result;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_curve_key_collection_set(
    const CNA_CurveKeyCollectionHandle collectionHandle,
    const int32_t index,
    const CNA_CurveKey key)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (const CNA_Result result = ValidateKey(key); result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::shared_ptr<CurveKeyCollection> collection;
        if (const CNA_Result result = GetCollection(collectionHandle, &collection);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        collection->setItemProperty(index, ToNative(key));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_curve_key_collection_add(
    const CNA_CurveKeyCollectionHandle collectionHandle,
    const CNA_CurveKey key)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (const CNA_Result result = ValidateKey(key); result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::shared_ptr<CurveKeyCollection> collection;
        if (const CNA_Result result = GetCollection(collectionHandle, &collection);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        collection->Add(ToNative(key));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_curve_key_collection_clear(
    const CNA_CurveKeyCollectionHandle collectionHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<CurveKeyCollection> collection;
        if (const CNA_Result result = GetCollection(collectionHandle, &collection);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        collection->Clear();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_curve_key_collection_clone(
    const CNA_CurveKeyCollectionHandle collectionHandle,
    CNA_CurveKeyCollectionHandle* const outCollection)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outCollection == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The CurveKeyCollection output handle is null.");
        }
        *outCollection = CNA_INVALID_HANDLE;
        std::shared_ptr<CurveKeyCollection> collection;
        if (const CNA_Result result = GetCollection(collectionHandle, &collection);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const auto clone = std::make_shared<CurveKeyCollection>(collection->Clone());
        const CNA_Result result = GetRuntimeHandles().Create(
            ObjectKind::CurveKeyCollection,
            clone,
            outCollection);
        if (result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The cloned CurveKeyCollection handle could not be created.");
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_curve_key_collection_contains(
    const CNA_CurveKeyCollectionHandle collectionHandle,
    const CNA_CurveKey key,
    CNA_Bool* const outContains)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outContains == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The Boolean output is null.");
        }
        if (const CNA_Result result = ValidateKey(key); result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::shared_ptr<CurveKeyCollection> collection;
        if (const CNA_Result result = GetCollection(collectionHandle, &collection);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const CNA_Bool result = collection->Contains(ToNative(key)) ? CNA_TRUE : CNA_FALSE;
        *outContains = result;
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
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The key destination or count output is invalid.");
        }
        if (destinationIndex < 0 || static_cast<uint64_t>(destinationIndex) > capacity) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_RANGE,
                "The destination index is out of range.");
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
            return Fail(
                CNA_RESULT_BUFFER_TOO_SMALL,
                CNA_ERROR_CATEGORY_RANGE,
                "The destination cannot hold all CurveKeyCollection items.");
        }
        for (uint64_t index = 0U; index < count; ++index) {
            destination[start + index] = ToC(
                collection->getItemProperty(static_cast<int32_t>(index)));
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_curve_key_collection_index_of(
    const CNA_CurveKeyCollectionHandle collectionHandle,
    const CNA_CurveKey key,
    int32_t* const outIndex)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outIndex == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The index output is null.");
        }
        if (const CNA_Result result = ValidateKey(key); result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::shared_ptr<CurveKeyCollection> collection;
        if (const CNA_Result result = GetCollection(collectionHandle, &collection);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const int32_t result = collection->IndexOf(ToNative(key));
        *outIndex = result;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_curve_key_collection_remove(
    const CNA_CurveKeyCollectionHandle collectionHandle,
    const CNA_CurveKey key,
    CNA_Bool* const outRemoved)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outRemoved == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The Boolean output is null.");
        }
        if (const CNA_Result result = ValidateKey(key); result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::shared_ptr<CurveKeyCollection> collection;
        if (const CNA_Result result = GetCollection(collectionHandle, &collection);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const CNA_Bool result = collection->Remove(ToNative(key)) ? CNA_TRUE : CNA_FALSE;
        *outRemoved = result;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_curve_key_collection_remove_at(
    const CNA_CurveKeyCollectionHandle collectionHandle,
    const int32_t index)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<CurveKeyCollection> collection;
        if (const CNA_Result result = GetCollection(collectionHandle, &collection);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        collection->RemoveAt(index);
        return CNA_RESULT_SUCCESS;
    });
}
