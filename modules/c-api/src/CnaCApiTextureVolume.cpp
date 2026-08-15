// SPDX-License-Identifier: MS-PL

#include "CNA/C/texture_volume.h"
#include "CnaCApiDetail.hpp"
#include "CnaCApiGraphicsDetail.hpp"
#include "CnaCApiRuntimeDetail.hpp"

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture3D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "System/IO/MemoryStream.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace CNA::C::Detail {

CNA_Result GetOwnedTexture3D(
    const CNA_Handle handle,
    std::shared_ptr<Texture3DResource>* const outTexture)
{
    const CNA_Result result = GetRuntimeHandles().Get(
        handle, ObjectKind::Texture3D, outTexture);
    if (result == CNA_RESULT_SUCCESS) {
        return CNA_RESULT_SUCCESS;
    }
    return Fail(
        result,
        ErrorCategoryForResult(result),
        "The owned Texture3D handle is invalid for this call.");
}

CNA_Result GetOwnedTextureCube(
    const CNA_Handle handle,
    TextureCubeResourceView* const outTexture)
{
    if (outTexture == nullptr) {
        return Fail(
            CNA_RESULT_INVALID_ARGUMENT,
            CNA_ERROR_CATEGORY_ARGUMENT,
            "The TextureCube resource output is null.");
    }
    ObjectKind kind = ObjectKind::Unknown;
    if (const CNA_Result result = GetRuntimeHandles().GetKind(handle, &kind);
        result != CNA_RESULT_SUCCESS) {
        return Fail(
            result,
            ErrorCategoryForResult(result),
            "The TextureCube-derived handle is invalid for this call.");
    }
    if (kind == ObjectKind::TextureCube) {
        std::shared_ptr<TextureCubeResource> texture;
        if (const CNA_Result result = GetRuntimeHandles().Get(
                handle, ObjectKind::TextureCube, &texture);
            result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The owned TextureCube handle is invalid for this call.");
        }
        *outTexture = TextureCubeResourceView{
            texture->value,
            texture,
            texture->parentGame,
            &texture->activeEffectReferenceCount};
        return CNA_RESULT_SUCCESS;
    }
    if (kind == ObjectKind::RenderTargetCube) {
        std::shared_ptr<RenderTargetCubeResource> target;
        if (const CNA_Result result = GetRuntimeHandles().Get(
                handle, ObjectKind::RenderTargetCube, &target);
            result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The owned RenderTargetCube handle is invalid for this call.");
        }
        *outTexture = TextureCubeResourceView{
            std::static_pointer_cast<Microsoft::Xna::Framework::Graphics::TextureCube>(
                target->value),
            target,
            target->parentGame,
            &target->activeEffectReferenceCount};
        return CNA_RESULT_SUCCESS;
    }
    return Fail(
        CNA_RESULT_INVALID_HANDLE,
        CNA_ERROR_CATEGORY_HANDLE,
        "The handle is not a TextureCube or RenderTargetCube.");
}

CNA_Result GetOwnedTexture(
    const CNA_Handle handle,
    TextureResourceView* const outTexture)
{
    if (outTexture == nullptr) {
        return Fail(
            CNA_RESULT_INVALID_ARGUMENT,
            CNA_ERROR_CATEGORY_ARGUMENT,
            "The texture resource output is null.");
    }
    ObjectKind kind = ObjectKind::Unknown;
    if (const CNA_Result result = GetRuntimeHandles().GetKind(handle, &kind);
        result != CNA_RESULT_SUCCESS) {
        return Fail(
            result,
            ErrorCategoryForResult(result),
            "The texture handle is invalid for this call.");
    }
    if (kind == ObjectKind::Texture2D || kind == ObjectKind::RenderTarget2D) {
        std::shared_ptr<Texture2DResource> texture;
        if (const CNA_Result result = GetOwnedTexture2D(handle, &texture);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outTexture = TextureResourceView{
            std::static_pointer_cast<Microsoft::Xna::Framework::Graphics::Texture>(texture->value),
            texture,
            texture->parentGame,
            &texture->activeEffectReferenceCount};
        return CNA_RESULT_SUCCESS;
    }
    if (kind == ObjectKind::Texture3D) {
        std::shared_ptr<Texture3DResource> texture;
        if (const CNA_Result result = GetOwnedTexture3D(handle, &texture);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outTexture = TextureResourceView{
            std::static_pointer_cast<Microsoft::Xna::Framework::Graphics::Texture>(texture->value),
            texture,
            texture->parentGame,
            &texture->activeEffectReferenceCount};
        return CNA_RESULT_SUCCESS;
    }
    TextureCubeResourceView cube;
    if (kind == ObjectKind::TextureCube || kind == ObjectKind::RenderTargetCube) {
        if (const CNA_Result result = GetOwnedTextureCube(handle, &cube);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outTexture = TextureResourceView{
            std::static_pointer_cast<Microsoft::Xna::Framework::Graphics::Texture>(cube.value),
            cube.retentionOwner,
            cube.parentGame,
            cube.activeEffectReferenceCount};
        return CNA_RESULT_SUCCESS;
    }
    return Fail(
        CNA_RESULT_INVALID_HANDLE,
        CNA_ERROR_CATEGORY_HANDLE,
        "The handle does not refer to a supported texture resource.");
}

} // namespace CNA::C::Detail

namespace {

using CNA::C::Detail::AddOwnedGraphicsResource;
using CNA::C::Detail::BorrowedGraphicsDevice;
using CNA::C::Detail::CallWithExceptionBarrier;
using CNA::C::Detail::ErrorCategoryForResult;
using CNA::C::Detail::Fail;
using CNA::C::Detail::GetBorrowedGraphicsDevice;
using CNA::C::Detail::GetOwnedTexture3D;
using CNA::C::Detail::GetOwnedTextureCube;
using CNA::C::Detail::GetRuntimeHandles;
using CNA::C::Detail::ObjectKind;
using CNA::C::Detail::RemoveOwnedGraphicsResource;
using CNA::C::Detail::Texture3DResource;
using CNA::C::Detail::TextureCubeResource;
using CNA::C::Detail::TextureCubeResourceView;
using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Rectangle;
using Microsoft::Xna::Framework::Graphics::CubeMapFace;
using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
using Microsoft::Xna::Framework::Graphics::Texture3D;
using Microsoft::Xna::Framework::Graphics::TextureCube;

constexpr uint32_t StructureVersion = UINT32_C(1);

struct Texture3DTransferView final {
    int level;
    int left;
    int top;
    int right;
    int bottom;
    int front;
    int back;
    int startIndex;
    int elementCount;
    uint64_t requiredElements;
};

struct TextureCubeTransferView final {
    CubeMapFace face;
    int level;
    Rectangle rectangle;
    bool hasRectangle;
    int startIndex;
    int elementCount;
    uint64_t requiredElements;
};

[[nodiscard]] CNA_Result InvalidArgument(const char* const message)
{
    return Fail(CNA_RESULT_INVALID_ARGUMENT, CNA_ERROR_CATEGORY_ARGUMENT, message);
}

[[nodiscard]] bool IsBool(const CNA_Bool value) noexcept
{
    return value == CNA_FALSE || value == CNA_TRUE;
}

[[nodiscard]] bool IsSurfaceFormat(const CNA_SurfaceFormat value) noexcept
{
    return value <= CNA_SURFACE_FORMAT_USHORT_EXT;
}

[[nodiscard]] bool IsZero(const uint8_t* const bytes, const std::size_t count) noexcept
{
    for (std::size_t index = 0U; index < count; ++index) {
        if (bytes[index] != 0U) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] int MipDimension(const int base, const int level) noexcept
{
    return std::max(1, base >> level);
}

[[nodiscard]] CNA_Result ValidatePositiveDimensions(
    const uint32_t width,
    const uint32_t height,
    const uint32_t depth,
    const char* const message)
{
    const uint64_t max = static_cast<uint64_t>(std::numeric_limits<int>::max());
    if (width == 0U || height == 0U || depth == 0U || width > max || height > max ||
        depth > max || static_cast<uint64_t>(width) * height > max ||
        static_cast<uint64_t>(width) * height * depth > max) {
        return InvalidArgument(message);
    }
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result ValidateArrayWindow(
    const void* const data,
    const uint64_t capacity,
    const int startIndex,
    const int elementCount,
    const char* const message)
{
    const uint64_t requiredCapacity =
        static_cast<uint64_t>(startIndex) + static_cast<uint64_t>(elementCount);
    if (data == nullptr || capacity < requiredCapacity) {
        return capacity < requiredCapacity
            ? Fail(CNA_RESULT_BUFFER_TOO_SMALL, CNA_ERROR_CATEGORY_RANGE, message)
            : InvalidArgument(message);
    }
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result ValidateTexture3DTransfer(
    const Texture3D& texture,
    const CNA_Texture3DTransfer* const transfer,
    const bool validateArrayWindow,
    Texture3DTransferView* const outView)
{
    const uint64_t max = static_cast<uint64_t>(std::numeric_limits<int>::max());
    if (transfer == nullptr || outView == nullptr ||
        transfer->struct_size < sizeof(CNA_Texture3DTransfer) ||
        transfer->struct_version != StructureVersion || transfer->reserved != 0U ||
        transfer->level < 0 || transfer->level >= texture.getLevelCountProperty()) {
        return InvalidArgument("The Texture3D transfer descriptor is invalid.");
    }
    const int width = MipDimension(texture.getWidthProperty(), transfer->level);
    const int height = MipDimension(texture.getHeightProperty(), transfer->level);
    const int depth = MipDimension(texture.getDepthProperty(), transfer->level);
    if (transfer->left < 0 || transfer->top < 0 || transfer->front < 0 ||
        transfer->right <= transfer->left || transfer->bottom <= transfer->top ||
        transfer->back <= transfer->front || transfer->right > width ||
        transfer->bottom > height || transfer->back > depth) {
        return InvalidArgument("The Texture3D transfer box is outside its mip level.");
    }
    const uint64_t required =
        static_cast<uint64_t>(transfer->right - transfer->left) *
        static_cast<uint64_t>(transfer->bottom - transfer->top) *
        static_cast<uint64_t>(transfer->back - transfer->front);
    if (required > max || (validateArrayWindow &&
        (transfer->start_index > max || transfer->element_count > max ||
         transfer->start_index + transfer->element_count > max ||
         transfer->element_count < required))) {
        return InvalidArgument("The Texture3D transfer array window is invalid.");
    }
    *outView = Texture3DTransferView{
        transfer->level,
        transfer->left,
        transfer->top,
        transfer->right,
        transfer->bottom,
        transfer->front,
        transfer->back,
        validateArrayWindow ? static_cast<int>(transfer->start_index) : 0,
        validateArrayWindow ? static_cast<int>(transfer->element_count) :
            static_cast<int>(required),
        required};
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result ValidateTextureCubeTransfer(
    const TextureCube& texture,
    const CNA_TextureCubeTransfer* const transfer,
    TextureCubeTransferView* const outView)
{
    const uint64_t max = static_cast<uint64_t>(std::numeric_limits<int>::max());
    if (transfer == nullptr || outView == nullptr ||
        transfer->struct_size < sizeof(CNA_TextureCubeTransfer) ||
        transfer->struct_version != StructureVersion ||
        transfer->face > CNA_CUBE_MAP_FACE_NEGATIVE_Z || transfer->level < 0 ||
        transfer->level >= texture.getLevelCountProperty() ||
        !IsBool(transfer->has_rectangle) || !IsZero(transfer->reserved0, 3U) ||
        transfer->reserved1 != 0U || transfer->start_index > max ||
        transfer->element_count > max ||
        transfer->start_index + transfer->element_count > max) {
        return InvalidArgument("The TextureCube transfer descriptor is invalid.");
    }
    const int size = MipDimension(texture.getSizeProperty(), transfer->level);
    Rectangle rectangle(0, 0, size, size);
    if (transfer->has_rectangle == CNA_TRUE) {
        const CNA_Rectangle& requested = transfer->rectangle;
        if (requested.x < 0 || requested.y < 0 || requested.width <= 0 ||
            requested.height <= 0 || requested.x > size - requested.width ||
            requested.y > size - requested.height) {
            return InvalidArgument("The TextureCube transfer rectangle is outside its mip level.");
        }
        rectangle = Rectangle(
            requested.x, requested.y, requested.width, requested.height);
    }
    const uint64_t required =
        static_cast<uint64_t>(rectangle.Width) * rectangle.Height;
    if (transfer->element_count < required) {
        return InvalidArgument("The TextureCube element count is smaller than its region.");
    }
    *outView = TextureCubeTransferView{
        static_cast<CubeMapFace>(transfer->face),
        transfer->level,
        rectangle,
        transfer->has_rectangle == CNA_TRUE,
        static_cast<int>(transfer->start_index),
        static_cast<int>(transfer->element_count),
        required};
    return CNA_RESULT_SUCCESS;
}

template<typename TResource>
[[nodiscard]] CNA_Result CopyTypeName(
    const TResource& resource,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outByteCount)
{
    if (outByteCount == nullptr || (destination == nullptr && capacity != 0U)) {
        return InvalidArgument("The texture type-name output buffer is invalid.");
    }
    const std::string& name = resource.GetTypeName();
    *outByteCount = static_cast<uint64_t>(name.size());
    if (capacity < name.size()) {
        return Fail(
            CNA_RESULT_BUFFER_TOO_SMALL,
            CNA_ERROR_CATEGORY_RANGE,
            "The texture type-name output buffer is too small.");
    }
    if (!name.empty()) {
        std::memcpy(destination, name.data(), name.size());
    }
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result CreateTexture3DHandle(
    std::shared_ptr<Texture3D> texture,
    const CNA_Handle parentGame,
    CNA_Handle* const outTexture)
{
    const auto resource = std::make_shared<Texture3DResource>(
        Texture3DResource{std::move(texture), parentGame, 0U});
    const CNA_Result result = GetRuntimeHandles().Create(
        ObjectKind::Texture3D, resource, outTexture);
    if (result != CNA_RESULT_SUCCESS) {
        return Fail(
            result,
            ErrorCategoryForResult(result),
            "The owned Texture3D handle could not be created.");
    }
    AddOwnedGraphicsResource();
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result CreateTextureCubeHandle(
    std::shared_ptr<TextureCube> texture,
    const CNA_Handle parentGame,
    CNA_Handle* const outTexture)
{
    const auto resource = std::make_shared<TextureCubeResource>(
        TextureCubeResource{std::move(texture), parentGame, 0U});
    const CNA_Result result = GetRuntimeHandles().Create(
        ObjectKind::TextureCube, resource, outTexture);
    if (result != CNA_RESULT_SUCCESS) {
        return Fail(
            result,
            ErrorCategoryForResult(result),
            "The owned TextureCube handle could not be created.");
    }
    AddOwnedGraphicsResource();
    return CNA_RESULT_SUCCESS;
}

void CopyColorsToNative(
    const CNA_Color* const source,
    const uint64_t start,
    const uint64_t count,
    std::vector<Color>* const destination)
{
    const Color zero(
        static_cast<uint8_t>(0U), static_cast<uint8_t>(0U),
        static_cast<uint8_t>(0U), static_cast<uint8_t>(0U));
    destination->assign(static_cast<std::size_t>(start + count), zero);
    for (uint64_t index = start; index < start + count; ++index) {
        (*destination)[static_cast<std::size_t>(index)] = Color(
            source[index].r, source[index].g, source[index].b, source[index].a);
    }
}

void CopyColorsFromNative(
    const std::vector<Color>& source,
    const uint64_t start,
    const uint64_t count,
    CNA_Color* const destination)
{
    for (uint64_t index = start; index < start + count; ++index) {
        const Color& color = source[static_cast<std::size_t>(index)];
        destination[index] = CNA_Color{
            color.getRProperty(), color.getGProperty(),
            color.getBProperty(), color.getAProperty()};
    }
}

} // namespace

CNA_Result cna_texture3d_create(
    const CNA_Handle graphicsDeviceHandle,
    const CNA_Texture3DCreateInfo* const createInfo,
    CNA_Handle* const outTexture)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outTexture == nullptr) {
            return InvalidArgument("The Texture3D output handle is null.");
        }
        *outTexture = CNA_INVALID_HANDLE;
        if (createInfo == nullptr ||
            createInfo->struct_size < sizeof(CNA_Texture3DCreateInfo) ||
            createInfo->struct_version != StructureVersion ||
            !IsBool(createInfo->mip_map) || !IsZero(createInfo->reserved0, 3U) ||
            createInfo->reserved1 != 0U || !IsSurfaceFormat(createInfo->format)) {
            return InvalidArgument("The Texture3D creation configuration is invalid.");
        }
        if (createInfo->format != CNA_SURFACE_FORMAT_COLOR) {
            return Fail(
                CNA_RESULT_NOT_SUPPORTED,
                CNA_ERROR_CATEGORY_NOT_SUPPORTED,
                "The Texture3D surface format is unsupported by the native contract.");
        }
        if (const CNA_Result result = ValidatePositiveDimensions(
                createInfo->width, createInfo->height, createInfo->depth,
                "The Texture3D dimensions are invalid or too large.");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::shared_ptr<BorrowedGraphicsDevice> graphicsDevice;
        if (const CNA_Result result = GetBorrowedGraphicsDevice(
                graphicsDeviceHandle, &graphicsDevice);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        auto texture = std::make_shared<Texture3D>(
            *graphicsDevice->value,
            static_cast<int>(createInfo->width),
            static_cast<int>(createInfo->height),
            static_cast<int>(createInfo->depth),
            createInfo->mip_map == CNA_TRUE,
            SurfaceFormat::Color);
        return CreateTexture3DHandle(texture, graphicsDevice->parentGame, outTexture);
    });
}

CNA_Result cna_texture3d_destroy(const CNA_Handle textureHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<Texture3DResource> texture;
        if (const CNA_Result result = GetOwnedTexture3D(textureHandle, &texture);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (texture->activeEffectReferenceCount != 0U) {
            return Fail(
                CNA_RESULT_INVALID_STATE,
                CNA_ERROR_CATEGORY_STATE,
                "The Texture3D is retained by an EffectParameter.");
        }
        if (const CNA_Result result = GetRuntimeHandles().Release(textureHandle);
            result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The owned Texture3D handle could not be released.");
        }
        RemoveOwnedGraphicsResource();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_texture3d_get_info(
    const CNA_Handle textureHandle,
    CNA_Texture3DInfo* const outInfo)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outInfo == nullptr || outInfo->struct_size < sizeof(CNA_Texture3DInfo) ||
            outInfo->struct_version != StructureVersion) {
            return InvalidArgument("The Texture3D info output structure is invalid.");
        }
        std::shared_ptr<Texture3DResource> texture;
        if (const CNA_Result result = GetOwnedTexture3D(textureHandle, &texture);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outInfo = CNA_Texture3DInfo{
            sizeof(CNA_Texture3DInfo),
            StructureVersion,
            static_cast<uint32_t>(texture->value->getWidthProperty()),
            static_cast<uint32_t>(texture->value->getHeightProperty()),
            static_cast<uint32_t>(texture->value->getDepthProperty()),
            static_cast<uint32_t>(texture->value->getLevelCountProperty()),
            static_cast<CNA_SurfaceFormat>(texture->value->getFormatProperty()),
            0U};
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_texture3d_get_type_name_byte_count(
    const CNA_Handle textureHandle,
    uint64_t* const outByteCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outByteCount == nullptr) {
            return InvalidArgument("The Texture3D type-name byte-count output is null.");
        }
        std::shared_ptr<Texture3DResource> texture;
        if (const CNA_Result result = GetOwnedTexture3D(textureHandle, &texture);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outByteCount = static_cast<uint64_t>(texture->value->GetTypeName().size());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_texture3d_copy_type_name(
    const CNA_Handle textureHandle,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outByteCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<Texture3DResource> texture;
        if (const CNA_Result result = GetOwnedTexture3D(textureHandle, &texture);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CopyTypeName(*texture->value, destination, capacity, outByteCount);
    });
}

CNA_Result cna_texture3d_set_data(
    const CNA_Handle textureHandle,
    const CNA_Texture3DTransfer* const transfer,
    const CNA_Color* const data,
    const uint64_t dataCapacity)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<Texture3DResource> texture;
        if (const CNA_Result result = GetOwnedTexture3D(textureHandle, &texture);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        Texture3DTransferView view{};
        if (const CNA_Result result = ValidateTexture3DTransfer(
                *texture->value, transfer, true, &view);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (const CNA_Result result = ValidateArrayWindow(
                data, dataCapacity, view.startIndex, view.elementCount,
                "The Texture3D upload array is too small or null.");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::vector<Color> native;
        CopyColorsToNative(
            data,
            static_cast<uint64_t>(view.startIndex),
            view.requiredElements,
            &native);
        texture->value->SetData(
            view.level, view.left, view.top, view.right, view.bottom, view.front, view.back,
            native.data(), view.startIndex, view.elementCount);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_texture3d_get_data(
    const CNA_Handle textureHandle,
    const CNA_Texture3DTransfer* const transfer,
    CNA_Color* const destination,
    const uint64_t destinationCapacity,
    uint64_t* const outRequiredElements)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outRequiredElements == nullptr) {
            return InvalidArgument("The Texture3D required-element output is null.");
        }
        std::shared_ptr<Texture3DResource> texture;
        if (const CNA_Result result = GetOwnedTexture3D(textureHandle, &texture);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        Texture3DTransferView view{};
        if (const CNA_Result result = ValidateTexture3DTransfer(
                *texture->value, transfer, true, &view);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outRequiredElements = view.requiredElements;
        if (const CNA_Result result = ValidateArrayWindow(
                destination, destinationCapacity, view.startIndex, view.elementCount,
                "The Texture3D readback array is too small or null.");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const Color zero(
            static_cast<uint8_t>(0U), static_cast<uint8_t>(0U),
            static_cast<uint8_t>(0U), static_cast<uint8_t>(0U));
        std::vector<Color> native(
            static_cast<std::size_t>(view.startIndex + view.elementCount), zero);
        texture->value->GetData(
            view.level, view.left, view.top, view.right, view.bottom, view.front, view.back,
            native.data(), view.startIndex, view.elementCount);
        CopyColorsFromNative(
            native,
            static_cast<uint64_t>(view.startIndex),
            view.requiredElements,
            destination);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_texture3d_set_data_bytes(
    const CNA_Handle textureHandle,
    const CNA_Texture3DTransfer* const transfer,
    const uint8_t* const data,
    const uint64_t dataByteCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<Texture3DResource> texture;
        if (const CNA_Result result = GetOwnedTexture3D(textureHandle, &texture);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        Texture3DTransferView view{};
        if (const CNA_Result result = ValidateTexture3DTransfer(
                *texture->value, transfer, false, &view);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const uint64_t requiredBytes = view.requiredElements * 4U;
        if (transfer->start_index != 0U || data == nullptr || dataByteCount != requiredBytes ||
            requiredBytes > static_cast<uint64_t>(std::numeric_limits<int>::max())) {
            return InvalidArgument("The raw Texture3D upload buffer is invalid.");
        }
        texture->value->SetDataPointerEXT(
            view.level, view.left, view.top, view.right, view.bottom, view.front, view.back,
            data, static_cast<int>(dataByteCount));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_texturecube_create(
    const CNA_Handle graphicsDeviceHandle,
    const CNA_TextureCubeCreateInfo* const createInfo,
    CNA_Handle* const outTexture)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outTexture == nullptr) {
            return InvalidArgument("The TextureCube output handle is null.");
        }
        *outTexture = CNA_INVALID_HANDLE;
        if (createInfo == nullptr ||
            createInfo->struct_size < sizeof(CNA_TextureCubeCreateInfo) ||
            createInfo->struct_version != StructureVersion ||
            !IsBool(createInfo->mip_map) || !IsZero(createInfo->reserved0, 3U) ||
            !IsSurfaceFormat(createInfo->format) || createInfo->reserved1 != 0U) {
            return InvalidArgument("The TextureCube creation configuration is invalid.");
        }
        if (createInfo->format != CNA_SURFACE_FORMAT_COLOR) {
            return Fail(
                CNA_RESULT_NOT_SUPPORTED,
                CNA_ERROR_CATEGORY_NOT_SUPPORTED,
                "The TextureCube surface format is unsupported by the native contract.");
        }
        if (const CNA_Result result = ValidatePositiveDimensions(
                createInfo->size, createInfo->size, 1U,
                "The TextureCube face size is invalid or too large.");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::shared_ptr<BorrowedGraphicsDevice> graphicsDevice;
        if (const CNA_Result result = GetBorrowedGraphicsDevice(
                graphicsDeviceHandle, &graphicsDevice);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        auto texture = std::make_shared<TextureCube>(
            *graphicsDevice->value,
            static_cast<int>(createInfo->size),
            createInfo->mip_map == CNA_TRUE,
            SurfaceFormat::Color);
        return CreateTextureCubeHandle(texture, graphicsDevice->parentGame, outTexture);
    });
}

CNA_Result cna_texturecube_create_from_dds_memory(
    const CNA_Handle graphicsDeviceHandle,
    const uint8_t* const ddsData,
    const uint64_t ddsByteCount,
    CNA_Handle* const outTexture)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outTexture == nullptr) {
            return InvalidArgument("The decoded TextureCube output handle is null.");
        }
        *outTexture = CNA_INVALID_HANDLE;
        if (ddsData == nullptr || ddsByteCount == 0U ||
            ddsByteCount > static_cast<uint64_t>(std::numeric_limits<int>::max())) {
            return InvalidArgument("The TextureCube DDS source buffer is invalid.");
        }
        std::shared_ptr<BorrowedGraphicsDevice> graphicsDevice;
        if (const CNA_Result result = GetBorrowedGraphicsDevice(
                graphicsDeviceHandle, &graphicsDevice);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        System::IO::MemoryStream stream(ddsData, static_cast<int>(ddsByteCount), false);
        TextureCube decoded = TextureCube::DDSFromStreamEXT(*graphicsDevice->value, stream);
        return CreateTextureCubeHandle(
            std::make_shared<TextureCube>(std::move(decoded)),
            graphicsDevice->parentGame,
            outTexture);
    });
}

CNA_Result cna_texturecube_destroy(const CNA_Handle textureHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<TextureCubeResource> texture;
        if (const CNA_Result result = GetRuntimeHandles().Get(
                textureHandle, ObjectKind::TextureCube, &texture);
            result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The owned TextureCube handle is invalid for destruction.");
        }
        if (texture->activeEffectReferenceCount != 0U) {
            return Fail(
                CNA_RESULT_INVALID_STATE,
                CNA_ERROR_CATEGORY_STATE,
                "The TextureCube is retained by an EffectParameter.");
        }
        if (const CNA_Result result = GetRuntimeHandles().Release(textureHandle);
            result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The owned TextureCube handle could not be released.");
        }
        RemoveOwnedGraphicsResource();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_texturecube_get_info(
    const CNA_Handle textureHandle,
    CNA_TextureCubeInfo* const outInfo)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outInfo == nullptr || outInfo->struct_size < sizeof(CNA_TextureCubeInfo) ||
            outInfo->struct_version != StructureVersion) {
            return InvalidArgument("The TextureCube info output structure is invalid.");
        }
        TextureCubeResourceView texture;
        if (const CNA_Result result = GetOwnedTextureCube(textureHandle, &texture);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outInfo = CNA_TextureCubeInfo{
            sizeof(CNA_TextureCubeInfo),
            StructureVersion,
            static_cast<uint32_t>(texture.value->getSizeProperty()),
            static_cast<uint32_t>(texture.value->getLevelCountProperty()),
            static_cast<CNA_SurfaceFormat>(texture.value->getFormatProperty()),
            0U};
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_texturecube_get_type_name_byte_count(
    const CNA_Handle textureHandle,
    uint64_t* const outByteCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outByteCount == nullptr) {
            return InvalidArgument("The TextureCube type-name byte-count output is null.");
        }
        TextureCubeResourceView texture;
        if (const CNA_Result result = GetOwnedTextureCube(textureHandle, &texture);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outByteCount = static_cast<uint64_t>(texture.value->GetTypeName().size());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_texturecube_copy_type_name(
    const CNA_Handle textureHandle,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outByteCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        TextureCubeResourceView texture;
        if (const CNA_Result result = GetOwnedTextureCube(textureHandle, &texture);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CopyTypeName(*texture.value, destination, capacity, outByteCount);
    });
}

CNA_Result cna_texturecube_set_data(
    const CNA_Handle textureHandle,
    const CNA_TextureCubeTransfer* const transfer,
    const CNA_Color* const data,
    const uint64_t dataCapacity)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        TextureCubeResourceView texture;
        if (const CNA_Result result = GetOwnedTextureCube(textureHandle, &texture);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        TextureCubeTransferView view{};
        if (const CNA_Result result = ValidateTextureCubeTransfer(
                *texture.value, transfer, &view);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (const CNA_Result result = ValidateArrayWindow(
                data, dataCapacity, view.startIndex, view.elementCount,
                "The TextureCube upload array is too small or null.");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::vector<Color> native;
        CopyColorsToNative(
            data,
            static_cast<uint64_t>(view.startIndex),
            view.requiredElements,
            &native);
        texture.value->SetData(
            view.face,
            view.level,
            view.hasRectangle ? &view.rectangle : nullptr,
            native.data(),
            view.startIndex,
            view.elementCount);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_texturecube_get_data(
    const CNA_Handle textureHandle,
    const CNA_TextureCubeTransfer* const transfer,
    CNA_Color* const destination,
    const uint64_t destinationCapacity,
    uint64_t* const outRequiredElements)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outRequiredElements == nullptr) {
            return InvalidArgument("The TextureCube required-element output is null.");
        }
        TextureCubeResourceView texture;
        if (const CNA_Result result = GetOwnedTextureCube(textureHandle, &texture);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        TextureCubeTransferView view{};
        if (const CNA_Result result = ValidateTextureCubeTransfer(
                *texture.value, transfer, &view);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outRequiredElements = view.requiredElements;
        if (const CNA_Result result = ValidateArrayWindow(
                destination, destinationCapacity, view.startIndex, view.elementCount,
                "The TextureCube readback array is too small or null.");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const Color zero(
            static_cast<uint8_t>(0U), static_cast<uint8_t>(0U),
            static_cast<uint8_t>(0U), static_cast<uint8_t>(0U));
        std::vector<Color> native(
            static_cast<std::size_t>(view.startIndex + view.elementCount), zero);
        texture.value->GetData(
            view.face,
            view.level,
            view.hasRectangle ? &view.rectangle : nullptr,
            native.data(),
            view.startIndex,
            view.elementCount);
        CopyColorsFromNative(
            native,
            static_cast<uint64_t>(view.startIndex),
            view.requiredElements,
            destination);
        return CNA_RESULT_SUCCESS;
    });
}
