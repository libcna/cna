// SPDX-License-Identifier: MS-PL

#ifndef CNA_C_TEXTURE_H
#define CNA_C_TEXTURE_H

#include "CNA/C/graphics.h"
#include "CNA/C/math_values.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Fixed-width identity of the element representation used by a texture transfer. */
typedef uint32_t CNA_TextureDataType;

/** @brief Four unsigned RGBA8 channel bytes represented by CNA_Color. */
#define CNA_TEXTURE_DATA_COLOR UINT32_C(0)
/** @brief Raw CNA_PackedBgr565 values. */
#define CNA_TEXTURE_DATA_BGR565 UINT32_C(1)
/** @brief Raw CNA_PackedBgra5551 values. */
#define CNA_TEXTURE_DATA_BGRA5551 UINT32_C(2)
/** @brief Raw CNA_PackedBgra4444 values. */
#define CNA_TEXTURE_DATA_BGRA4444 UINT32_C(3)
/** @brief Raw bytes, including supported compressed block streams. */
#define CNA_TEXTURE_DATA_BYTE UINT32_C(4)
/** @brief Raw CNA_PackedNormalizedByte2 values. */
#define CNA_TEXTURE_DATA_NORMALIZED_BYTE2 UINT32_C(5)
/** @brief Raw CNA_PackedNormalizedByte4 values. */
#define CNA_TEXTURE_DATA_NORMALIZED_BYTE4 UINT32_C(6)
/** @brief Raw CNA_PackedRgba1010102 values. */
#define CNA_TEXTURE_DATA_RGBA1010102 UINT32_C(7)
/** @brief Raw CNA_PackedRg32 values. */
#define CNA_TEXTURE_DATA_RG32 UINT32_C(8)
/** @brief Raw CNA_PackedRgba64 values. */
#define CNA_TEXTURE_DATA_RGBA64 UINT32_C(9)
/** @brief Raw CNA_PackedAlpha8 values. */
#define CNA_TEXTURE_DATA_ALPHA8 UINT32_C(10)
/** @brief IEEE binary32 scalar values. */
#define CNA_TEXTURE_DATA_SINGLE UINT32_C(11)
/** @brief CNA_Vector2 values. */
#define CNA_TEXTURE_DATA_VECTOR2 UINT32_C(12)
/** @brief CNA_Vector4 values. */
#define CNA_TEXTURE_DATA_VECTOR4 UINT32_C(13)
/** @brief Raw CNA_PackedHalfSingle values. */
#define CNA_TEXTURE_DATA_HALF_SINGLE UINT32_C(14)
/** @brief Raw CNA_PackedHalfVector2 values. */
#define CNA_TEXTURE_DATA_HALF_VECTOR2 UINT32_C(15)
/** @brief Raw CNA_PackedHalfVector4 values. */
#define CNA_TEXTURE_DATA_HALF_VECTOR4 UINT32_C(16)
/** @brief Unsigned 16-bit scalar values. */
#define CNA_TEXTURE_DATA_USHORT UINT32_C(17)

/** @brief Fixed-width encoded image identity. */
typedef uint32_t CNA_TextureImageFormat;

/** @brief Portable Network Graphics encoding. */
#define CNA_TEXTURE_IMAGE_FORMAT_PNG UINT32_C(0)
/** @brief JPEG encoding using CNA's configured/default quality. */
#define CNA_TEXTURE_IMAGE_FORMAT_JPEG UINT32_C(1)

/** @brief Describes the common format and mip-level state of a texture handle. */
typedef struct CNA_TextureInfo {
    /** @brief Size of this caller-provided structure in bytes. */
    uint32_t struct_size;
    /** @brief Version of this caller-provided structure. */
    uint32_t struct_version;
    /** @brief Number of mipmap levels. */
    uint32_t level_count;
    /** @brief Surface-format identity. */
    CNA_SurfaceFormat format;
} CNA_TextureInfo;

/** @brief Selects one mip level, optional rectangle and caller-array window. */
typedef struct CNA_Texture2DTransfer {
    /** @brief Size of this caller-provided structure in bytes. */
    uint32_t struct_size;
    /** @brief Version of this caller-provided structure. */
    uint32_t struct_version;
    /** @brief Mip level; zero is the full-size level. */
    int32_t level;
    /** @brief Whether @ref rectangle selects a sub-region. */
    CNA_Bool has_rectangle;
    /** @brief Reserved bytes; must be zero. */
    uint8_t reserved[3];
    /** @brief Texel-space rectangle when @ref has_rectangle is true. */
    CNA_Rectangle rectangle;
    /** @brief First element in the caller array. */
    uint64_t start_index;
    /** @brief Element count passed to the native overload. */
    uint64_t element_count;
} CNA_Texture2DTransfer;

/** @brief Optional resize/crop configuration for encoded-memory decoding. */
typedef struct CNA_Texture2DDecodeInfo {
    /** @brief Size of this caller-provided structure in bytes. */
    uint32_t struct_size;
    /** @brief Version of this caller-provided structure. */
    uint32_t struct_version;
    /** @brief Requested output width in pixels. */
    uint32_t width;
    /** @brief Requested output height in pixels. */
    uint32_t height;
    /** @brief True to cover-and-crop; false to fit while preserving aspect ratio. */
    CNA_Bool zoom;
    /** @brief Reserved bytes; must be zero. */
    uint8_t reserved[7];
} CNA_Texture2DDecodeInfo;

/** @brief Reports whether a Texture2D currently retains renderer and CPU-shadow storage. */
typedef struct CNA_Texture2DStorageInfo {
    /** @brief Size of this caller-provided structure in bytes. */
    uint32_t struct_size;
    /** @brief Version of this caller-provided structure. */
    uint32_t struct_version;
    /** @brief True while a renderer resource is retained. */
    CNA_Bool has_renderer;
    /** @brief True while an authoritative CPU pixel shadow is retained. */
    CNA_Bool has_cpu_shadow;
    /** @brief Reserved bytes; returned as zero. */
    uint8_t reserved[6];
} CNA_Texture2DStorageInfo;

/**
 * @brief Gets common format and mip-level information from a supported texture handle.
 *
 * @param texture Texture2D, Texture3D, TextureCube or matching render-target handle.
 * @param out_info Caller-initialized versioned output structure.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_texture_get_info(CNA_Handle texture, CNA_TextureInfo* out_info);

/**
 * @brief Gets the square texel count represented by one compression block.
 *
 * @param format Surface format.
 * @param out_size_squared Receives 16 for block compression or 1 otherwise.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_texture_get_block_size_squared(
    CNA_SurfaceFormat format,
    int32_t* out_size_squared);

/**
 * @brief Gets bytes per compression block or uncompressed texel.
 *
 * @param format Surface format.
 * @param out_size Receives the byte size.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_texture_get_format_size(
    CNA_SurfaceFormat format,
    int32_t* out_size);

/**
 * @brief Gets the canonical pixel-store alignment for a surface format.
 *
 * @param format Surface format.
 * @param out_alignment Receives an alignment from one through eight.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_texture_get_pixel_store_alignment(
    CNA_SurfaceFormat format,
    int32_t* out_alignment);

/**
 * @brief Validates that an element size evenly divides a surface-format unit.
 *
 * @param format Surface format.
 * @param element_size_in_bytes Positive element byte size.
 * @return Success when compatible, otherwise a CNA result code.
 */
CNA_C_API CNA_Result cna_texture_validate_get_data_format(
    CNA_SurfaceFormat format,
    int32_t element_size_in_bytes);

/**
 * @brief Validates the renderer-independent base Texture format contract.
 *
 * @param format Surface format.
 * @return Success for Color, NOT_SUPPORTED for another valid format, or INVALID_ARGUMENT.
 */
CNA_C_API CNA_Result cna_texture_validate_format(CNA_SurfaceFormat format);

/**
 * @brief Creates a standalone default Texture2D with no device, dimensions or renderer.
 *
 * @param out_texture Receives an owned standalone handle.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_texture2d_create_standalone(CNA_Handle* out_texture);

/**
 * @brief Decodes an image file into a standalone CPU-backed Texture2D.
 *
 * @param path Validated UTF-8 path.
 * @param out_texture Receives an owned standalone handle.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_texture2d_create_from_file(
    CNA_StringView path,
    CNA_Handle* out_texture);

/**
 * @brief Decodes an image file into a game-owned Texture2D.
 *
 * @param graphics_device Callback-scoped graphics-device handle.
 * @param path Validated UTF-8 path.
 * @param out_texture Receives an owned game-child handle.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_texture2d_create_from_file_with_device(
    CNA_Handle graphics_device,
    CNA_StringView path,
    CNA_Handle* out_texture);

/**
 * @brief Creates and uploads a Color Texture2D in one operation.
 *
 * @param graphics_device Callback-scoped graphics-device handle.
 * @param width Positive width.
 * @param height Positive height.
 * @param pixels Exact width-times-height RGBA8 array.
 * @param pixel_count Number of pixels.
 * @param out_texture Receives an owned game-child handle.
 * @return A CNA result code; failure leaves @p out_texture invalid.
 */
CNA_C_API CNA_Result cna_texture2d_create_from_rgba8(
    CNA_Handle graphics_device,
    uint32_t width,
    uint32_t height,
    const CNA_Color* pixels,
    uint64_t pixel_count,
    CNA_Handle* out_texture);

/**
 * @brief Creates a standalone CPU-only texture for headless/test integration.
 *
 * @param width Positive width.
 * @param height Positive height.
 * @param format Reported surface format.
 * @param pixels Exact width-times-height Color array.
 * @param pixel_count Number of pixels.
 * @param out_texture Receives an owned standalone handle.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_texture2d_create_cpu_only_rgba8(
    uint32_t width,
    uint32_t height,
    CNA_SurfaceFormat format,
    const CNA_Color* pixels,
    uint64_t pixel_count,
    CNA_Handle* out_texture);

/**
 * @brief Uploads tightly packed raw RGBA8 bytes through the native SetDataRGBA extension.
 *
 * @param texture Texture2D or RenderTarget2D handle.
 * @param rgba_bytes Exact width-times-height-times-four byte array.
 * @param pixel_count Number of four-byte pixels represented by @p rgba_bytes.
 * @return A CNA result code; the count must exactly match the texture dimensions.
 */
CNA_C_API CNA_Result cna_texture2d_set_data_rgba8_bytes(
    CNA_Handle texture,
    const uint8_t* rgba_bytes,
    uint64_t pixel_count);

/**
 * @brief Gets the exact native type-name byte count.
 *
 * @param texture Texture2D or RenderTarget2D handle.
 * @param out_byte_count Receives the byte count without a terminator.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_texture2d_get_type_name_byte_count(
    CNA_Handle texture,
    uint64_t* out_byte_count);

/**
 * @brief Copies the exact native type name without a terminator.
 *
 * @param texture Texture2D or RenderTarget2D handle.
 * @param destination Destination bytes, or null only for zero capacity.
 * @param capacity Destination capacity.
 * @param out_byte_count Receives the required byte count.
 * @return A CNA result code; capacity failure performs no partial write.
 */
CNA_C_API CNA_Result cna_texture2d_copy_type_name(
    CNA_Handle texture,
    char* destination,
    uint64_t capacity,
    uint64_t* out_byte_count);

/**
 * @brief Gets renderer/CPU-shadow availability without exposing native pointers.
 *
 * @param texture Texture2D or RenderTarget2D handle.
 * @param out_info Caller-initialized versioned output structure.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_texture2d_get_storage_info(
    CNA_Handle texture,
    CNA_Texture2DStorageInfo* out_info);

/**
 * @brief Uploads one typed mip/rectangle transfer from a caller array.
 *
 * @param texture Texture2D or RenderTarget2D handle.
 * @param data_type Native overload representation.
 * @param transfer Versioned mip, rectangle and array window.
 * @param data Caller array copied during the call.
 * @param data_capacity Capacity of @p data measured in elements of @p data_type.
 * @return A CNA result code; the native overload validates type/format compatibility.
 */
CNA_C_API CNA_Result cna_texture2d_set_data(
    CNA_Handle texture,
    CNA_TextureDataType data_type,
    const CNA_Texture2DTransfer* transfer,
    const void* data,
    uint64_t data_capacity);

/**
 * @brief Reads one typed mip/rectangle transfer into a caller array atomically.
 *
 * @param texture Texture2D or RenderTarget2D handle.
 * @param data_type Native overload representation.
 * @param transfer Versioned mip, rectangle and array window.
 * @param destination Caller output array.
 * @param destination_capacity Capacity measured in elements of @p data_type.
 * @param out_required_elements Receives the exact number of elements written for the region.
 * @return A CNA result code; failure does not modify @p destination.
 */
CNA_C_API CNA_Result cna_texture2d_get_data(
    CNA_Handle texture,
    CNA_TextureDataType data_type,
    const CNA_Texture2DTransfer* transfer,
    void* destination,
    uint64_t destination_capacity,
    uint64_t* out_required_elements);

/**
 * @brief Decodes an encoded image-memory block into a game-owned Texture2D.
 *
 * @param graphics_device Callback-scoped graphics-device handle.
 * @param encoded_data Encoded PNG/JPEG/DDS or another native-supported image payload.
 * @param encoded_byte_count Payload byte count.
 * @param decode_info Optional resize/crop configuration; null preserves source dimensions.
 * @param out_texture Receives an owned game-child handle.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_texture2d_create_from_encoded_memory(
    CNA_Handle graphics_device,
    const uint8_t* encoded_data,
    uint64_t encoded_byte_count,
    const CNA_Texture2DDecodeInfo* decode_info,
    CNA_Handle* out_texture);

/**
 * @brief Gets the encoded PNG/JPEG byte count for a texture.
 *
 * @param texture Texture2D or RenderTarget2D handle.
 * @param image_format PNG or JPEG.
 * @param target_width Positive encoded width.
 * @param target_height Positive encoded height.
 * @param out_byte_count Receives encoded byte count.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_texture2d_get_encoded_byte_count(
    CNA_Handle texture,
    CNA_TextureImageFormat image_format,
    uint32_t target_width,
    uint32_t target_height,
    uint64_t* out_byte_count);

/**
 * @brief Encodes a texture as PNG/JPEG into a caller-owned byte buffer.
 *
 * @param texture Texture2D or RenderTarget2D handle.
 * @param image_format PNG or JPEG.
 * @param target_width Positive encoded width.
 * @param target_height Positive encoded height.
 * @param destination Destination bytes, or null only for zero capacity.
 * @param capacity Destination capacity in bytes.
 * @param out_byte_count Receives the required encoded byte count.
 * @return A CNA result code; insufficient capacity performs no partial write.
 */
CNA_C_API CNA_Result cna_texture2d_copy_encoded(
    CNA_Handle texture,
    CNA_TextureImageFormat image_format,
    uint32_t target_width,
    uint32_t target_height,
    uint8_t* destination,
    uint64_t capacity,
    uint64_t* out_byte_count);

/**
 * @brief Encodes a texture directly to a UTF-8 file path.
 *
 * @param texture Texture2D or RenderTarget2D handle.
 * @param image_format PNG or JPEG.
 * @param path Validated UTF-8 destination path.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_texture2d_save_file(
    CNA_Handle texture,
    CNA_TextureImageFormat image_format,
    CNA_StringView path);

#ifdef __cplusplus
}
#endif

#endif
