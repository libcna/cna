// SPDX-License-Identifier: MS-PL

#include "CNA/C/graphics_resource.h"
#include "CnaCApiDetail.hpp"
#include "CnaCApiGraphicsDetail.hpp"
#include "CnaCApiRuntimeDetail.hpp"

#include "Microsoft/Xna/Framework/Graphics/GraphicsResource.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture3D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "System/EventArgs.hpp"

#include <cstring>
#include <memory>
#include <string>
#include <utility>

namespace {

using CNA::C::Detail::BorrowGameGraphicsDevice;
using CNA::C::Detail::BorrowedGraphicsDevice;
using CNA::C::Detail::CallWithExceptionBarrier;
using CNA::C::Detail::CopyStringView;
using CNA::C::Detail::ErrorCategoryForResult;
using CNA::C::Detail::Fail;
using CNA::C::Detail::GetOwnedTexture2D;
using CNA::C::Detail::GetRuntimeHandles;
using CNA::C::Detail::IndexBufferResource;
using CNA::C::Detail::ObjectKind;
using CNA::C::Detail::RenderTargetCubeResource;
using CNA::C::Detail::Texture2DResource;
using CNA::C::Detail::Texture3DResource;
using CNA::C::Detail::TextureCubeResource;
using CNA::C::Detail::VertexBufferResource;
using Microsoft::Xna::Framework::Graphics::GraphicsResource;
using Microsoft::Xna::Framework::Graphics::IndexBuffer;
using Microsoft::Xna::Framework::Graphics::RenderTargetCube;
using Microsoft::Xna::Framework::Graphics::Texture2D;
using Microsoft::Xna::Framework::Graphics::Texture3D;
using Microsoft::Xna::Framework::Graphics::TextureCube;
using Microsoft::Xna::Framework::Graphics::VertexDeclaration;
using Microsoft::Xna::Framework::Graphics::VertexBuffer;

struct GraphicsResourceView final {
    std::shared_ptr<GraphicsResource> value;
    CNA_Handle parentGame = CNA_INVALID_HANDLE;
    std::shared_ptr<Texture2DResource> texture;
    uint64_t* activeEffectReferenceCount = nullptr;
};

class DisposingRegistration final {
public:
    using Token = System::EventHandler<System::EventArgs>::Token;

    DisposingRegistration(std::weak_ptr<GraphicsResource> resource, const Token token)
        : resource_(std::move(resource)), token_(token)
    {
    }

    ~DisposingRegistration()
    {
        Unsubscribe();
    }

    void Unsubscribe()
    {
        if (!subscribed_) {
            return;
        }
        subscribed_ = false;
        if (const std::shared_ptr<GraphicsResource> resource = resource_.lock()) {
            resource->Disposing.Remove(token_);
        }
    }

private:
    std::weak_ptr<GraphicsResource> resource_;
    Token token_;
    bool subscribed_ = true;
};

[[nodiscard]] CNA_Result InvalidResource(const CNA_Result result)
{
    return Fail(
        result,
        ErrorCategoryForResult(result),
        "The handle does not refer to a supported graphics resource for this call.");
}

[[nodiscard]] CNA_Result ResolveGraphicsResource(
    const CNA_Handle handle,
    GraphicsResourceView* const outResource)
{
    if (outResource == nullptr) {
        return Fail(
            CNA_RESULT_INVALID_ARGUMENT,
            CNA_ERROR_CATEGORY_ARGUMENT,
            "The internal graphics-resource output is null.");
    }
    ObjectKind kind = ObjectKind::Unknown;
    if (const CNA_Result result = GetRuntimeHandles().GetKind(handle, &kind);
        result != CNA_RESULT_SUCCESS) {
        return InvalidResource(result);
    }

    GraphicsResourceView result;
    if (kind == ObjectKind::Texture2D || kind == ObjectKind::RenderTarget2D) {
        std::shared_ptr<Texture2DResource> texture;
        if (const CNA_Result getResult = GetOwnedTexture2D(handle, &texture);
            getResult != CNA_RESULT_SUCCESS) {
            return getResult;
        }
        result.value = std::static_pointer_cast<GraphicsResource>(texture->value);
        result.parentGame = texture->parentGame;
        result.activeEffectReferenceCount = &texture->activeEffectReferenceCount;
        result.texture = std::move(texture);
    } else if (kind == ObjectKind::Texture3D) {
        std::shared_ptr<Texture3DResource> texture;
        const CNA_Result getResult = GetRuntimeHandles().Get(
            handle, ObjectKind::Texture3D, &texture);
        if (getResult != CNA_RESULT_SUCCESS) {
            return InvalidResource(getResult);
        }
        result.value = std::static_pointer_cast<GraphicsResource>(texture->value);
        result.parentGame = texture->parentGame;
        result.activeEffectReferenceCount = &texture->activeEffectReferenceCount;
    } else if (kind == ObjectKind::TextureCube) {
        std::shared_ptr<TextureCubeResource> texture;
        const CNA_Result getResult = GetRuntimeHandles().Get(
            handle, ObjectKind::TextureCube, &texture);
        if (getResult != CNA_RESULT_SUCCESS) {
            return InvalidResource(getResult);
        }
        result.value = std::static_pointer_cast<GraphicsResource>(texture->value);
        result.parentGame = texture->parentGame;
        result.activeEffectReferenceCount = &texture->activeEffectReferenceCount;
    } else if (kind == ObjectKind::RenderTargetCube) {
        std::shared_ptr<RenderTargetCubeResource> target;
        const CNA_Result getResult = GetRuntimeHandles().Get(
            handle, ObjectKind::RenderTargetCube, &target);
        if (getResult != CNA_RESULT_SUCCESS) {
            return InvalidResource(getResult);
        }
        result.value = std::static_pointer_cast<GraphicsResource>(target->value);
        result.parentGame = target->parentGame;
        result.activeEffectReferenceCount = &target->activeEffectReferenceCount;
    } else if (kind == ObjectKind::VertexDeclaration) {
        std::shared_ptr<VertexDeclaration> declaration;
        const CNA_Result getResult = GetRuntimeHandles().Get(
            handle, ObjectKind::VertexDeclaration, &declaration);
        if (getResult != CNA_RESULT_SUCCESS) {
            return InvalidResource(getResult);
        }
        result.value = std::static_pointer_cast<GraphicsResource>(declaration);
    } else if (kind == ObjectKind::VertexBuffer) {
        std::shared_ptr<VertexBufferResource> buffer;
        const CNA_Result getResult = GetRuntimeHandles().Get(
            handle, ObjectKind::VertexBuffer, &buffer);
        if (getResult != CNA_RESULT_SUCCESS) {
            return InvalidResource(getResult);
        }
        result.value = std::static_pointer_cast<GraphicsResource>(buffer->value);
        result.parentGame = buffer->parentGame;
    } else if (kind == ObjectKind::IndexBuffer) {
        std::shared_ptr<IndexBufferResource> buffer;
        const CNA_Result getResult = GetRuntimeHandles().Get(
            handle, ObjectKind::IndexBuffer, &buffer);
        if (getResult != CNA_RESULT_SUCCESS) {
            return InvalidResource(getResult);
        }
        result.value = std::static_pointer_cast<GraphicsResource>(buffer->value);
        result.parentGame = buffer->parentGame;
    } else {
        return InvalidResource(CNA_RESULT_INVALID_HANDLE);
    }
    *outResource = std::move(result);
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result EnsureDisposable(const GraphicsResourceView& resource)
{
    if ((resource.texture != nullptr &&
         (resource.texture->activeBatchReferenceCount != 0U ||
          resource.texture->activeFontReferenceCount != 0U)) ||
        (resource.activeEffectReferenceCount != nullptr &&
         *resource.activeEffectReferenceCount != 0U)) {
        return Fail(
            CNA_RESULT_INVALID_STATE,
            CNA_ERROR_CATEGORY_STATE,
            "The texture is retained by an active SpriteBatch, SpriteFont or EffectParameter.");
    }
    return CNA_RESULT_SUCCESS;
}

template<typename TCallable>
[[nodiscard]] CNA_Result CopyResourceString(
    const CNA_Handle handle,
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
                "The graphics-resource string output buffer is invalid.");
        }
        GraphicsResourceView resource;
        if (const CNA_Result result = ResolveGraphicsResource(handle, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const std::string text = std::forward<TCallable>(callable)(*resource.value);
        *outByteCount = static_cast<uint64_t>(text.size());
        if (capacity < text.size()) {
            return Fail(
                CNA_RESULT_BUFFER_TOO_SMALL,
                CNA_ERROR_CATEGORY_RANGE,
                "The destination cannot hold the complete graphics-resource string.");
        }
        if (!text.empty()) {
            std::memcpy(destination, text.data(), text.size());
        }
        return CNA_RESULT_SUCCESS;
    });
}

} // namespace

CNA_Result cna_graphics_resource_get_graphics_device(
    const CNA_Handle resourceHandle,
    CNA_Handle* const outGraphicsDevice)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outGraphicsDevice == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The graphics-device output handle is null.");
        }
        *outGraphicsDevice = CNA_INVALID_HANDLE;
        GraphicsResourceView resource;
        if (const CNA_Result result = ResolveGraphicsResource(resourceHandle, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (resource.value->getGraphicsDeviceProperty() == nullptr) {
            return CNA_RESULT_SUCCESS;
        }
        if (resource.parentGame == CNA_INVALID_HANDLE) {
            return Fail(
                CNA_RESULT_NOT_SUPPORTED,
                CNA_ERROR_CATEGORY_NOT_SUPPORTED,
                "The resource's graphics-device owner cannot be represented by a C handle.");
        }
        CNA_Handle borrowedHandle = CNA_INVALID_HANDLE;
        if (const CNA_Result result = BorrowGameGraphicsDevice(
                resource.parentGame, &borrowedHandle);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::shared_ptr<BorrowedGraphicsDevice> borrowed;
        const CNA_Result lookupResult = GetRuntimeHandles().Get(
            borrowedHandle, ObjectKind::GraphicsDevice, &borrowed);
        if (lookupResult != CNA_RESULT_SUCCESS ||
            borrowed->value != resource.value->getGraphicsDeviceProperty()) {
            return Fail(
                CNA_RESULT_INTERNAL,
                CNA_ERROR_CATEGORY_INTERNAL,
                "The resource and borrowed graphics-device owners disagree.");
        }
        *outGraphicsDevice = borrowedHandle;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_graphics_resource_get_is_disposed(
    const CNA_Handle resourceHandle,
    CNA_Bool* const outIsDisposed)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outIsDisposed == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The disposal-state output is null.");
        }
        GraphicsResourceView resource;
        if (const CNA_Result result = ResolveGraphicsResource(resourceHandle, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const CNA_Bool disposed = resource.value->getIsDisposedProperty() ? CNA_TRUE : CNA_FALSE;
        *outIsDisposed = disposed;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_graphics_resource_get_name_byte_count(
    const CNA_Handle resourceHandle,
    uint64_t* const outByteCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outByteCount == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The name byte-count output is null.");
        }
        GraphicsResourceView resource;
        if (const CNA_Result result = ResolveGraphicsResource(resourceHandle, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const uint64_t count = resource.value->getNameProperty().size();
        *outByteCount = count;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_graphics_resource_copy_name(
    const CNA_Handle resourceHandle,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outByteCount)
{
    return CopyResourceString(
        resourceHandle,
        destination,
        capacity,
        outByteCount,
        [](GraphicsResource& resource) { return resource.getNameProperty(); });
}

CNA_Result cna_graphics_resource_set_name(
    const CNA_Handle resourceHandle,
    const CNA_StringView name)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::string copiedName;
        if (const CNA_Result result = CopyStringView(name, true, &copiedName);
            result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The graphics-resource name is not valid UTF-8.");
        }
        GraphicsResourceView resource;
        if (const CNA_Result result = ResolveGraphicsResource(resourceHandle, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        resource.value->setNameProperty(copiedName);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_graphics_resource_get_string_byte_count(
    const CNA_Handle resourceHandle,
    uint64_t* const outByteCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outByteCount == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The string byte-count output is null.");
        }
        GraphicsResourceView resource;
        if (const CNA_Result result = ResolveGraphicsResource(resourceHandle, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const uint64_t count = resource.value->ToString().size();
        *outByteCount = count;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_graphics_resource_copy_string(
    const CNA_Handle resourceHandle,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outByteCount)
{
    return CopyResourceString(
        resourceHandle,
        destination,
        capacity,
        outByteCount,
        [](GraphicsResource& resource) { return resource.ToString(); });
}

CNA_Result cna_graphics_resource_get_tag(
    const CNA_Handle resourceHandle,
    CNA_GraphicsResourceTag* const outTag)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outTag == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The graphics-resource tag output is null.");
        }
        GraphicsResourceView resource;
        if (const CNA_Result result = ResolveGraphicsResource(resourceHandle, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        uint64_t tag = 0U;
        const CNA_Result result = GetRuntimeHandles().GetUserTag(resourceHandle, &tag);
        if (result != CNA_RESULT_SUCCESS) {
            return InvalidResource(result);
        }
        *outTag = tag;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_graphics_resource_set_tag(
    const CNA_Handle resourceHandle,
    const CNA_GraphicsResourceTag tag)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        GraphicsResourceView resource;
        if (const CNA_Result result = ResolveGraphicsResource(resourceHandle, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const CNA_Result result = GetRuntimeHandles().SetUserTag(resourceHandle, tag);
        return result == CNA_RESULT_SUCCESS ? CNA_RESULT_SUCCESS : InvalidResource(result);
    });
}

CNA_Result cna_graphics_resource_dispose(const CNA_Handle resourceHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        GraphicsResourceView resource;
        if (const CNA_Result result = ResolveGraphicsResource(resourceHandle, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (const CNA_Result result = EnsureDisposable(resource); result != CNA_RESULT_SUCCESS) {
            return result;
        }
        resource.value->Dispose();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_graphics_resource_subscribe_disposing(
    const CNA_Handle resourceHandle,
    const CNA_GraphicsResourceDisposingCallback callback,
    void* const context,
    CNA_GraphicsResourceEventRegistrationHandle* const outRegistration)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outRegistration == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The disposing-registration output handle is null.");
        }
        *outRegistration = CNA_INVALID_HANDLE;
        if (callback == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The disposing callback is null.");
        }
        GraphicsResourceView resource;
        if (const CNA_Result result = ResolveGraphicsResource(resourceHandle, &resource);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const auto token = resource.value->Disposing.Add(
            [resourceHandle, callback, context](System::Object*, const System::EventArgs&) {
                callback(resourceHandle, context);
            });
        const auto registration = std::make_shared<DisposingRegistration>(resource.value, token);
        const CNA_Result result = GetRuntimeHandles().Create(
            ObjectKind::EventRegistration,
            registration,
            outRegistration);
        if (result == CNA_RESULT_SUCCESS) {
            return CNA_RESULT_SUCCESS;
        }
        registration->Unsubscribe();
        return Fail(
            result,
            ErrorCategoryForResult(result),
            "The disposing-registration handle could not be created.");
    });
}

CNA_Result cna_graphics_resource_unsubscribe_disposing(
    const CNA_GraphicsResourceEventRegistrationHandle registrationHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<DisposingRegistration> registration;
        const CNA_Result getResult = GetRuntimeHandles().Get(
            registrationHandle,
            ObjectKind::EventRegistration,
            &registration);
        if (getResult != CNA_RESULT_SUCCESS) {
            return Fail(
                getResult,
                ErrorCategoryForResult(getResult),
                "The graphics-resource event-registration handle is invalid.");
        }
        registration->Unsubscribe();
        const CNA_Result releaseResult = GetRuntimeHandles().Release(registrationHandle);
        if (releaseResult == CNA_RESULT_SUCCESS) {
            return CNA_RESULT_SUCCESS;
        }
        return Fail(
            releaseResult,
            ErrorCategoryForResult(releaseResult),
            "The graphics-resource event-registration handle could not be released.");
    });
}
