// SPDX-License-Identifier: MS-PL

#include "CNA/C/texture.h"
#include "CnaCApiDetail.hpp"
#include "CnaCApiGraphicsDetail.hpp"
#include "CnaCApiRuntimeDetail.hpp"

#include "Microsoft/Xna/Framework/Color.hpp"
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
#include "Microsoft/Xna/Framework/Graphics/Texture.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "System/IO/MemoryStream.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace CNA::C::Detail {

bool IsTexture2DFormatSupportedByBuild(const uint32_t format) noexcept
{
#if 1
    return format == CNA_SURFACE_FORMAT_COLOR;
#endif
}

} // namespace CNA::C::Detail

namespace {

using CNA::C::Detail::BorrowedGraphicsDevice;
using CNA::C::Detail::CallWithExceptionBarrier;
using CNA::C::Detail::CopyStringView;
using CNA::C::Detail::CreateOwnedTexture2D;
using CNA::C::Detail::CreateStandaloneTexture2D;
using CNA::C::Detail::ErrorCategoryForResult;
using CNA::C::Detail::Fail;
using CNA::C::Detail::GetBorrowedGraphicsDevice;
using CNA::C::Detail::GetOwnedTexture;
using CNA::C::Detail::GetOwnedTexture2D;
using CNA::C::Detail::TextureResourceView;
using CNA::C::Detail::Texture2DResource;
using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Rectangle;
using Microsoft::Xna::Framework::Vector2;
using Microsoft::Xna::Framework::Vector4;
using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
using Microsoft::Xna::Framework::Graphics::Texture;
using Microsoft::Xna::Framework::Graphics::Texture2D;
namespace PackedVector = Microsoft::Xna::Framework::Graphics::PackedVector;

constexpr uint32_t StructureVersion = UINT32_C(1);

struct TransferView final {
    int level;
    Rectangle rectangle;
    const Rectangle* rectanglePointer;
    int startIndex;
    int elementCount;
    uint64_t requiredElements;
};

[[nodiscard]] bool IsSurfaceFormat(const uint32_t format) noexcept
{
    return format <= CNA_SURFACE_FORMAT_USHORT_EXT;
}

[[nodiscard]] bool IsDataType(const CNA_TextureDataType dataType) noexcept
{
    return dataType <= CNA_TEXTURE_DATA_USHORT;
}

[[nodiscard]] bool IsImageFormat(const CNA_TextureImageFormat format) noexcept
{
    return format == CNA_TEXTURE_IMAGE_FORMAT_PNG ||
        format == CNA_TEXTURE_IMAGE_FORMAT_JPEG;
}

[[nodiscard]] bool IsCompressedFormat(const SurfaceFormat format) noexcept
{
    return Texture::GetBlockSizeSquaredEXT(format) != 1;
}

// Its only caller is the SDL_RENDERER mip-level refusal below, so every other renderer compiles
// this without using it.
[[nodiscard, maybe_unused]] bool IsTransferTypeCompatible(
    const SurfaceFormat format,
    const CNA_TextureDataType dataType) noexcept
{
    switch (dataType) {
        case CNA_TEXTURE_DATA_COLOR:
#if 1
            return Texture::GetFormatSizeEXT(format) % 4 == 0;
#endif
        case CNA_TEXTURE_DATA_BGR565: return format == SurfaceFormat::Bgr565;
        case CNA_TEXTURE_DATA_BGRA5551: return format == SurfaceFormat::Bgra5551;
        case CNA_TEXTURE_DATA_BGRA4444: return format == SurfaceFormat::Bgra4444;
        case CNA_TEXTURE_DATA_BYTE:
#if 1
            return format == SurfaceFormat::ByteEXT;
#endif
        case CNA_TEXTURE_DATA_NORMALIZED_BYTE2:
            return format == SurfaceFormat::NormalizedByte2;
        case CNA_TEXTURE_DATA_NORMALIZED_BYTE4:
            return format == SurfaceFormat::NormalizedByte4;
        case CNA_TEXTURE_DATA_RGBA1010102: return format == SurfaceFormat::Rgba1010102;
        case CNA_TEXTURE_DATA_RG32: return format == SurfaceFormat::Rg32;
        case CNA_TEXTURE_DATA_RGBA64: return format == SurfaceFormat::Rgba64;
        case CNA_TEXTURE_DATA_ALPHA8: return format == SurfaceFormat::Alpha8;
        case CNA_TEXTURE_DATA_SINGLE: return format == SurfaceFormat::Single;
        case CNA_TEXTURE_DATA_VECTOR2: return format == SurfaceFormat::Vector2;
        case CNA_TEXTURE_DATA_VECTOR4: return format == SurfaceFormat::Vector4;
        case CNA_TEXTURE_DATA_HALF_SINGLE: return format == SurfaceFormat::HalfSingle;
        case CNA_TEXTURE_DATA_HALF_VECTOR2: return format == SurfaceFormat::HalfVector2;
        case CNA_TEXTURE_DATA_HALF_VECTOR4:
            return format == SurfaceFormat::HalfVector4 ||
                format == SurfaceFormat::HdrBlendable;
        case CNA_TEXTURE_DATA_USHORT: return format == SurfaceFormat::UShortEXT;
        default: return false;
    }
}

[[nodiscard]] CNA_Result InvalidArgument(const char* const message)
{
    return Fail(CNA_RESULT_INVALID_ARGUMENT, CNA_ERROR_CATEGORY_ARGUMENT, message);
}

[[nodiscard]] CNA_Result ValidateDimensions(
    const uint32_t width,
    const uint32_t height,
    uint64_t* const outPixelCount)
{
    if (outPixelCount == nullptr || width == 0U || height == 0U) {
        return InvalidArgument("Texture2D dimensions must be positive.");
    }
    const uint64_t count = static_cast<uint64_t>(width) * height;
    if (width > static_cast<uint32_t>(std::numeric_limits<int>::max()) ||
        height > static_cast<uint32_t>(std::numeric_limits<int>::max()) ||
        count > static_cast<uint64_t>(std::numeric_limits<int>::max())) {
        return Fail(
            CNA_RESULT_OVERFLOW,
            CNA_ERROR_CATEGORY_RANGE,
            "Texture2D dimensions exceed the native element range.");
    }
    *outPixelCount = count;
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result ValidateTransfer(
    const Texture2D& texture,
    const CNA_TextureDataType dataType,
    const CNA_Texture2DTransfer* const transfer,
    TransferView* const outView)
{
    if (outView == nullptr || transfer == nullptr ||
        transfer->struct_size < sizeof(CNA_Texture2DTransfer) ||
        transfer->struct_version != StructureVersion || !IsDataType(dataType) ||
        (transfer->has_rectangle != CNA_FALSE && transfer->has_rectangle != CNA_TRUE) ||
        transfer->reserved[0] != 0U || transfer->reserved[1] != 0U ||
        transfer->reserved[2] != 0U || transfer->level < 0 ||
        transfer->start_index > static_cast<uint64_t>(std::numeric_limits<int>::max()) ||
        transfer->element_count > static_cast<uint64_t>(std::numeric_limits<int>::max()) ||
        transfer->start_index + transfer->element_count >
            static_cast<uint64_t>(std::numeric_limits<int>::max())) {
        return InvalidArgument("The Texture2D transfer descriptor is invalid.");
    }
    if (transfer->level >= texture.getLevelCountProperty()) {
        return InvalidArgument("The Texture2D mip level is outside the allocated chain.");
    }

    const int levelWidth = std::max(1, texture.getWidthProperty() >> transfer->level);
    const int levelHeight = std::max(1, texture.getHeightProperty() >> transfer->level);
    Rectangle rectangle(0, 0, levelWidth, levelHeight);
    if (transfer->has_rectangle == CNA_TRUE) {
        const CNA_Rectangle& requested = transfer->rectangle;
        if (requested.x < 0 || requested.y < 0 || requested.width <= 0 ||
            requested.height <= 0 || requested.x > levelWidth - requested.width ||
            requested.y > levelHeight - requested.height) {
            return InvalidArgument("The Texture2D transfer rectangle is outside the mip level.");
        }
        rectangle = Rectangle(
            requested.x, requested.y, requested.width, requested.height);
    }

    uint64_t required = 0U;
    if (dataType == CNA_TEXTURE_DATA_BYTE &&
        IsCompressedFormat(texture.getFormatProperty())) {
        const uint64_t blockColumns =
            (static_cast<uint64_t>(rectangle.Width) + 3U) / 4U;
        const uint64_t blockRows =
            (static_cast<uint64_t>(rectangle.Height) + 3U) / 4U;
        required = blockColumns * blockRows * static_cast<uint64_t>(
            Texture::GetFormatSizeEXT(texture.getFormatProperty()));
    } else {
        required = static_cast<uint64_t>(rectangle.Width) * rectangle.Height;
    }
    if (transfer->element_count < required) {
        return InvalidArgument("The Texture2D element count is smaller than the requested region.");
    }
    *outView = TransferView{
        transfer->level,
        rectangle,
        nullptr,
        static_cast<int>(transfer->start_index),
        static_cast<int>(transfer->element_count),
        required};
    outView->rectanglePointer = transfer->has_rectangle == CNA_TRUE
        ? &outView->rectangle : nullptr;
    return CNA_RESULT_SUCCESS;
}

template<typename TRaw>
[[nodiscard]] TRaw LoadRaw(const void* const data, const uint64_t index)
{
    TRaw result{};
    std::memcpy(
        &result,
        static_cast<const uint8_t*>(data) + index * sizeof(TRaw),
        sizeof(TRaw));
    return result;
}

template<typename TRaw>
void StoreRaw(void* const destination, const uint64_t index, const TRaw value)
{
    std::memcpy(
        static_cast<uint8_t*>(destination) + index * sizeof(TRaw),
        &value,
        sizeof(TRaw));
}

template<typename TNative, typename TRaw>
void SetPackedData(
    Texture2D& texture,
    const TransferView& transfer,
    const void* const data)
{
    const uint64_t total = static_cast<uint64_t>(transfer.startIndex) + transfer.elementCount;
    std::vector<TNative> native(static_cast<std::size_t>(total));
    const uint64_t end =
        static_cast<uint64_t>(transfer.startIndex) + transfer.requiredElements;
    for (uint64_t index = static_cast<uint64_t>(transfer.startIndex); index < end; ++index) {
        native[index].setPackedValueProperty(LoadRaw<TRaw>(data, index));
    }
    texture.SetData(
        transfer.level,
        transfer.rectanglePointer,
        native.data(),
        transfer.startIndex,
        transfer.elementCount);
}

template<typename TNative, typename TRaw>
void GetPackedData(
    const Texture2D& texture,
    const TransferView& transfer,
    void* const destination)
{
    const uint64_t total = static_cast<uint64_t>(transfer.startIndex) + transfer.elementCount;
    std::vector<TNative> native(static_cast<std::size_t>(total));
    texture.GetData(
        transfer.level,
        transfer.rectanglePointer,
        native.data(),
        transfer.startIndex,
        transfer.elementCount);
    const uint64_t end = static_cast<uint64_t>(transfer.startIndex) + transfer.requiredElements;
    for (uint64_t index = static_cast<uint64_t>(transfer.startIndex); index < end; ++index) {
        StoreRaw<TRaw>(destination, index, native[index].getPackedValueProperty());
    }
}

template<typename TNative>
void SetScalarData(
    Texture2D& texture,
    const TransferView& transfer,
    const void* const data)
{
    const uint64_t total = static_cast<uint64_t>(transfer.startIndex) + transfer.elementCount;
    std::vector<TNative> native(static_cast<std::size_t>(total));
    const uint64_t start = static_cast<uint64_t>(transfer.startIndex);
    std::memcpy(
        native.data() + transfer.startIndex,
        static_cast<const uint8_t*>(data) + start * sizeof(TNative),
        static_cast<std::size_t>(transfer.requiredElements) * sizeof(TNative));
    texture.SetData(
        transfer.level,
        transfer.rectanglePointer,
        native.data(),
        transfer.startIndex,
        transfer.elementCount);
}

template<typename TNative>
void GetScalarData(
    const Texture2D& texture,
    const TransferView& transfer,
    void* const destination)
{
    const uint64_t total = static_cast<uint64_t>(transfer.startIndex) + transfer.elementCount;
    std::vector<TNative> native(static_cast<std::size_t>(total));
    texture.GetData(
        transfer.level,
        transfer.rectanglePointer,
        native.data(),
        transfer.startIndex,
        transfer.elementCount);
    const uint64_t start = static_cast<uint64_t>(transfer.startIndex);
    std::memcpy(
        static_cast<uint8_t*>(destination) + start * sizeof(TNative),
        native.data() + transfer.startIndex,
        static_cast<std::size_t>(transfer.requiredElements) * sizeof(TNative));
}

void SetColorData(
    Texture2D& texture,
    const TransferView& transfer,
    const void* const data)
{
    const uint64_t total = static_cast<uint64_t>(transfer.startIndex) + transfer.elementCount;
    const Color zero(
        static_cast<uint8_t>(0U),
        static_cast<uint8_t>(0U),
        static_cast<uint8_t>(0U),
        static_cast<uint8_t>(0U));
    std::vector<Color> native(static_cast<std::size_t>(total), zero);
    const uint64_t end =
        static_cast<uint64_t>(transfer.startIndex) + transfer.requiredElements;
    for (uint64_t index = static_cast<uint64_t>(transfer.startIndex); index < end; ++index) {
        const CNA_Color value = LoadRaw<CNA_Color>(data, index);
        native[index] = Color(value.r, value.g, value.b, value.a);
    }
    texture.SetData(
        transfer.level,
        transfer.rectanglePointer,
        native.data(),
        transfer.startIndex,
        transfer.elementCount);
}

void GetColorData(
    const Texture2D& texture,
    const TransferView& transfer,
    void* const destination)
{
    const uint64_t total = static_cast<uint64_t>(transfer.startIndex) + transfer.elementCount;
    std::vector<Color> native;
    native.reserve(static_cast<std::size_t>(total));
    for (uint64_t index = 0U; index < total; ++index) {
        native.emplace_back(
            static_cast<uint8_t>(0U),
            static_cast<uint8_t>(0U),
            static_cast<uint8_t>(0U),
            static_cast<uint8_t>(0U));
    }
    texture.GetData(
        transfer.level,
        transfer.rectanglePointer,
        native.data(),
        transfer.startIndex,
        transfer.elementCount);
    const uint64_t end = static_cast<uint64_t>(transfer.startIndex) + transfer.requiredElements;
    for (uint64_t index = static_cast<uint64_t>(transfer.startIndex); index < end; ++index) {
        const Color& value = native[index];
        StoreRaw<CNA_Color>(
            destination,
            index,
            CNA_Color{
                value.getRProperty(), value.getGProperty(),
                value.getBProperty(), value.getAProperty()});
    }
}

void SetVector2Data(
    Texture2D& texture,
    const TransferView& transfer,
    const void* const data)
{
    const uint64_t total = static_cast<uint64_t>(transfer.startIndex) + transfer.elementCount;
    std::vector<Vector2> native(static_cast<std::size_t>(total));
    const uint64_t end =
        static_cast<uint64_t>(transfer.startIndex) + transfer.requiredElements;
    for (uint64_t index = static_cast<uint64_t>(transfer.startIndex); index < end; ++index) {
        const CNA_Vector2 value = LoadRaw<CNA_Vector2>(data, index);
        native[index] = Vector2(value.x, value.y);
    }
    texture.SetData(
        transfer.level, transfer.rectanglePointer, native.data(),
        transfer.startIndex, transfer.elementCount);
}

void GetVector2Data(
    const Texture2D& texture,
    const TransferView& transfer,
    void* const destination)
{
    const uint64_t total = static_cast<uint64_t>(transfer.startIndex) + transfer.elementCount;
    std::vector<Vector2> native(static_cast<std::size_t>(total));
    texture.GetData(
        transfer.level, transfer.rectanglePointer, native.data(),
        transfer.startIndex, transfer.elementCount);
    const uint64_t end = static_cast<uint64_t>(transfer.startIndex) + transfer.requiredElements;
    for (uint64_t index = static_cast<uint64_t>(transfer.startIndex); index < end; ++index) {
        StoreRaw<CNA_Vector2>(destination, index, CNA_Vector2{native[index].X, native[index].Y});
    }
}

void SetVector4Data(
    Texture2D& texture,
    const TransferView& transfer,
    const void* const data)
{
    const uint64_t total = static_cast<uint64_t>(transfer.startIndex) + transfer.elementCount;
    std::vector<Vector4> native(static_cast<std::size_t>(total));
    const uint64_t end =
        static_cast<uint64_t>(transfer.startIndex) + transfer.requiredElements;
    for (uint64_t index = static_cast<uint64_t>(transfer.startIndex); index < end; ++index) {
        const CNA_Vector4 value = LoadRaw<CNA_Vector4>(data, index);
        native[index] = Vector4(value.x, value.y, value.z, value.w);
    }
    texture.SetData(
        transfer.level, transfer.rectanglePointer, native.data(),
        transfer.startIndex, transfer.elementCount);
}

void GetVector4Data(
    const Texture2D& texture,
    const TransferView& transfer,
    void* const destination)
{
    const uint64_t total = static_cast<uint64_t>(transfer.startIndex) + transfer.elementCount;
    std::vector<Vector4> native(static_cast<std::size_t>(total));
    texture.GetData(
        transfer.level, transfer.rectanglePointer, native.data(),
        transfer.startIndex, transfer.elementCount);
    const uint64_t end = static_cast<uint64_t>(transfer.startIndex) + transfer.requiredElements;
    for (uint64_t index = static_cast<uint64_t>(transfer.startIndex); index < end; ++index) {
        StoreRaw<CNA_Vector4>(
            destination,
            index,
            CNA_Vector4{native[index].X, native[index].Y, native[index].Z, native[index].W});
    }
}

void DispatchSetData(
    Texture2D& texture,
    const CNA_TextureDataType dataType,
    const TransferView& transfer,
    const void* const data)
{
    switch (dataType) {
        case CNA_TEXTURE_DATA_COLOR: SetColorData(texture, transfer, data); break;
        case CNA_TEXTURE_DATA_BGR565:
            SetPackedData<PackedVector::Bgr565, CNA_PackedBgr565>(texture, transfer, data); break;
        case CNA_TEXTURE_DATA_BGRA5551:
            SetPackedData<PackedVector::Bgra5551, CNA_PackedBgra5551>(texture, transfer, data); break;
        case CNA_TEXTURE_DATA_BGRA4444:
            SetPackedData<PackedVector::Bgra4444, CNA_PackedBgra4444>(texture, transfer, data); break;
        case CNA_TEXTURE_DATA_BYTE: SetScalarData<uint8_t>(texture, transfer, data); break;
        case CNA_TEXTURE_DATA_NORMALIZED_BYTE2:
            SetPackedData<PackedVector::NormalizedByte2, CNA_PackedNormalizedByte2>(texture, transfer, data); break;
        case CNA_TEXTURE_DATA_NORMALIZED_BYTE4:
            SetPackedData<PackedVector::NormalizedByte4, CNA_PackedNormalizedByte4>(texture, transfer, data); break;
        case CNA_TEXTURE_DATA_RGBA1010102:
            SetPackedData<PackedVector::Rgba1010102, CNA_PackedRgba1010102>(texture, transfer, data); break;
        case CNA_TEXTURE_DATA_RG32:
            SetPackedData<PackedVector::Rg32, CNA_PackedRg32>(texture, transfer, data); break;
        case CNA_TEXTURE_DATA_RGBA64:
            SetPackedData<PackedVector::Rgba64, CNA_PackedRgba64>(texture, transfer, data); break;
        case CNA_TEXTURE_DATA_ALPHA8:
            SetPackedData<PackedVector::Alpha8, CNA_PackedAlpha8>(texture, transfer, data); break;
        case CNA_TEXTURE_DATA_SINGLE: SetScalarData<float>(texture, transfer, data); break;
        case CNA_TEXTURE_DATA_VECTOR2: SetVector2Data(texture, transfer, data); break;
        case CNA_TEXTURE_DATA_VECTOR4: SetVector4Data(texture, transfer, data); break;
        case CNA_TEXTURE_DATA_HALF_SINGLE:
            SetPackedData<PackedVector::HalfSingle, CNA_PackedHalfSingle>(texture, transfer, data); break;
        case CNA_TEXTURE_DATA_HALF_VECTOR2:
            SetPackedData<PackedVector::HalfVector2, CNA_PackedHalfVector2>(texture, transfer, data); break;
        case CNA_TEXTURE_DATA_HALF_VECTOR4:
            SetPackedData<PackedVector::HalfVector4, CNA_PackedHalfVector4>(texture, transfer, data); break;
        case CNA_TEXTURE_DATA_USHORT: SetScalarData<uint16_t>(texture, transfer, data); break;
        default: break;
    }
}

void DispatchGetData(
    const Texture2D& texture,
    const CNA_TextureDataType dataType,
    const TransferView& transfer,
    void* const destination)
{
    switch (dataType) {
        case CNA_TEXTURE_DATA_COLOR: GetColorData(texture, transfer, destination); break;
        case CNA_TEXTURE_DATA_BGR565:
            GetPackedData<PackedVector::Bgr565, CNA_PackedBgr565>(texture, transfer, destination); break;
        case CNA_TEXTURE_DATA_BGRA5551:
            GetPackedData<PackedVector::Bgra5551, CNA_PackedBgra5551>(texture, transfer, destination); break;
        case CNA_TEXTURE_DATA_BGRA4444:
            GetPackedData<PackedVector::Bgra4444, CNA_PackedBgra4444>(texture, transfer, destination); break;
        case CNA_TEXTURE_DATA_BYTE: GetScalarData<uint8_t>(texture, transfer, destination); break;
        case CNA_TEXTURE_DATA_NORMALIZED_BYTE2:
            GetPackedData<PackedVector::NormalizedByte2, CNA_PackedNormalizedByte2>(texture, transfer, destination); break;
        case CNA_TEXTURE_DATA_NORMALIZED_BYTE4:
            GetPackedData<PackedVector::NormalizedByte4, CNA_PackedNormalizedByte4>(texture, transfer, destination); break;
        case CNA_TEXTURE_DATA_RGBA1010102:
            GetPackedData<PackedVector::Rgba1010102, CNA_PackedRgba1010102>(texture, transfer, destination); break;
        case CNA_TEXTURE_DATA_RG32:
            GetPackedData<PackedVector::Rg32, CNA_PackedRg32>(texture, transfer, destination); break;
        case CNA_TEXTURE_DATA_RGBA64:
            GetPackedData<PackedVector::Rgba64, CNA_PackedRgba64>(texture, transfer, destination); break;
        case CNA_TEXTURE_DATA_ALPHA8:
            GetPackedData<PackedVector::Alpha8, CNA_PackedAlpha8>(texture, transfer, destination); break;
        case CNA_TEXTURE_DATA_SINGLE: GetScalarData<float>(texture, transfer, destination); break;
        case CNA_TEXTURE_DATA_VECTOR2: GetVector2Data(texture, transfer, destination); break;
        case CNA_TEXTURE_DATA_VECTOR4: GetVector4Data(texture, transfer, destination); break;
        case CNA_TEXTURE_DATA_HALF_SINGLE:
            GetPackedData<PackedVector::HalfSingle, CNA_PackedHalfSingle>(texture, transfer, destination); break;
        case CNA_TEXTURE_DATA_HALF_VECTOR2:
            GetPackedData<PackedVector::HalfVector2, CNA_PackedHalfVector2>(texture, transfer, destination); break;
        case CNA_TEXTURE_DATA_HALF_VECTOR4:
            GetPackedData<PackedVector::HalfVector4, CNA_PackedHalfVector4>(texture, transfer, destination); break;
        case CNA_TEXTURE_DATA_USHORT: GetScalarData<uint16_t>(texture, transfer, destination); break;
        default: break;
    }
}

[[nodiscard]] CNA_Result ValidateArrayWindow(
    const void* const data,
    const uint64_t capacity,
    const TransferView& transfer,
    const char* const message)
{
    if (data == nullptr && capacity != 0U) {
        return InvalidArgument(message);
    }
    const uint64_t requiredCapacity =
        static_cast<uint64_t>(transfer.startIndex) + transfer.elementCount;
    if (capacity < requiredCapacity) {
        return Fail(CNA_RESULT_BUFFER_TOO_SMALL, CNA_ERROR_CATEGORY_RANGE, message);
    }
    if (data == nullptr) {
        return InvalidArgument(message);
    }
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result CopyTypeName(
    const CNA_Handle textureHandle,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outByteCount)
{
    if (outByteCount == nullptr || (destination == nullptr && capacity != 0U)) {
        return InvalidArgument("The Texture2D type-name output buffer is invalid.");
    }
    std::shared_ptr<Texture2DResource> texture;
    if (const CNA_Result result = GetOwnedTexture2D(textureHandle, &texture);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    const std::string& name = texture->value->GetTypeName();
    *outByteCount = name.size();
    if (capacity < name.size()) {
        return Fail(
            CNA_RESULT_BUFFER_TOO_SMALL,
            CNA_ERROR_CATEGORY_RANGE,
            "The Texture2D type-name output buffer is too small.");
    }
    if (!name.empty()) {
        std::memcpy(destination, name.data(), name.size());
    }
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result EncodeTexture(
    const Texture2D& texture,
    const CNA_TextureImageFormat imageFormat,
    const uint32_t targetWidth,
    const uint32_t targetHeight,
    std::vector<uint8_t>* const outBytes)
{
    if (outBytes == nullptr || !IsImageFormat(imageFormat) || targetWidth == 0U ||
        targetHeight == 0U ||
        targetWidth > static_cast<uint32_t>(std::numeric_limits<int>::max()) ||
        targetHeight > static_cast<uint32_t>(std::numeric_limits<int>::max())) {
        return InvalidArgument("The Texture2D encoding configuration is invalid.");
    }
    System::IO::MemoryStream stream;
    if (imageFormat == CNA_TEXTURE_IMAGE_FORMAT_PNG) {
        texture.SaveAsPng(
            &stream, static_cast<int>(targetWidth), static_cast<int>(targetHeight));
    } else {
        texture.SaveAsJpeg(
            &stream, static_cast<int>(targetWidth), static_cast<int>(targetHeight));
    }
    *outBytes = stream.ToArray();
    return CNA_RESULT_SUCCESS;
}

} // namespace

CNA_Result cna_texture_get_info(
    const CNA_Handle textureHandle,
    CNA_TextureInfo* const outInfo)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outInfo == nullptr || outInfo->struct_size < sizeof(CNA_TextureInfo) ||
            outInfo->struct_version != StructureVersion) {
            return InvalidArgument("The texture info output structure is invalid.");
        }
        TextureResourceView texture;
        if (const CNA_Result result = GetOwnedTexture(textureHandle, &texture);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outInfo = CNA_TextureInfo{
            sizeof(CNA_TextureInfo),
            StructureVersion,
            static_cast<uint32_t>(texture.value->getLevelCountProperty()),
            static_cast<CNA_SurfaceFormat>(texture.value->getFormatProperty())};
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_texture_get_block_size_squared(
    const CNA_SurfaceFormat format,
    int32_t* const outSizeSquared)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outSizeSquared == nullptr || !IsSurfaceFormat(format)) {
            return InvalidArgument("The texture block-size query is invalid.");
        }
        *outSizeSquared = Texture::GetBlockSizeSquaredEXT(static_cast<SurfaceFormat>(format));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_texture_get_format_size(
    const CNA_SurfaceFormat format,
    int32_t* const outSize)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outSize == nullptr || !IsSurfaceFormat(format)) {
            return InvalidArgument("The texture format-size query is invalid.");
        }
        *outSize = Texture::GetFormatSizeEXT(static_cast<SurfaceFormat>(format));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_texture_get_pixel_store_alignment(
    const CNA_SurfaceFormat format,
    int32_t* const outAlignment)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outAlignment == nullptr || !IsSurfaceFormat(format)) {
            return InvalidArgument("The texture pixel-store alignment query is invalid.");
        }
        *outAlignment = Texture::GetPixelStoreAlignment(static_cast<SurfaceFormat>(format));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_texture_validate_get_data_format(
    const CNA_SurfaceFormat format,
    const int32_t elementSizeInBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (!IsSurfaceFormat(format) || elementSizeInBytes <= 0) {
            return InvalidArgument("The texture data-format validation arguments are invalid.");
        }
        Texture::ValidateGetDataFormat(
            static_cast<SurfaceFormat>(format), elementSizeInBytes);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_texture_validate_format(const CNA_SurfaceFormat format)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (!IsSurfaceFormat(format)) {
            return InvalidArgument("The texture surface-format identity is invalid.");
        }
        if (format != CNA_SURFACE_FORMAT_COLOR) {
            return Fail(
                CNA_RESULT_NOT_SUPPORTED,
                CNA_ERROR_CATEGORY_NOT_SUPPORTED,
                "The base Texture contract supports only Color on every renderer.");
        }
        Texture::ValidateFormat(SurfaceFormat::Color);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_texture2d_create_standalone(CNA_Handle* const outTexture)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outTexture == nullptr) {
            return InvalidArgument("The standalone Texture2D output handle is null.");
        }
        *outTexture = CNA_INVALID_HANDLE;
        return CreateStandaloneTexture2D(std::make_shared<Texture2D>(), outTexture);
    });
}

CNA_Result cna_texture2d_create_from_file(
    const CNA_StringView path,
    CNA_Handle* const outTexture)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outTexture == nullptr) {
            return InvalidArgument("The standalone file Texture2D output handle is null.");
        }
        *outTexture = CNA_INVALID_HANDLE;
        std::string copiedPath;
        if (const CNA_Result result = CopyStringView(path, true, &copiedPath);
            result != CNA_RESULT_SUCCESS) {
            return Fail(result, ErrorCategoryForResult(result), "The texture path is invalid UTF-8.");
        }
        return CreateStandaloneTexture2D(
            std::make_shared<Texture2D>(copiedPath), outTexture);
    });
}

CNA_Result cna_texture2d_create_from_file_with_device(
    const CNA_Handle graphicsDeviceHandle,
    const CNA_StringView path,
    CNA_Handle* const outTexture)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outTexture == nullptr) {
            return InvalidArgument("The file Texture2D output handle is null.");
        }
        *outTexture = CNA_INVALID_HANDLE;
        std::string copiedPath;
        if (const CNA_Result result = CopyStringView(path, true, &copiedPath);
            result != CNA_RESULT_SUCCESS) {
            return Fail(result, ErrorCategoryForResult(result), "The texture path is invalid UTF-8.");
        }
        std::shared_ptr<BorrowedGraphicsDevice> graphicsDevice;
        if (const CNA_Result result = GetBorrowedGraphicsDevice(
                graphicsDeviceHandle, &graphicsDevice);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CreateOwnedTexture2D(
            std::make_shared<Texture2D>(copiedPath, *graphicsDevice->value),
            graphicsDevice->parentGame,
            outTexture);
    });
}

CNA_Result cna_texture2d_create_from_rgba8(
    const CNA_Handle graphicsDeviceHandle,
    const uint32_t width,
    const uint32_t height,
    const CNA_Color* const pixels,
    const uint64_t pixelCount,
    CNA_Handle* const outTexture)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outTexture == nullptr) {
            return InvalidArgument("The RGBA8 Texture2D output handle is null.");
        }
        *outTexture = CNA_INVALID_HANDLE;
        uint64_t required = 0U;
        if (const CNA_Result result = ValidateDimensions(width, height, &required);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (pixels == nullptr || pixelCount != required) {
            return InvalidArgument("The RGBA8 Texture2D pixel array has the wrong size.");
        }
        std::shared_ptr<BorrowedGraphicsDevice> graphicsDevice;
        if (const CNA_Result result = GetBorrowedGraphicsDevice(
                graphicsDeviceHandle, &graphicsDevice);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::vector<uint8_t> rgba(static_cast<std::size_t>(required) * 4U);
        for (uint64_t index = 0U; index < required; ++index) {
            rgba[index * 4U] = pixels[index].r;
            rgba[index * 4U + 1U] = pixels[index].g;
            rgba[index * 4U + 2U] = pixels[index].b;
            rgba[index * 4U + 3U] = pixels[index].a;
        }
        auto texture = std::make_shared<Texture2D>(Texture2D::CreateFromPixels(
            *graphicsDevice->value,
            static_cast<int>(width),
            static_cast<int>(height),
            rgba));
        return CreateOwnedTexture2D(texture, graphicsDevice->parentGame, outTexture);
    });
}

CNA_Result cna_texture2d_create_cpu_only_rgba8(
    const uint32_t width,
    const uint32_t height,
    const CNA_SurfaceFormat format,
    const CNA_Color* const pixels,
    const uint64_t pixelCount,
    CNA_Handle* const outTexture)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outTexture == nullptr) {
            return InvalidArgument("The CPU-only Texture2D output handle is null.");
        }
        *outTexture = CNA_INVALID_HANDLE;
        uint64_t required = 0U;
        if (const CNA_Result result = ValidateDimensions(width, height, &required);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (!IsSurfaceFormat(format) || pixels == nullptr || pixelCount != required) {
            return InvalidArgument("The CPU-only Texture2D configuration is invalid.");
        }
        std::vector<Color> nativePixels;
        nativePixels.reserve(static_cast<std::size_t>(required));
        for (uint64_t index = 0U; index < required; ++index) {
            nativePixels.emplace_back(
                pixels[index].r, pixels[index].g, pixels[index].b, pixels[index].a);
        }
        auto texture = std::make_shared<Texture2D>(Texture2D::CreateCpuOnlyForTests(
            static_cast<int>(width),
            static_cast<int>(height),
            static_cast<SurfaceFormat>(format),
            nativePixels));
        return CreateStandaloneTexture2D(texture, outTexture);
    });
}

CNA_Result cna_texture2d_set_data_rgba8_bytes(
    const CNA_Handle textureHandle,
    const uint8_t* const rgbaBytes,
    const uint64_t pixelCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<Texture2DResource> texture;
        if (const CNA_Result result = GetOwnedTexture2D(textureHandle, &texture);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const uint64_t required =
            static_cast<uint64_t>(texture->value->getWidthProperty()) *
            static_cast<uint64_t>(texture->value->getHeightProperty());
        if (rgbaBytes == nullptr || pixelCount != required ||
            pixelCount > static_cast<uint64_t>(std::numeric_limits<int>::max())) {
            return InvalidArgument(
                "The raw RGBA8 Texture2D upload must exactly match its dimensions.");
        }
        texture->value->SetDataRGBA(rgbaBytes, static_cast<int>(pixelCount));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_texture2d_get_type_name_byte_count(
    const CNA_Handle textureHandle,
    uint64_t* const outByteCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outByteCount == nullptr) {
            return InvalidArgument("The Texture2D type-name byte-count output is null.");
        }
        std::shared_ptr<Texture2DResource> texture;
        if (const CNA_Result result = GetOwnedTexture2D(textureHandle, &texture);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outByteCount = texture->value->GetTypeName().size();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_texture2d_copy_type_name(
    const CNA_Handle textureHandle,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outByteCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        return CopyTypeName(textureHandle, destination, capacity, outByteCount);
    });
}

CNA_Result cna_texture2d_get_storage_info(
    const CNA_Handle textureHandle,
    CNA_Texture2DStorageInfo* const outInfo)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outInfo == nullptr || outInfo->struct_size < sizeof(CNA_Texture2DStorageInfo) ||
            outInfo->struct_version != StructureVersion) {
            return InvalidArgument("The Texture2D storage-info output structure is invalid.");
        }
        std::shared_ptr<Texture2DResource> texture;
        if (const CNA_Result result = GetOwnedTexture2D(textureHandle, &texture);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outInfo = CNA_Texture2DStorageInfo{
            sizeof(CNA_Texture2DStorageInfo),
            StructureVersion,
            texture->value->HasRenderer() ? CNA_TRUE : CNA_FALSE,
            texture->value->GetCpuPixelsWeak().expired() ? CNA_FALSE : CNA_TRUE,
            {0U, 0U, 0U, 0U, 0U, 0U}};
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_texture2d_set_data(
    const CNA_Handle textureHandle,
    const CNA_TextureDataType dataType,
    const CNA_Texture2DTransfer* const transfer,
    const void* const data,
    const uint64_t dataCapacity)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<Texture2DResource> texture;
        if (const CNA_Result result = GetOwnedTexture2D(textureHandle, &texture);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        TransferView view{};
        if (const CNA_Result result = ValidateTransfer(
                *texture->value, dataType, transfer, &view);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (const CNA_Result result = ValidateArrayWindow(
                data, dataCapacity, view, "The Texture2D upload array is too small or null.");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
#ifdef CNA_RENDERER_SDL_RENDERER
        if (view.level > 0) {
            if (!IsTransferTypeCompatible(texture->value->getFormatProperty(), dataType)) {
                return InvalidArgument(
                    "The Texture2D upload element type does not match its surface format.");
            }
            return Fail(
                CNA_RESULT_NOT_SUPPORTED,
                CNA_ERROR_CATEGORY_NOT_SUPPORTED,
                "SDL_RENDERER does not support mip-level Texture2D uploads.");
        }
#endif
        DispatchSetData(*texture->value, dataType, view, data);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_texture2d_get_data(
    const CNA_Handle textureHandle,
    const CNA_TextureDataType dataType,
    const CNA_Texture2DTransfer* const transfer,
    void* const destination,
    const uint64_t destinationCapacity,
    uint64_t* const outRequiredElements)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outRequiredElements == nullptr) {
            return InvalidArgument("The Texture2D required-element output is null.");
        }
        std::shared_ptr<Texture2DResource> texture;
        if (const CNA_Result result = GetOwnedTexture2D(textureHandle, &texture);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        TransferView view{};
        if (const CNA_Result result = ValidateTransfer(
                *texture->value, dataType, transfer, &view);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outRequiredElements = view.requiredElements;
        if (const CNA_Result result = ValidateArrayWindow(
                destination,
                destinationCapacity,
                view,
                "The Texture2D readback array is too small or null.");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        DispatchGetData(*texture->value, dataType, view, destination);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_texture2d_create_from_encoded_memory(
    const CNA_Handle graphicsDeviceHandle,
    const uint8_t* const encodedData,
    const uint64_t encodedByteCount,
    const CNA_Texture2DDecodeInfo* const decodeInfo,
    CNA_Handle* const outTexture)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outTexture == nullptr) {
            return InvalidArgument("The decoded Texture2D output handle is null.");
        }
        *outTexture = CNA_INVALID_HANDLE;
        if (encodedData == nullptr || encodedByteCount == 0U ||
            encodedByteCount > static_cast<uint64_t>(std::numeric_limits<int>::max())) {
            return InvalidArgument("The encoded Texture2D source buffer is invalid.");
        }
        if (decodeInfo != nullptr &&
            (decodeInfo->struct_size < sizeof(CNA_Texture2DDecodeInfo) ||
             decodeInfo->struct_version != StructureVersion || decodeInfo->width == 0U ||
             decodeInfo->height == 0U ||
             decodeInfo->width > static_cast<uint32_t>(std::numeric_limits<int>::max()) ||
             decodeInfo->height > static_cast<uint32_t>(std::numeric_limits<int>::max()) ||
             (decodeInfo->zoom != CNA_FALSE && decodeInfo->zoom != CNA_TRUE) ||
             std::any_of(
                 std::begin(decodeInfo->reserved),
                 std::end(decodeInfo->reserved),
                 [](const uint8_t value) { return value != 0U; }))) {
            return InvalidArgument("The Texture2D decode configuration is invalid.");
        }
        std::shared_ptr<BorrowedGraphicsDevice> graphicsDevice;
        if (const CNA_Result result = GetBorrowedGraphicsDevice(
                graphicsDeviceHandle, &graphicsDevice);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        System::IO::MemoryStream stream(
            encodedData, static_cast<int>(encodedByteCount), false);
        Texture2D decoded = decodeInfo == nullptr
            ? Texture2D::FromStream(*graphicsDevice->value, stream)
            : Texture2D::FromStream(
                *graphicsDevice->value,
                stream,
                static_cast<int>(decodeInfo->width),
                static_cast<int>(decodeInfo->height),
                decodeInfo->zoom == CNA_TRUE);
        return CreateOwnedTexture2D(
            std::make_shared<Texture2D>(std::move(decoded)),
            graphicsDevice->parentGame,
            outTexture);
    });
}

CNA_Result cna_texture2d_get_encoded_byte_count(
    const CNA_Handle textureHandle,
    const CNA_TextureImageFormat imageFormat,
    const uint32_t targetWidth,
    const uint32_t targetHeight,
    uint64_t* const outByteCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outByteCount == nullptr) {
            return InvalidArgument("The encoded Texture2D byte-count output is null.");
        }
        std::shared_ptr<Texture2DResource> texture;
        if (const CNA_Result result = GetOwnedTexture2D(textureHandle, &texture);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::vector<uint8_t> encoded;
        if (const CNA_Result result = EncodeTexture(
                *texture->value, imageFormat, targetWidth, targetHeight, &encoded);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outByteCount = encoded.size();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_texture2d_copy_encoded(
    const CNA_Handle textureHandle,
    const CNA_TextureImageFormat imageFormat,
    const uint32_t targetWidth,
    const uint32_t targetHeight,
    uint8_t* const destination,
    const uint64_t capacity,
    uint64_t* const outByteCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outByteCount == nullptr || (destination == nullptr && capacity != 0U)) {
            return InvalidArgument("The encoded Texture2D output buffer is invalid.");
        }
        std::shared_ptr<Texture2DResource> texture;
        if (const CNA_Result result = GetOwnedTexture2D(textureHandle, &texture);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::vector<uint8_t> encoded;
        if (const CNA_Result result = EncodeTexture(
                *texture->value, imageFormat, targetWidth, targetHeight, &encoded);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outByteCount = encoded.size();
        if (capacity < encoded.size()) {
            return Fail(
                CNA_RESULT_BUFFER_TOO_SMALL,
                CNA_ERROR_CATEGORY_RANGE,
                "The encoded Texture2D output buffer is too small.");
        }
        if (!encoded.empty()) {
            std::memcpy(destination, encoded.data(), encoded.size());
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_texture2d_save_file(
    const CNA_Handle textureHandle,
    const CNA_TextureImageFormat imageFormat,
    const CNA_StringView path)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (!IsImageFormat(imageFormat)) {
            return InvalidArgument("The encoded Texture2D image format is invalid.");
        }
        std::string copiedPath;
        if (const CNA_Result result = CopyStringView(path, true, &copiedPath);
            result != CNA_RESULT_SUCCESS) {
            return Fail(result, ErrorCategoryForResult(result), "The texture path is invalid UTF-8.");
        }
        std::shared_ptr<Texture2DResource> texture;
        if (const CNA_Result result = GetOwnedTexture2D(textureHandle, &texture);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (imageFormat == CNA_TEXTURE_IMAGE_FORMAT_PNG) {
            texture->value->SaveAsPng(copiedPath);
        } else {
            texture->value->SaveAsJpeg(copiedPath);
        }
        return CNA_RESULT_SUCCESS;
    });
}
