// SPDX-License-Identifier: MS-PL

#ifndef CNA_C_ENGINE_LAYER_H
#define CNA_C_ENGINE_LAYER_H

#include "CNA/C/effects.h"
#include "CNA/C/graphics.h"
#include "CNA/C/graphics_ext.h"
#include "CNA/C/math_values.h"
#include "CNA/C/render_target.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The foundations of CNA's CNAEXT engine layer: its version, its resources (storage buffers,
 * compute shaders, GPU timers, render-target pools, shader-effect caches), its full-screen and
 * post-process pass machinery, and the PBR material binding that moves a material onto an effect.
 *
 * Every declaration here exists in every build. The routes that need a native engine-layer object
 * return `CNA_RESULT_NOT_SUPPORTED` when the layer is absent, exactly as
 * @ref cna_graphics_ext_is_available reports. That keeps the exported ABI one shape regardless of
 * how CNA was configured, which is what makes the recorded ABI baseline meaningful; a build option
 * that changed the export list would leave the baseline describing neither build.
 *
 * The identities and the pure value operations below work in either build.
 */

/**
 * @brief Engine-layer revision this header declares.
 *
 * Compare it against @ref cna_engine_layer_get_version, which reports what the linked library was
 * built with. When the two disagree, a header and a library from different builds have been mixed.
 * This is a revision marker, not an ABI compatibility promise.
 */
#define CNA_ENGINE_LAYER_VERSION INT32_C(2)

/**
 * @brief Returns the engine-layer revision the linked library was built with.
 *
 * @param out_version Receives the revision, or `0` when this build has no engine layer.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null output.
 */
CNA_C_API CNA_Result cna_engine_layer_get_version(int32_t* out_version);

/**
 * @brief Copies the engine-layer revision as UTF-8 text, without a terminator.
 *
 * @param destination Caller-owned destination, or null only when @p capacity is zero.
 * @param capacity Destination capacity in bytes.
 * @param out_bytes Receives the required byte count without a terminator.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL`, or an argument error. No partial
 * string is written.
 */
CNA_C_API CNA_Result cna_engine_layer_copy_version_string(
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

/* ---------------------------------------------------------------------------------------------
 * Identities
 * ------------------------------------------------------------------------------------------- */

/** @brief Fixed-width identity of how the depth/normal prepass stores linear depth. */
typedef uint32_t CNA_DepthEncoding;

/** @brief Whatever the prepass decides for the active renderer. The default. */
#define CNA_DEPTH_ENCODING_AUTOMATIC UINT32_C(0)
/** @brief 32 bits of depth across an 8-bit RGBA target; needs no capability. */
#define CNA_DEPTH_ENCODING_PACKED UINT32_C(1)
/** @brief One half-float channel. Known to defeat this layer's screen-space effects. */
#define CNA_DEPTH_ENCODING_HALF_FLOAT UINT32_C(2)

/** @brief Fixed-width identity of how a compute shader may access a bound image. */
typedef uint32_t CNA_GraphicsImageAccess;

/** @brief The shader only reads the image. */
#define CNA_GRAPHICS_IMAGE_ACCESS_READ_ONLY UINT32_C(0)
/** @brief The shader only writes the image. */
#define CNA_GRAPHICS_IMAGE_ACCESS_WRITE_ONLY UINT32_C(1)
/** @brief The shader both reads and writes the image. */
#define CNA_GRAPHICS_IMAGE_ACCESS_READ_WRITE UINT32_C(2)

/**
 * @brief Fixed-width bit mask of the memory accesses a compute barrier orders.
 *
 * The members are bits, so C's own `|` and `&` combine and test them; the canonical `operator|`,
 * `operator&` and `HasBarrier` need no route of their own beyond
 * @ref cna_graphics_memory_barrier_has, which exists so the containment test has one spelling that
 * a C consumer can call rather than re-derive.
 */
typedef uint32_t CNA_GraphicsMemoryBarrier;

/** @brief Orders nothing. */
#define CNA_GRAPHICS_MEMORY_BARRIER_NONE UINT32_C(0)
/** @brief Orders vertex-attribute array reads. */
#define CNA_GRAPHICS_MEMORY_BARRIER_VERTEX_ATTRIB_ARRAY (UINT32_C(1) << 0)
/** @brief Orders element (index) array reads. */
#define CNA_GRAPHICS_MEMORY_BARRIER_ELEMENT_ARRAY (UINT32_C(1) << 1)
/** @brief Orders uniform reads. */
#define CNA_GRAPHICS_MEMORY_BARRIER_UNIFORM (UINT32_C(1) << 2)
/** @brief Orders texture fetches. */
#define CNA_GRAPHICS_MEMORY_BARRIER_TEXTURE_FETCH (UINT32_C(1) << 3)
/** @brief Orders shader image accesses. */
#define CNA_GRAPHICS_MEMORY_BARRIER_SHADER_IMAGE_ACCESS (UINT32_C(1) << 4)
/** @brief Orders shader storage-buffer accesses. */
#define CNA_GRAPHICS_MEMORY_BARRIER_SHADER_STORAGE (UINT32_C(1) << 5)
/** @brief Orders buffer updates. */
#define CNA_GRAPHICS_MEMORY_BARRIER_BUFFER_UPDATE (UINT32_C(1) << 6)
/** @brief Orders framebuffer accesses. */
#define CNA_GRAPHICS_MEMORY_BARRIER_FRAMEBUFFER (UINT32_C(1) << 7)
/** @brief Orders indirect-command reads. */
#define CNA_GRAPHICS_MEMORY_BARRIER_INDIRECT_COMMAND (UINT32_C(1) << 8)
/** @brief Every bit above, folded together. */
#define CNA_GRAPHICS_MEMORY_BARRIER_ALL                                                            \
    (CNA_GRAPHICS_MEMORY_BARRIER_VERTEX_ATTRIB_ARRAY | CNA_GRAPHICS_MEMORY_BARRIER_ELEMENT_ARRAY |  \
     CNA_GRAPHICS_MEMORY_BARRIER_UNIFORM | CNA_GRAPHICS_MEMORY_BARRIER_TEXTURE_FETCH |              \
     CNA_GRAPHICS_MEMORY_BARRIER_SHADER_IMAGE_ACCESS | CNA_GRAPHICS_MEMORY_BARRIER_SHADER_STORAGE | \
     CNA_GRAPHICS_MEMORY_BARRIER_BUFFER_UPDATE | CNA_GRAPHICS_MEMORY_BARRIER_FRAMEBUFFER |          \
     CNA_GRAPHICS_MEMORY_BARRIER_INDIRECT_COMMAND)

/**
 * @brief Reports whether a barrier mask contains every bit of another.
 *
 * @param mask The mask to test.
 * @param bit The bit or bits required.
 * @param out_contains Receives `CNA_TRUE` when every bit of @p bit is set in @p mask.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null output.
 */
CNA_C_API CNA_Result cna_graphics_memory_barrier_has(
    CNA_GraphicsMemoryBarrier mask,
    CNA_GraphicsMemoryBarrier bit,
    CNA_Bool* out_contains);

/* ---------------------------------------------------------------------------------------------
 * Storage buffers
 * ------------------------------------------------------------------------------------------- */

/**
 * @brief Owned handle for one engine-layer storage buffer.
 *
 * Release it with @ref cna_storage_buffer_destroy before destroying the game that owns its device.
 */
typedef CNA_Handle CNA_StorageBufferHandle;

/**
 * @brief Creates a storage buffer of a given size in bytes.
 *
 * @param graphics_device The device to create on.
 * @param byte_size The buffer size in bytes.
 * @param out_buffer Receives the owned handle; set invalid on failure.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_storage_buffer_create(
    CNA_Handle graphics_device,
    uint64_t byte_size,
    CNA_StorageBufferHandle* out_buffer);

/**
 * @brief Creates a storage buffer sized as a count of fixed-size elements.
 *
 * This is the C form of the canonical `StorageBufferT<T>` class template. C has no templates, so
 * the element type becomes an element *size* carried by the buffer: the buffer remembers both the
 * count and the size, which is what lets @ref cna_storage_buffer_set_elements enforce the same
 * "more elements than the buffer holds" refusal the template enforces. A caller's `T` must be
 * trivially copyable, exactly as the template's static assertion requires — bytes are what reach
 * the GPU.
 *
 * @param graphics_device The device to create on.
 * @param element_count How many elements the buffer holds.
 * @param element_byte_size The size of one element in bytes; must not be zero.
 * @param out_buffer Receives the owned handle; set invalid on failure.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_storage_buffer_create_typed(
    CNA_Handle graphics_device,
    uint64_t element_count,
    uint64_t element_byte_size,
    CNA_StorageBufferHandle* out_buffer);

/**
 * @brief Uploads bytes into the buffer.
 *
 * @param buffer The buffer.
 * @param data The source bytes; null only when @p byte_size is zero.
 * @param byte_size How many bytes to upload; must not exceed the buffer's size.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_storage_buffer_set_bytes(
    CNA_StorageBufferHandle buffer,
    const void* data,
    uint64_t byte_size);

/**
 * @brief Reads bytes back from the buffer.
 *
 * @param buffer The buffer.
 * @param destination Caller-owned destination; null only when @p byte_size is zero.
 * @param byte_size How many bytes to read; must not exceed the buffer's size.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_storage_buffer_get_bytes(
    CNA_StorageBufferHandle buffer,
    void* destination,
    uint64_t byte_size);

/**
 * @brief Returns the buffer's size in bytes.
 *
 * @param buffer The buffer.
 * @param out_byte_size Receives the size.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_storage_buffer_get_byte_size(
    CNA_StorageBufferHandle buffer,
    uint64_t* out_byte_size);

/**
 * @brief Uploads a count of elements, refusing more than the buffer holds.
 *
 * The C form of `StorageBufferT<T>::setData`. @p element_byte_size must equal the size the buffer
 * was created with; a mismatch is an argument error rather than a silent reinterpretation.
 *
 * @param buffer The buffer, created by @ref cna_storage_buffer_create_typed.
 * @param data The source elements; null only when @p element_count is zero.
 * @param element_count How many elements to upload.
 * @param element_byte_size The size of one element in bytes.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` when the count exceeds the buffer or
 * the element size disagrees, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_storage_buffer_set_elements(
    CNA_StorageBufferHandle buffer,
    const void* data,
    uint64_t element_count,
    uint64_t element_byte_size);

/**
 * @brief Reads the buffer's whole element range back.
 *
 * The C form of `StorageBufferT<T>::getData`, which returns every element the buffer holds.
 *
 * @param buffer The buffer, created by @ref cna_storage_buffer_create_typed.
 * @param destination Caller-owned destination for @p element_count elements.
 * @param element_count Must equal the buffer's element count.
 * @param element_byte_size Must equal the buffer's element size.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` on a count or size disagreement,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_storage_buffer_get_elements(
    CNA_StorageBufferHandle buffer,
    void* destination,
    uint64_t element_count,
    uint64_t element_byte_size);

/**
 * @brief Returns how many elements the buffer was created to hold.
 *
 * @param buffer The buffer.
 * @param out_element_count Receives the count; zero for a buffer created by byte size.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_storage_buffer_get_element_count(
    CNA_StorageBufferHandle buffer,
    uint64_t* out_element_count);

/**
 * @brief Returns the element size the buffer was created with.
 *
 * @param buffer The buffer.
 * @param out_element_byte_size Receives the size; zero for a buffer created by byte size.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_storage_buffer_get_element_byte_size(
    CNA_StorageBufferHandle buffer,
    uint64_t* out_element_byte_size);

/**
 * @brief Releases the buffer.
 *
 * @param buffer The buffer; an invalid handle is an error, not a silent no-op.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_storage_buffer_destroy(CNA_StorageBufferHandle buffer);

/* ---------------------------------------------------------------------------------------------
 * Compute shaders
 * ------------------------------------------------------------------------------------------- */

/**
 * @brief Owned handle for one engine-layer compute shader.
 *
 * Release it with @ref cna_compute_shader_destroy before destroying the game that owns its device.
 */
typedef CNA_Handle CNA_ComputeShaderHandle;

/**
 * @brief Compiles a compute shader from GLSL ES 3.10 source.
 *
 * Creation succeeds even when the source does not compile: ask @ref cna_compute_shader_is_valid and
 * read @ref cna_compute_shader_copy_compile_error. That mirrors the canonical class, which records
 * the failure rather than throwing, because a renderer without compute is a documented boundary
 * rather than a defect.
 *
 * @param graphics_device The device to compile on.
 * @param source The shader source as UTF-8.
 * @param out_shader Receives the owned handle; set invalid on failure.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_compute_shader_create(
    CNA_Handle graphics_device,
    CNA_StringView source,
    CNA_ComputeShaderHandle* out_shader);

/**
 * @brief Sets a signed-integer uniform.
 *
 * @param shader The shader.
 * @param name The uniform's name as UTF-8.
 * @param value The value.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_compute_shader_set_uniform_int(
    CNA_ComputeShaderHandle shader,
    CNA_StringView name,
    int32_t value);

/**
 * @brief Sets a floating-point uniform.
 *
 * @param shader The shader.
 * @param name The uniform's name as UTF-8.
 * @param value The value.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_compute_shader_set_uniform_float(
    CNA_ComputeShaderHandle shader,
    CNA_StringView name,
    float value);

/**
 * @brief Binds a storage buffer to a numbered binding point.
 *
 * The buffer is borrowed, not owned: it must outlive every dispatch that reads it.
 *
 * @param shader The shader.
 * @param binding The binding index.
 * @param buffer The storage buffer.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_compute_shader_bind_storage_buffer(
    CNA_ComputeShaderHandle shader,
    int32_t binding,
    CNA_StorageBufferHandle buffer);

/**
 * @brief Binds a texture to a numbered sampler unit.
 *
 * @param shader The shader.
 * @param unit The texture unit.
 * @param sampler_name The sampler uniform's name as UTF-8.
 * @param texture The texture to bind; borrowed.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_compute_shader_bind_texture(
    CNA_ComputeShaderHandle shader,
    int32_t unit,
    CNA_StringView sampler_name,
    CNA_Handle texture);

/**
 * @brief Reports whether this renderer supports binding an image for shader read/write.
 *
 * @param shader The shader.
 * @param out_supported Receives `CNA_TRUE` when @ref cna_compute_shader_bind_image can work.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_compute_shader_is_image_binding_supported(
    CNA_ComputeShaderHandle shader,
    CNA_Bool* out_supported);

/**
 * @brief Binds a texture as a read/write image.
 *
 * @param shader The shader.
 * @param unit The image unit.
 * @param texture The texture to bind; borrowed.
 * @param access How the shader will access it.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer or where image
 * binding is unavailable, or an error.
 */
CNA_C_API CNA_Result cna_compute_shader_bind_image(
    CNA_ComputeShaderHandle shader,
    int32_t unit,
    CNA_Handle texture,
    CNA_GraphicsImageAccess access);

/**
 * @brief Dispatches the shader over a group grid.
 *
 * @param shader The shader.
 * @param groups_x Work groups along X.
 * @param groups_y Work groups along Y.
 * @param groups_z Work groups along Z.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_compute_shader_dispatch(
    CNA_ComputeShaderHandle shader,
    int32_t groups_x,
    int32_t groups_y,
    int32_t groups_z);

/**
 * @brief Orders the given memory accesses against later commands.
 *
 * @param shader The shader.
 * @param bits The accesses to order; combine the `CNA_GRAPHICS_MEMORY_BARRIER_*` bits with `|`.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_compute_shader_barrier(
    CNA_ComputeShaderHandle shader,
    CNA_GraphicsMemoryBarrier bits);

/**
 * @brief Reports whether the shader compiled.
 *
 * @param shader The shader.
 * @param out_valid Receives `CNA_TRUE` when the shader is usable.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_compute_shader_is_valid(
    CNA_ComputeShaderHandle shader,
    CNA_Bool* out_valid);

/**
 * @brief Copies the compile error as UTF-8 bytes, without a terminator.
 *
 * @param shader The shader.
 * @param destination Caller-owned destination, or null only when @p capacity is zero.
 * @param capacity Destination capacity in bytes.
 * @param out_bytes Receives the required byte count; zero when the shader compiled.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL`, `CNA_RESULT_NOT_SUPPORTED` without
 * the engine layer, or an error. No partial string is written.
 */
CNA_C_API CNA_Result cna_compute_shader_copy_compile_error(
    CNA_ComputeShaderHandle shader,
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

/**
 * @brief Releases the shader.
 *
 * @param shader The shader; an invalid handle is an error, not a silent no-op.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_compute_shader_destroy(CNA_ComputeShaderHandle shader);

#ifdef __cplusplus
}
#endif

#endif
