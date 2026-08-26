// SPDX-License-Identifier: MS-PL

#include "CNA/C/engine_layer.h"
#include "CnaCApiDetail.hpp"
#include "CnaCApiGraphicsDetail.hpp"
#include "CnaCApiRuntimeDetail.hpp"

#include "CNA/GraphicsImageAccess.hpp"
#include "CNA/GraphicsMemoryBarrier.hpp"

#include <cstring>
#include <memory>
#include <string>

#ifdef CNA_CNAEXT
#include "CNA/Graphics/ComputeShader.hpp"
#include "CNA/Graphics/DepthEncoding.hpp"
#include "CNA/Graphics/EngineLayerVersion.hpp"
#include "CNA/Graphics/StorageBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <type_traits>
#endif

namespace {

using CNA::C::Detail::CallWithExceptionBarrier;
using CNA::C::Detail::Fail;

// The engine layer is compiled out by default (CNA_CNAEXT is OFF at CMakeLists.txt:81), and this
// ABI keeps one shape regardless: every route below is declared and exported in both builds, and
// the ones that need a native engine-layer object answer this instead of vanishing. An export list
// that changed with a CMake option would make the recorded ABI baseline describe neither build.
[[nodiscard, maybe_unused]] CNA_Result ExtensionUnavailable()
{
    return Fail(
        CNA_RESULT_NOT_SUPPORTED,
        CNA_ERROR_CATEGORY_NOT_SUPPORTED,
        "This CNA build does not contain the extended graphics layer.");
}

template<typename TValue>
[[nodiscard]] CNA_Result StoreValue(TValue* const output, const TValue value) noexcept
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (output == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The output value is null.");
        }
        *output = value;
        return CNA_RESULT_SUCCESS;
    });
}

template<typename TCallable>
[[nodiscard, maybe_unused]] CNA_Result CopyFormattedString(
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes,
    TCallable&& callable) noexcept
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outBytes == nullptr || (destination == nullptr && capacity != 0U)) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The string destination or required-byte output is invalid.");
        }
        const std::string text = std::forward<TCallable>(callable)();
        *outBytes = static_cast<uint64_t>(text.size());
        if (capacity < text.size()) {
            return Fail(
                CNA_RESULT_BUFFER_TOO_SMALL,
                CNA_ERROR_CATEGORY_RANGE,
                "The destination cannot hold the complete text.");
        }
        if (!text.empty()) {
            std::memcpy(destination, text.data(), text.size());
        }
        return CNA_RESULT_SUCCESS;
    });
}

template<typename TEnum>
[[nodiscard]] constexpr uint32_t NativeOrdinal(const TEnum value) noexcept
{
    return static_cast<uint32_t>(static_cast<std::underlying_type_t<TEnum>>(value));
}

// The C constants are the contract; these prove they still name what the canonical enumerations
// name. GraphicsImageAccess and GraphicsMemoryBarrier are not behind CNA_CNAEXT -- they live in
// modules/graphics and exist in every build -- so their agreement is checked in every build too.
static_assert(
    NativeOrdinal(CNA::GraphicsImageAccess::ReadOnly) == CNA_GRAPHICS_IMAGE_ACCESS_READ_ONLY &&
    NativeOrdinal(CNA::GraphicsImageAccess::WriteOnly) == CNA_GRAPHICS_IMAGE_ACCESS_WRITE_ONLY &&
    NativeOrdinal(CNA::GraphicsImageAccess::ReadWrite) == CNA_GRAPHICS_IMAGE_ACCESS_READ_WRITE);

static_assert(
    NativeOrdinal(CNA::GraphicsMemoryBarrier::None) == CNA_GRAPHICS_MEMORY_BARRIER_NONE &&
    NativeOrdinal(CNA::GraphicsMemoryBarrier::VertexAttribArray) ==
        CNA_GRAPHICS_MEMORY_BARRIER_VERTEX_ATTRIB_ARRAY &&
    NativeOrdinal(CNA::GraphicsMemoryBarrier::ElementArray) ==
        CNA_GRAPHICS_MEMORY_BARRIER_ELEMENT_ARRAY &&
    NativeOrdinal(CNA::GraphicsMemoryBarrier::Uniform) == CNA_GRAPHICS_MEMORY_BARRIER_UNIFORM &&
    NativeOrdinal(CNA::GraphicsMemoryBarrier::TextureFetch) ==
        CNA_GRAPHICS_MEMORY_BARRIER_TEXTURE_FETCH &&
    NativeOrdinal(CNA::GraphicsMemoryBarrier::ShaderImageAccess) ==
        CNA_GRAPHICS_MEMORY_BARRIER_SHADER_IMAGE_ACCESS &&
    NativeOrdinal(CNA::GraphicsMemoryBarrier::ShaderStorage) ==
        CNA_GRAPHICS_MEMORY_BARRIER_SHADER_STORAGE &&
    NativeOrdinal(CNA::GraphicsMemoryBarrier::BufferUpdate) ==
        CNA_GRAPHICS_MEMORY_BARRIER_BUFFER_UPDATE &&
    NativeOrdinal(CNA::GraphicsMemoryBarrier::Framebuffer) ==
        CNA_GRAPHICS_MEMORY_BARRIER_FRAMEBUFFER &&
    NativeOrdinal(CNA::GraphicsMemoryBarrier::IndirectCommand) ==
        CNA_GRAPHICS_MEMORY_BARRIER_INDIRECT_COMMAND &&
    NativeOrdinal(CNA::GraphicsMemoryBarrier::All) == CNA_GRAPHICS_MEMORY_BARRIER_ALL);

#ifdef CNA_CNAEXT

using CNA::C::Detail::AddOwnedGraphicsResourceFor;
using CNA::C::Detail::BorrowedGraphicsDevice;
using CNA::C::Detail::CopyStringView;
using CNA::C::Detail::ErrorCategoryForResult;
using CNA::C::Detail::GetBorrowedGraphicsDevice;
using CNA::C::Detail::GetOwnedTexture2D;
using CNA::C::Detail::GetRuntimeHandles;
using CNA::C::Detail::ObjectKind;
using CNA::C::Detail::RemoveOwnedGraphicsResourceFor;
using CNA::C::Detail::Texture2DResource;

namespace Ext = CNA::Graphics;

static_assert(
    NativeOrdinal(Ext::DepthEncoding::Automatic) == CNA_DEPTH_ENCODING_AUTOMATIC &&
    NativeOrdinal(Ext::DepthEncoding::Packed) == CNA_DEPTH_ENCODING_PACKED &&
    NativeOrdinal(Ext::DepthEncoding::HalfFloat) == CNA_DEPTH_ENCODING_HALF_FLOAT);

static_assert(
    CNA_ENGINE_LAYER_VERSION == CNA_CNAEXT_ENGINE_VERSION,
    "the C header's engine-layer revision must equal the canonical macro it mirrors");

// elementCount and elementByteSize are what StorageBufferT<T> carries in its type. C has no
// templates, so the buffer carries them as values instead -- which is what lets the element routes
// enforce the same "more elements than the buffer holds" refusal the template's setData enforces.
// Both are zero for a buffer created by byte size, and the element routes refuse such a buffer.
struct StorageBufferResource final {
    std::shared_ptr<Ext::StorageBuffer> value;
    CNA_Handle parentGame;
    uint64_t elementCount;
    uint64_t elementByteSize;
};

struct ComputeShaderResource final {
    std::shared_ptr<Ext::ComputeShader> value;
    CNA_Handle parentGame;
};

[[nodiscard]] CNA_Result GetStorageBuffer(
    const CNA_Handle handle,
    std::shared_ptr<StorageBufferResource>* const outBuffer)
{
    const CNA_Result result =
        GetRuntimeHandles().Get(handle, ObjectKind::StorageBuffer, outBuffer);
    if (result == CNA_RESULT_SUCCESS) {
        return CNA_RESULT_SUCCESS;
    }
    return Fail(
        result,
        ErrorCategoryForResult(result),
        "The StorageBuffer handle is invalid for this call.");
}

[[nodiscard]] CNA_Result GetComputeShader(
    const CNA_Handle handle,
    std::shared_ptr<ComputeShaderResource>* const outShader)
{
    const CNA_Result result =
        GetRuntimeHandles().Get(handle, ObjectKind::ComputeShader, outShader);
    if (result == CNA_RESULT_SUCCESS) {
        return CNA_RESULT_SUCCESS;
    }
    return Fail(
        result,
        ErrorCategoryForResult(result),
        "The ComputeShader handle is invalid for this call.");
}

// The element routes exist to reproduce StorageBufferT<T>, so they refuse a buffer that was never
// given an element shape rather than guessing one. A guess here would silently reinterpret bytes.
[[nodiscard]] CNA_Result RequireElementShape(
    const StorageBufferResource& buffer,
    const uint64_t elementByteSize)
{
    if (buffer.elementByteSize == 0U) {
        return Fail(
            CNA_RESULT_INVALID_ARGUMENT,
            CNA_ERROR_CATEGORY_ARGUMENT,
            "This storage buffer was created by byte size and has no element shape; use the byte "
            "routes.");
    }
    if (elementByteSize != buffer.elementByteSize) {
        return Fail(
            CNA_RESULT_INVALID_ARGUMENT,
            CNA_ERROR_CATEGORY_ARGUMENT,
            "The element size does not match the size this storage buffer was created with.");
    }
    return CNA_RESULT_SUCCESS;
}

#endif // CNA_CNAEXT

} // namespace

CNA_Result cna_engine_layer_get_version(int32_t* const outVersion)
{
#ifdef CNA_CNAEXT
    return StoreValue(outVersion, static_cast<int32_t>(CNA::Graphics::getEngineLayerVersion()));
#else
    return StoreValue(outVersion, INT32_C(0));
#endif
}

CNA_Result cna_engine_layer_copy_version_string(
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
#ifdef CNA_CNAEXT
    return CopyFormattedString(destination, capacity, outBytes, [] {
        return CNA::Graphics::getEngineLayerVersionString();
    });
#else
    (void)destination;
    (void)capacity;
    if (outBytes != nullptr) {
        *outBytes = UINT64_C(0);
    }
    return ExtensionUnavailable();
#endif
}

CNA_Result cna_graphics_memory_barrier_has(
    const CNA_GraphicsMemoryBarrier mask,
    const CNA_GraphicsMemoryBarrier bit,
    CNA_Bool* const outContains)
{
    return StoreValue(outContains, static_cast<CNA_Bool>((mask & bit) == bit ? CNA_TRUE : CNA_FALSE));
}

#ifndef CNA_CNAEXT

CNA_Result cna_storage_buffer_create(
    const CNA_Handle graphicsDevice,
    const uint64_t byteSize,
    CNA_StorageBufferHandle* const outBuffer)
{
    (void)graphicsDevice;
    (void)byteSize;
    if (outBuffer != nullptr) {
        *outBuffer = CNA_INVALID_HANDLE;
    }
    return ExtensionUnavailable();
}

CNA_Result cna_storage_buffer_create_typed(
    const CNA_Handle graphicsDevice,
    const uint64_t elementCount,
    const uint64_t elementByteSize,
    CNA_StorageBufferHandle* const outBuffer)
{
    (void)graphicsDevice;
    (void)elementCount;
    (void)elementByteSize;
    if (outBuffer != nullptr) {
        *outBuffer = CNA_INVALID_HANDLE;
    }
    return ExtensionUnavailable();
}

CNA_Result cna_storage_buffer_set_bytes(
    const CNA_StorageBufferHandle buffer,
    const void* const data,
    const uint64_t byteSize)
{
    (void)buffer;
    (void)data;
    (void)byteSize;
    return ExtensionUnavailable();
}

CNA_Result cna_storage_buffer_get_bytes(
    const CNA_StorageBufferHandle buffer,
    void* const destination,
    const uint64_t byteSize)
{
    (void)buffer;
    (void)destination;
    (void)byteSize;
    return ExtensionUnavailable();
}

CNA_Result cna_storage_buffer_get_byte_size(
    const CNA_StorageBufferHandle buffer,
    uint64_t* const outByteSize)
{
    (void)buffer;
    (void)outByteSize;
    return ExtensionUnavailable();
}

CNA_Result cna_storage_buffer_set_elements(
    const CNA_StorageBufferHandle buffer,
    const void* const data,
    const uint64_t elementCount,
    const uint64_t elementByteSize)
{
    (void)buffer;
    (void)data;
    (void)elementCount;
    (void)elementByteSize;
    return ExtensionUnavailable();
}

CNA_Result cna_storage_buffer_get_elements(
    const CNA_StorageBufferHandle buffer,
    void* const destination,
    const uint64_t elementCount,
    const uint64_t elementByteSize)
{
    (void)buffer;
    (void)destination;
    (void)elementCount;
    (void)elementByteSize;
    return ExtensionUnavailable();
}

CNA_Result cna_storage_buffer_get_element_count(
    const CNA_StorageBufferHandle buffer,
    uint64_t* const outElementCount)
{
    (void)buffer;
    (void)outElementCount;
    return ExtensionUnavailable();
}

CNA_Result cna_storage_buffer_get_element_byte_size(
    const CNA_StorageBufferHandle buffer,
    uint64_t* const outElementByteSize)
{
    (void)buffer;
    (void)outElementByteSize;
    return ExtensionUnavailable();
}

CNA_Result cna_storage_buffer_destroy(const CNA_StorageBufferHandle buffer)
{
    (void)buffer;
    return ExtensionUnavailable();
}

CNA_Result cna_compute_shader_create(
    const CNA_Handle graphicsDevice,
    const CNA_StringView source,
    CNA_ComputeShaderHandle* const outShader)
{
    (void)graphicsDevice;
    (void)source;
    if (outShader != nullptr) {
        *outShader = CNA_INVALID_HANDLE;
    }
    return ExtensionUnavailable();
}

CNA_Result cna_compute_shader_set_uniform_int(
    const CNA_ComputeShaderHandle shader,
    const CNA_StringView name,
    const int32_t value)
{
    (void)shader;
    (void)name;
    (void)value;
    return ExtensionUnavailable();
}

CNA_Result cna_compute_shader_set_uniform_float(
    const CNA_ComputeShaderHandle shader,
    const CNA_StringView name,
    const float value)
{
    (void)shader;
    (void)name;
    (void)value;
    return ExtensionUnavailable();
}

CNA_Result cna_compute_shader_bind_storage_buffer(
    const CNA_ComputeShaderHandle shader,
    const int32_t binding,
    const CNA_StorageBufferHandle buffer)
{
    (void)shader;
    (void)binding;
    (void)buffer;
    return ExtensionUnavailable();
}

CNA_Result cna_compute_shader_bind_texture(
    const CNA_ComputeShaderHandle shader,
    const int32_t unit,
    const CNA_StringView samplerName,
    const CNA_Handle texture)
{
    (void)shader;
    (void)unit;
    (void)samplerName;
    (void)texture;
    return ExtensionUnavailable();
}

CNA_Result cna_compute_shader_is_image_binding_supported(
    const CNA_ComputeShaderHandle shader,
    CNA_Bool* const outSupported)
{
    (void)shader;
    (void)outSupported;
    return ExtensionUnavailable();
}

CNA_Result cna_compute_shader_bind_image(
    const CNA_ComputeShaderHandle shader,
    const int32_t unit,
    const CNA_Handle texture,
    const CNA_GraphicsImageAccess access)
{
    (void)shader;
    (void)unit;
    (void)texture;
    (void)access;
    return ExtensionUnavailable();
}

CNA_Result cna_compute_shader_dispatch(
    const CNA_ComputeShaderHandle shader,
    const int32_t groupsX,
    const int32_t groupsY,
    const int32_t groupsZ)
{
    (void)shader;
    (void)groupsX;
    (void)groupsY;
    (void)groupsZ;
    return ExtensionUnavailable();
}

CNA_Result cna_compute_shader_barrier(
    const CNA_ComputeShaderHandle shader,
    const CNA_GraphicsMemoryBarrier bits)
{
    (void)shader;
    (void)bits;
    return ExtensionUnavailable();
}

CNA_Result cna_compute_shader_is_valid(
    const CNA_ComputeShaderHandle shader,
    CNA_Bool* const outValid)
{
    (void)shader;
    (void)outValid;
    return ExtensionUnavailable();
}

CNA_Result cna_compute_shader_copy_compile_error(
    const CNA_ComputeShaderHandle shader,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    (void)shader;
    (void)destination;
    (void)capacity;
    if (outBytes != nullptr) {
        *outBytes = UINT64_C(0);
    }
    return ExtensionUnavailable();
}

CNA_Result cna_compute_shader_destroy(const CNA_ComputeShaderHandle shader)
{
    (void)shader;
    return ExtensionUnavailable();
}

#else // CNA_CNAEXT

namespace {

[[nodiscard]] CNA_Result CreateStorageBufferHandle(
    const CNA_Handle graphicsDeviceHandle,
    const uint64_t byteSize,
    const uint64_t elementCount,
    const uint64_t elementByteSize,
    CNA_StorageBufferHandle* const outBuffer)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outBuffer == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The storage-buffer output handle is null.");
        }
        *outBuffer = CNA_INVALID_HANDLE;
        if (byteSize > static_cast<uint64_t>(SIZE_MAX)) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_RANGE,
                "The storage-buffer size does not fit this platform's size type.");
        }
        std::shared_ptr<BorrowedGraphicsDevice> graphicsDevice;
        if (const CNA_Result result =
                GetBorrowedGraphicsDevice(graphicsDeviceHandle, &graphicsDevice);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }

        auto native = std::make_shared<Ext::StorageBuffer>(
            *graphicsDevice->value, static_cast<std::size_t>(byteSize));
        const auto resource = std::make_shared<StorageBufferResource>(StorageBufferResource{
            std::move(native), graphicsDevice->parentGame, elementCount, elementByteSize});
        const CNA_Result result =
            GetRuntimeHandles().Create(ObjectKind::StorageBuffer, resource, outBuffer);
        if (result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The owned storage-buffer handle could not be created.");
        }
        AddOwnedGraphicsResourceFor(graphicsDevice->parentGame);
        return CNA_RESULT_SUCCESS;
    });
}

} // namespace

CNA_Result cna_storage_buffer_create(
    const CNA_Handle graphicsDeviceHandle,
    const uint64_t byteSize,
    CNA_StorageBufferHandle* const outBuffer)
{
    return CreateStorageBufferHandle(
        graphicsDeviceHandle, byteSize, UINT64_C(0), UINT64_C(0), outBuffer);
}

CNA_Result cna_storage_buffer_create_typed(
    const CNA_Handle graphicsDeviceHandle,
    const uint64_t elementCount,
    const uint64_t elementByteSize,
    CNA_StorageBufferHandle* const outBuffer)
{
    if (outBuffer != nullptr) {
        *outBuffer = CNA_INVALID_HANDLE;
    }
    if (elementByteSize == 0U) {
        return Fail(
            CNA_RESULT_INVALID_ARGUMENT,
            CNA_ERROR_CATEGORY_ARGUMENT,
            "A typed storage buffer needs a non-zero element size.");
    }
    // The canonical template computes elementCount * sizeof(T); a C caller supplies both, so the
    // overflow the template cannot have is one this route must refuse rather than wrap.
    if (elementCount != 0U && elementByteSize > UINT64_MAX / elementCount) {
        return Fail(
            CNA_RESULT_INVALID_ARGUMENT,
            CNA_ERROR_CATEGORY_RANGE,
            "The element count times the element size overflows.");
    }
    return CreateStorageBufferHandle(
        graphicsDeviceHandle,
        elementCount * elementByteSize,
        elementCount,
        elementByteSize,
        outBuffer);
}

CNA_Result cna_storage_buffer_set_bytes(
    const CNA_StorageBufferHandle bufferHandle,
    const void* const data,
    const uint64_t byteSize)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (data == nullptr && byteSize != 0U) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The storage-buffer source is null.");
        }
        std::shared_ptr<StorageBufferResource> buffer;
        if (const CNA_Result result = GetStorageBuffer(bufferHandle, &buffer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (byteSize > static_cast<uint64_t>(buffer->value->getByteSize())) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_RANGE,
                "More bytes than the storage buffer holds.");
        }
        buffer->value->setBytes(data, static_cast<std::size_t>(byteSize));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_storage_buffer_get_bytes(
    const CNA_StorageBufferHandle bufferHandle,
    void* const destination,
    const uint64_t byteSize)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (destination == nullptr && byteSize != 0U) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The storage-buffer destination is null.");
        }
        std::shared_ptr<StorageBufferResource> buffer;
        if (const CNA_Result result = GetStorageBuffer(bufferHandle, &buffer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (byteSize > static_cast<uint64_t>(buffer->value->getByteSize())) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_RANGE,
                "More bytes than the storage buffer holds.");
        }
        buffer->value->getBytes(destination, static_cast<std::size_t>(byteSize));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_storage_buffer_get_byte_size(
    const CNA_StorageBufferHandle bufferHandle,
    uint64_t* const outByteSize)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outByteSize == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The storage-buffer size output is null.");
        }
        std::shared_ptr<StorageBufferResource> buffer;
        if (const CNA_Result result = GetStorageBuffer(bufferHandle, &buffer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outByteSize = static_cast<uint64_t>(buffer->value->getByteSize());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_storage_buffer_set_elements(
    const CNA_StorageBufferHandle bufferHandle,
    const void* const data,
    const uint64_t elementCount,
    const uint64_t elementByteSize)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (data == nullptr && elementCount != 0U) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The storage-buffer source is null.");
        }
        std::shared_ptr<StorageBufferResource> buffer;
        if (const CNA_Result result = GetStorageBuffer(bufferHandle, &buffer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (const CNA_Result result = RequireElementShape(*buffer, elementByteSize);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        // StorageBufferT<T>::setData throws std::invalid_argument here; at this boundary the same
        // refusal is a result rather than an exception, which is what every other route does too.
        if (elementCount > buffer->elementCount) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_RANGE,
                "More elements than the storage buffer holds.");
        }
        buffer->value->setBytes(data, static_cast<std::size_t>(elementCount * elementByteSize));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_storage_buffer_get_elements(
    const CNA_StorageBufferHandle bufferHandle,
    void* const destination,
    const uint64_t elementCount,
    const uint64_t elementByteSize)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (destination == nullptr && elementCount != 0U) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The storage-buffer destination is null.");
        }
        std::shared_ptr<StorageBufferResource> buffer;
        if (const CNA_Result result = GetStorageBuffer(bufferHandle, &buffer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (const CNA_Result result = RequireElementShape(*buffer, elementByteSize);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        // getData() returns the buffer's whole element range, so the C form takes exactly that
        // count rather than an arbitrary prefix; a shorter read is the byte route's job.
        if (elementCount != buffer->elementCount) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_RANGE,
                "The element count must equal the storage buffer's own element count.");
        }
        buffer->value->getBytes(destination, static_cast<std::size_t>(elementCount * elementByteSize));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_storage_buffer_get_element_count(
    const CNA_StorageBufferHandle bufferHandle,
    uint64_t* const outElementCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outElementCount == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The element-count output is null.");
        }
        std::shared_ptr<StorageBufferResource> buffer;
        if (const CNA_Result result = GetStorageBuffer(bufferHandle, &buffer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outElementCount = buffer->elementCount;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_storage_buffer_get_element_byte_size(
    const CNA_StorageBufferHandle bufferHandle,
    uint64_t* const outElementByteSize)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outElementByteSize == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The element-size output is null.");
        }
        std::shared_ptr<StorageBufferResource> buffer;
        if (const CNA_Result result = GetStorageBuffer(bufferHandle, &buffer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outElementByteSize = buffer->elementByteSize;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_storage_buffer_destroy(const CNA_StorageBufferHandle bufferHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<StorageBufferResource> buffer;
        if (const CNA_Result result = GetStorageBuffer(bufferHandle, &buffer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const CNA_Result releaseResult = GetRuntimeHandles().Release(bufferHandle);
        if (releaseResult != CNA_RESULT_SUCCESS) {
            return Fail(
                releaseResult,
                ErrorCategoryForResult(releaseResult),
                "The owned storage-buffer handle could not be released.");
        }
        RemoveOwnedGraphicsResourceFor(buffer->parentGame);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_compute_shader_create(
    const CNA_Handle graphicsDeviceHandle,
    const CNA_StringView source,
    CNA_ComputeShaderHandle* const outShader)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outShader == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The compute-shader output handle is null.");
        }
        *outShader = CNA_INVALID_HANDLE;
        std::string nativeSource;
        if (const CNA_Result result = CopyStringView(source, false, &nativeSource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::shared_ptr<BorrowedGraphicsDevice> graphicsDevice;
        if (const CNA_Result result =
                GetBorrowedGraphicsDevice(graphicsDeviceHandle, &graphicsDevice);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }

        auto native =
            std::make_shared<Ext::ComputeShader>(*graphicsDevice->value, nativeSource);
        const auto resource = std::make_shared<ComputeShaderResource>(
            ComputeShaderResource{std::move(native), graphicsDevice->parentGame});
        const CNA_Result result =
            GetRuntimeHandles().Create(ObjectKind::ComputeShader, resource, outShader);
        if (result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The owned compute-shader handle could not be created.");
        }
        AddOwnedGraphicsResourceFor(graphicsDevice->parentGame);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_compute_shader_set_uniform_int(
    const CNA_ComputeShaderHandle shaderHandle,
    const CNA_StringView name,
    const int32_t value)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::string nativeName;
        if (const CNA_Result result = CopyStringView(name, true, &nativeName);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::shared_ptr<ComputeShaderResource> shader;
        if (const CNA_Result result = GetComputeShader(shaderHandle, &shader);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        shader->value->setUniform(nativeName, static_cast<int>(value));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_compute_shader_set_uniform_float(
    const CNA_ComputeShaderHandle shaderHandle,
    const CNA_StringView name,
    const float value)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::string nativeName;
        if (const CNA_Result result = CopyStringView(name, true, &nativeName);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::shared_ptr<ComputeShaderResource> shader;
        if (const CNA_Result result = GetComputeShader(shaderHandle, &shader);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        shader->value->setUniform(nativeName, value);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_compute_shader_bind_storage_buffer(
    const CNA_ComputeShaderHandle shaderHandle,
    const int32_t binding,
    const CNA_StorageBufferHandle bufferHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<ComputeShaderResource> shader;
        if (const CNA_Result result = GetComputeShader(shaderHandle, &shader);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::shared_ptr<StorageBufferResource> buffer;
        if (const CNA_Result result = GetStorageBuffer(bufferHandle, &buffer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        shader->value->bindStorageBuffer(static_cast<int>(binding), *buffer->value);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_compute_shader_bind_texture(
    const CNA_ComputeShaderHandle shaderHandle,
    const int32_t unit,
    const CNA_StringView samplerName,
    const CNA_Handle textureHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::string nativeName;
        if (const CNA_Result result = CopyStringView(samplerName, true, &nativeName);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::shared_ptr<ComputeShaderResource> shader;
        if (const CNA_Result result = GetComputeShader(shaderHandle, &shader);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::shared_ptr<Texture2DResource> texture;
        if (const CNA_Result result = GetOwnedTexture2D(textureHandle, &texture);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        shader->value->bindTexture(static_cast<int>(unit), nativeName, *texture->value);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_compute_shader_is_image_binding_supported(
    const CNA_ComputeShaderHandle shaderHandle,
    CNA_Bool* const outSupported)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outSupported == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The image-binding support output is null.");
        }
        std::shared_ptr<ComputeShaderResource> shader;
        if (const CNA_Result result = GetComputeShader(shaderHandle, &shader);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outSupported = shader->value->isImageBindingSupported() ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_compute_shader_bind_image(
    const CNA_ComputeShaderHandle shaderHandle,
    const int32_t unit,
    const CNA_Handle textureHandle,
    const CNA_GraphicsImageAccess access)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (access > CNA_GRAPHICS_IMAGE_ACCESS_READ_WRITE) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The image access is not a defined CNA_GRAPHICS_IMAGE_ACCESS_* value.");
        }
        std::shared_ptr<ComputeShaderResource> shader;
        if (const CNA_Result result = GetComputeShader(shaderHandle, &shader);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::shared_ptr<Texture2DResource> texture;
        if (const CNA_Result result = GetOwnedTexture2D(textureHandle, &texture);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        shader->value->bindImage(
            static_cast<int>(unit),
            *texture->value,
            static_cast<CNA::GraphicsImageAccess>(access));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_compute_shader_dispatch(
    const CNA_ComputeShaderHandle shaderHandle,
    const int32_t groupsX,
    const int32_t groupsY,
    const int32_t groupsZ)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<ComputeShaderResource> shader;
        if (const CNA_Result result = GetComputeShader(shaderHandle, &shader);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        shader->value->dispatch(
            static_cast<int>(groupsX), static_cast<int>(groupsY), static_cast<int>(groupsZ));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_compute_shader_barrier(
    const CNA_ComputeShaderHandle shaderHandle,
    const CNA_GraphicsMemoryBarrier bits)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if ((bits & ~CNA_GRAPHICS_MEMORY_BARRIER_ALL) != 0U) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The barrier mask contains a bit no CNA_GRAPHICS_MEMORY_BARRIER_* value defines.");
        }
        std::shared_ptr<ComputeShaderResource> shader;
        if (const CNA_Result result = GetComputeShader(shaderHandle, &shader);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        shader->value->barrier(static_cast<CNA::GraphicsMemoryBarrier>(bits));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_compute_shader_is_valid(
    const CNA_ComputeShaderHandle shaderHandle,
    CNA_Bool* const outValid)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outValid == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The validity output is null.");
        }
        std::shared_ptr<ComputeShaderResource> shader;
        if (const CNA_Result result = GetComputeShader(shaderHandle, &shader);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outValid = shader->value->isValid() ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_compute_shader_copy_compile_error(
    const CNA_ComputeShaderHandle shaderHandle,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    std::shared_ptr<ComputeShaderResource> shader;
    if (const CNA_Result result = GetComputeShader(shaderHandle, &shader);
        result != CNA_RESULT_SUCCESS) {
        if (outBytes != nullptr) {
            *outBytes = UINT64_C(0);
        }
        return result;
    }
    return CopyFormattedString(destination, capacity, outBytes, [&shader] {
        return shader->value->getCompileError();
    });
}

CNA_Result cna_compute_shader_destroy(const CNA_ComputeShaderHandle shaderHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<ComputeShaderResource> shader;
        if (const CNA_Result result = GetComputeShader(shaderHandle, &shader);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const CNA_Result releaseResult = GetRuntimeHandles().Release(shaderHandle);
        if (releaseResult != CNA_RESULT_SUCCESS) {
            return Fail(
                releaseResult,
                ErrorCategoryForResult(releaseResult),
                "The owned compute-shader handle could not be released.");
        }
        RemoveOwnedGraphicsResourceFor(shader->parentGame);
        return CNA_RESULT_SUCCESS;
    });
}

#endif // CNA_CNAEXT
