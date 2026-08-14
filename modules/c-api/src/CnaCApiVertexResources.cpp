// SPDX-License-Identifier: MS-PL

#include "CNA/C/vertex_resources.h"
#include "CnaCApiDetail.hpp"
#include "CnaCApiRuntimeDetail.hpp"

#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"

#include <algorithm>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

using CNA::C::Detail::CallWithExceptionBarrier;
using CNA::C::Detail::CheckedElementByteCount;
using CNA::C::Detail::ErrorCategoryForResult;
using CNA::C::Detail::Fail;
using CNA::C::Detail::GetRuntimeHandles;
using CNA::C::Detail::ObjectKind;
using Microsoft::Xna::Framework::Graphics::VertexDeclaration;
using Microsoft::Xna::Framework::Graphics::VertexElement;
using Microsoft::Xna::Framework::Graphics::VertexElementFormat;
using Microsoft::Xna::Framework::Graphics::VertexElementUsage;

static_assert(sizeof(int) == sizeof(int32_t));

[[nodiscard]] bool TryMapVertexElementFormat(
    const CNA_VertexElementFormat format,
    VertexElementFormat* const outFormat) noexcept
{
    if (outFormat == nullptr || format > CNA_VERTEX_ELEMENT_FORMAT_HALF_VECTOR4) {
        return false;
    }
    *outFormat = static_cast<VertexElementFormat>(format);
    return true;
}

[[nodiscard]] bool TryMapVertexElementUsage(
    const CNA_VertexElementUsage usage,
    VertexElementUsage* const outUsage) noexcept
{
    if (outUsage == nullptr || usage > CNA_VERTEX_ELEMENT_USAGE_TESSELLATE_FACTOR) {
        return false;
    }
    *outUsage = static_cast<VertexElementUsage>(usage);
    return true;
}

[[nodiscard]] int32_t FormatByteSize(const CNA_VertexElementFormat format) noexcept
{
    switch (format) {
        case CNA_VERTEX_ELEMENT_FORMAT_SINGLE:
        case CNA_VERTEX_ELEMENT_FORMAT_COLOR:
        case CNA_VERTEX_ELEMENT_FORMAT_BYTE4:
        case CNA_VERTEX_ELEMENT_FORMAT_SHORT2:
        case CNA_VERTEX_ELEMENT_FORMAT_NORMALIZED_SHORT2:
        case CNA_VERTEX_ELEMENT_FORMAT_HALF_VECTOR2:
            return 4;
        case CNA_VERTEX_ELEMENT_FORMAT_VECTOR2:
        case CNA_VERTEX_ELEMENT_FORMAT_SHORT4:
        case CNA_VERTEX_ELEMENT_FORMAT_NORMALIZED_SHORT4:
        case CNA_VERTEX_ELEMENT_FORMAT_HALF_VECTOR4:
            return 8;
        case CNA_VERTEX_ELEMENT_FORMAT_VECTOR3:
            return 12;
        default:
            return 16;
    }
}

[[nodiscard]] CNA_Result CopyNativeElements(
    const CNA_VertexElement* const elements,
    const uint64_t elementCount,
    const bool computeStride,
    std::vector<VertexElement>* const outElements,
    int32_t* const outComputedStride)
{
    if (outElements == nullptr || outComputedStride == nullptr || elementCount == 0U) {
        return Fail(
            CNA_RESULT_INVALID_ARGUMENT,
            CNA_ERROR_CATEGORY_ARGUMENT,
            "A vertex declaration requires at least one element.");
    }

    std::size_t byteCount = 0U;
    const CNA_Result byteResult = CheckedElementByteCount(
        elements, elementCount, sizeof(CNA_VertexElement), &byteCount);
    if (byteResult != CNA_RESULT_SUCCESS) {
        return Fail(
            byteResult,
            ErrorCategoryForResult(byteResult),
            "The vertex-element array is invalid or too large.");
    }
    static_cast<void>(byteCount);
    if (elementCount > outElements->max_size()) {
        return Fail(
            CNA_RESULT_OVERFLOW,
            CNA_ERROR_CATEGORY_RANGE,
            "The vertex-element count is too large.");
    }

    std::vector<VertexElement> converted;
    converted.reserve(static_cast<std::size_t>(elementCount));
    int32_t computedStride = 0;
    for (uint64_t index = 0U; index < elementCount; ++index) {
        const CNA_VertexElement element = elements[index];
        VertexElementFormat format{};
        VertexElementUsage usage{};
        if (!TryMapVertexElementFormat(element.format, &format) ||
            !TryMapVertexElementUsage(element.usage, &usage) ||
            element.offset < 0 || element.usage_index < 0) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "A vertex element contains an invalid identity, offset or usage index.");
        }
        if (computeStride) {
            const int32_t formatSize = FormatByteSize(element.format);
            if (element.offset > std::numeric_limits<int32_t>::max() - formatSize) {
                return Fail(
                    CNA_RESULT_OVERFLOW,
                    CNA_ERROR_CATEGORY_RANGE,
                    "The computed vertex stride overflows Int32.");
            }
            computedStride = std::max(computedStride, element.offset + formatSize);
        }
        converted.emplace_back(element.offset, format, usage, element.usage_index);
    }
    *outElements = std::move(converted);
    *outComputedStride = computedStride;
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_VertexElement ToC(const VertexElement& value) noexcept
{
    return CNA_VertexElement{
        value.getOffsetProperty(),
        static_cast<CNA_VertexElementFormat>(value.getVertexElementFormatProperty()),
        static_cast<CNA_VertexElementUsage>(value.getVertexElementUsageProperty()),
        value.getUsageIndexProperty()};
}

[[nodiscard]] CNA_Result GetDeclaration(
    const CNA_VertexDeclarationHandle handle,
    std::shared_ptr<VertexDeclaration>* const outDeclaration)
{
    const CNA_Result result = GetRuntimeHandles().Get(
        handle,
        ObjectKind::VertexDeclaration,
        outDeclaration);
    if (result == CNA_RESULT_SUCCESS) {
        return CNA_RESULT_SUCCESS;
    }
    return Fail(
        result,
        ErrorCategoryForResult(result),
        "The VertexDeclaration handle is invalid for this call.");
}

[[nodiscard]] CNA_Result RegisterDeclaration(
    std::shared_ptr<VertexDeclaration> declaration,
    CNA_VertexDeclarationHandle* const outDeclaration)
{
    const CNA_Result result = GetRuntimeHandles().Create(
        ObjectKind::VertexDeclaration,
        std::move(declaration),
        outDeclaration);
    if (result == CNA_RESULT_SUCCESS) {
        return CNA_RESULT_SUCCESS;
    }
    return Fail(
        result,
        ErrorCategoryForResult(result),
        "The VertexDeclaration handle could not be created.");
}

template<typename TCallable>
[[nodiscard]] CNA_Result CopyString(
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outByteCount,
    TCallable&& callable) noexcept
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outByteCount == nullptr || (destination == nullptr && capacity != 0U)) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The string output buffer is invalid.");
        }
        const std::string value = std::forward<TCallable>(callable)();
        *outByteCount = static_cast<uint64_t>(value.size());
        if (capacity < value.size()) {
            return Fail(
                CNA_RESULT_BUFFER_TOO_SMALL,
                CNA_ERROR_CATEGORY_RANGE,
                "The destination cannot hold the complete type name.");
        }
        if (!value.empty()) {
            std::memcpy(destination, value.data(), value.size());
        }
        return CNA_RESULT_SUCCESS;
    });
}

} // namespace

CNA_Result cna_vertex_declaration_create_empty(
    CNA_VertexDeclarationHandle* const outDeclaration)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outDeclaration == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The VertexDeclaration output handle is null.");
        }
        *outDeclaration = CNA_INVALID_HANDLE;
        return RegisterDeclaration(std::make_shared<VertexDeclaration>(), outDeclaration);
    });
}

CNA_Result cna_vertex_declaration_create(
    const CNA_VertexElement* const elements,
    const uint64_t elementCount,
    CNA_VertexDeclarationHandle* const outDeclaration)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outDeclaration == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The VertexDeclaration output handle is null.");
        }
        *outDeclaration = CNA_INVALID_HANDLE;
        std::vector<VertexElement> nativeElements;
        int32_t computedStride = 0;
        if (const CNA_Result result = CopyNativeElements(
                elements, elementCount, true, &nativeElements, &computedStride);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return RegisterDeclaration(
            std::make_shared<VertexDeclaration>(
                computedStride, std::move(nativeElements)),
            outDeclaration);
    });
}

CNA_Result cna_vertex_declaration_create_with_stride(
    const int32_t vertexStride,
    const CNA_VertexElement* const elements,
    const uint64_t elementCount,
    CNA_VertexDeclarationHandle* const outDeclaration)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outDeclaration == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The VertexDeclaration output handle is null.");
        }
        *outDeclaration = CNA_INVALID_HANDLE;
        if (vertexStride <= 0) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_RANGE,
                "The explicit vertex stride must be positive.");
        }
        std::vector<VertexElement> nativeElements;
        int32_t ignoredStride = 0;
        if (const CNA_Result result = CopyNativeElements(
                elements, elementCount, false, &nativeElements, &ignoredStride);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return RegisterDeclaration(
            std::make_shared<VertexDeclaration>(
                vertexStride, std::move(nativeElements)),
            outDeclaration);
    });
}

CNA_Result cna_vertex_declaration_destroy(
    const CNA_VertexDeclarationHandle declarationHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<VertexDeclaration> declaration;
        if (const CNA_Result result = GetDeclaration(declarationHandle, &declaration);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const CNA_Result result = GetRuntimeHandles().Release(declarationHandle);
        if (result == CNA_RESULT_SUCCESS) {
            return CNA_RESULT_SUCCESS;
        }
        return Fail(
            result,
            ErrorCategoryForResult(result),
            "The VertexDeclaration handle could not be destroyed.");
    });
}

CNA_Result cna_vertex_declaration_get_stride(
    const CNA_VertexDeclarationHandle declarationHandle,
    int32_t* const outVertexStride)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outVertexStride == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The vertex-stride output is null.");
        }
        std::shared_ptr<VertexDeclaration> declaration;
        if (const CNA_Result result = GetDeclaration(declarationHandle, &declaration);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const int32_t stride = declaration->getVertexStrideProperty();
        *outVertexStride = stride;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_vertex_declaration_copy_elements(
    const CNA_VertexDeclarationHandle declarationHandle,
    CNA_VertexElement* const destination,
    const uint64_t capacity,
    uint64_t* const outElementCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outElementCount == nullptr || (destination == nullptr && capacity != 0U)) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The vertex-element output buffer is invalid.");
        }
        std::shared_ptr<VertexDeclaration> declaration;
        if (const CNA_Result result = GetDeclaration(declarationHandle, &declaration);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const auto& elements = declaration->GetVertexElements();
        const uint64_t required = static_cast<uint64_t>(elements.size());
        *outElementCount = required;
        if (capacity < required) {
            return Fail(
                CNA_RESULT_BUFFER_TOO_SMALL,
                CNA_ERROR_CATEGORY_RANGE,
                "The vertex-element output buffer is too small.");
        }
        for (uint64_t index = 0U; index < required; ++index) {
            destination[index] = ToC(elements[static_cast<std::size_t>(index)]);
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_vertex_declaration_get_type_name_byte_count(
    const CNA_VertexDeclarationHandle declarationHandle,
    uint64_t* const outByteCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outByteCount == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The type-name byte-count output is null.");
        }
        std::shared_ptr<VertexDeclaration> declaration;
        if (const CNA_Result result = GetDeclaration(declarationHandle, &declaration);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const uint64_t count = declaration->GetTypeName().size();
        *outByteCount = count;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_vertex_declaration_copy_type_name(
    const CNA_VertexDeclarationHandle declarationHandle,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outByteCount)
{
    std::shared_ptr<VertexDeclaration> declaration;
    if (const CNA_Result result = GetDeclaration(declarationHandle, &declaration);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    return CopyString(destination, capacity, outByteCount, [&] {
        return declaration->GetTypeName();
    });
}

CNA_Result cna_vertex_buffer_binding_init(
    const CNA_VertexBufferHandle vertexBuffer,
    const int32_t vertexOffset,
    const int32_t instanceFrequency,
    CNA_VertexBufferBinding* const outBinding)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outBinding == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The vertex-buffer binding output is null.");
        }
        if (vertexBuffer == CNA_INVALID_HANDLE) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "A non-default vertex-buffer binding requires a nonzero handle token.");
        }
        if (vertexOffset < 0 || instanceFrequency < 0) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_RANGE,
                "The vertex offset and instance frequency must be nonnegative.");
        }
        const CNA_VertexBufferBinding binding{
            vertexBuffer, vertexOffset, instanceFrequency};
        *outBinding = binding;
        return CNA_RESULT_SUCCESS;
    });
}
