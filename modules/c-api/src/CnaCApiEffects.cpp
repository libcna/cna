// SPDX-License-Identifier: MS-PL

#include "CNA/C/effects.h"
#include "CnaCApiDetail.hpp"
#include "CnaCApiRuntimeDetail.hpp"

#include "Microsoft/Xna/Framework/Graphics/EffectAnnotation.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectAnnotationCollection.hpp"

#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

using CNA::C::Detail::CallWithExceptionBarrier;
using CNA::C::Detail::CheckedElementByteCount;
using CNA::C::Detail::CopyStringView;
using CNA::C::Detail::ErrorCategoryForResult;
using CNA::C::Detail::Fail;
using CNA::C::Detail::GetRuntimeHandles;
using CNA::C::Detail::ObjectKind;
using Microsoft::Xna::Framework::Matrix;
using Microsoft::Xna::Framework::Vector2;
using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Vector4;
using Microsoft::Xna::Framework::Graphics::EffectAnnotation;
using Microsoft::Xna::Framework::Graphics::EffectAnnotationCollection;
using Microsoft::Xna::Framework::Graphics::EffectParameterClass;
using Microsoft::Xna::Framework::Graphics::EffectParameterType;

constexpr uint32_t StructureVersion = UINT32_C(1);

struct AnnotationResource final {
    std::shared_ptr<EffectAnnotation> value;
};

struct AnnotationCollectionResource final {
    std::shared_ptr<EffectAnnotationCollection> value;
};

[[nodiscard]] CNA_Result InvalidArgument(const char* const message)
{
    return Fail(CNA_RESULT_INVALID_ARGUMENT, CNA_ERROR_CATEGORY_ARGUMENT, message);
}

[[nodiscard]] CNA_Result GetAnnotation(
    const CNA_EffectAnnotationHandle handle,
    std::shared_ptr<AnnotationResource>* const outAnnotation)
{
    const CNA_Result result = GetRuntimeHandles().Get(
        handle, ObjectKind::EffectAnnotation, outAnnotation);
    if (result == CNA_RESULT_SUCCESS) {
        return CNA_RESULT_SUCCESS;
    }
    return Fail(
        result,
        ErrorCategoryForResult(result),
        "The EffectAnnotation handle is invalid for this call.");
}

[[nodiscard]] CNA_Result GetCollection(
    const CNA_EffectAnnotationCollectionHandle handle,
    std::shared_ptr<AnnotationCollectionResource>* const outCollection)
{
    const CNA_Result result = GetRuntimeHandles().Get(
        handle, ObjectKind::EffectAnnotationCollection, outCollection);
    if (result == CNA_RESULT_SUCCESS) {
        return CNA_RESULT_SUCCESS;
    }
    return Fail(
        result,
        ErrorCategoryForResult(result),
        "The EffectAnnotationCollection handle is invalid for this call.");
}

[[nodiscard]] CNA_Result CreateAnnotationHandle(
    const EffectAnnotation& annotation,
    CNA_EffectAnnotationHandle* const outAnnotation)
{
    const auto resource = std::make_shared<AnnotationResource>(
        AnnotationResource{std::make_shared<EffectAnnotation>(annotation)});
    const CNA_Result result = GetRuntimeHandles().Create(
        ObjectKind::EffectAnnotation, resource, outAnnotation);
    if (result == CNA_RESULT_SUCCESS) {
        return CNA_RESULT_SUCCESS;
    }
    return Fail(
        result,
        ErrorCategoryForResult(result),
        "The owned EffectAnnotation handle could not be created.");
}

template<typename TCallable>
[[nodiscard]] CNA_Result GetStringByteCount(
    uint64_t* const outByteCount,
    TCallable&& callable)
{
    if (outByteCount == nullptr) {
        return InvalidArgument("The EffectAnnotation string byte-count output is null.");
    }
    const std::string value = std::forward<TCallable>(callable)();
    *outByteCount = static_cast<uint64_t>(value.size());
    return CNA_RESULT_SUCCESS;
}

template<typename TCallable>
[[nodiscard]] CNA_Result CopyString(
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outByteCount,
    TCallable&& callable)
{
    if (outByteCount == nullptr || (destination == nullptr && capacity != 0U)) {
        return InvalidArgument("The EffectAnnotation string output is invalid.");
    }
    const std::string value = std::forward<TCallable>(callable)();
    *outByteCount = static_cast<uint64_t>(value.size());
    if (capacity < value.size()) {
        return Fail(
            CNA_RESULT_BUFFER_TOO_SMALL,
            CNA_ERROR_CATEGORY_RANGE,
            "The destination cannot hold the complete EffectAnnotation string.");
    }
    if (!value.empty()) {
        std::memcpy(destination, value.data(), value.size());
    }
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Vector2 ToC(const Vector2 value) noexcept
{
    return CNA_Vector2{value.X, value.Y};
}

[[nodiscard]] CNA_Vector3 ToC(const Vector3 value) noexcept
{
    return CNA_Vector3{value.X, value.Y, value.Z};
}

[[nodiscard]] CNA_Vector4 ToC(const Vector4 value) noexcept
{
    return CNA_Vector4{value.X, value.Y, value.Z, value.W};
}

[[nodiscard]] CNA_Matrix ToC(const Matrix value) noexcept
{
    return CNA_Matrix{
        value.M11, value.M12, value.M13, value.M14,
        value.M21, value.M22, value.M23, value.M24,
        value.M31, value.M32, value.M33, value.M34,
        value.M41, value.M42, value.M43, value.M44};
}

} // namespace

CNA_Result cna_effect_annotation_create(
    const CNA_EffectAnnotationCreateInfo* const createInfo,
    CNA_EffectAnnotationHandle* const outAnnotation)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outAnnotation == nullptr) {
            return InvalidArgument("The EffectAnnotation output handle is null.");
        }
        *outAnnotation = CNA_INVALID_HANDLE;
        if (createInfo == nullptr ||
            createInfo->struct_size < sizeof(CNA_EffectAnnotationCreateInfo) ||
            createInfo->struct_version != StructureVersion ||
            createInfo->parameter_class > CNA_EFFECT_PARAMETER_CLASS_STRUCT ||
            createInfo->parameter_type > CNA_EFFECT_PARAMETER_TYPE_TEXTURE_CUBE) {
            return InvalidArgument("The EffectAnnotation creation configuration is invalid.");
        }
        std::size_t dataByteCount = 0U;
        if (const CNA_Result result = CheckedElementByteCount(
                createInfo->data,
                createInfo->data_count,
                sizeof(float),
                &dataByteCount);
            result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The EffectAnnotation data buffer is invalid.");
        }
        std::string name;
        std::string semantic;
        std::string cachedString;
        if (const CNA_Result result = CopyStringView(createInfo->name, true, &name);
            result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The EffectAnnotation name is not valid UTF-8 text.");
        }
        if (const CNA_Result result = CopyStringView(
                createInfo->semantic, true, &semantic);
            result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The EffectAnnotation semantic is not valid UTF-8 text.");
        }
        if (const CNA_Result result = CopyStringView(
                createInfo->cached_string, true, &cachedString);
            result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The EffectAnnotation cached string is not valid UTF-8 text.");
        }
        std::vector<float> data;
        if (dataByteCount != 0U) {
            data.assign(
                createInfo->data,
                createInfo->data + static_cast<std::size_t>(createInfo->data_count));
        }
        const EffectAnnotation native(
            std::move(name),
            std::move(semantic),
            createInfo->row_count,
            createInfo->column_count,
            static_cast<EffectParameterClass>(createInfo->parameter_class),
            static_cast<EffectParameterType>(createInfo->parameter_type),
            std::move(data),
            std::move(cachedString));
        return CreateAnnotationHandle(native, outAnnotation);
    });
}

CNA_Result cna_effect_annotation_destroy(const CNA_EffectAnnotationHandle annotationHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<AnnotationResource> annotation;
        if (const CNA_Result result = GetAnnotation(annotationHandle, &annotation);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const CNA_Result result = GetRuntimeHandles().Release(annotationHandle);
        if (result == CNA_RESULT_SUCCESS) {
            return CNA_RESULT_SUCCESS;
        }
        return Fail(
            result,
            ErrorCategoryForResult(result),
            "The owned EffectAnnotation handle could not be released.");
    });
}

CNA_Result cna_effect_annotation_get_info(
    const CNA_EffectAnnotationHandle annotationHandle,
    CNA_EffectAnnotationInfo* const outInfo)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outInfo == nullptr ||
            outInfo->struct_size < sizeof(CNA_EffectAnnotationInfo) ||
            outInfo->struct_version != StructureVersion) {
            return InvalidArgument("The EffectAnnotation info output structure is invalid.");
        }
        std::shared_ptr<AnnotationResource> annotation;
        if (const CNA_Result result = GetAnnotation(annotationHandle, &annotation);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outInfo = CNA_EffectAnnotationInfo{
            sizeof(CNA_EffectAnnotationInfo),
            StructureVersion,
            annotation->value->getRowCountProperty(),
            annotation->value->getColumnCountProperty(),
            static_cast<CNA_EffectParameterClass>(
                annotation->value->getParameterClassProperty()),
            static_cast<CNA_EffectParameterType>(
                annotation->value->getParameterTypeProperty())};
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_effect_annotation_get_name_byte_count(
    const CNA_EffectAnnotationHandle annotationHandle,
    uint64_t* const outByteCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<AnnotationResource> annotation;
        if (const CNA_Result result = GetAnnotation(annotationHandle, &annotation);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return GetStringByteCount(outByteCount, [&]() {
            return annotation->value->getNameProperty();
        });
    });
}

CNA_Result cna_effect_annotation_copy_name(
    const CNA_EffectAnnotationHandle annotationHandle,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outByteCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<AnnotationResource> annotation;
        if (const CNA_Result result = GetAnnotation(annotationHandle, &annotation);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CopyString(destination, capacity, outByteCount, [&]() {
            return annotation->value->getNameProperty();
        });
    });
}

CNA_Result cna_effect_annotation_get_semantic_byte_count(
    const CNA_EffectAnnotationHandle annotationHandle,
    uint64_t* const outByteCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<AnnotationResource> annotation;
        if (const CNA_Result result = GetAnnotation(annotationHandle, &annotation);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return GetStringByteCount(outByteCount, [&]() {
            return annotation->value->getSemanticProperty();
        });
    });
}

CNA_Result cna_effect_annotation_copy_semantic(
    const CNA_EffectAnnotationHandle annotationHandle,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outByteCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<AnnotationResource> annotation;
        if (const CNA_Result result = GetAnnotation(annotationHandle, &annotation);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CopyString(destination, capacity, outByteCount, [&]() {
            return annotation->value->getSemanticProperty();
        });
    });
}

CNA_Result cna_effect_annotation_get_value_boolean(
    const CNA_EffectAnnotationHandle annotationHandle,
    CNA_Bool* const outValue)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outValue == nullptr) {
            return InvalidArgument("The EffectAnnotation Boolean output is null.");
        }
        std::shared_ptr<AnnotationResource> annotation;
        if (const CNA_Result result = GetAnnotation(annotationHandle, &annotation);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outValue = annotation->value->GetValueBoolean() ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_effect_annotation_get_value_int32(
    const CNA_EffectAnnotationHandle annotationHandle,
    int32_t* const outValue)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outValue == nullptr) {
            return InvalidArgument("The EffectAnnotation Int32 output is null.");
        }
        std::shared_ptr<AnnotationResource> annotation;
        if (const CNA_Result result = GetAnnotation(annotationHandle, &annotation);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outValue = static_cast<int32_t>(annotation->value->GetValueInt32());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_effect_annotation_get_value_single(
    const CNA_EffectAnnotationHandle annotationHandle,
    float* const outValue)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outValue == nullptr) {
            return InvalidArgument("The EffectAnnotation Single output is null.");
        }
        std::shared_ptr<AnnotationResource> annotation;
        if (const CNA_Result result = GetAnnotation(annotationHandle, &annotation);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outValue = annotation->value->GetValueSingle();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_effect_annotation_get_value_string_byte_count(
    const CNA_EffectAnnotationHandle annotationHandle,
    uint64_t* const outByteCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<AnnotationResource> annotation;
        if (const CNA_Result result = GetAnnotation(annotationHandle, &annotation);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return GetStringByteCount(outByteCount, [&]() {
            return annotation->value->GetValueString();
        });
    });
}

CNA_Result cna_effect_annotation_copy_value_string(
    const CNA_EffectAnnotationHandle annotationHandle,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outByteCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<AnnotationResource> annotation;
        if (const CNA_Result result = GetAnnotation(annotationHandle, &annotation);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CopyString(destination, capacity, outByteCount, [&]() {
            return annotation->value->GetValueString();
        });
    });
}

CNA_Result cna_effect_annotation_get_value_vector2(
    const CNA_EffectAnnotationHandle annotationHandle,
    CNA_Vector2* const outValue)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outValue == nullptr) {
            return InvalidArgument("The EffectAnnotation Vector2 output is null.");
        }
        std::shared_ptr<AnnotationResource> annotation;
        if (const CNA_Result result = GetAnnotation(annotationHandle, &annotation);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outValue = ToC(annotation->value->GetValueVector2());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_effect_annotation_get_value_vector3(
    const CNA_EffectAnnotationHandle annotationHandle,
    CNA_Vector3* const outValue)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outValue == nullptr) {
            return InvalidArgument("The EffectAnnotation Vector3 output is null.");
        }
        std::shared_ptr<AnnotationResource> annotation;
        if (const CNA_Result result = GetAnnotation(annotationHandle, &annotation);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outValue = ToC(annotation->value->GetValueVector3());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_effect_annotation_get_value_vector4(
    const CNA_EffectAnnotationHandle annotationHandle,
    CNA_Vector4* const outValue)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outValue == nullptr) {
            return InvalidArgument("The EffectAnnotation Vector4 output is null.");
        }
        std::shared_ptr<AnnotationResource> annotation;
        if (const CNA_Result result = GetAnnotation(annotationHandle, &annotation);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outValue = ToC(annotation->value->GetValueVector4());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_effect_annotation_get_value_matrix(
    const CNA_EffectAnnotationHandle annotationHandle,
    CNA_Matrix* const outValue)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outValue == nullptr) {
            return InvalidArgument("The EffectAnnotation Matrix output is null.");
        }
        std::shared_ptr<AnnotationResource> annotation;
        if (const CNA_Result result = GetAnnotation(annotationHandle, &annotation);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outValue = ToC(annotation->value->GetValueMatrix());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_effect_annotation_collection_create(
    CNA_EffectAnnotationCollectionHandle* const outCollection)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outCollection == nullptr) {
            return InvalidArgument("The EffectAnnotationCollection output handle is null.");
        }
        *outCollection = CNA_INVALID_HANDLE;
        const auto resource = std::make_shared<AnnotationCollectionResource>(
            AnnotationCollectionResource{std::make_shared<EffectAnnotationCollection>()});
        const CNA_Result result = GetRuntimeHandles().Create(
            ObjectKind::EffectAnnotationCollection, resource, outCollection);
        if (result == CNA_RESULT_SUCCESS) {
            return CNA_RESULT_SUCCESS;
        }
        return Fail(
            result,
            ErrorCategoryForResult(result),
            "The owned EffectAnnotationCollection handle could not be created.");
    });
}

CNA_Result cna_effect_annotation_collection_destroy(
    const CNA_EffectAnnotationCollectionHandle collectionHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<AnnotationCollectionResource> collection;
        if (const CNA_Result result = GetCollection(collectionHandle, &collection);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const CNA_Result result = GetRuntimeHandles().Release(collectionHandle);
        if (result == CNA_RESULT_SUCCESS) {
            return CNA_RESULT_SUCCESS;
        }
        return Fail(
            result,
            ErrorCategoryForResult(result),
            "The owned EffectAnnotationCollection handle could not be released.");
    });
}

CNA_Result cna_effect_annotation_collection_get_count(
    const CNA_EffectAnnotationCollectionHandle collectionHandle,
    uint64_t* const outCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outCount == nullptr) {
            return InvalidArgument("The EffectAnnotationCollection count output is null.");
        }
        std::shared_ptr<AnnotationCollectionResource> collection;
        if (const CNA_Result result = GetCollection(collectionHandle, &collection);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outCount = static_cast<uint64_t>(collection->value->getCountProperty());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_effect_annotation_collection_add(
    const CNA_EffectAnnotationCollectionHandle collectionHandle,
    const CNA_EffectAnnotationHandle annotationHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<AnnotationCollectionResource> collection;
        if (const CNA_Result result = GetCollection(collectionHandle, &collection);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::shared_ptr<AnnotationResource> annotation;
        if (const CNA_Result result = GetAnnotation(annotationHandle, &annotation);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        collection->value->Add(*annotation->value);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_effect_annotation_collection_get_at(
    const CNA_EffectAnnotationCollectionHandle collectionHandle,
    const uint64_t index,
    CNA_EffectAnnotationHandle* const outAnnotation)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outAnnotation == nullptr) {
            return InvalidArgument("The EffectAnnotation output handle is null.");
        }
        *outAnnotation = CNA_INVALID_HANDLE;
        std::shared_ptr<AnnotationCollectionResource> collection;
        if (const CNA_Result result = GetCollection(collectionHandle, &collection);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (index >= static_cast<uint64_t>(collection->value->getCountProperty()) ||
            index > static_cast<uint64_t>(std::numeric_limits<int>::max())) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_RANGE,
                "The EffectAnnotationCollection index is outside the collection.");
        }
        return CreateAnnotationHandle(
            (*collection->value)[static_cast<int>(index)], outAnnotation);
    });
}

CNA_Result cna_effect_annotation_collection_find(
    const CNA_EffectAnnotationCollectionHandle collectionHandle,
    const CNA_StringView name,
    CNA_Bool* const outFound,
    CNA_EffectAnnotationHandle* const outAnnotation)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outFound == nullptr || outAnnotation == nullptr) {
            return InvalidArgument("The EffectAnnotationCollection find output is null.");
        }
        *outFound = CNA_FALSE;
        *outAnnotation = CNA_INVALID_HANDLE;
        std::shared_ptr<AnnotationCollectionResource> collection;
        if (const CNA_Result result = GetCollection(collectionHandle, &collection);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::string copiedName;
        if (const CNA_Result result = CopyStringView(name, true, &copiedName);
            result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The EffectAnnotation lookup name is not valid UTF-8 text.");
        }
        const EffectAnnotation* const annotation =
            static_cast<const EffectAnnotationCollection&>(*collection->value)[copiedName];
        if (annotation == nullptr) {
            return CNA_RESULT_SUCCESS;
        }
        const CNA_Result result = CreateAnnotationHandle(*annotation, outAnnotation);
        if (result == CNA_RESULT_SUCCESS) {
            *outFound = CNA_TRUE;
        }
        return result;
    });
}
