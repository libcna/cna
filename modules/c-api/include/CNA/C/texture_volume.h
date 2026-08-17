// SPDX-License-Identifier: MS-PL

#ifndef CNA_C_TEXTURE_VOLUME_H
#define CNA_C_TEXTURE_VOLUME_H

#include "CNA/C/render_target.h"
#include "CNA/C/texture.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Fully qualified native type name represented by a Texture3D handle. */
#define CNA_TEXTURE_3D_TYPE_NAME "Microsoft.Xna.Framework.Graphics.Texture3D"
/** @brief Fully qualified native type name represented by a TextureCube handle. */
#define CNA_TEXTURE_CUBE_TYPE_NAME "Microsoft.Xna.Framework.Graphics.TextureCube"

/** @brief Configures an owned three-dimensional texture. */
typedef struct CNA_Texture3DCreateInfo {
    /** @brief Size of this caller-provided structure in bytes. */
    uint32_t struct_size;
    /** @brief Version of this caller-provided structure. */
    uint32_t struct_version;
    /** @brief Width in texels. */
    uint32_t width;
    /** @brief Height in texels. */
    uint32_t height;
    /** @brief Depth in texels. */
    uint32_t depth;
    /** @brief Whether to allocate the complete native mip chain. */
    CNA_Bool mip_map;
    /** @brief Reserved bytes; must be zero. */
    uint8_t reserved0[3];
    /** @brief Requested surface format. */
    CNA_SurfaceFormat format;
    /** @brief Reserved for future use; must be zero. */
    uint32_t reserved1;
} CNA_Texture3DCreateInfo;

/** @brief Reports the dimensions and common texture state of a Texture3D. */
typedef struct CNA_Texture3DInfo {
    /** @brief Size of this caller-provided structure in bytes. */
    uint32_t struct_size;
    /** @brief Version of this caller-provided structure. */
    uint32_t struct_version;
    /** @brief Width in texels. */
    uint32_t width;
    /** @brief Height in texels. */
    uint32_t height;
    /** @brief Depth in texels. */
    uint32_t depth;
    /** @brief Number of allocated mip levels. */
    uint32_t level_count;
    /** @brief Surface-format identity. */
    CNA_SurfaceFormat format;
    /** @brief Reserved for future use; returned as zero. */
    uint32_t reserved;
} CNA_Texture3DInfo;

/** @brief Selects a Texture3D mip box and caller-array window. */
typedef struct CNA_Texture3DTransfer {
    /** @brief Size of this caller-provided structure in bytes. */
    uint32_t struct_size;
    /** @brief Version of this caller-provided structure. */
    uint32_t struct_version;
    /** @brief Mip level; zero is the full-size level. */
    int32_t level;
    /** @brief Inclusive left texel coordinate. */
    int32_t left;
    /** @brief Inclusive top texel coordinate. */
    int32_t top;
    /** @brief Exclusive right texel coordinate. */
    int32_t right;
    /** @brief Exclusive bottom texel coordinate. */
    int32_t bottom;
    /** @brief Inclusive front slice coordinate. */
    int32_t front;
    /** @brief Exclusive back slice coordinate. */
    int32_t back;
    /** @brief Reserved for future use; must be zero. */
    uint32_t reserved;
    /** @brief First element in the caller array. */
    uint64_t start_index;
    /** @brief Element count passed to the native overload. */
    uint64_t element_count;
} CNA_Texture3DTransfer;

/** @brief Configures an owned cube-map texture. */
typedef struct CNA_TextureCubeCreateInfo {
    /** @brief Size of this caller-provided structure in bytes. */
    uint32_t struct_size;
    /** @brief Version of this caller-provided structure. */
    uint32_t struct_version;
    /** @brief Width and height of each square face. */
    uint32_t size;
    /** @brief Whether to allocate the complete native mip chain. */
    CNA_Bool mip_map;
    /** @brief Reserved bytes; must be zero. */
    uint8_t reserved0[3];
    /** @brief Requested surface format. */
    CNA_SurfaceFormat format;
    /** @brief Reserved for future use; must be zero. */
    uint32_t reserved1;
} CNA_TextureCubeCreateInfo;

/** @brief Reports the dimensions and common texture state of a TextureCube. */
typedef struct CNA_TextureCubeInfo {
    /** @brief Size of this caller-provided structure in bytes. */
    uint32_t struct_size;
    /** @brief Version of this caller-provided structure. */
    uint32_t struct_version;
    /** @brief Width and height of each square face. */
    uint32_t size;
    /** @brief Number of allocated mip levels. */
    uint32_t level_count;
    /** @brief Surface-format identity. */
    CNA_SurfaceFormat format;
    /** @brief Reserved for future use; returned as zero. */
    uint32_t reserved;
} CNA_TextureCubeInfo;

/** @brief Selects a TextureCube face, mip rectangle and caller-array window. */
typedef struct CNA_TextureCubeTransfer {
    /** @brief Size of this caller-provided structure in bytes. */
    uint32_t struct_size;
    /** @brief Version of this caller-provided structure. */
    uint32_t struct_version;
    /** @brief Cube-map face identity. */
    CNA_CubeMapFace face;
    /** @brief Mip level; zero is the full-size face. */
    int32_t level;
    /** @brief Whether @ref rectangle selects a sub-region. */
    CNA_Bool has_rectangle;
    /** @brief Reserved bytes; must be zero. */
    uint8_t reserved0[3];
    /** @brief Texel-space rectangle when @ref has_rectangle is true. */
    CNA_Rectangle rectangle;
    /** @brief Reserved for future use; must be zero. */
    uint32_t reserved1;
    /** @brief First element in the caller array. */
    uint64_t start_index;
    /** @brief Element count passed to the native overload. */
    uint64_t element_count;
} CNA_TextureCubeTransfer;

/**
 * @brief Creates a game-owned Texture3D when the selected renderer supports volume storage.
 *
 * @param graphics_device Callback-scoped graphics-device handle.
 * @param create_info Versioned dimensions, mip and format configuration.
 * @param out_texture Receives an owned game-child handle.
 * @return A CNA result code; unsupported renderers return NOT_SUPPORTED.
 */
CNA_C_API CNA_Result cna_texture3d_create(
    CNA_Handle graphics_device,
    const CNA_Texture3DCreateInfo* create_info,
    CNA_Handle* out_texture);

/**
 * @brief Destroys an owned Texture3D handle.
 *
 * @param texture Owned Texture3D handle.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_texture3d_destroy(CNA_Handle texture);

/**
 * @brief Gets Texture3D dimensions and common texture state.
 *
 * @param texture Texture3D handle.
 * @param out_info Caller-initialized versioned output structure.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_texture3d_get_info(
    CNA_Handle texture,
    CNA_Texture3DInfo* out_info);

/**
 * @brief Gets the exact native Texture3D type-name byte count.
 *
 * @param texture Texture3D handle.
 * @param out_byte_count Receives the byte count without a terminator.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_texture3d_get_type_name_byte_count(
    CNA_Handle texture,
    uint64_t* out_byte_count);

/**
 * @brief Copies the exact native Texture3D type name without a terminator.
 *
 * @param texture Texture3D handle.
 * @param destination Destination bytes, or null only for zero capacity.
 * @param capacity Destination capacity in bytes.
 * @param out_byte_count Receives the required byte count.
 * @return A CNA result code; capacity failure performs no partial write.
 */
CNA_C_API CNA_Result cna_texture3d_copy_type_name(
    CNA_Handle texture,
    char* destination,
    uint64_t capacity,
    uint64_t* out_byte_count);

/**
 * @brief Uploads Color voxels to one Texture3D mip box.
 *
 * @param texture Texture3D handle.
 * @param transfer Versioned mip-box and source-array window.
 * @param data Caller-owned Color array copied during the call.
 * @param data_capacity Capacity measured in CNA_Color elements.
 * @return A CNA result code; unsupported storage returns NOT_SUPPORTED atomically.
 */
CNA_C_API CNA_Result cna_texture3d_set_data(
    CNA_Handle texture,
    const CNA_Texture3DTransfer* transfer,
    const CNA_Color* data,
    uint64_t data_capacity);

/**
 * @brief Reads Color voxels from one Texture3D mip box atomically.
 *
 * @param texture Texture3D handle.
 * @param transfer Versioned mip-box and destination-array window.
 * @param destination Caller-owned Color output array.
 * @param destination_capacity Capacity measured in CNA_Color elements.
 * @param out_required_elements Receives the exact box voxel count.
 * @return A CNA result code; failure leaves @p destination unchanged.
 */
CNA_C_API CNA_Result cna_texture3d_get_data(
    CNA_Handle texture,
    const CNA_Texture3DTransfer* transfer,
    CNA_Color* destination,
    uint64_t destination_capacity,
    uint64_t* out_required_elements);

/**
 * @brief Uploads tightly packed raw bytes through the native SetDataPointerEXT route.
 *
 * @param texture Texture3D handle.
 * @param transfer Versioned mip-box descriptor; start_index must be zero and element_count is ignored.
 * @param data Caller-owned raw bytes copied during the call.
 * @param data_byte_count Exact byte count for the requested volume region.
 * @return A CNA result code; Color textures require four bytes per voxel.
 */
CNA_C_API CNA_Result cna_texture3d_set_data_bytes(
    CNA_Handle texture,
    const CNA_Texture3DTransfer* transfer,
    const uint8_t* data,
    uint64_t data_byte_count);

/**
 * @brief Creates a game-owned TextureCube.
 *
 * @param graphics_device Callback-scoped graphics-device handle.
 * @param create_info Versioned size, mip and format configuration.
 * @param out_texture Receives an owned game-child handle.
 * @return A CNA result code; creation may succeed even when face storage is unavailable.
 */
CNA_C_API CNA_Result cna_texturecube_create(
    CNA_Handle graphics_device,
    const CNA_TextureCubeCreateInfo* create_info,
    CNA_Handle* out_texture);

/**
 * @brief Decodes DDS cube-map bytes through the native stream factory.
 *
 * @param graphics_device Callback-scoped graphics-device handle.
 * @param dds_data Caller-owned complete DDS payload.
 * @param dds_byte_count Payload size in bytes.
 * @param out_texture Receives an owned game-child handle.
 * @return A CNA result code; unsupported cube storage returns NOT_SUPPORTED.
 */
CNA_C_API CNA_Result cna_texturecube_create_from_dds_memory(
    CNA_Handle graphics_device,
    const uint8_t* dds_data,
    uint64_t dds_byte_count,
    CNA_Handle* out_texture);

/**
 * @brief Destroys an owned TextureCube handle but not a RenderTargetCube handle.
 *
 * @param texture Owned TextureCube handle.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_texturecube_destroy(CNA_Handle texture);

/**
 * @brief Gets TextureCube dimensions and common texture state.
 *
 * @param texture TextureCube or RenderTargetCube handle.
 * @param out_info Caller-initialized versioned output structure.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_texturecube_get_info(
    CNA_Handle texture,
    CNA_TextureCubeInfo* out_info);

/**
 * @brief Gets the exact native TextureCube-derived type-name byte count.
 *
 * @param texture TextureCube or RenderTargetCube handle.
 * @param out_byte_count Receives the byte count without a terminator.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_texturecube_get_type_name_byte_count(
    CNA_Handle texture,
    uint64_t* out_byte_count);

/**
 * @brief Copies the exact native TextureCube-derived type name without a terminator.
 *
 * @param texture TextureCube or RenderTargetCube handle.
 * @param destination Destination bytes, or null only for zero capacity.
 * @param capacity Destination capacity in bytes.
 * @param out_byte_count Receives the required byte count.
 * @return A CNA result code; capacity failure performs no partial write.
 */
CNA_C_API CNA_Result cna_texturecube_copy_type_name(
    CNA_Handle texture,
    char* destination,
    uint64_t capacity,
    uint64_t* out_byte_count);

/**
 * @brief Uploads Color texels to one TextureCube face/mip rectangle.
 *
 * @param texture TextureCube or RenderTargetCube handle.
 * @param transfer Versioned face, mip, rectangle and source-array window.
 * @param data Caller-owned Color array copied during the call.
 * @param data_capacity Capacity measured in CNA_Color elements.
 * @return A CNA result code; unsupported storage returns NOT_SUPPORTED atomically.
 */
CNA_C_API CNA_Result cna_texturecube_set_data(
    CNA_Handle texture,
    const CNA_TextureCubeTransfer* transfer,
    const CNA_Color* data,
    uint64_t data_capacity);

/**
 * @brief Reads Color texels from one TextureCube face/mip rectangle atomically.
 *
 * @param texture TextureCube or RenderTargetCube handle.
 * @param transfer Versioned face, mip, rectangle and destination-array window.
 * @param destination Caller-owned Color output array.
 * @param destination_capacity Capacity measured in CNA_Color elements.
 * @param out_required_elements Receives the exact rectangle texel count.
 * @return A CNA result code; failure leaves @p destination unchanged.
 */
CNA_C_API CNA_Result cna_texturecube_get_data(
    CNA_Handle texture,
    const CNA_TextureCubeTransfer* transfer,
    CNA_Color* destination,
    uint64_t destination_capacity,
    uint64_t* out_required_elements);

#ifdef __cplusplus
}
#endif

#endif
