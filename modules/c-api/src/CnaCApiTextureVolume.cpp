// SPDX-License-Identifier: MS-PL

#include "CNA/C/texture_volume.h"
#include "CnaCApiMarshal.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/Alpha8.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/Bgr565.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/Bgra4444.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/Bgra5551.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/HalfSingle.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/HalfVector2.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/HalfVector4.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/NormalizedByte2.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/NormalizedByte4.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/Rg32.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/Rgba1010102.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/Rgba64.hpp"

#include "CnaCApiDetail.hpp"
#include "CnaCApiGraphicsDetail.hpp"
#include "CnaCApiRuntimeDetail.hpp"
#include "CnaCApiMarshal.hpp"
#include "CnaCApiOutput.hpp"
#include "CnaCApiRenderer.hpp"

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
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
    return LookupHandle(handle, ObjectKind::Texture3D, outTexture);
}

CNA_Result GetOwnedTextureCube(
    const CNA_Handle handle,
    TextureCubeResourceView* const outTexture)
{
    if (outTexture == nullptr) {
        return InvalidArgument("The TextureCube resource output is null.");
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
        if (const CNA_Result result = LookupHandle(handle, ObjectKind::TextureCube, &texture);
            result != CNA_RESULT_SUCCESS) {
            return result;
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
        if (const CNA_Result result = LookupHandle(handle, ObjectKind::RenderTargetCube, &target);
            result != CNA_RESULT_SUCCESS) {
            return result;
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
        return InvalidArgument("The texture resource output is null.");
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

using CNA::C::Detail::AddOwnedGraphicsResourceFor;
using CNA::C::Detail::BorrowedGraphicsDevice;
using CNA::C::Detail::BufferTooSmall;
using CNA::C::Detail::CallWithExceptionBarrier;
using CNA::C::Detail::CreateHandle;
using CNA::C::Detail::ErrorCategoryForResult;
using CNA::C::Detail::Fail;
using CNA::C::Detail::GetBorrowedGraphicsDevice;
using CNA::C::Detail::GetOwnedTexture3D;
using CNA::C::Detail::GetOwnedTextureCube;
using CNA::C::Detail::GetRuntimeHandles;
using CNA::C::Detail::InvalidArgument;
using CNA::C::Detail::InvalidState;
using CNA::C::Detail::IsCanonicalBool;
using CNA::C::Detail::IsSurfaceFormat;
using CNA::C::Detail::IsVersionedStructure;
using CNA::C::Detail::IsZero;
using CNA::C::Detail::LookupHandle;
using CNA::C::Detail::NotSupported;
using CNA::C::Detail::ObjectKind;
using CNA::C::Detail::ReleaseHandle;
using CNA::C::Detail::RemoveOwnedGraphicsResourceFor;
using CNA::C::Detail::StructureVersion;
using CNA::C::Detail::Texture3DResource;
using CNA::C::Detail::TextureCubeResource;
using CNA::C::Detail::TextureCubeResourceView;
using CNA::C::Detail::ValidateVersionedStructure;
using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Rectangle;
using Microsoft::Xna::Framework::Graphics::CubeMapFace;
using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
using Microsoft::Xna::Framework::Graphics::Texture3D;
using Microsoft::Xna::Framework::Graphics::TextureCube;

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
            ? BufferTooSmall(message)
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
        !IsCanonicalBool(transfer->has_rectangle) || !IsZero(transfer->reserved0, 3U) ||
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
        return BufferTooSmall("The texture type-name output buffer is too small.");
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
    if (const CNA_Result result = CreateHandle(ObjectKind::Texture3D, resource, outTexture);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    AddOwnedGraphicsResourceFor(parentGame);
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result CreateTextureCubeHandle(
    std::shared_ptr<TextureCube> texture,
    const CNA_Handle parentGame,
    CNA_Handle* const outTexture)
{
    const auto resource = std::make_shared<TextureCubeResource>(
        TextureCubeResource{std::move(texture), parentGame, 0U});
    if (const CNA_Result result = CreateHandle(ObjectKind::TextureCube, resource, outTexture);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    AddOwnedGraphicsResourceFor(parentGame);
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

namespace CNA::C::Detail {

CNA_Result CreateOwnedTextureCube(
    std::shared_ptr<TextureCube> texture,
    const CNA_Handle parentGame,
    CNA_Handle* const outTexture)
{
    if (texture == nullptr || outTexture == nullptr) {
        return InvalidArgument("The owned TextureCube factory arguments are invalid.");
    }
    return CreateTextureCubeHandle(std::move(texture), parentGame, outTexture);
}

} // namespace CNA::C::Detail

namespace {

[[nodiscard]] CNA_Result Texture3dCreate(
    const CNA_Handle graphicsDeviceHandle,
    const CNA_Texture3DCreateInfo* const createInfo,
    CNA_Handle* const outTexture,
    const bool allowFormat = false)
{
    if (!IsVersionedStructure(createInfo) ||
        !IsCanonicalBool(createInfo->mip_map) || !IsZero(createInfo->reserved0, 3U) ||
        createInfo->reserved1 != 0U || !IsSurfaceFormat(createInfo->format)) {
        return InvalidArgument("The Texture3D creation configuration is invalid.");
    }
    if (!allowFormat && createInfo->format != CNA_SURFACE_FORMAT_COLOR) {
        return NotSupported(
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
    const auto verdict = graphicsDevice->value->GetRenderer().ClassifyTexture3DFormatEXT(
        static_cast<int>(createInfo->format));
    using CNA::Internal::Renderers::RendererFormatVerdict;
    if (verdict == RendererFormatVerdict::Unsupported ||
        (verdict == RendererFormatVerdict::Defer && createInfo->format != CNA_SURFACE_FORMAT_COLOR)) {
        return NotSupported("The Texture3D surface format is unavailable on the selected renderer.");
    }
    auto texture = std::make_shared<Texture3D>(
        *graphicsDevice->value,
        static_cast<int>(createInfo->width),
        static_cast<int>(createInfo->height),
        static_cast<int>(createInfo->depth),
        createInfo->mip_map == CNA_TRUE,
        static_cast<SurfaceFormat>(createInfo->format));
    return CreateTexture3DHandle(texture, graphicsDevice->parentGame, outTexture);
}

[[nodiscard]] CNA_Result Texture3dDestroy(const CNA_Handle textureHandle)
{
    std::shared_ptr<Texture3DResource> texture;
    if (const CNA_Result result = GetOwnedTexture3D(textureHandle, &texture);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (texture->activeEffectReferenceCount != 0U) {
        return InvalidState("The Texture3D is retained by an EffectParameter.");
    }
    if (const CNA_Result result = ReleaseHandle(textureHandle);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    RemoveOwnedGraphicsResourceFor(texture->parentGame);
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result Texture3dSetData(
    const CNA_Handle textureHandle,
    const CNA_Texture3DTransfer* const transfer,
    const CNA_Color* const data,
    const uint64_t dataCapacity)
{
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
}

[[nodiscard]] CNA_Result Texture3dGetData(
    const CNA_Handle textureHandle,
    const CNA_Texture3DTransfer* const transfer,
    CNA_Color* const destination,
    const uint64_t destinationCapacity,
    uint64_t* const outRequiredElements)
{
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
}

[[nodiscard]] CNA_Result Texture3dSetDataBytes(
    const CNA_Handle textureHandle,
    const CNA_Texture3DTransfer* const transfer,
    const uint8_t* const data,
    const uint64_t dataByteCount)
{
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
}

[[nodiscard]] CNA_Result TexturecubeCreate(
    const CNA_Handle graphicsDeviceHandle,
    const CNA_TextureCubeCreateInfo* const createInfo,
    CNA_Handle* const outTexture,
    const bool allowFormat = false)
{
    if (!IsVersionedStructure(createInfo) ||
        !IsCanonicalBool(createInfo->mip_map) || !IsZero(createInfo->reserved0, 3U) ||
        !IsSurfaceFormat(createInfo->format) || createInfo->reserved1 != 0U) {
        return InvalidArgument("The TextureCube creation configuration is invalid.");
    }
    if (!allowFormat && createInfo->format != CNA_SURFACE_FORMAT_COLOR) {
        return NotSupported(
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
    const auto verdict = graphicsDevice->value->GetRenderer().ClassifyTextureCubeFormatEXT(
        static_cast<int>(createInfo->format));
    using CNA::Internal::Renderers::RendererFormatVerdict;
    if (verdict == RendererFormatVerdict::Unsupported ||
        (verdict == RendererFormatVerdict::Defer && createInfo->format != CNA_SURFACE_FORMAT_COLOR)) {
        return NotSupported("The TextureCube surface format is unavailable on the selected renderer.");
    }
    auto texture = std::make_shared<TextureCube>(
        *graphicsDevice->value,
        static_cast<int>(createInfo->size),
        createInfo->mip_map == CNA_TRUE,
        static_cast<SurfaceFormat>(createInfo->format));
    return CreateTextureCubeHandle(texture, graphicsDevice->parentGame, outTexture);
}

[[nodiscard]] CNA_Result TexturecubeCreateFromDdsMemory(
    const CNA_Handle graphicsDeviceHandle,
    const uint8_t* const ddsData,
    const uint64_t ddsByteCount,
    CNA_Handle* const outTexture)
{
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
}

[[nodiscard]] CNA_Result TexturecubeDestroy(const CNA_Handle textureHandle)
{
    std::shared_ptr<TextureCubeResource> texture;
    if (const CNA_Result result =
            LookupHandle(textureHandle, ObjectKind::TextureCube, &texture);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (texture->activeEffectReferenceCount != 0U) {
        return InvalidState("The TextureCube is retained by an EffectParameter.");
    }
    if (const CNA_Result result = ReleaseHandle(textureHandle);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    RemoveOwnedGraphicsResourceFor(texture->parentGame);
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result TexturecubeSetData(
    const CNA_Handle textureHandle,
    const CNA_TextureCubeTransfer* const transfer,
    const CNA_Color* const data,
    const uint64_t dataCapacity)
{
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
}

[[nodiscard]] CNA_Result TexturecubeGetData(
    const CNA_Handle textureHandle,
    const CNA_TextureCubeTransfer* const transfer,
    CNA_Color* const destination,
    const uint64_t destinationCapacity,
    uint64_t* const outRequiredElements)
{
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
}

} // namespace

// The routes generated from modules/c-api/spec/TextureVolume.toml.

namespace {
namespace PV = Microsoft::Xna::Framework::Graphics::PackedVector;
using Microsoft::Xna::Framework::Vector2;
using Microsoft::Xna::Framework::Vector4;
using Microsoft::Xna::Framework::Graphics::Texture;
using CNA::C::Detail::Overflow;
using CNA::C::Detail::ToNative;
using CNA::C::Detail::ToC;
template<size_t N> struct RawTransferElement { uint8_t bytes[N]; };

uint64_t TransferElementSize(CNA_TextureDataType type, uint32_t rawSize)
{
    if (type == CNA_TEXTURE_DATA_RAW_EXT) return rawSize;
    if (rawSize != 0 || type > CNA_TEXTURE_DATA_USHORT) return 0;
    switch (type) {
    case CNA_TEXTURE_DATA_BYTE: case CNA_TEXTURE_DATA_ALPHA8: return 1;
    case CNA_TEXTURE_DATA_BGR565: case CNA_TEXTURE_DATA_BGRA5551: case CNA_TEXTURE_DATA_BGRA4444:
    case CNA_TEXTURE_DATA_NORMALIZED_BYTE2: case CNA_TEXTURE_DATA_HALF_SINGLE:
    case CNA_TEXTURE_DATA_USHORT: return 2;
    case CNA_TEXTURE_DATA_VECTOR2: case CNA_TEXTURE_DATA_HALF_VECTOR4: case CNA_TEXTURE_DATA_RGBA64: return 8;
    case CNA_TEXTURE_DATA_VECTOR4: return 16;
    default: return 4;
    }
}
CNA_Result TypedTransferWindow(uint64_t start, uint64_t count, uint64_t capacity, uint64_t width,
    const void* data, uint64_t required)
{
    if (width == 0 || width > 16 || (width & (width - 1)) != 0)
        return InvalidArgument("Invalid typed texture element size.");
    if (start > INT32_MAX || count > INT32_MAX || start + count > INT32_MAX ||
        capacity > std::numeric_limits<size_t>::max() / width)
        return Overflow("Typed texture window cannot fit the native/host range.");
    if (count < required) return InvalidArgument("Typed texture count is smaller than its region.");
    if (data == nullptr && capacity != 0) return InvalidArgument("Null typed texture array.");
    if (start + count > capacity) return BufferTooSmall("Typed texture array is too small.");
    if (data == nullptr) return InvalidArgument("Null typed texture array.");
    return CNA_RESULT_SUCCESS;
}
template<typename Native, typename Wire, typename Call>
void TransferNativeValues(bool setting, void* data, uint64_t start, uint64_t required, Call&& call)
{
    std::vector<Native> values(static_cast<size_t>(required));
    auto* bytes = static_cast<uint8_t*>(data) + static_cast<size_t>(start) * sizeof(Wire);
    if (setting) {
        for (size_t i = 0; i < values.size(); ++i) {
            Wire wire{};
            std::memcpy(&wire, bytes + i * sizeof(Wire), sizeof(Wire));
            if constexpr (requires(Native n) { n.setPackedValueProperty(wire); })
                values[i].setPackedValueProperty(wire);
            else if constexpr (std::same_as<Native, Color> || std::same_as<Native, Vector2> || std::same_as<Native, Vector4>)
                values[i] = ToNative(wire);
            else values[i] = wire;
        }
    }
    call(values.data(), static_cast<int>(required));
    if (!setting) {
        for (size_t i = 0; i < values.size(); ++i) {
            Wire wire{};
            if constexpr (std::is_integral_v<Wire> && requires(Native n) { n.getPackedValueProperty(); })
                wire = values[i].getPackedValueProperty();
            else if constexpr (std::same_as<Native, Color> || std::same_as<Native, Vector2> || std::same_as<Native, Vector4>)
                wire = ToC(values[i]);
            else wire = values[i];
            std::memcpy(bytes + i * sizeof(Wire), &wire, sizeof(Wire));
        }
    }
}
template<typename Call>
void DispatchTextureTransfer(CNA_TextureDataType type, uint32_t rawSize,
    bool setting, void* data, uint64_t start, uint64_t required, Call&& call)
{
#define TRANSFER_CASE(identity, native, wire) case identity: TransferNativeValues<native, wire>(setting, data, start, required, call); break
    switch (type) {
    TRANSFER_CASE(CNA_TEXTURE_DATA_COLOR, Color, CNA_Color);
    TRANSFER_CASE(CNA_TEXTURE_DATA_BGR565, PV::Bgr565, uint16_t);
    TRANSFER_CASE(CNA_TEXTURE_DATA_BGRA5551, PV::Bgra5551, uint16_t);
    TRANSFER_CASE(CNA_TEXTURE_DATA_BGRA4444, PV::Bgra4444, uint16_t);
    TRANSFER_CASE(CNA_TEXTURE_DATA_BYTE, uint8_t, uint8_t);
    TRANSFER_CASE(CNA_TEXTURE_DATA_NORMALIZED_BYTE2, PV::NormalizedByte2, uint16_t);
    TRANSFER_CASE(CNA_TEXTURE_DATA_NORMALIZED_BYTE4, PV::NormalizedByte4, uint32_t);
    TRANSFER_CASE(CNA_TEXTURE_DATA_RGBA1010102, PV::Rgba1010102, uint32_t);
    TRANSFER_CASE(CNA_TEXTURE_DATA_RG32, PV::Rg32, uint32_t);
    TRANSFER_CASE(CNA_TEXTURE_DATA_RGBA64, PV::Rgba64, uint64_t);
    TRANSFER_CASE(CNA_TEXTURE_DATA_ALPHA8, PV::Alpha8, uint8_t);
    TRANSFER_CASE(CNA_TEXTURE_DATA_SINGLE, float, float);
    TRANSFER_CASE(CNA_TEXTURE_DATA_VECTOR2, Vector2, CNA_Vector2);
    TRANSFER_CASE(CNA_TEXTURE_DATA_VECTOR4, Vector4, CNA_Vector4);
    TRANSFER_CASE(CNA_TEXTURE_DATA_HALF_SINGLE, PV::HalfSingle, uint16_t);
    TRANSFER_CASE(CNA_TEXTURE_DATA_HALF_VECTOR2, PV::HalfVector2, uint32_t);
    TRANSFER_CASE(CNA_TEXTURE_DATA_HALF_VECTOR4, PV::HalfVector4, uint64_t);
    TRANSFER_CASE(CNA_TEXTURE_DATA_USHORT, uint16_t, uint16_t);
    default:
        switch (rawSize) {
        TRANSFER_CASE(1, RawTransferElement<1>, RawTransferElement<1>);
        TRANSFER_CASE(2, RawTransferElement<2>, RawTransferElement<2>);
        TRANSFER_CASE(4, RawTransferElement<4>, RawTransferElement<4>);
        TRANSFER_CASE(8, RawTransferElement<8>, RawTransferElement<8>);
        TRANSFER_CASE(16, RawTransferElement<16>, RawTransferElement<16>);
        }
    }
#undef TRANSFER_CASE
}
CNA_Result Texture3dTypedTransfer(CNA_Handle handle, CNA_TextureDataType type, uint32_t rawSize,
    const CNA_Texture3DTransfer* transfer, void* data, uint64_t capacity, bool setting,
    uint64_t* requiredOutput)
{
    if (!IsVersionedStructure(transfer)) return InvalidArgument("Invalid volume transfer descriptor.");
    if (transfer->start_index > INT32_MAX || transfer->element_count > INT32_MAX)
        return Overflow("Volume transfer window exceeds native range.");
    std::shared_ptr<Texture3DResource> texture;
    if (auto result = GetOwnedTexture3D(handle, &texture); result != CNA_RESULT_SUCCESS) return result;
    Texture3DTransferView view{};
    if (auto result = ValidateTexture3DTransfer(*texture->value, transfer, false, &view); result != CNA_RESULT_SUCCESS) return result;
    const auto width = TransferElementSize(type, rawSize);
    const auto format = texture->value->getFormatProperty();
    const uint64_t formatWidth = Texture::GetFormatSizeEXT(format);
    if (width == 0 || width > formatWidth || formatWidth % width != 0 || Texture::GetBlockSizeSquaredEXT(format) != 1)
        return InvalidArgument("Element width does not divide uncompressed volume format.");
    const uint64_t required = view.requiredElements * formatWidth / width;
    if (required > INT32_MAX) return Overflow("Volume transfer region exceeds native range.");
    if (requiredOutput != nullptr) *requiredOutput = required;
    if (auto result = TypedTransferWindow(transfer->start_index, transfer->element_count, capacity, width, data, required); result != CNA_RESULT_SUCCESS) return result;
    // The native typed path requires an exact box byte count; don't hide oversized caller windows.
    if (transfer->element_count != required) return InvalidArgument("Volume transfer count must exactly match the box.");
    DispatchTextureTransfer(type, rawSize, setting, data, transfer->start_index, required,
        [&](auto* values, int count) {
            if (setting) texture->value->SetData(view.level, view.left, view.top, view.right,
                view.bottom, view.front, view.back, values, 0, count);
            else texture->value->GetData(view.level, view.left, view.top, view.right,
                view.bottom, view.front, view.back, values, 0, count);
        });
    return CNA_RESULT_SUCCESS;
}
CNA_Result TexturecubeTypedTransfer(CNA_Handle handle, CNA_TextureDataType type, uint32_t rawSize,
    const CNA_TextureCubeTransfer* transfer, void* data, uint64_t capacity, bool setting,
    uint64_t* requiredOutput)
{
    if (!IsVersionedStructure(transfer)) return InvalidArgument("Invalid cube transfer descriptor.");
    if (transfer->start_index > INT32_MAX || transfer->element_count > INT32_MAX)
        return Overflow("Cube transfer window exceeds native range.");
    TextureCubeResourceView texture;
    if (auto result = GetOwnedTextureCube(handle, &texture); result != CNA_RESULT_SUCCESS) return result;
    // Validate region using a count sufficient for shape validation; typed size is computed next.
    auto shape = *transfer;
    shape.start_index = 0;
    shape.element_count = INT32_MAX;
    TextureCubeTransferView view{};
    if (auto result = ValidateTextureCubeTransfer(*texture.value, &shape, &view); result != CNA_RESULT_SUCCESS) return result;
    const auto width = TransferElementSize(type, rawSize);
    const auto format = texture.value->getFormatProperty();
    const uint64_t formatWidth = Texture::GetFormatSizeEXT(format);
    const bool compressed = Texture::GetBlockSizeSquaredEXT(format) != 1;
    if (width == 0 || (compressed ? type != CNA_TEXTURE_DATA_BYTE :
        (width > formatWidth || formatWidth % width != 0)))
        return InvalidArgument("Invalid element width for cube format.");
    const uint64_t required = compressed ?
        ((static_cast<uint64_t>(view.rectangle.Width) + 3) / 4) *
        ((static_cast<uint64_t>(view.rectangle.Height) + 3) / 4) * formatWidth :
        view.requiredElements * formatWidth / width;
    if (required > INT32_MAX) return Overflow("Cube transfer region exceeds native range.");
    if (requiredOutput != nullptr) *requiredOutput = required;
    if (auto result = TypedTransferWindow(transfer->start_index, transfer->element_count, capacity, width, data, required); result != CNA_RESULT_SUCCESS) return result;
    if (transfer->element_count != required) return InvalidArgument("Cube transfer count must exactly match its region.");
    DispatchTextureTransfer(type, rawSize, setting, data, transfer->start_index, required,
        [&](auto* values, int count) {
            if (setting) texture.value->SetData(view.face, view.level,
                view.hasRectangle ? &view.rectangle : nullptr, values, 0, count);
            else texture.value->GetData(view.face, view.level,
                view.hasRectangle ? &view.rectangle : nullptr, values, 0, count);
        });
    return CNA_RESULT_SUCCESS;
}
} // namespace

#include "generated/TextureVolume.inc"
