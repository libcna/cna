// SPDX-License-Identifier: MS-PL

#ifndef CNA_C_ENGINE_LAYER_H
#define CNA_C_ENGINE_LAYER_H

#include <stddef.h>

#include "CNA/C/effects.h"
#include "CNA/C/graphics.h"
#include "CNA/C/graphics_ext.h"
#include "CNA/C/graphics_state.h"
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

/* ---------------------------------------------------------------------------------------------
 * GPU timers
 * ------------------------------------------------------------------------------------------- */

/** @brief Owned handle for one non-blocking engine-layer GPU timer. */
typedef CNA_Handle CNA_GpuTimerHandle;

/**
 * @brief Creates a GPU timer for a device.
 *
 * Creation also succeeds where the renderer has no timer query; use
 * @ref cna_gpu_timer_is_supported and @ref cna_gpu_timer_copy_unsupported_reason to inspect that
 * state.
 *
 * @param graphics_device The device to measure on.
 * @param out_timer Receives the owned handle; set invalid on failure.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_gpu_timer_create(
    CNA_Handle graphics_device,
    CNA_GpuTimerHandle* out_timer);

/**
 * @brief Reports whether the renderer supplied a GPU timer query.
 *
 * @param timer The timer.
 * @param out_supported Receives the support state.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_gpu_timer_is_supported(
    CNA_GpuTimerHandle timer,
    CNA_Bool* out_supported);

/**
 * @brief Copies why the timer is unsupported as UTF-8 bytes, without a terminator.
 *
 * A supported timer reports zero bytes.
 *
 * @param timer The timer.
 * @param destination Caller-owned destination, or null only when @p capacity is zero.
 * @param capacity Destination capacity in bytes.
 * @param out_bytes Receives the required byte count.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL`, `CNA_RESULT_NOT_SUPPORTED` without
 * the engine layer, or an error. No partial string is written.
 */
CNA_C_API CNA_Result cna_gpu_timer_copy_unsupported_reason(
    CNA_GpuTimerHandle timer,
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

/**
 * @brief Opens the timed range, or does nothing when unsupported or already open.
 *
 * @param timer The timer.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_gpu_timer_begin(CNA_GpuTimerHandle timer);

/**
 * @brief Closes the timed range, or does nothing when unsupported or not open.
 *
 * @param timer The timer.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_gpu_timer_end(CNA_GpuTimerHandle timer);

/**
 * @brief Reports whether the last closed range can be collected without blocking.
 *
 * @param timer The timer.
 * @param out_available Receives the availability state.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_gpu_timer_is_result_available(
    CNA_GpuTimerHandle timer,
    CNA_Bool* out_available);

/**
 * @brief Collects a finished result without blocking.
 *
 * @param timer The timer.
 * @param out_collected Receives `CNA_TRUE` only when a new result was collected.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_gpu_timer_poll(
    CNA_GpuTimerHandle timer,
    CNA_Bool* out_collected);

/**
 * @brief Gets the most recently collected GPU time in milliseconds.
 *
 * @param timer The timer.
 * @param out_milliseconds Receives the elapsed time, or zero before the first result.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_gpu_timer_get_last_milliseconds(
    CNA_GpuTimerHandle timer,
    double* out_milliseconds);

/**
 * @brief Gets how many results have been collected.
 *
 * @param timer The timer.
 * @param out_sample_count Receives the sample count.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_gpu_timer_get_sample_count(
    CNA_GpuTimerHandle timer,
    int32_t* out_sample_count);

/**
 * @brief Reports whether a timed range is currently open.
 *
 * @param timer The timer.
 * @param out_open Receives the open state.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_gpu_timer_is_open(
    CNA_GpuTimerHandle timer,
    CNA_Bool* out_open);

/**
 * @brief Releases a GPU timer and its query object.
 *
 * @param timer The timer.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_gpu_timer_destroy(CNA_GpuTimerHandle timer);

/* ---------------------------------------------------------------------------------------------
 * Render-target pools
 * ------------------------------------------------------------------------------------------- */

/** @brief Owned handle for one engine-layer render-target pool. */
typedef CNA_Handle CNA_RenderTargetPoolHandle;

/**
 * @brief Creates an empty render-target pool for one device.
 *
 * @param graphics_device The device every pooled target is created on.
 * @param out_pool Receives the owned handle; set invalid on failure.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_render_target_pool_create(
    CNA_Handle graphics_device,
    CNA_RenderTargetPoolHandle* out_pool);

/**
 * @brief Acquires a pool-owned two-dimensional render target.
 *
 * The returned handle is a borrowed view. Release that view with
 * @ref cna_render_target_destroy; doing so does not dispose the pool-owned target. The pool refuses
 * reset or destruction until every borrowed view has been released.
 *
 * @param pool The pool.
 * @param width Positive width in pixels.
 * @param height Positive height in pixels.
 * @param format One `CNA_SURFACE_FORMAT_*` identity.
 * @param depth_format One `CNA_DEPTH_FORMAT_*` identity.
 * @param slot Distinguishes targets having the same shape.
 * @param out_render_target Receives the borrowed view; set invalid on failure.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_render_target_pool_acquire(
    CNA_RenderTargetPoolHandle pool,
    int32_t width,
    int32_t height,
    CNA_SurfaceFormat format,
    CNA_DepthFormat depth_format,
    int32_t slot,
    CNA_Handle* out_render_target);

/**
 * @brief Releases every pooled target.
 *
 * @param pool The pool.
 * @return `CNA_RESULT_INVALID_STATE` while a borrowed target view is outstanding,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or another documented result.
 */
CNA_C_API CNA_Result cna_render_target_pool_reset(CNA_RenderTargetPoolHandle pool);

/**
 * @brief Gets how many targets the pool owns.
 *
 * @param pool The pool.
 * @param out_target_count Receives the target count.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_render_target_pool_get_target_count(
    CNA_RenderTargetPoolHandle pool,
    uint64_t* out_target_count);

/**
 * @brief Gets the pool's estimated colour-storage size in bytes.
 *
 * @param pool The pool.
 * @param out_bytes Receives the estimate.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_render_target_pool_get_estimated_bytes(
    CNA_RenderTargetPoolHandle pool,
    uint64_t* out_bytes);

/**
 * @brief Releases a render-target pool and every target it owns.
 *
 * @param pool The pool.
 * @return `CNA_RESULT_INVALID_STATE` while a borrowed target view is outstanding,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or another documented result.
 */
CNA_C_API CNA_Result cna_render_target_pool_destroy(CNA_RenderTargetPoolHandle pool);

/* ---------------------------------------------------------------------------------------------
 * Shader-effect factories
 * ------------------------------------------------------------------------------------------- */

/** @brief Owned handle for one engine-layer named shader-effect cache. */
typedef CNA_Handle CNA_ShaderEffectFactoryHandle;

/**
 * @brief Creates a shader-effect factory for one device.
 *
 * @param graphics_device The device every cached effect is compiled on.
 * @param out_factory Receives the owned handle; set invalid on failure.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_shader_effect_factory_create(
    CNA_Handle graphics_device,
    CNA_ShaderEffectFactoryHandle* out_factory);

/**
 * @brief Gets a named cached shader effect, compiling it on the first request.
 *
 * The returned ordinary `CNA_EffectHandle` is a borrowed view accepted by every effect route.
 * Release it with @ref cna_effect_destroy. Disposing a factory-owned effect is refused, and the
 * factory refuses clear or destruction until the effect view and every child view taken from it
 * have been released.
 *
 * @param factory The factory.
 * @param name Non-empty stable cache key as UTF-8.
 * @param vertex_source Vertex shader source as UTF-8.
 * @param fragment_source Fragment shader source as UTF-8.
 * @param out_effect Receives the borrowed effect view; set invalid on failure.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_shader_effect_factory_acquire(
    CNA_ShaderEffectFactoryHandle factory,
    CNA_StringView name,
    CNA_StringView vertex_source,
    CNA_StringView fragment_source,
    CNA_EffectHandle* out_effect);

/**
 * @brief Reports whether a name is present in the cache.
 *
 * @param factory The factory.
 * @param name The key to look for as UTF-8.
 * @param out_contains Receives the result.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_shader_effect_factory_contains(
    CNA_ShaderEffectFactoryHandle factory,
    CNA_StringView name,
    CNA_Bool* out_contains);

/**
 * @brief Gets how many distinct shaders the factory has compiled since construction.
 *
 * Clearing the cache does not reset this count.
 *
 * @param factory The factory.
 * @param out_compile_count Receives the count.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_shader_effect_factory_get_compile_count(
    CNA_ShaderEffectFactoryHandle factory,
    uint64_t* out_compile_count);

/**
 * @brief Releases every cached effect.
 *
 * @param factory The factory.
 * @return `CNA_RESULT_INVALID_STATE` while a borrowed effect view is outstanding,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or another documented result.
 */
CNA_C_API CNA_Result cna_shader_effect_factory_clear(CNA_ShaderEffectFactoryHandle factory);

/**
 * @brief Releases the factory and every effect it cached.
 *
 * @param factory The factory.
 * @return `CNA_RESULT_INVALID_STATE` while a borrowed effect view is outstanding,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or another documented result.
 */
CNA_C_API CNA_Result cna_shader_effect_factory_destroy(CNA_ShaderEffectFactoryHandle factory);

/* ---------------------------------------------------------------------------------------------
 * Scoped render targets
 * ------------------------------------------------------------------------------------------- */

/** @brief Owned handle for one active render-target restoration scope. */
typedef CNA_Handle CNA_ScopedRenderTargetHandle;

/**
 * @brief Records the current binding and binds a destination until the scope is ended.
 *
 * Scopes on one device may nest, but @ref cna_scoped_render_target_end must close them in reverse
 * order. An out-of-order end returns `CNA_RESULT_INVALID_STATE` and changes neither scope nor
 * binding.
 *
 * @param graphics_device The device to bind on.
 * @param destination A two-dimensional render target, or `CNA_INVALID_HANDLE` for the backbuffer.
 * @param out_scope Receives the owned active-scope handle; set invalid on failure.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_scoped_render_target_begin(
    CNA_Handle graphics_device,
    CNA_Handle destination,
    CNA_ScopedRenderTargetHandle* out_scope);

/**
 * @brief Reports whether the scope recorded a previous native binding.
 *
 * @param scope The active scope.
 * @param out_recorded Receives the state.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_scoped_render_target_get_has_recorded_previous(
    CNA_ScopedRenderTargetHandle scope,
    CNA_Bool* out_recorded);

/**
 * @brief Restores the scope's previous binding and releases the scope.
 *
 * @param scope The active scope; it must be the most recently opened scope on its device.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_STATE` for an out-of-order end,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or another documented result.
 */
CNA_C_API CNA_Result cna_scoped_render_target_end(CNA_ScopedRenderTargetHandle scope);

/* ---------------------------------------------------------------------------------------------
 * Full-screen drawing
 * ------------------------------------------------------------------------------------------- */

/**
 * @brief Owned handle for one engine-layer full-screen pass.
 *
 * Release it with @ref cna_fullscreen_pass_destroy before destroying the game that owns its device.
 */
typedef CNA_Handle CNA_FullscreenPassHandle;

/**
 * @brief Creates a full-screen pass.
 *
 * @param graphics_device The device to draw on.
 * @param out_pass Receives the owned handle; set invalid on failure.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_fullscreen_pass_create(
    CNA_Handle graphics_device,
    CNA_FullscreenPassHandle* out_pass);

/**
 * @brief Draws a source texture over a destination target, through an optional effect.
 *
 * @param pass The pass.
 * @param source The source texture, or `CNA_INVALID_HANDLE` for none; borrowed.
 * @param destination The destination render target, or `CNA_INVALID_HANDLE` for the back buffer.
 * @param effect The effect to draw through, or `CNA_INVALID_HANDLE` for a straight copy; borrowed.
 * @param width Destination width in pixels.
 * @param height Destination height in pixels.
 * @param sampler The sampler state to use, or null for the pass's own default.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_fullscreen_pass_draw(
    CNA_FullscreenPassHandle pass,
    CNA_Handle source,
    CNA_Handle destination,
    CNA_EffectHandle effect,
    int32_t width,
    int32_t height,
    const CNA_SamplerState* sampler);

/**
 * @brief Draws a source texture over whatever target is already bound.
 *
 * @param pass The pass.
 * @param source The source texture, or `CNA_INVALID_HANDLE` for none; borrowed.
 * @param effect The effect to draw through, or `CNA_INVALID_HANDLE` for a straight copy; borrowed.
 * @param width Destination width in pixels.
 * @param height Destination height in pixels.
 * @param sampler The sampler state to use, or null for the pass's own default.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_fullscreen_pass_draw_over_current_target(
    CNA_FullscreenPassHandle pass,
    CNA_Handle source,
    CNA_EffectHandle effect,
    int32_t width,
    int32_t height,
    const CNA_SamplerState* sampler);

/**
 * @brief Releases the full-screen pass.
 *
 * @param pass The pass; an invalid handle is an error, not a silent no-op.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_fullscreen_pass_destroy(CNA_FullscreenPassHandle pass);

/* ---------------------------------------------------------------------------------------------
 * Post-process passes
 * ------------------------------------------------------------------------------------------- */

/**
 * @brief One frame's worth of inputs to a post-process pass.
 *
 * The canonical struct holds raw C++ pointers to the frame's textures and to the pipeline
 * settings; here each texture becomes a handle. Fill it with
 * @ref cna_post_process_context_init before setting the fields your pass reads; initializing
 * is not optional, because a later engine-layer revision may add a field and a zero-filled
 * struct would then mean something different from a defaulted one.
 *
 * **The canonical `settings` pointer is not here yet, and that is deliberate.** It points at a
 * `RenderPipelineSettings`, whose C form is still a subset of the canonical type; carrying that
 * subset would silently apply engine defaults for every field the subset omits. Until
 * `CBIND-088` binds the settings type in full, a pass applied from C sees no settings and uses
 * its own defaults, which is exactly what the canonical struct means by a null `settings`.
 */
/**
 * @brief Forward declaration so a post-process context can point at settings declared below.
 *
 * The full definition is further down this header; a context only ever holds a pointer.
 */
struct CNA_RenderPipelineSettingsEXT;

typedef struct CNA_PostProcessContext {
    /** @brief Size of this structure in bytes. */
    uint32_t struct_size;
    /** @brief Version of this structure. */
    uint32_t struct_version;
    /** @brief The colour input, or `CNA_INVALID_HANDLE` for none; borrowed. */
    CNA_Handle source;
    /** @brief The linear-depth input, or `CNA_INVALID_HANDLE` when the pass reads no depth. */
    CNA_Handle source_depth;
    /** @brief The normals input, or `CNA_INVALID_HANDLE` when the pass reads no normals. */
    CNA_Handle source_normals;
    /** @brief The velocity input, or `CNA_INVALID_HANDLE` when the pass reads no velocity. */
    CNA_Handle source_velocity;
    /** @brief The destination render target, or `CNA_INVALID_HANDLE` for the back buffer. */
    CNA_Handle destination;
    /** @brief Destination width in pixels. */
    int32_t width;
    /** @brief Destination height in pixels. */
    int32_t height;
    /** @brief Seconds elapsed since the previous frame. */
    float elapsed_seconds;
    /** @brief The camera's near plane distance. */
    float near_plane;
    /** @brief The camera's far plane distance. */
    float far_plane;
    /** @brief `CNA_TRUE` when @ref previous_view_projection describes a real previous frame. */
    CNA_Bool has_previous_frame;
    /** @brief Padding; write zero. */
    uint8_t reserved[3];
    /** @brief The camera's projection matrix. */
    CNA_Matrix projection;
    /** @brief The inverse of @ref projection. */
    CNA_Matrix inverse_projection;
    /** @brief The inverse of the camera's view matrix. */
    CNA_Matrix inverse_view;
    /** @brief The previous frame's view-projection matrix, for reprojection. */
    CNA_Matrix previous_view_projection;

    /**
     * @brief The settings a pass reads, or null when it has none. Appended in version 2.
     *
     * `CBIND-084C` deferred this field, `CBIND-088B` could not add it, and `CBIND-100` made the
     * structure growable so it could. **It is a borrowed pointer, not a copy**, matching the
     * canonical `const RenderPipelineSettings*` exactly: the caller owns the settings and must
     * keep them alive for the call.
     *
     * A caller compiled against version 1 does not have this field, sets `struct_size` to the
     * smaller size, and is accepted -- see @ref CNA_POST_PROCESS_CONTEXT_SIZE_V1. Reaching it
     * requires `struct_size` to cover it; CNA never reads past what `struct_size` declares.
     */
    const struct CNA_RenderPipelineSettingsEXT* settings;
} CNA_PostProcessContext;

/**
 * @brief The size of @ref CNA_PostProcessContext as version 1 defined it.
 *
 * The mandatory prefix. A caller compiled before `settings` existed passes this as `struct_size`
 * and every route still works; anything smaller is refused, because CNA would otherwise read
 * fields the caller never allocated. This constant is what makes the structure growable rather
 * than frozen, and it is why the size check is `<` rather than `!=`.
 */
#define CNA_POST_PROCESS_CONTEXT_SIZE_V1 \
    ((uint32_t)(offsetof(CNA_PostProcessContext, settings)))

/** @brief The structure version that added @ref CNA_PostProcessContext::settings. */
#define CNA_POST_PROCESS_CONTEXT_VERSION_2 UINT32_C(2)

/**
 * @brief Fills a post-process context with the canonical defaults.
 *
 * @param out_context Receives the defaults: no textures, zero size, identity matrices, no settings
 * and no previous frame.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null output.
 */
CNA_C_API CNA_Result cna_post_process_context_init(CNA_PostProcessContext* out_context);

/**
 * @brief Owned handle for one engine-layer post-process pass.
 *
 * A pass is created by one of the concrete constructors -- @ref cna_blit_pass_create,
 * @ref cna_post_process_effect_pass_create, @ref cna_post_process_effect_pass_create_owning -- and then driven through the
 * shared operations below, which are the C form of the abstract `PostProcessPass` contract. C
 * cannot derive from that contract, so what crosses this ABI is the set of operations it declares
 * rather than the base type itself.
 */
typedef CNA_Handle CNA_PostProcessPassHandle;

/**
 * @brief Creates a pass that copies its source to its destination unchanged.
 *
 * @param graphics_device The device to draw on.
 * @param out_pass Receives the owned handle; set invalid on failure.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_blit_pass_create(
    CNA_Handle graphics_device,
    CNA_PostProcessPassHandle* out_pass);

/**
 * @brief Creates a pass that draws its source through a **borrowed** effect.
 *
 * The effect is not owned: it must outlive the pass, and destroying the pass leaves it alive.
 *
 * @param graphics_device The device to draw on.
 * @param effect The effect to draw through, or `CNA_INVALID_HANDLE` for none; borrowed.
 * @param name The pass's name as UTF-8, used in diagnostics.
 * @param out_pass Receives the owned handle; set invalid on failure.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_post_process_effect_pass_create(
    CNA_Handle graphics_device,
    CNA_EffectHandle effect,
    CNA_StringView name,
    CNA_PostProcessPassHandle* out_pass);

/**
 * @brief Creates a pass that **takes ownership** of an effect.
 *
 * The C form of the canonical constructor taking a `unique_ptr`. On success the effect handle is
 * **consumed**: it stops being valid, the caller must not destroy it, and the pass releases the
 * effect when it is itself destroyed. On failure the caller keeps the effect and its handle stays
 * valid, so a refused call never strands a resource.
 *
 * @param graphics_device The device to draw on.
 * @param effect The effect to take over; consumed on success.
 * @param name The pass's name as UTF-8, used in diagnostics.
 * @param out_pass Receives the owned handle; set invalid on failure.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_post_process_effect_pass_create_owning(
    CNA_Handle graphics_device,
    CNA_EffectHandle effect,
    CNA_StringView name,
    CNA_PostProcessPassHandle* out_pass);

/**
 * @brief Returns the effect an effect pass draws through.
 *
 * The returned handle is borrowed from the pass; do not destroy it.
 *
 * @param pass The pass; must be an effect pass.
 * @param out_effect Receives the borrowed effect handle, or `CNA_INVALID_HANDLE` when the pass has
 * no effect.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for a pass that is not an effect
 * pass, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_post_process_effect_pass_get_effect(
    CNA_PostProcessPassHandle pass,
    CNA_EffectHandle* out_effect);

/**
 * @brief Replaces the effect an effect pass draws through, borrowing the new one.
 *
 * A pass created by @ref cna_post_process_effect_pass_create_owning still owns the effect it was given; setting
 * a new one does not release it, exactly as the canonical setter does not.
 *
 * @param pass The pass; must be an effect pass.
 * @param effect The effect to draw through, or `CNA_INVALID_HANDLE` to draw nothing; borrowed.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for a pass that is not an effect
 * pass, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_post_process_effect_pass_set_effect(
    CNA_PostProcessPassHandle pass,
    CNA_EffectHandle effect);

/**
 * @brief Runs the pass over one frame's inputs.
 *
 * @param pass The pass.
 * @param context The frame's inputs.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_post_process_pass_apply(
    CNA_PostProcessPassHandle pass,
    const CNA_PostProcessContext* context);

/**
 * @brief Copies the pass's name as UTF-8 bytes, without a terminator.
 *
 * @param pass The pass.
 * @param destination Caller-owned destination, or null only when @p capacity is zero.
 * @param capacity Destination capacity in bytes.
 * @param out_bytes Receives the required byte count without a terminator.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL`, `CNA_RESULT_NOT_SUPPORTED` without
 * the engine layer, or an error. No partial string is written.
 */
CNA_C_API CNA_Result cna_post_process_pass_copy_name(
    CNA_PostProcessPassHandle pass,
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

/**
 * @brief Reports whether the pass can do its real work on a device.
 *
 * A pass that answers `CNA_FALSE` is not broken. This layer's contract is that such a pass degrades
 * -- typically to a copy -- rather than failing, so a chain may still run it and get the documented
 * fallback. Ask this to know which you will get, not to decide whether calling is safe.
 *
 * @param pass The pass.
 * @param graphics_device The device to ask about.
 * @param out_supported Receives `CNA_TRUE` when the pass can do its real work there.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_post_process_pass_is_supported(
    CNA_PostProcessPassHandle pass,
    CNA_Handle graphics_device,
    CNA_Bool* out_supported);

/**
 * @brief Releases the pass, and the effect it owns if it owns one.
 *
 * @param pass The pass; an invalid handle is an error, not a silent no-op.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_post_process_pass_destroy(CNA_PostProcessPassHandle pass);

/* ---------------------------------------------------------------------------------------------
 * PBR material binding
 * ------------------------------------------------------------------------------------------- */

/**
 * @brief Applies a material's values to a PBR effect.
 *
 * @param effect A `PbrEffect` handle.
 * @param material The material to apply; every field of the full material crosses.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` when @p effect is not a `PbrEffect`,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_pbr_effect_apply_material(
    CNA_EffectHandle effect,
    const CNA_PbrMaterialEXT* material);

/**
 * @brief Applies a material's values to a skinned PBR effect.
 *
 * @param effect A `SkinnedPbrEffect` handle.
 * @param material The material to apply; every field of the full material crosses.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` when @p effect is not a
 * `SkinnedPbrEffect`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_skinned_pbr_effect_apply_material(
    CNA_EffectHandle effect,
    const CNA_PbrMaterialEXT* material);

/**
 * @brief Reads a material back out of a PBR effect.
 *
 * @param effect A `PbrEffect` handle.
 * @param out_material Receives the material the effect currently carries.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` when @p effect is not a `PbrEffect`,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_pbr_effect_extract_material(
    CNA_EffectHandle effect,
    CNA_PbrMaterialEXT* out_material);

/**
 * @brief Reads a material back out of a skinned PBR effect.
 *
 * @param effect A `SkinnedPbrEffect` handle.
 * @param out_material Receives the material the effect currently carries.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` when @p effect is not a
 * `SkinnedPbrEffect`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_skinned_pbr_effect_extract_material(
    CNA_EffectHandle effect,
    CNA_PbrMaterialEXT* out_material);

/**
 * @brief Applies the device state a material implies -- blending, depth write and culling.
 *
 * @param material The material whose state to apply.
 * @param graphics_device The device to set state on.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_pbr_material_apply_state(
    const CNA_PbrMaterialEXT* material,
    CNA_Handle graphics_device);

/* ---------------------------------------------------------------------------------------------
 * Lights
 *
 * Every type below is a pure value, so all of these routes work in **both** builds: filling a
 * light with its canonical defaults needs no engine-layer object. `PunctualLightEXT` and
 * `ShadowCascadeStateEXT` are not behind `CNA_CNAEXT` at all -- they live in `modules/graphics` --
 * and the other three are, but only their defaults are checked against the canonical structures,
 * which is a compile-time assertion rather than a call.
 * ------------------------------------------------------------------------------------------- */

/** @brief A directional light: colour and intensity arriving from one direction, everywhere. */
typedef struct CNA_DirectionalLightEXT {
    /** @brief Size of this structure in bytes. */
    uint32_t struct_size;
    /** @brief Version of this structure. */
    uint32_t struct_version;
    /** @brief The direction the light travels; the default points straight down. */
    CNA_Vector3 direction;
    /** @brief Linear RGB colour; the default is white. */
    CNA_Vector3 color;
    /** @brief Scalar multiplier on @ref color. */
    float intensity;
    /** @brief `CNA_TRUE` when this light should be given a shadow map. */
    CNA_Bool casts_shadows;
    /** @brief Padding; write zero. */
    uint8_t reserved[3];
} CNA_DirectionalLightEXT;

/**
 * @brief Fills a directional light with the canonical defaults.
 *
 * @param out_light Receives direction (0, -1, 0), white, intensity 1 and no shadows.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null output.
 */
CNA_C_API CNA_Result cna_directional_light_ext_init(CNA_DirectionalLightEXT* out_light);

/** @brief A point light: colour radiating from a position, falling off to a range. */
typedef struct CNA_PointLightEXT {
    /** @brief Size of this structure in bytes. */
    uint32_t struct_size;
    /** @brief Version of this structure. */
    uint32_t struct_version;
    /** @brief World-space position. */
    CNA_Vector3 position;
    /** @brief Linear RGB colour; the default is white. */
    CNA_Vector3 color;
    /** @brief Scalar multiplier on @ref color. */
    float intensity;
    /** @brief Distance at which the light stops contributing. */
    float range;
    /** @brief `CNA_TRUE` when this light should be given a shadow cube. */
    CNA_Bool casts_shadows;
    /** @brief Padding; write zero. */
    uint8_t reserved[3];
} CNA_PointLightEXT;

/**
 * @brief Fills a point light with the canonical defaults.
 *
 * @param out_light Receives the origin, white, intensity 1, range 20 and no shadows.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null output.
 */
CNA_C_API CNA_Result cna_point_light_ext_init(CNA_PointLightEXT* out_light);

/** @brief A spot light: a point light restricted to a cone. */
typedef struct CNA_SpotLightEXT {
    /** @brief Size of this structure in bytes. */
    uint32_t struct_size;
    /** @brief Version of this structure. */
    uint32_t struct_version;
    /** @brief World-space position. */
    CNA_Vector3 position;
    /** @brief The direction the cone points; the default points straight down. */
    CNA_Vector3 direction;
    /** @brief Linear RGB colour; the default is white. */
    CNA_Vector3 color;
    /** @brief Scalar multiplier on @ref color. */
    float intensity;
    /** @brief Distance at which the light stops contributing. */
    float range;
    /** @brief Half-angle in radians inside which the light is at full strength. */
    float inner_angle;
    /** @brief Half-angle in radians at which the light has fallen to nothing. */
    float outer_angle;
    /** @brief `CNA_TRUE` when this light should be given a shadow map. */
    CNA_Bool casts_shadows;
    /** @brief Padding; write zero. */
    uint8_t reserved[3];
} CNA_SpotLightEXT;

/**
 * @brief Fills a spot light with the canonical defaults.
 *
 * @param out_light Receives the origin, direction (0, -1, 0), white, intensity 1, range 20, inner
 * angle 0.35, outer angle 0.5 and no shadows.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null output.
 */
CNA_C_API CNA_Result cna_spot_light_ext_init(CNA_SpotLightEXT* out_light);

/** @brief Fixed-width identity of which kind of punctual light a `CNA_PunctualLightEXT` carries. */
typedef uint32_t CNA_PunctualLightKindEXT;

/** @brief No light; the slot contributes nothing. */
#define CNA_PUNCTUAL_LIGHT_KIND_EXT_NONE UINT32_C(0)
/** @brief A point light. */
#define CNA_PUNCTUAL_LIGHT_KIND_EXT_POINT UINT32_C(1)
/** @brief A spot light. */
#define CNA_PUNCTUAL_LIGHT_KIND_EXT_SPOT UINT32_C(2)

/**
 * @brief One punctual light as an effect consumes it, with its shadow resources attached.
 *
 * This is the shape the stock effects read, which is why it carries both a cube and a 2D shadow
 * texture: a point light shadows into the cube and a spot light into the map, and @ref kind says
 * which one is meaningful.
 */
typedef struct CNA_PunctualLightEXT {
    /** @brief Size of this structure in bytes. */
    uint32_t struct_size;
    /** @brief Version of this structure. */
    uint32_t struct_version;
    /** @brief Which kind of light this is; `NONE` means the slot is unused. */
    CNA_PunctualLightKindEXT kind;
    /** @brief Padding; write zero. */
    uint32_t reserved;
    /** @brief World-space position. */
    CNA_Vector3 position;
    /** @brief The direction a spot light points; the default points straight down. */
    CNA_Vector3 direction;
    /** @brief Linear RGB diffuse colour; the default is white. */
    CNA_Vector3 diffuse_color;
    /** @brief Distance at which the light stops contributing. */
    float range;
    /** @brief Half-angle in radians inside which a spot light is at full strength. */
    float inner_angle;
    /** @brief Half-angle in radians at which a spot light has fallen to nothing. */
    float outer_angle;
    /** @brief Depth bias applied when sampling this light's shadow. */
    float shadow_depth_bias;
    /** @brief Borrowed TextureCube handle for a point light's shadow, or `CNA_INVALID_HANDLE`. */
    CNA_Handle shadow_cube;
    /** @brief Borrowed Texture2D handle for a spot light's shadow, or `CNA_INVALID_HANDLE`. */
    CNA_Handle shadow_map;
    /** @brief The transform that takes world space into this light's shadow space. */
    CNA_Matrix shadow_view_projection;
} CNA_PunctualLightEXT;

/**
 * @brief Fills a punctual light with the canonical defaults.
 *
 * @param out_light Receives kind `NONE`, the origin, direction (0, -1, 0), white, range 20, inner
 * angle 0.35, outer angle 0.5, depth bias 0.004, no shadow textures and an identity transform.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null output.
 */
CNA_C_API CNA_Result cna_punctual_light_ext_init(CNA_PunctualLightEXT* out_light);

/** @brief The greatest number of cascades a `CNA_ShadowCascadeStateEXT` can describe. */
#define CNA_SHADOW_CASCADE_MAX_EXT 4

/**
 * @brief The cascaded-shadow state an effect reads for one frame.
 *
 * @ref count says how many entries of @ref world_to_atlas and @ref split_distance are meaningful;
 * the rest are left at their defaults rather than removed, so the array stays a fixed layout.
 */
typedef struct CNA_ShadowCascadeStateEXT {
    /** @brief Size of this structure in bytes. */
    uint32_t struct_size;
    /** @brief Version of this structure. */
    uint32_t struct_version;
    /** @brief How many cascades are in use; zero disables cascaded shadows. */
    int32_t count;
    /** @brief Width in world units over which neighbouring cascades cross-fade. */
    float blend_band;
    /** @brief Transform from world space into each cascade's atlas region. */
    CNA_Matrix world_to_atlas[CNA_SHADOW_CASCADE_MAX_EXT];
    /** @brief View-space distance at which each cascade ends. */
    float split_distance[CNA_SHADOW_CASCADE_MAX_EXT];
    /** @brief The camera view the splits were computed against. */
    CNA_Matrix camera_view;
    /** @brief `CNA_TRUE` to tint each cascade differently, for diagnosing split placement. */
    CNA_Bool debug_tint;
    /** @brief Padding; write zero. */
    uint8_t reserved[3];
} CNA_ShadowCascadeStateEXT;

/**
 * @brief Fills a cascaded-shadow state with the canonical defaults.
 *
 * @param out_state Receives zero cascades, identity transforms, zero splits, no blend band and no
 * debug tint -- which is the state that disables cascaded shadows.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null output.
 */
CNA_C_API CNA_Result cna_shadow_cascade_state_ext_init(CNA_ShadowCascadeStateEXT* out_state);

/* ---------------------------------------------------------------------------------------------
 * Shadow maps
 *
 * **Casting and sampling are two different questions, and a shadow map answers only the first.**
 * Each map's `_is_supported` reports whether its caster shader exists and links -- the honest
 * answer, since a renderer can advertise custom effects and still fail to compile this one. Whether
 * the renderer can *sample* the result is @ref cna_graphics_device_supports_shadow_sampling_ext,
 * and a frame needs both: a map that rasters on a renderer that cannot sample it produces a
 * shadow texture nothing reads, which looks exactly like a scene with no occluders.
 * ------------------------------------------------------------------------------------------- */

/**
 * @brief Reports whether the renderer can sample a shadow map in a shader.
 *
 * Ask this **as well as** a shadow map's own `_is_supported`, not instead of it: one answers
 * whether the shadow can be drawn, the other whether anything can read it.
 *
 * @param graphics_device The device to ask.
 * @param out_supported Receives `CNA_TRUE` when shadow sampling is available.
 * @return `CNA_RESULT_SUCCESS`, or a documented argument/handle failure.
 */
CNA_C_API CNA_Result cna_graphics_device_supports_shadow_sampling_ext(
    CNA_Handle graphics_device,
    CNA_Bool* out_supported);

/**
 * @brief Owned handle for one directional-light shadow map.
 *
 * Release it with @ref cna_shadow_map_destroy before destroying the game that owns its device.
 * The effects and the texture it hands out are borrowed from it and stop being valid with it.
 */
typedef CNA_Handle CNA_ShadowMapHandle;

/**
 * @brief Creates a directional-light shadow map at a quality preset.
 *
 * Creation succeeds on a renderer that cannot cast shadows; ask @ref cna_shadow_map_is_supported.
 *
 * @param graphics_device The device to render on.
 * @param quality One `CNA_SHADOW_QUALITY_*` identity, which selects the map's size and filter.
 * @param out_shadow_map Receives the owned handle; set invalid on failure.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_shadow_map_create(
    CNA_Handle graphics_device,
    CNA_ShadowQuality quality,
    CNA_ShadowMapHandle* out_shadow_map);

/**
 * @brief Reports whether this renderer can cast into the map.
 *
 * @param shadow_map The map.
 * @param out_supported Receives `CNA_TRUE` when the caster shader exists and links.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_shadow_map_is_supported(
    CNA_ShadowMapHandle shadow_map,
    CNA_Bool* out_supported);

/**
 * @brief Opens the shadow pass, binding the map and computing the light's transform.
 *
 * @param shadow_map The map.
 * @param light The directional light to cast from.
 * @param scene_bounds The world-space bounds the shadow must cover.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_shadow_map_begin(
    CNA_ShadowMapHandle shadow_map,
    const CNA_DirectionalLightEXT* light,
    const CNA_BoundingBox* scene_bounds);

/**
 * @brief Closes the shadow pass opened by @ref cna_shadow_map_begin.
 *
 * @param shadow_map The map.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_shadow_map_end(CNA_ShadowMapHandle shadow_map);

/**
 * @brief Returns the caster effect, borrowed from the map.
 *
 * @param shadow_map The map.
 * @param out_effect Receives the borrowed effect, or `CNA_INVALID_HANDLE` when unsupported.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_shadow_map_get_caster_effect(
    CNA_ShadowMapHandle shadow_map,
    CNA_EffectHandle* out_effect);

/**
 * @brief Returns the skinned caster effect, borrowed from the map.
 *
 * @param shadow_map The map.
 * @param out_effect Receives the borrowed effect, or `CNA_INVALID_HANDLE` when unsupported.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_shadow_map_get_skinned_caster_effect(
    CNA_ShadowMapHandle shadow_map,
    CNA_EffectHandle* out_effect);

/**
 * @brief Applies the caster effect for a rigid draw inside the pass.
 *
 * @param shadow_map The map.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_shadow_map_apply_caster(CNA_ShadowMapHandle shadow_map);

/**
 * @brief Applies the skinned caster effect with a bone palette.
 *
 * @param shadow_map The map.
 * @param bone_transforms Array of bone transforms; null only when @p bone_count is zero.
 * @param bone_count Number of transforms.
 * @param weights_per_vertex How many bone weights each vertex carries.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_shadow_map_apply_skinned_caster(
    CNA_ShadowMapHandle shadow_map,
    const CNA_Matrix* bone_transforms,
    uint64_t bone_count,
    int32_t weights_per_vertex);

/**
 * @brief Returns the shadow texture, borrowed from the map.
 *
 * @param shadow_map The map.
 * The handle is a borrow that keeps the map alive; release it with
 * @ref cna_render_target_destroy, which does not dispose the map's own target. The map
 * refuses to be destroyed while a borrow is outstanding.
 *
 * @param out_texture Receives the borrowed Texture2D, or `CNA_INVALID_HANDLE` when unsupported.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_shadow_map_get_shadow_texture(
    CNA_ShadowMapHandle shadow_map,
    CNA_Handle* out_texture);

/**
 * @brief Returns the transform from world space into the map, as of the last `begin`.
 *
 * @param shadow_map The map.
 * @param out_matrix Receives the transform.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_shadow_map_get_light_view_projection(
    CNA_ShadowMapHandle shadow_map,
    CNA_Matrix* out_matrix);

/**
 * @brief Returns the map's edge length in texels.
 *
 * @param shadow_map The map.
 * @param out_size Receives the size.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_shadow_map_get_size(CNA_ShadowMapHandle shadow_map, int32_t* out_size);

/**
 * @brief Returns the quality preset the map was created with.
 *
 * @param shadow_map The map.
 * @param out_quality Receives the preset.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_shadow_map_get_quality(
    CNA_ShadowMapHandle shadow_map,
    CNA_ShadowQuality* out_quality);

/**
 * @brief Returns the depth bias applied when casting.
 *
 * @param shadow_map The map.
 * @param out_bias Receives the bias.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_shadow_map_get_depth_bias(
    CNA_ShadowMapHandle shadow_map,
    float* out_bias);

/**
 * @brief Sets the depth bias applied when casting.
 *
 * @param shadow_map The map.
 * @param bias The bias.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_shadow_map_set_depth_bias(CNA_ShadowMapHandle shadow_map, float bias);

/**
 * @brief Returns the filter radius in texels the map's quality selects.
 *
 * @param shadow_map The map.
 * @param out_radius Receives the radius.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_shadow_map_get_filter_radius(
    CNA_ShadowMapHandle shadow_map,
    int32_t* out_radius);

/**
 * @brief Computes a directional light's view transform for a scene, without a map.
 *
 * A pure function of its arguments, so it needs no handle and works wherever the layer is present.
 *
 * @param light The directional light.
 * @param scene_bounds The world-space bounds to cover.
 * @param out_matrix Receives the view transform.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_shadow_map_compute_light_view(
    const CNA_DirectionalLightEXT* light,
    const CNA_BoundingBox* scene_bounds,
    CNA_Matrix* out_matrix);

/**
 * @brief Computes the projection that fits a scene into a light's view.
 *
 * @param light_view The view transform, from @ref cna_shadow_map_compute_light_view.
 * @param scene_bounds The world-space bounds to cover.
 * @param out_matrix Receives the projection.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_shadow_map_compute_light_projection(
    const CNA_Matrix* light_view,
    const CNA_BoundingBox* scene_bounds,
    CNA_Matrix* out_matrix);

/**
 * @brief Returns the map size a quality preset selects.
 *
 * @param quality One `CNA_SHADOW_QUALITY_*` identity.
 * @param out_size Receives the edge length in texels.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_shadow_map_size_for_quality(
    CNA_ShadowQuality quality,
    int32_t* out_size);

/**
 * @brief Returns the filter radius a quality preset selects.
 *
 * @param quality One `CNA_SHADOW_QUALITY_*` identity.
 * @param out_radius Receives the radius in texels.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_shadow_map_filter_radius_for_quality(
    CNA_ShadowQuality quality,
    int32_t* out_radius);

/**
 * @brief Releases the shadow map, its texture and its effects.
 *
 * @param shadow_map The map; an invalid handle is an error, not a silent no-op.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_shadow_map_destroy(CNA_ShadowMapHandle shadow_map);

/**
 * @brief Owned handle for one spot-light shadow map.
 *
 * Release it with @ref cna_spot_shadow_map_destroy before destroying the game that owns its device.
 */
typedef CNA_Handle CNA_SpotShadowMapHandle;

/**
 * @brief Creates a spot-light shadow map at a quality preset.
 *
 * @param graphics_device The device to render on.
 * @param quality One `CNA_SHADOW_QUALITY_*` identity.
 * @param out_shadow_map Receives the owned handle; set invalid on failure.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_spot_shadow_map_create(
    CNA_Handle graphics_device,
    CNA_ShadowQuality quality,
    CNA_SpotShadowMapHandle* out_shadow_map);

/**
 * @brief Reports whether this renderer can cast into the map.
 *
 * @param shadow_map The map.
 * @param out_supported Receives `CNA_TRUE` when the caster shader exists and links.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_spot_shadow_map_is_supported(
    CNA_SpotShadowMapHandle shadow_map,
    CNA_Bool* out_supported);

/**
 * @brief Opens the shadow pass for a spot light.
 *
 * @param shadow_map The map.
 * @param light The spot light to cast from.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_spot_shadow_map_begin(
    CNA_SpotShadowMapHandle shadow_map,
    const CNA_SpotLightEXT* light);

/**
 * @brief Closes the shadow pass opened by @ref cna_spot_shadow_map_begin.
 *
 * @param shadow_map The map.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_spot_shadow_map_end(CNA_SpotShadowMapHandle shadow_map);

/**
 * @brief Returns the shadow texture, borrowed from the map.
 *
 * @param shadow_map The map.
 * The handle is a borrow that keeps the map alive; release it with
 * @ref cna_render_target_destroy, which does not dispose the map's own target. The map
 * refuses to be destroyed while a borrow is outstanding.
 *
 * @param out_texture Receives the borrowed Texture2D, or `CNA_INVALID_HANDLE` when unsupported.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_spot_shadow_map_get_shadow_texture(
    CNA_SpotShadowMapHandle shadow_map,
    CNA_Handle* out_texture);

/**
 * @brief Returns the caster effect, borrowed from the map.
 *
 * @param shadow_map The map.
 * @param out_effect Receives the borrowed effect, or `CNA_INVALID_HANDLE` when unsupported.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_spot_shadow_map_get_caster_effect(
    CNA_SpotShadowMapHandle shadow_map,
    CNA_EffectHandle* out_effect);

/**
 * @brief Returns the map's edge length in texels.
 *
 * @param shadow_map The map.
 * @param out_size Receives the size.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_spot_shadow_map_get_size(
    CNA_SpotShadowMapHandle shadow_map,
    int32_t* out_size);

/**
 * @brief Returns the quality preset the map was created with.
 *
 * @param shadow_map The map.
 * @param out_quality Receives the preset.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_spot_shadow_map_get_quality(
    CNA_SpotShadowMapHandle shadow_map,
    CNA_ShadowQuality* out_quality);

/**
 * @brief Returns the transform from world space into the map, as of the last `begin`.
 *
 * @param shadow_map The map.
 * @param out_matrix Receives the transform.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_spot_shadow_map_get_light_view_projection(
    CNA_SpotShadowMapHandle shadow_map,
    CNA_Matrix* out_matrix);

/**
 * @brief Returns the position of the light the map last cast from.
 *
 * @param shadow_map The map.
 * @param out_position Receives the position.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_spot_shadow_map_get_light_position(
    CNA_SpotShadowMapHandle shadow_map,
    CNA_Vector3* out_position);

/**
 * @brief Returns the range of the light the map last cast from.
 *
 * @param shadow_map The map.
 * @param out_range Receives the range.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_spot_shadow_map_get_light_range(
    CNA_SpotShadowMapHandle shadow_map,
    float* out_range);

/**
 * @brief Returns the depth bias applied when casting.
 *
 * @param shadow_map The map.
 * @param out_bias Receives the bias.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_spot_shadow_map_get_depth_bias(
    CNA_SpotShadowMapHandle shadow_map,
    float* out_bias);

/**
 * @brief Sets the depth bias applied when casting.
 *
 * @param shadow_map The map.
 * @param bias The bias.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_spot_shadow_map_set_depth_bias(
    CNA_SpotShadowMapHandle shadow_map,
    float bias);

/**
 * @brief Computes a spot light's view transform, without a map.
 *
 * @param light The spot light.
 * @param out_matrix Receives the view transform.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_spot_shadow_map_compute_light_view(
    const CNA_SpotLightEXT* light,
    CNA_Matrix* out_matrix);

/**
 * @brief Computes a spot light's projection from its cone and range.
 *
 * @param light The spot light.
 * @param out_matrix Receives the projection.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_spot_shadow_map_compute_light_projection(
    const CNA_SpotLightEXT* light,
    CNA_Matrix* out_matrix);

/**
 * @brief Releases the spot shadow map, its texture and its effect.
 *
 * @param shadow_map The map; an invalid handle is an error, not a silent no-op.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_spot_shadow_map_destroy(CNA_SpotShadowMapHandle shadow_map);

/* ---------------------------------------------------------------------------------------------
 * Cascaded and cube shadow maps
 * ------------------------------------------------------------------------------------------- */

/**
 * @brief Owned handle for one cascaded directional-light shadow map.
 *
 * Release it with @ref cna_cascaded_shadow_map_destroy. Its atlas texture and caster effect are
 * borrows that keep it alive, and destroying it is refused while one is outstanding.
 */
typedef CNA_Handle CNA_CascadedShadowMapHandle;

/**
 * @brief Creates a cascaded shadow map.
 *
 * @param graphics_device The device to render on.
 * @param quality One `CNA_SHADOW_QUALITY_*` identity.
 * @param cascade_count How many cascades, from 1 to `CNA_SHADOW_CASCADE_MAX_EXT`.
 * @param out_shadow_map Receives the owned handle; set invalid on failure.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_cascaded_shadow_map_create(
    CNA_Handle graphics_device,
    CNA_ShadowQuality quality,
    int32_t cascade_count,
    CNA_CascadedShadowMapHandle* out_shadow_map);

/**
 * @brief Reports whether this renderer can cast into the atlas.
 *
 * @param shadow_map The map.
 * @param out_supported Receives `CNA_TRUE` when the caster shader exists and links.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_cascaded_shadow_map_is_supported(
    CNA_CascadedShadowMapHandle shadow_map,
    CNA_Bool* out_supported);

/**
 * @brief Recomputes every cascade's transform and split for a light and a camera.
 *
 * Call this once per frame before opening any cascade.
 *
 * @param shadow_map The map.
 * @param light The directional light to cast from.
 * @param camera_view The camera's view matrix.
 * @param camera_projection The camera's projection matrix.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_cascaded_shadow_map_update(
    CNA_CascadedShadowMapHandle shadow_map,
    const CNA_DirectionalLightEXT* light,
    const CNA_Matrix* camera_view,
    const CNA_Matrix* camera_projection);

/**
 * @brief Opens the pass for one cascade's region of the atlas.
 *
 * @param shadow_map The map.
 * @param cascade_index Which cascade, from zero to the count given at creation.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_cascaded_shadow_map_begin(
    CNA_CascadedShadowMapHandle shadow_map,
    int32_t cascade_index);

/**
 * @brief Closes the cascade pass opened by @ref cna_cascaded_shadow_map_begin.
 *
 * @param shadow_map The map.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_cascaded_shadow_map_end(CNA_CascadedShadowMapHandle shadow_map);

/**
 * @brief Returns how many cascades the map holds.
 *
 * @param shadow_map The map.
 * @param out_count Receives the count.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_cascaded_shadow_map_get_cascade_count(
    CNA_CascadedShadowMapHandle shadow_map,
    int32_t* out_count);

/**
 * @brief Returns one cascade's edge length in texels.
 *
 * @param shadow_map The map.
 * @param out_size Receives the size.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_cascaded_shadow_map_get_cascade_size(
    CNA_CascadedShadowMapHandle shadow_map,
    int32_t* out_size);

/**
 * @brief Returns the atlas texture, borrowed from the map.
 *
 * Release the borrow with @ref cna_render_target_destroy, which does not dispose the map's own
 * atlas. The map refuses to be destroyed while a borrow is outstanding.
 *
 * @param shadow_map The map.
 * @param out_texture Receives the borrowed Texture2D, or `CNA_INVALID_HANDLE` when unsupported.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_cascaded_shadow_map_get_shadow_texture(
    CNA_CascadedShadowMapHandle shadow_map,
    CNA_Handle* out_texture);

/**
 * @brief Returns the caster effect, borrowed from the map.
 *
 * @param shadow_map The map.
 * @param out_effect Receives the borrowed effect, or `CNA_INVALID_HANDLE` when unsupported.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_cascaded_shadow_map_get_caster_effect(
    CNA_CascadedShadowMapHandle shadow_map,
    CNA_EffectHandle* out_effect);

/**
 * @brief Returns one cascade's world-to-atlas transform.
 *
 * @param shadow_map The map.
 * @param cascade_index Which cascade.
 * @param out_matrix Receives the transform.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_cascaded_shadow_map_get_cascade_matrix(
    CNA_CascadedShadowMapHandle shadow_map,
    int32_t cascade_index,
    CNA_Matrix* out_matrix);

/**
 * @brief Returns the view-space distance at which one cascade ends.
 *
 * @param shadow_map The map.
 * @param cascade_index Which cascade.
 * @param out_distance Receives the distance.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_cascaded_shadow_map_get_split_distance(
    CNA_CascadedShadowMapHandle shadow_map,
    int32_t cascade_index,
    float* out_distance);

/**
 * @brief Returns the width in world units over which neighbouring cascades cross-fade.
 *
 * @param shadow_map The map.
 * @param out_band Receives the band.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_cascaded_shadow_map_get_blend_band(
    CNA_CascadedShadowMapHandle shadow_map,
    float* out_band);

/**
 * @brief Sets the cascade cross-fade width.
 *
 * @param shadow_map The map.
 * @param band The width in world units.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_cascaded_shadow_map_set_blend_band(
    CNA_CascadedShadowMapHandle shadow_map,
    float band);

/**
 * @brief Reports whether each cascade is tinted differently for diagnosis.
 *
 * @param shadow_map The map.
 * @param out_enabled Receives `CNA_TRUE` when debug tinting is on.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_cascaded_shadow_map_is_debug_tint_enabled(
    CNA_CascadedShadowMapHandle shadow_map,
    CNA_Bool* out_enabled);

/**
 * @brief Turns per-cascade debug tinting on or off.
 *
 * @param shadow_map The map.
 * @param enabled `CNA_TRUE` to tint each cascade differently.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for a non-canonical boolean,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_cascaded_shadow_map_set_debug_tint_enabled(
    CNA_CascadedShadowMapHandle shadow_map,
    CNA_Bool enabled);

/**
 * @brief Returns which cascade covers a view-space depth.
 *
 * @param shadow_map The map.
 * @param view_depth The depth to place.
 * @param out_index Receives the cascade index.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_cascaded_shadow_map_select_cascade(
    CNA_CascadedShadowMapHandle shadow_map,
    float view_depth,
    int32_t* out_index);

/**
 * @brief Returns the blend between uniform and logarithmic split placement.
 *
 * @param shadow_map The map.
 * @param out_lambda Receives the lambda.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_cascaded_shadow_map_get_split_lambda(
    CNA_CascadedShadowMapHandle shadow_map,
    float* out_lambda);

/**
 * @brief Sets the blend between uniform and logarithmic split placement.
 *
 * @param shadow_map The map.
 * @param lambda Zero for uniform splits, one for logarithmic.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_cascaded_shadow_map_set_split_lambda(
    CNA_CascadedShadowMapHandle shadow_map,
    float lambda);

/**
 * @brief Computes the split distances for a frustum, without a map.
 *
 * The canonical function returns a vector, so the C form is a caller-owned array and a required
 * count: pass a null destination with zero capacity to learn the count first.
 *
 * @param near_plane The camera's near plane.
 * @param far_plane The camera's far plane.
 * @param cascade_count How many cascades to place.
 * @param lambda Zero for uniform splits, one for logarithmic.
 * @param destination Caller-owned destination, or null only when @p capacity is zero.
 * @param capacity Destination capacity in elements.
 * @param out_count Receives the number of distances the call produces.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL`, `CNA_RESULT_NOT_SUPPORTED` without
 * the engine layer, or an error. No partial result is written.
 */
CNA_C_API CNA_Result cna_cascaded_shadow_map_compute_split_distances(
    float near_plane,
    float far_plane,
    int32_t cascade_count,
    float lambda,
    float* destination,
    uint64_t capacity,
    uint64_t* out_count);

/** @brief How many corners @ref cna_cascaded_shadow_map_compute_frustum_corners writes. */
#define CNA_FRUSTUM_CORNER_COUNT_EXT 8

/**
 * @brief Computes the eight world-space corners of a view-projection frustum.
 *
 * The canonical function returns a fixed-size array, so the C form takes a destination of exactly
 * @ref CNA_FRUSTUM_CORNER_COUNT_EXT elements rather than a count the caller could get wrong.
 *
 * @param view The view matrix.
 * @param projection The projection matrix.
 * @param out_corners Destination for eight corners.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_cascaded_shadow_map_compute_frustum_corners(
    const CNA_Matrix* view,
    const CNA_Matrix* projection,
    CNA_Vector3* out_corners);

/**
 * @brief Computes the bounding sphere of eight frustum corners.
 *
 * @param corners Eight corners, as @ref cna_cascaded_shadow_map_compute_frustum_corners writes.
 * @param out_centre Receives the sphere's centre.
 * @param out_radius Receives the sphere's radius.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_cascaded_shadow_map_compute_bounding_sphere(
    const CNA_Vector3* corners,
    CNA_Vector3* out_centre,
    float* out_radius);

/**
 * @brief Snaps a cascade centre to the shadow map's texel grid.
 *
 * Without this a cascade's contents shimmer as the camera moves, because the same world position
 * lands on a slightly different texel each frame.
 *
 * @param centre The cascade centre.
 * @param radius The cascade's bounding radius.
 * @param cascade_size The cascade's edge length in texels.
 * @param out_centre Receives the snapped centre.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_cascaded_shadow_map_snap_to_texel_grid(
    const CNA_Vector3* centre,
    float radius,
    int32_t cascade_size,
    CNA_Vector3* out_centre);

/**
 * @brief Releases the cascaded shadow map, its atlas and its effect.
 *
 * @param shadow_map The map; an invalid handle is an error, not a silent no-op.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_cascaded_shadow_map_destroy(CNA_CascadedShadowMapHandle shadow_map);

/** @brief How many faces a cube shadow map renders. */
#define CNA_CUBE_SHADOW_FACE_COUNT_EXT 6

/**
 * @brief Owned handle for one point-light cube shadow map.
 *
 * Release it with @ref cna_cube_shadow_map_destroy. Its cube texture and caster effect are borrows
 * that keep it alive, and destroying it is refused while one is outstanding.
 */
typedef CNA_Handle CNA_CubeShadowMapHandle;

/**
 * @brief Creates a point-light cube shadow map.
 *
 * @param graphics_device The device to render on.
 * @param quality One `CNA_SHADOW_QUALITY_*` identity.
 * @param out_shadow_map Receives the owned handle; set invalid on failure.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_cube_shadow_map_create(
    CNA_Handle graphics_device,
    CNA_ShadowQuality quality,
    CNA_CubeShadowMapHandle* out_shadow_map);

/**
 * @brief Reports whether this renderer can cast into the cube.
 *
 * @param shadow_map The map.
 * @param out_supported Receives `CNA_TRUE` when the caster shader exists and links.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_cube_shadow_map_is_supported(
    CNA_CubeShadowMapHandle shadow_map,
    CNA_Bool* out_supported);

/**
 * @brief Recomputes all six face transforms for a point light.
 *
 * Call this once per frame before opening any face.
 *
 * @param shadow_map The map.
 * @param light The point light to cast from.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_cube_shadow_map_update(
    CNA_CubeShadowMapHandle shadow_map,
    const CNA_PointLightEXT* light);

/**
 * @brief Opens the pass for one cube face.
 *
 * @param shadow_map The map.
 * @param face_index Which face, from zero to `CNA_CUBE_SHADOW_FACE_COUNT_EXT` minus one.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_cube_shadow_map_begin(
    CNA_CubeShadowMapHandle shadow_map,
    int32_t face_index);

/**
 * @brief Closes the face pass opened by @ref cna_cube_shadow_map_begin.
 *
 * @param shadow_map The map.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_cube_shadow_map_end(CNA_CubeShadowMapHandle shadow_map);

/**
 * @brief Returns the cube texture, borrowed from the map.
 *
 * Release the borrow with @ref cna_texturecube_destroy, which does not dispose the map's own cube.
 *
 * @param shadow_map The map.
 * @param out_texture Receives the borrowed TextureCube, or `CNA_INVALID_HANDLE` when unsupported.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_cube_shadow_map_get_shadow_texture(
    CNA_CubeShadowMapHandle shadow_map,
    CNA_Handle* out_texture);

/**
 * @brief Returns the caster effect, borrowed from the map.
 *
 * @param shadow_map The map.
 * @param out_effect Receives the borrowed effect, or `CNA_INVALID_HANDLE` when unsupported.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_cube_shadow_map_get_caster_effect(
    CNA_CubeShadowMapHandle shadow_map,
    CNA_EffectHandle* out_effect);

/**
 * @brief Returns one face's edge length in texels.
 *
 * @param shadow_map The map.
 * @param out_size Receives the size.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_cube_shadow_map_get_size(
    CNA_CubeShadowMapHandle shadow_map,
    int32_t* out_size);

/**
 * @brief Returns the quality preset the map was created with.
 *
 * @param shadow_map The map.
 * @param out_quality Receives the preset.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_cube_shadow_map_get_quality(
    CNA_CubeShadowMapHandle shadow_map,
    CNA_ShadowQuality* out_quality);

/**
 * @brief Returns the position of the light the map last updated for.
 *
 * @param shadow_map The map.
 * @param out_position Receives the position.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_cube_shadow_map_get_light_position(
    CNA_CubeShadowMapHandle shadow_map,
    CNA_Vector3* out_position);

/**
 * @brief Returns the range of the light the map last updated for.
 *
 * @param shadow_map The map.
 * @param out_range Receives the range.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_cube_shadow_map_get_light_range(
    CNA_CubeShadowMapHandle shadow_map,
    float* out_range);

/**
 * @brief Returns the depth bias applied when casting.
 *
 * @param shadow_map The map.
 * @param out_bias Receives the bias.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_cube_shadow_map_get_depth_bias(
    CNA_CubeShadowMapHandle shadow_map,
    float* out_bias);

/**
 * @brief Sets the depth bias applied when casting.
 *
 * @param shadow_map The map.
 * @param bias The bias.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_cube_shadow_map_set_depth_bias(
    CNA_CubeShadowMapHandle shadow_map,
    float bias);

/**
 * @brief Computes one cube face's view transform, without a map.
 *
 * @param face One `CNA_CUBE_MAP_FACE_*` identity.
 * @param position The light's world-space position.
 * @param out_matrix Receives the view transform.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_cube_shadow_map_compute_face_view(
    CNA_CubeMapFace face,
    const CNA_Vector3* position,
    CNA_Matrix* out_matrix);

/**
 * @brief Computes the projection every cube face shares, from the light's range.
 *
 * @param range The light's range.
 * @param out_matrix Receives the projection.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_cube_shadow_map_compute_face_projection(
    float range,
    CNA_Matrix* out_matrix);

/**
 * @brief Returns the face size a quality preset selects.
 *
 * @param quality One `CNA_SHADOW_QUALITY_*` identity.
 * @param out_size Receives the edge length in texels.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_cube_shadow_map_size_for_quality(
    CNA_ShadowQuality quality,
    int32_t* out_size);

/**
 * @brief Releases the cube shadow map, its cube and its effect.
 *
 * @param shadow_map The map; an invalid handle is an error, not a silent no-op.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_cube_shadow_map_destroy(CNA_CubeShadowMapHandle shadow_map);

/* ---------------------------------------------------------------------------------------------
 * The shadow-receiver contract
 *
 * `IShadowReceiverEXT` is an interface an effect implements, not an object a caller holds. C
 * cannot implement it and has nothing to hold, so what crosses this ABI is the set of operations
 * it declares, applied to an effect handle: each route resolves the effect and refuses with
 * `CNA_RESULT_INVALID_ARGUMENT` if that concrete effect is not a shadow receiver. This is the same
 * answer `PostProcessPass` got -- the operations cross, the type does not.
 * ------------------------------------------------------------------------------------------- */

/**
 * @brief Binds a shadow map for an effect to sample.
 *
 * The texture is borrowed: it must outlive every draw through the effect, and the effect does not
 * release it.
 *
 * @param effect An effect that implements the shadow-receiver contract.
 * @param shadow_map The Texture2D to sample, or `CNA_INVALID_HANDLE` to bind none.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` when the effect is not a shadow
 * receiver, or a documented handle failure.
 */
CNA_C_API CNA_Result cna_effect_set_shadow_map_ext(
    CNA_EffectHandle effect,
    CNA_Handle shadow_map);

/**
 * @brief Returns the shadow map an effect samples.
 *
 * The handle is a fresh name for the same texture, released with @ref cna_render_target_destroy;
 * it is not the handle originally passed to @ref cna_effect_set_shadow_map_ext, because the
 * canonical interface stores a raw pointer and has no handle to give back. **It does not keep the
 * texture alive**: the effect borrows the shadow map rather than owning it, so the texture's
 * lifetime remains yours, exactly as it is when you set it.
 *
 * @param effect An effect that implements the shadow-receiver contract.
 * @param out_shadow_map Receives the borrowed texture, or `CNA_INVALID_HANDLE` when none is bound.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` when the effect is not a shadow
 * receiver, or a documented handle failure.
 */
CNA_C_API CNA_Result cna_effect_get_shadow_map_ext(
    CNA_EffectHandle effect,
    CNA_Handle* out_shadow_map);

/**
 * @brief Sets the transform that takes world space into the bound shadow map.
 *
 * @param effect An effect that implements the shadow-receiver contract.
 * @param light_view_projection The transform.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` when the effect is not a shadow
 * receiver, or a documented handle failure.
 */
CNA_C_API CNA_Result cna_effect_set_light_view_projection_ext(
    CNA_EffectHandle effect,
    const CNA_Matrix* light_view_projection);

/**
 * @brief Returns the transform that takes world space into the bound shadow map.
 *
 * @param effect An effect that implements the shadow-receiver contract.
 * @param out_matrix Receives the transform.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` when the effect is not a shadow
 * receiver, or a documented handle failure.
 */
CNA_C_API CNA_Result cna_effect_get_light_view_projection_ext(
    CNA_EffectHandle effect,
    CNA_Matrix* out_matrix);

/**
 * @brief Turns shadow sampling on or off for an effect.
 *
 * @param effect An effect that implements the shadow-receiver contract.
 * @param enabled `CNA_TRUE` to sample the bound shadow map.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for a non-canonical boolean or an
 * effect that is not a shadow receiver, or a documented handle failure.
 */
CNA_C_API CNA_Result cna_effect_set_shadows_enabled_ext(
    CNA_EffectHandle effect,
    CNA_Bool enabled);

/**
 * @brief Reports whether an effect is sampling its shadow map.
 *
 * @param effect An effect that implements the shadow-receiver contract.
 * @param out_enabled Receives `CNA_TRUE` when shadow sampling is on.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` when the effect is not a shadow
 * receiver, or a documented handle failure.
 */
CNA_C_API CNA_Result cna_effect_is_shadows_enabled_ext(
    CNA_EffectHandle effect,
    CNA_Bool* out_enabled);

/**
 * @brief Sets the depth bias an effect applies when sampling its shadow map.
 *
 * @param effect An effect that implements the shadow-receiver contract.
 * @param bias The bias.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` when the effect is not a shadow
 * receiver, or a documented handle failure.
 */
CNA_C_API CNA_Result cna_effect_set_shadow_depth_bias_ext(CNA_EffectHandle effect, float bias);

/**
 * @brief Returns the depth bias an effect applies when sampling its shadow map.
 *
 * @param effect An effect that implements the shadow-receiver contract.
 * @param out_bias Receives the bias.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` when the effect is not a shadow
 * receiver, or a documented handle failure.
 */
CNA_C_API CNA_Result cna_effect_get_shadow_depth_bias_ext(
    CNA_EffectHandle effect,
    float* out_bias);

/**
 * @brief Sets how wide a filter an effect uses when sampling its shadow map.
 *
 * @param effect An effect that implements the shadow-receiver contract.
 * @param radius The filter radius in texels.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` when the effect is not a shadow
 * receiver, or a documented handle failure.
 */
CNA_C_API CNA_Result cna_effect_set_shadow_filter_radius_ext(
    CNA_EffectHandle effect,
    int32_t radius);

/**
 * @brief Returns how wide a filter an effect uses when sampling its shadow map.
 *
 * @param effect An effect that implements the shadow-receiver contract.
 * @param out_radius Receives the radius in texels.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` when the effect is not a shadow
 * receiver, or a documented handle failure.
 */
CNA_C_API CNA_Result cna_effect_get_shadow_filter_radius_ext(
    CNA_EffectHandle effect,
    int32_t* out_radius);

/**
 * @brief Gives an effect the cascaded-shadow state to sample with.
 *
 * @param effect An effect that implements the shadow-receiver contract.
 * @param state The cascade state, from @ref cna_shadow_cascade_state_ext_init.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` when the effect is not a shadow
 * receiver or the state was not initialized, or a documented handle failure.
 */
CNA_C_API CNA_Result cna_effect_set_shadow_cascades_ext(
    CNA_EffectHandle effect,
    const CNA_ShadowCascadeStateEXT* state);

/**
 * @brief Returns the cascaded-shadow state an effect samples with.
 *
 * @param effect An effect that implements the shadow-receiver contract.
 * @param out_state Receives the state.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` when the effect is not a shadow
 * receiver, or a documented handle failure.
 */
CNA_C_API CNA_Result cna_effect_get_shadow_cascades_ext(
    CNA_EffectHandle effect,
    CNA_ShadowCascadeStateEXT* out_state);

/**
 * @brief Gives an effect the punctual light to shade and shadow with.
 *
 * @param effect An effect that implements the shadow-receiver contract.
 * @param light The light, from @ref cna_punctual_light_ext_init.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` when the effect is not a shadow
 * receiver or the light was not initialized, or a documented handle failure.
 */
CNA_C_API CNA_Result cna_effect_set_punctual_light_ext(
    CNA_EffectHandle effect,
    const CNA_PunctualLightEXT* light);

/**
 * @brief Returns the punctual light an effect shades with.
 *
 * The light's two shadow-texture handles come back as `CNA_INVALID_HANDLE`: the canonical
 * structure holds raw pointers, and this ABI does not invent a name for a texture it does not
 * track. Every other field round-trips.
 *
 * @param effect An effect that implements the shadow-receiver contract.
 * @param out_light Receives the light.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` when the effect is not a shadow
 * receiver, or a documented handle failure.
 */
CNA_C_API CNA_Result cna_effect_get_punctual_light_ext(
    CNA_EffectHandle effect,
    CNA_PunctualLightEXT* out_light);

/**
 * @brief Applies a cascaded shadow map's whole state to a receiving effect.
 *
 * The C form of the canonical `applyToReceiver`, and the reason the receiver contract is bound at
 * all: it moves the atlas, the cascade transforms, the splits and the blend band across in one
 * call, so a caller cannot set half of them.
 *
 * @param shadow_map The cascaded shadow map.
 * @param effect An effect that implements the shadow-receiver contract.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` when the effect is not a shadow
 * receiver, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_cascaded_shadow_map_apply_to_receiver(
    CNA_CascadedShadowMapHandle shadow_map,
    CNA_EffectHandle effect);

/* ---------------------------------------------------------------------------------------------
 * The clustered shadow budget
 * ------------------------------------------------------------------------------------------- */

/** @brief How many shadow-casting clustered lights a policy admits unless told otherwise. */
#define CNA_CLUSTERED_SHADOW_DEFAULT_BUDGET_EXT INT32_C(4)

/** @brief The score margin a light must beat to displace one already selected. */
#define CNA_CLUSTERED_SHADOW_DEFAULT_HYSTERESIS_EXT 1.25F

/**
 * @brief Owned handle for one clustered shadow budget policy.
 *
 * The policy is a pure CPU object -- it needs no device -- but it is still parented to a game so
 * its lifetime is accounted for like every other owned resource.
 */
typedef CNA_Handle CNA_ClusteredShadowPolicyHandle;

/**
 * @brief Creates a shadow budget policy.
 *
 * @param game The owning game.
 * @param budget How many lights may cast shadows at once.
 * @param out_policy Receives the owned handle; set invalid on failure.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_clustered_shadow_policy_create(
    CNA_Handle game,
    int32_t budget,
    CNA_ClusteredShadowPolicyHandle* out_policy);

/**
 * @brief Returns how many lights may cast shadows at once.
 *
 * @param policy The policy.
 * @param out_budget Receives the budget.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_clustered_shadow_policy_get_budget(
    CNA_ClusteredShadowPolicyHandle policy,
    int32_t* out_budget);

/**
 * @brief Sets how many lights may cast shadows at once.
 *
 * @param policy The policy.
 * @param budget The new budget.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_clustered_shadow_policy_set_budget(
    CNA_ClusteredShadowPolicyHandle policy,
    int32_t budget);

/**
 * @brief Returns the margin a light must beat to displace one already selected.
 *
 * @param policy The policy.
 * @param out_hysteresis Receives the margin.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_clustered_shadow_policy_get_hysteresis(
    CNA_ClusteredShadowPolicyHandle policy,
    float* out_hysteresis);

/**
 * @brief Sets the margin a light must beat to displace one already selected.
 *
 * A margin above one is what stops two similarly-scored lights swapping the same shadow slot
 * every frame.
 *
 * @param policy The policy.
 * @param hysteresis The margin.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_clustered_shadow_policy_set_hysteresis(
    CNA_ClusteredShadowPolicyHandle policy,
    float hysteresis);

/**
 * @brief Copies the indices of the lights the policy currently admits.
 *
 * @param policy The policy.
 * @param destination Caller-owned destination, or null only when @p capacity is zero.
 * @param capacity Destination capacity in elements.
 * @param out_count Receives how many indices are selected.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL`, `CNA_RESULT_NOT_SUPPORTED` without
 * the engine layer, or an error. No partial result is written.
 */
CNA_C_API CNA_Result cna_clustered_shadow_policy_copy_selected(
    CNA_ClusteredShadowPolicyHandle policy,
    int32_t* destination,
    uint64_t capacity,
    uint64_t* out_count);

/**
 * @brief Reports whether one light index is currently admitted.
 *
 * @param policy The policy.
 * @param light_index The light to ask about.
 * @param out_selected Receives `CNA_TRUE` when the light may cast.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_clustered_shadow_policy_is_selected(
    CNA_ClusteredShadowPolicyHandle policy,
    int32_t light_index,
    CNA_Bool* out_selected);

/**
 * @brief Returns the score the policy last computed for one light.
 *
 * @param policy The policy.
 * @param light_index The light to ask about.
 * @param out_score Receives the score.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_clustered_shadow_policy_get_score(
    CNA_ClusteredShadowPolicyHandle policy,
    int32_t light_index,
    float* out_score);

/**
 * @brief Returns how many lights asked to cast a shadow.
 *
 * @param policy The policy.
 * @param out_count Receives the count.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_clustered_shadow_policy_get_request_count(
    CNA_ClusteredShadowPolicyHandle policy,
    int32_t* out_count);

/**
 * @brief Returns how many lights asked and were refused.
 *
 * This is the number that says whether the budget is too small, so it is worth logging rather
 * than only the selection.
 *
 * @param policy The policy.
 * @param out_count Receives the count.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_clustered_shadow_policy_get_refused_count(
    CNA_ClusteredShadowPolicyHandle policy,
    int32_t* out_count);

/**
 * @brief Forgets every selection and score.
 *
 * @param policy The policy.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_clustered_shadow_policy_reset(CNA_ClusteredShadowPolicyHandle policy);

/**
 * @brief Releases the policy.
 *
 * @param policy The policy; an invalid handle is an error, not a silent no-op.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_clustered_shadow_policy_destroy(CNA_ClusteredShadowPolicyHandle policy);

/* ---------------------------------------------------------------------------------------------
 * The depth/normal prepass
 *
 * The prepass renders linear depth and view-space normals for the screen-space effects that need
 * them. How many passes that takes depends on the renderer: one with multiple render targets,
 * otherwise two, or three when velocity is on -- ask @ref cna_depth_normal_prepass_get_pass_count
 * rather than assuming, and drive `begin`/`end` once per pass.
 * ------------------------------------------------------------------------------------------- */

/**
 * @brief Owned handle for one depth/normal prepass.
 *
 * Release it with @ref cna_depth_normal_prepass_destroy. Its effects and textures are borrows that
 * keep it alive, and destroying it is refused while one is outstanding.
 */
typedef CNA_Handle CNA_DepthNormalPrepassHandle;

/**
 * @brief Creates a depth/normal prepass at a target size.
 *
 * @param graphics_device The device to render on.
 * @param width Target width in pixels; must be positive.
 * @param height Target height in pixels; must be positive.
 * @param encoding How to store linear depth; `CNA_DEPTH_ENCODING_AUTOMATIC` lets the prepass
 *        decide, which is what @ref cna_depth_normal_prepass_uses_packed_depth_ext reports.
 * @param out_prepass Receives the owned handle; set invalid on failure.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for a non-positive size or an
 * undefined encoding, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_depth_normal_prepass_create(
    CNA_Handle graphics_device,
    int32_t width,
    int32_t height,
    CNA_DepthEncoding encoding,
    CNA_DepthNormalPrepassHandle* out_prepass);

/**
 * @brief Resizes the prepass targets.
 *
 * @param prepass The prepass.
 * @param width New width in pixels; must be positive.
 * @param height New height in pixels; must be positive.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for a non-positive size,
 * `CNA_RESULT_INVALID_STATE` while a pass is open, `CNA_RESULT_NOT_SUPPORTED` without the engine
 * layer, or an error.
 */
CNA_C_API CNA_Result cna_depth_normal_prepass_resize(
    CNA_DepthNormalPrepassHandle prepass,
    int32_t width,
    int32_t height);

/**
 * @brief Returns how many passes this renderer needs to fill the prepass.
 *
 * One where multiple render targets are available, otherwise two -- or three with velocity on.
 *
 * @param prepass The prepass.
 * @param out_count Receives the count.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_depth_normal_prepass_get_pass_count(
    CNA_DepthNormalPrepassHandle prepass,
    int32_t* out_count);

/**
 * @brief Opens one prepass pass.
 *
 * @param prepass The prepass.
 * @param pass_index Which pass, from zero to the count minus one.
 * @param view The camera's view matrix.
 * @param projection The camera's projection matrix.
 * @param near_plane The near plane; must be positive.
 * @param far_plane The far plane; must be beyond the near plane, because depth is normalised by it.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for planes that cannot normalise,
 * `CNA_RESULT_INVALID_STATE` when a pass is already open, `CNA_RESULT_NOT_SUPPORTED` without the
 * engine layer, or an error.
 */
CNA_C_API CNA_Result cna_depth_normal_prepass_begin(
    CNA_DepthNormalPrepassHandle prepass,
    int32_t pass_index,
    const CNA_Matrix* view,
    const CNA_Matrix* projection,
    float near_plane,
    float far_plane);

/**
 * @brief Closes the pass opened by @ref cna_depth_normal_prepass_begin.
 *
 * @param prepass The prepass.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_STATE` when no pass is open,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_depth_normal_prepass_end(CNA_DepthNormalPrepassHandle prepass);

/**
 * @brief Returns the rigid prepass effect, borrowed from the prepass.
 *
 * @param prepass The prepass.
 * @param out_effect Receives the borrowed effect, or `CNA_INVALID_HANDLE` when unsupported.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_depth_normal_prepass_get_prepass_effect(
    CNA_DepthNormalPrepassHandle prepass,
    CNA_EffectHandle* out_effect);

/**
 * @brief Returns the skinned prepass effect, borrowed from the prepass.
 *
 * @param prepass The prepass.
 * @param out_effect Receives the borrowed effect, or `CNA_INVALID_HANDLE` when unsupported.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_depth_normal_prepass_get_skinned_prepass_effect(
    CNA_DepthNormalPrepassHandle prepass,
    CNA_EffectHandle* out_effect);

/**
 * @brief Returns the linear-depth texture, borrowed from the prepass.
 *
 * Release the borrow with @ref cna_render_target_destroy, which does not dispose the prepass's own
 * target.
 *
 * @param prepass The prepass.
 * @param out_texture Receives the borrowed texture, or `CNA_INVALID_HANDLE` when there is none.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_depth_normal_prepass_get_depth_texture(
    CNA_DepthNormalPrepassHandle prepass,
    CNA_Handle* out_texture);

/**
 * @brief Returns the view-space normal texture, borrowed from the prepass.
 *
 * @param prepass The prepass.
 * @param out_texture Receives the borrowed texture, or `CNA_INVALID_HANDLE` when there is none.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_depth_normal_prepass_get_normal_texture(
    CNA_DepthNormalPrepassHandle prepass,
    CNA_Handle* out_texture);

/**
 * @brief Returns the velocity texture, borrowed from the prepass.
 *
 * @param prepass The prepass.
 * @param out_texture Receives the borrowed texture, or `CNA_INVALID_HANDLE` when velocity is off.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_depth_normal_prepass_get_velocity_texture_ext(
    CNA_DepthNormalPrepassHandle prepass,
    CNA_Handle* out_texture);

/**
 * @brief Reports whether the prepass can run on a device.
 *
 * @param prepass The prepass.
 * @param graphics_device The device to ask about.
 * @param out_supported Receives `CNA_TRUE` when the prepass shaders exist and link.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_depth_normal_prepass_is_supported(
    CNA_DepthNormalPrepassHandle prepass,
    CNA_Handle graphics_device,
    CNA_Bool* out_supported);

/**
 * @brief Reports whether the prepass fills its targets in one pass.
 *
 * @param prepass The prepass.
 * @param out_using Receives `CNA_TRUE` when multiple render targets are in use.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_depth_normal_prepass_is_using_multiple_render_targets(
    CNA_DepthNormalPrepassHandle prepass,
    CNA_Bool* out_using);

/**
 * @brief Reports whether depth is stored packed across an 8-bit target.
 *
 * @param prepass The prepass.
 * @param out_packed Receives `CNA_TRUE` when depth is packed.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_depth_normal_prepass_is_depth_packed(
    CNA_DepthNormalPrepassHandle prepass,
    CNA_Bool* out_packed);

/**
 * @brief Reports which depth encoding a device gets by default.
 *
 * The answer is a **measurement**, not a preference: a half-float depth target was measured to
 * make screen-space effects driven from the prepass occlude nothing, so the packed encoding is
 * what `CNA_DEPTH_ENCODING_AUTOMATIC` selects. Ask this rather than assuming either one.
 *
 * @param graphics_device The device to ask about.
 * @param out_packed Receives `CNA_TRUE` when the automatic encoding packs depth.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_depth_normal_prepass_uses_packed_depth_ext(
    CNA_Handle graphics_device,
    CNA_Bool* out_packed);

/**
 * @brief Returns the roughness the prepass writes alongside its normals.
 *
 * @param prepass The prepass.
 * @param out_roughness Receives the roughness.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_depth_normal_prepass_get_roughness(
    CNA_DepthNormalPrepassHandle prepass,
    float* out_roughness);

/**
 * @brief Sets the roughness the prepass writes alongside its normals.
 *
 * The value is **clamped** to zero-to-one rather than refused, exactly as the canonical setter
 * clamps it. It may be changed between draws inside one begin/end, which is how a scene with more
 * than one material describes itself.
 *
 * @param prepass The prepass.
 * @param roughness The roughness.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_depth_normal_prepass_set_roughness(
    CNA_DepthNormalPrepassHandle prepass,
    float roughness);

/**
 * @brief Reports whether the prepass also writes screen-space velocity.
 *
 * @param prepass The prepass.
 * @param out_enabled Receives `CNA_TRUE` when velocity is on.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_depth_normal_prepass_is_velocity_enabled_ext(
    CNA_DepthNormalPrepassHandle prepass,
    CNA_Bool* out_enabled);

/**
 * @brief Turns velocity output on or off, reallocating targets.
 *
 * @param prepass The prepass.
 * @param enabled `CNA_TRUE` to write velocity.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for a non-canonical boolean,
 * `CNA_RESULT_INVALID_STATE` while a pass is open, `CNA_RESULT_NOT_SUPPORTED` without the engine
 * layer, or an error.
 */
CNA_C_API CNA_Result cna_depth_normal_prepass_set_velocity_enabled_ext(
    CNA_DepthNormalPrepassHandle prepass,
    CNA_Bool enabled);

/**
 * @brief Gives the prepass the previous frame's world transform, for velocity.
 *
 * @param prepass The prepass.
 * @param previous_world The transform.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_depth_normal_prepass_set_previous_world_ext(
    CNA_DepthNormalPrepassHandle prepass,
    const CNA_Matrix* previous_world);

/**
 * @brief Gives the prepass the previous frame's camera, for velocity.
 *
 * @param prepass The prepass.
 * @param previous_view The previous view matrix.
 * @param previous_projection The previous projection matrix.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_depth_normal_prepass_set_previous_camera_ext(
    CNA_DepthNormalPrepassHandle prepass,
    const CNA_Matrix* previous_view,
    const CNA_Matrix* previous_projection);

/**
 * @brief Copies the GLSL a shader needs to decode this prepass's depth.
 *
 * @param packed `CNA_TRUE` for the packed encoding's decoder.
 * @param destination Caller-owned destination, or null only when @p capacity is zero.
 * @param capacity Destination capacity in bytes.
 * @param out_bytes Receives the required byte count without a terminator.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL`, `CNA_RESULT_NOT_SUPPORTED` without
 * the engine layer, or an error. No partial string is written.
 */
CNA_C_API CNA_Result cna_depth_normal_prepass_copy_depth_decode_glsl(
    CNA_Bool packed,
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

/**
 * @brief Copies the GLSL a shader needs to decode this prepass's velocity.
 *
 * @param destination Caller-owned destination, or null only when @p capacity is zero.
 * @param capacity Destination capacity in bytes.
 * @param out_bytes Receives the required byte count without a terminator.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL`, `CNA_RESULT_NOT_SUPPORTED` without
 * the engine layer, or an error. No partial string is written.
 */
CNA_C_API CNA_Result cna_depth_normal_prepass_copy_velocity_decode_glsl(
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

/**
 * @brief Reports whether a velocity texel carries a velocity at all.
 *
 * @param texel The texel read from the velocity texture.
 * @param out_has Receives `CNA_TRUE` when the texel encodes motion.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_depth_normal_prepass_has_velocity_ext(
    CNA_Color texel,
    CNA_Bool* out_has);

/**
 * @brief Decodes a velocity texel into screen-space motion.
 *
 * @param texel The texel read from the velocity texture.
 * @param out_velocity Receives the motion; zero when the texel carries none.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_depth_normal_prepass_decode_velocity_ext(
    CNA_Color texel,
    CNA_Vector2* out_velocity);

/**
 * @brief Packs a linear depth into four channel values.
 *
 * The canonical function writes four floats through references; the C form takes four outputs.
 *
 * @param value The depth to pack.
 * @param out_r Receives the red channel.
 * @param out_g Receives the green channel.
 * @param out_b Receives the blue channel.
 * @param out_a Receives the alpha channel.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_depth_normal_prepass_pack_depth(
    float value,
    float* out_r,
    float* out_g,
    float* out_b,
    float* out_a);

/**
 * @brief Unpacks four channel values back into a linear depth.
 *
 * @param r The red channel.
 * @param g The green channel.
 * @param b The blue channel.
 * @param a The alpha channel.
 * @param out_value Receives the depth.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_depth_normal_prepass_unpack_depth(
    float r,
    float g,
    float b,
    float a,
    float* out_value);

/**
 * @brief Releases the prepass, its targets and its effects.
 *
 * @param prepass The prepass; an invalid handle is an error, not a silent no-op.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_depth_normal_prepass_destroy(CNA_DepthNormalPrepassHandle prepass);

/* ---------------------------------------------------------------------------------------------
 * Contact shadows
 *
 * A `PostProcessPass`, so it is created here and then driven through the shared
 * `cna_post_process_pass_*` operations rather than through a handle kind of its own.
 * ------------------------------------------------------------------------------------------- */

/**
 * @brief Creates a screen-space contact-shadow pass.
 *
 * @param graphics_device The device to draw on.
 * @param out_pass Receives the owned post-process pass handle; set invalid on failure.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_contact_shadow_pass_create(
    CNA_Handle graphics_device,
    CNA_PostProcessPassHandle* out_pass);

/**
 * @brief Returns the direction the pass traces rays toward.
 *
 * @param pass A contact-shadow pass.
 * @param out_direction Receives the direction.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` when the pass is not a
 * contact-shadow pass, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_contact_shadow_pass_get_light_direction(
    CNA_PostProcessPassHandle pass,
    CNA_Vector3* out_direction);

/**
 * @brief Sets the direction the pass traces rays toward.
 *
 * @param pass A contact-shadow pass.
 * @param direction The direction.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` when the pass is not a
 * contact-shadow pass, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_contact_shadow_pass_set_light_direction(
    CNA_PostProcessPassHandle pass,
    const CNA_Vector3* direction);

/**
 * @brief Returns how far a ray travels before giving up.
 *
 * @param pass A contact-shadow pass.
 * @param out_distance Receives the distance.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` when the pass is not a
 * contact-shadow pass, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_contact_shadow_pass_get_max_distance(
    CNA_PostProcessPassHandle pass,
    float* out_distance);

/**
 * @brief Sets how far a ray travels before giving up.
 *
 * @param pass A contact-shadow pass.
 * @param distance The distance.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` when the pass is not a
 * contact-shadow pass, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_contact_shadow_pass_set_max_distance(
    CNA_PostProcessPassHandle pass,
    float distance);

/**
 * @brief Returns how many steps a ray takes.
 *
 * @param pass A contact-shadow pass.
 * @param out_count Receives the step count.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` when the pass is not a
 * contact-shadow pass, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_contact_shadow_pass_get_step_count(
    CNA_PostProcessPassHandle pass,
    int32_t* out_count);

/**
 * @brief Sets how many steps a ray takes.
 *
 * The canonical setter stores the value as given, so this route does too: a step count of zero
 * traces nothing, which is a way of switching the pass off rather than an error.
 *
 * @param pass A contact-shadow pass.
 * @param count The step count.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` when the pass is not a
 * contact-shadow pass, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_contact_shadow_pass_set_step_count(
    CNA_PostProcessPassHandle pass,
    int32_t count);

/**
 * @brief Returns how thick an occluder is assumed to be.
 *
 * @param pass A contact-shadow pass.
 * @param out_thickness Receives the thickness.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` when the pass is not a
 * contact-shadow pass, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_contact_shadow_pass_get_thickness(
    CNA_PostProcessPassHandle pass,
    float* out_thickness);

/**
 * @brief Sets how thick an occluder is assumed to be.
 *
 * @param pass A contact-shadow pass.
 * @param thickness The thickness.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` when the pass is not a
 * contact-shadow pass, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_contact_shadow_pass_set_thickness(
    CNA_PostProcessPassHandle pass,
    float thickness);

/**
 * @brief Returns how strongly the contact shadow darkens.
 *
 * @param pass A contact-shadow pass.
 * @param out_intensity Receives the intensity.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` when the pass is not a
 * contact-shadow pass, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_contact_shadow_pass_get_intensity(
    CNA_PostProcessPassHandle pass,
    float* out_intensity);

/**
 * @brief Sets how strongly the contact shadow darkens.
 *
 * @param pass A contact-shadow pass.
 * @param intensity The intensity.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` when the pass is not a
 * contact-shadow pass, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_contact_shadow_pass_set_intensity(
    CNA_PostProcessPassHandle pass,
    float intensity);

/**
 * @brief Returns the depth bias a ray uses before counting an occluder.
 *
 * @param pass A contact-shadow pass.
 * @param out_bias Receives the bias.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` when the pass is not a
 * contact-shadow pass, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_contact_shadow_pass_get_bias(
    CNA_PostProcessPassHandle pass,
    float* out_bias);

/**
 * @brief Sets the depth bias a ray uses before counting an occluder.
 *
 * @param pass A contact-shadow pass.
 * @param bias The bias.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` when the pass is not a
 * contact-shadow pass, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_contact_shadow_pass_set_bias(
    CNA_PostProcessPassHandle pass,
    float bias);

/**
 * @brief Copies the reason the pass fell back, as UTF-8 bytes without a terminator.
 *
 * An empty result means it did not fall back. This is the line worth logging when contact shadows
 * are missing from a frame that asked for them.
 *
 * @param pass A contact-shadow pass.
 * @param destination Caller-owned destination, or null only when @p capacity is zero.
 * @param capacity Destination capacity in bytes.
 * @param out_bytes Receives the required byte count without a terminator.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL`, `CNA_RESULT_INVALID_ARGUMENT` when
 * the pass is not a contact-shadow pass, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or
 * an error. No partial string is written.
 */
CNA_C_API CNA_Result cna_contact_shadow_pass_copy_fallback_reason(
    CNA_PostProcessPassHandle pass,
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

/**
 * @brief Reports whether a ray sample counts as occluded.
 *
 * A pure function of its arguments, so it needs no pass.
 *
 * @param ray_view_depth The ray's view-space depth.
 * @param scene_view_depth The scene's view-space depth at that pixel.
 * @param bias The depth bias.
 * @param thickness The assumed occluder thickness.
 * @param out_occluded Receives `CNA_TRUE` when the sample is occluded.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_contact_shadow_pass_is_occluded(
    float ray_view_depth,
    float scene_view_depth,
    float bias,
    float thickness,
    CNA_Bool* out_occluded);

/**
 * @brief Copies the GLSL occlusion test the pass uses, as UTF-8 bytes without a terminator.
 *
 * @param destination Caller-owned destination, or null only when @p capacity is zero.
 * @param capacity Destination capacity in bytes.
 * @param out_bytes Receives the required byte count without a terminator.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL`, `CNA_RESULT_NOT_SUPPORTED` without
 * the engine layer, or an error. No partial string is written.
 */
CNA_C_API CNA_Result cna_contact_shadow_pass_copy_occlusion_test_glsl(
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

/**
 * @brief Combines a shadow-map visibility with a contact-shadow visibility.
 *
 * Both inputs are clamped to zero-to-one before multiplying, exactly as the canonical function
 * clamps them, so a caller cannot brighten a pixel by passing a visibility above one.
 *
 * @param shadow_map_visibility Visibility from the shadow map.
 * @param contact_visibility Visibility from the contact-shadow pass.
 * @param out_visibility Receives the combined visibility.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_contact_shadow_pass_combine_visibility(
    float shadow_map_visibility,
    float contact_visibility,
    float* out_visibility);

/* ---------------------------------------------------------------------------------------------
 * Clustered lights
 *
 * A clustered light is a **value**, and the set is a collection of values rather than of objects.
 * Nothing here hands out a view onto something the set owns, so unlike the render-target pool or
 * the shadow maps there is no borrow to count and no destroy-time refusal: reading a light copies
 * it out.
 * ------------------------------------------------------------------------------------------- */

/** @brief Fixed-width identity of which kind of light a clustered light is. */
typedef uint32_t CNA_ClusteredLightType;

/** @brief A point light: radiates in every direction from its position. */
#define CNA_CLUSTERED_LIGHT_TYPE_POINT UINT32_C(0)
/** @brief A spot light: a point light restricted to a cone. */
#define CNA_CLUSTERED_LIGHT_TYPE_SPOT UINT32_C(1)

/** @brief One light in a clustered light set. */
typedef struct CNA_ClusteredLightEXT {
    /** @brief Size of this structure in bytes. */
    uint32_t struct_size;
    /** @brief Version of this structure. */
    uint32_t struct_version;
    /** @brief Which kind of light this is. */
    CNA_ClusteredLightType type;
    /** @brief `CNA_TRUE` when this light should be given a shadow. */
    CNA_Bool casts_shadows;
    /** @brief Padding; write zero. */
    uint8_t reserved[3];
    /** @brief World-space position. */
    CNA_Vector3 position;
    /** @brief The direction a spot light points; the default points straight down. */
    CNA_Vector3 direction;
    /** @brief Linear RGB colour; the default is white. */
    CNA_Vector3 color;
    /** @brief Scalar multiplier on @ref color; must not be negative. */
    float intensity;
    /** @brief Distance at which the light stops contributing; must be positive. */
    float range;
    /** @brief Half-angle in radians inside which a spot light is at full strength. */
    float inner_angle;
    /** @brief Half-angle in radians at which a spot light has fallen to nothing. */
    float outer_angle;
} CNA_ClusteredLightEXT;

/**
 * @brief Fills a clustered light with the canonical defaults.
 *
 * @param out_light Receives a point light at the origin: white, intensity 1, range 20, inner angle
 * 0.35, outer angle 0.5, direction (0, -1, 0) and no shadows.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null output.
 */
CNA_C_API CNA_Result cna_clustered_light_ext_init(CNA_ClusteredLightEXT* out_light);

/**
 * @brief Reports whether a light is usable in a set.
 *
 * A range must be positive, an intensity non-negative and finite, every vector finite, and a spot
 * light's direction non-degenerate with its inner angle no wider than its outer. This is the same
 * test @ref cna_clustered_light_set_add applies, exposed so a caller can ask before being refused.
 *
 * @param light The light to test.
 * @param out_usable Receives `CNA_TRUE` when the light would be accepted.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_clustered_light_set_is_usable(
    const CNA_ClusteredLightEXT* light,
    CNA_Bool* out_usable);

/** @brief The greatest number of lights a clustered light set holds. */
#define CNA_CLUSTERED_LIGHT_SET_MAX_EXT INT32_C(256)

/**
 * @brief Owned handle for one clustered light set.
 *
 * The set holds values, so nothing it returns keeps it alive and nothing it lends has to be
 * released. Destroy it with @ref cna_clustered_light_set_destroy.
 */
typedef CNA_Handle CNA_ClusteredLightSetHandle;

/**
 * @brief Creates an empty clustered light set.
 *
 * The set needs no device, but it is parented to a game so its lifetime is accounted for like
 * every other owned resource.
 *
 * @param game The owning game.
 * @param out_set Receives the owned handle; set invalid on failure.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_clustered_light_set_create(
    CNA_Handle game,
    CNA_ClusteredLightSetHandle* out_set);

/**
 * @brief Adds a clustered light, returning its index.
 *
 * @param set The set.
 * @param light The light to add; must be usable.
 * @param out_index Receives the new light's index.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for an unusable light,
 * `CNA_RESULT_INVALID_STATE` when the set already holds `CNA_CLUSTERED_LIGHT_SET_MAX_EXT` lights,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_clustered_light_set_add(
    CNA_ClusteredLightSetHandle set,
    const CNA_ClusteredLightEXT* light,
    int32_t* out_index);

/**
 * @brief Adds a point light, converting it to a clustered light.
 *
 * @param set The set.
 * @param light The point light to add.
 * @param out_index Receives the new light's index.
 * @return As @ref cna_clustered_light_set_add.
 */
CNA_C_API CNA_Result cna_clustered_light_set_add_point(
    CNA_ClusteredLightSetHandle set,
    const CNA_PointLightEXT* light,
    int32_t* out_index);

/**
 * @brief Adds a spot light, converting it to a clustered light.
 *
 * @param set The set.
 * @param light The spot light to add.
 * @param out_index Receives the new light's index.
 * @return As @ref cna_clustered_light_set_add.
 */
CNA_C_API CNA_Result cna_clustered_light_set_add_spot(
    CNA_ClusteredLightSetHandle set,
    const CNA_SpotLightEXT* light,
    int32_t* out_index);

/**
 * @brief Replaces the light at an index.
 *
 * @param set The set.
 * @param index The index to replace.
 * @param light The replacement; must be usable.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for an unusable light or an index the
 * set does not hold, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_clustered_light_set_replace_at(
    CNA_ClusteredLightSetHandle set,
    int32_t index,
    const CNA_ClusteredLightEXT* light);

/**
 * @brief Removes the light at an index, shifting the ones after it down.
 *
 * @param set The set.
 * @param index The index to remove.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for an index the set does not hold,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_clustered_light_set_remove_at(
    CNA_ClusteredLightSetHandle set,
    int32_t index);

/**
 * @brief Removes every light.
 *
 * @param set The set.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_clustered_light_set_clear(CNA_ClusteredLightSetHandle set);

/**
 * @brief Returns how many lights the set holds.
 *
 * @param set The set.
 * @param out_count Receives the count.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_clustered_light_set_get_count(
    CNA_ClusteredLightSetHandle set,
    int32_t* out_count);

/**
 * @brief Reports whether the set holds no lights.
 *
 * @param set The set.
 * @param out_empty Receives `CNA_TRUE` when the set is empty.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_clustered_light_set_is_empty(
    CNA_ClusteredLightSetHandle set,
    CNA_Bool* out_empty);

/**
 * @brief Copies out the light at an index.
 *
 * The light is a value, so this is a copy rather than a view: it stays correct after the set
 * changes, and nothing has to be released.
 *
 * @param set The set.
 * @param index The index to read.
 * @param out_light Receives the light.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for an index the set does not hold,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_clustered_light_set_get_at(
    CNA_ClusteredLightSetHandle set,
    int32_t index,
    CNA_ClusteredLightEXT* out_light);

/**
 * @brief Copies out every light the set holds.
 *
 * @param set The set.
 * @param destination Caller-owned destination, or null only when @p capacity is zero.
 * @param capacity Destination capacity in elements.
 * @param out_count Receives how many lights the set holds.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL`, `CNA_RESULT_NOT_SUPPORTED` without
 * the engine layer, or an error. No partial result is written.
 */
CNA_C_API CNA_Result cna_clustered_light_set_copy_lights(
    CNA_ClusteredLightSetHandle set,
    CNA_ClusteredLightEXT* destination,
    uint64_t capacity,
    uint64_t* out_count);

/**
 * @brief Returns the bounding sphere of the light at an index.
 *
 * @param set The set.
 * @param index The index to read.
 * @param out_bounds Receives the sphere.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for an index the set does not hold,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_clustered_light_set_get_bounds_at(
    CNA_ClusteredLightSetHandle set,
    int32_t index,
    CNA_BoundingSphere* out_bounds);

/**
 * @brief Copies out the bounding sphere of every light.
 *
 * @param set The set.
 * @param destination Caller-owned destination, or null only when @p capacity is zero.
 * @param capacity Destination capacity in elements.
 * @param out_count Receives how many spheres the set produces.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL`, `CNA_RESULT_NOT_SUPPORTED` without
 * the engine layer, or an error. No partial result is written.
 */
CNA_C_API CNA_Result cna_clustered_light_set_copy_bounds(
    CNA_ClusteredLightSetHandle set,
    CNA_BoundingSphere* destination,
    uint64_t capacity,
    uint64_t* out_count);

/**
 * @brief Releases the light set.
 *
 * @param set The set; an invalid handle is an error, not a silent no-op.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_clustered_light_set_destroy(CNA_ClusteredLightSetHandle set);

/* ---------------------------------------------------------------------------------------------
 * The cluster grid, the light assignment and the upload buffer
 *
 * Three objects that consume a light set. The grid divides the view frustum into clusters, the
 * assignment sorts lights into them, and the buffer uploads the result for a shader to read. The
 * grid and the assignment are pure CPU objects; only the buffer needs a device.
 * ------------------------------------------------------------------------------------------- */

/** @brief The most tiles a cluster grid takes along either screen axis. */
#define CNA_CLUSTER_GRID_MAX_TILES_PER_AXIS_EXT INT32_C(128)
/** @brief The most depth slices a cluster grid takes. */
#define CNA_CLUSTER_GRID_MAX_SLICE_COUNT_EXT INT32_C(256)
/** @brief The default tile count along X. */
#define CNA_CLUSTER_GRID_DEFAULT_TILES_X_EXT INT32_C(16)
/** @brief The default tile count along Y. */
#define CNA_CLUSTER_GRID_DEFAULT_TILES_Y_EXT INT32_C(8)
/** @brief The default depth-slice count. */
#define CNA_CLUSTER_GRID_DEFAULT_SLICE_COUNT_EXT INT32_C(24)

/**
 * @brief Owned handle for one cluster grid.
 *
 * A pure CPU object; it is parented to a game only so its lifetime is accounted for.
 */
typedef CNA_Handle CNA_ClusteredLightGridHandle;

/**
 * @brief Creates a cluster grid.
 *
 * @param game The owning game.
 * @param tiles_x Tiles along X, from 1 to `CNA_CLUSTER_GRID_MAX_TILES_PER_AXIS_EXT`.
 * @param tiles_y Tiles along Y, same range.
 * @param slice_count Depth slices, from 1 to `CNA_CLUSTER_GRID_MAX_SLICE_COUNT_EXT`.
 * @param out_grid Receives the owned handle; set invalid on failure.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for a dimension outside its range —
 * the cluster count is what the light-index list is sized from, so it is refused rather than
 * clamped — `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_clustered_light_grid_create(
    CNA_Handle game,
    int32_t tiles_x,
    int32_t tiles_y,
    int32_t slice_count,
    CNA_ClusteredLightGridHandle* out_grid);

/**
 * @brief Returns the grid's tile count along X.
 *
 * @param grid The grid.
 * @param out_tiles Receives the count.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_clustered_light_grid_get_tiles_x(
    CNA_ClusteredLightGridHandle grid,
    int32_t* out_tiles);

/**
 * @brief Returns the grid's tile count along Y.
 *
 * @param grid The grid.
 * @param out_tiles Receives the count.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_clustered_light_grid_get_tiles_y(
    CNA_ClusteredLightGridHandle grid,
    int32_t* out_tiles);

/**
 * @brief Returns the grid's depth-slice count.
 *
 * @param grid The grid.
 * @param out_slices Receives the count.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_clustered_light_grid_get_slice_count(
    CNA_ClusteredLightGridHandle grid,
    int32_t* out_slices);

/**
 * @brief Returns how many clusters the grid holds.
 *
 * @param grid The grid.
 * @param out_count Receives the count.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_clustered_light_grid_get_cluster_count(
    CNA_ClusteredLightGridHandle grid,
    int32_t* out_count);

/**
 * @brief Returns the flat index of a cluster coordinate.
 *
 * @param grid The grid.
 * @param x Tile along X.
 * @param y Tile along Y.
 * @param slice Depth slice.
 * @param out_index Receives the flat index.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for a coordinate outside the grid,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_clustered_light_grid_cluster_index(
    CNA_ClusteredLightGridHandle grid,
    int32_t x,
    int32_t y,
    int32_t slice,
    int32_t* out_index);

/**
 * @brief Gives the grid its shape from a camera projection.
 *
 * @param grid The grid.
 * @param projection The camera's projection matrix; must be invertible.
 * @param near_plane The near distance; must be positive.
 * @param far_plane The far distance; must exceed the near.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` when the planes cannot space the
 * slices — the spacing is a ratio of the two, so a zero near plane has no logarithm and an
 * inverted pair has no grid — or when the matrix will not invert, `CNA_RESULT_NOT_SUPPORTED`
 * without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_clustered_light_grid_set_projection(
    CNA_ClusteredLightGridHandle grid,
    const CNA_Matrix* projection,
    float near_plane,
    float far_plane);

/**
 * @brief Reports whether the grid has been given a projection.
 *
 * Until it has, it has no shape: @ref cna_clustered_light_grid_cluster_bounds refuses and an
 * assignment cannot sort into it.
 *
 * @param grid The grid.
 * @param out_has Receives `CNA_TRUE` when a projection has been set.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_clustered_light_grid_has_projection(
    CNA_ClusteredLightGridHandle grid,
    CNA_Bool* out_has);

/**
 * @brief Returns the near distance the grid was given.
 *
 * @param grid The grid.
 * @param out_near Receives the distance.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_clustered_light_grid_get_near_plane(
    CNA_ClusteredLightGridHandle grid,
    float* out_near);

/**
 * @brief Returns the far distance the grid was given.
 *
 * @param grid The grid.
 * @param out_far Receives the distance.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_clustered_light_grid_get_far_plane(
    CNA_ClusteredLightGridHandle grid,
    float* out_far);

/**
 * @brief Returns the inverse of the projection the grid was given.
 *
 * @param grid The grid.
 * @param out_matrix Receives the inverse.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_clustered_light_grid_get_inverse_projection(
    CNA_ClusteredLightGridHandle grid,
    CNA_Matrix* out_matrix);

/**
 * @brief Returns the view distance at which a depth slice begins.
 *
 * **The slice count itself is a valid argument**, and names the far edge of the last slice — there
 * are one more boundaries than slices. A caller that rejected it would lose the far plane.
 *
 * @param grid The grid.
 * @param slice The slice boundary, from zero to the slice count **inclusive**.
 * @param out_distance Receives the distance; zero when no projection has been set.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for a boundary outside that range,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_clustered_light_grid_slice_distance(
    CNA_ClusteredLightGridHandle grid,
    int32_t slice,
    float* out_distance);

/**
 * @brief Returns which slice covers a view distance.
 *
 * The result is **clamped** into the grid rather than refused, exactly as the canonical function
 * clamps it: a point behind the near plane belongs to the first slice and one beyond the far
 * plane to the last, which is what a renderer wants when a light straddles the frustum edge.
 *
 * @param grid The grid.
 * @param view_distance The distance to place.
 * @param out_slice Receives the slice index.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_clustered_light_grid_slice_for_view_distance(
    CNA_ClusteredLightGridHandle grid,
    float view_distance,
    int32_t* out_slice);

/**
 * @brief Returns the view-space bounds of one cluster.
 *
 * @param grid The grid.
 * @param x Tile along X.
 * @param y Tile along Y.
 * @param slice Depth slice.
 * @param out_bounds Receives the bounds.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for a coordinate outside the grid,
 * `CNA_RESULT_INVALID_STATE` when no projection has been set — the grid has no shape yet —
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_clustered_light_grid_cluster_bounds(
    CNA_ClusteredLightGridHandle grid,
    int32_t x,
    int32_t y,
    int32_t slice,
    CNA_BoundingBox* out_bounds);

/**
 * @brief Releases the grid.
 *
 * @param grid The grid; an invalid handle is an error, not a silent no-op.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_clustered_light_grid_destroy(CNA_ClusteredLightGridHandle grid);

/** @brief The most lights one assignment sorts; a scene needing more wants a second grid. */
#define CNA_CLUSTERED_ASSIGNMENT_MAX_LIGHTS_EXT INT32_C(1024)

/**
 * @brief Owned handle for one light-to-cluster assignment.
 *
 * A pure CPU object. Its index and offset arrays are read by copy, so nothing it returns keeps it
 * alive and destruction is never refused.
 */
typedef CNA_Handle CNA_ClusteredLightAssignmentHandle;

/**
 * @brief Creates an empty assignment.
 *
 * @param game The owning game.
 * @param out_assignment Receives the owned handle; set invalid on failure.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_clustered_light_assignment_create(
    CNA_Handle game,
    CNA_ClusteredLightAssignmentHandle* out_assignment);

/**
 * @brief Sorts a set of light bounds into a grid's clusters.
 *
 * The bounds are what @ref cna_clustered_light_set_copy_bounds produces, so a caller sorts the set
 * it already built rather than describing the lights twice.
 *
 * @param assignment The assignment.
 * @param grid The grid to sort into; must have a projection.
 * @param view The camera's view matrix.
 * @param bounds Array of light bounding spheres, in light-index order.
 * @param bounds_count How many spheres, at most `CNA_CLUSTERED_ASSIGNMENT_MAX_LIGHTS_EXT`.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for more lights than the index list
 * is sized for, `CNA_RESULT_INVALID_STATE` when the grid has no projection,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_clustered_light_assignment_assign(
    CNA_ClusteredLightAssignmentHandle assignment,
    CNA_ClusteredLightGridHandle grid,
    const CNA_Matrix* view,
    const CNA_BoundingSphere* bounds,
    uint64_t bounds_count);

/**
 * @brief Forgets every assignment.
 *
 * @param assignment The assignment.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_clustered_light_assignment_clear(
    CNA_ClusteredLightAssignmentHandle assignment);

/**
 * @brief Takes an assignment computed elsewhere — on the GPU, or by a caller's own sorter.
 *
 * The offsets describe where each cluster's run of light indices begins, so there is one more
 * offset than cluster.
 *
 * @param assignment The assignment.
 * @param light_count How many lights the indices may name.
 * @param offsets Cluster offsets; must begin at zero, never go backwards, and end at
 *        @p index_count.
 * @param offset_count How many offsets; at least one.
 * @param indices Light indices; each must be below @p light_count.
 * @param index_count How many indices.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` when the offsets do not begin at
 * zero, go backwards, do not end at the index count, or an index names a light that is not in the
 * set, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_clustered_light_assignment_adopt(
    CNA_ClusteredLightAssignmentHandle assignment,
    int32_t light_count,
    const int32_t* offsets,
    uint64_t offset_count,
    const int32_t* indices,
    uint64_t index_count);

/**
 * @brief Returns how many lights the assignment describes.
 *
 * @param assignment The assignment.
 * @param out_count Receives the count.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_clustered_light_assignment_get_light_count(
    CNA_ClusteredLightAssignmentHandle assignment,
    int32_t* out_count);

/**
 * @brief Returns how many clusters the assignment describes.
 *
 * @param assignment The assignment.
 * @param out_count Receives the count.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_clustered_light_assignment_get_cluster_count(
    CNA_ClusteredLightAssignmentHandle assignment,
    int32_t* out_count);

/**
 * @brief Copies out the light indices assigned to one cluster.
 *
 * The canonical accessor returns a span into the assignment's own storage; the C form copies,
 * so the result stays correct after the assignment is recomputed.
 *
 * @param assignment The assignment.
 * @param cluster_index Which cluster.
 * @param destination Caller-owned destination, or null only when @p capacity is zero.
 * @param capacity Destination capacity in elements.
 * @param out_count Receives how many lights that cluster has.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL`, `CNA_RESULT_INVALID_ARGUMENT` for a
 * cluster outside the assigned range, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an
 * error. No partial result is written.
 */
CNA_C_API CNA_Result cna_clustered_light_assignment_copy_lights_in_cluster(
    CNA_ClusteredLightAssignmentHandle assignment,
    int32_t cluster_index,
    int32_t* destination,
    uint64_t capacity,
    uint64_t* out_count);

/**
 * @brief Copies out the whole index array.
 *
 * @param assignment The assignment.
 * @param destination Caller-owned destination, or null only when @p capacity is zero.
 * @param capacity Destination capacity in elements.
 * @param out_count Receives how many indices there are.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL`, `CNA_RESULT_NOT_SUPPORTED` without
 * the engine layer, or an error. No partial result is written.
 */
CNA_C_API CNA_Result cna_clustered_light_assignment_copy_indices(
    CNA_ClusteredLightAssignmentHandle assignment,
    int32_t* destination,
    uint64_t capacity,
    uint64_t* out_count);

/**
 * @brief Copies out the whole offset array.
 *
 * @param assignment The assignment.
 * @param destination Caller-owned destination, or null only when @p capacity is zero.
 * @param capacity Destination capacity in elements.
 * @param out_count Receives how many offsets there are — one more than the cluster count.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL`, `CNA_RESULT_NOT_SUPPORTED` without
 * the engine layer, or an error. No partial result is written.
 */
CNA_C_API CNA_Result cna_clustered_light_assignment_copy_offsets(
    CNA_ClusteredLightAssignmentHandle assignment,
    int32_t* destination,
    uint64_t capacity,
    uint64_t* out_count);

/**
 * @brief Returns how many light references the assignment holds in total.
 *
 * @param assignment The assignment.
 * @param out_count Receives the count.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_clustered_light_assignment_get_total_reference_count(
    CNA_ClusteredLightAssignmentHandle assignment,
    int32_t* out_count);

/**
 * @brief Returns the largest number of lights any one cluster holds.
 *
 * The number worth watching: it sizes the shader's per-cluster loop.
 *
 * @param assignment The assignment.
 * @param out_count Receives the count.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_clustered_light_assignment_get_max_lights_per_cluster(
    CNA_ClusteredLightAssignmentHandle assignment,
    int32_t* out_count);

/**
 * @brief Releases the assignment.
 *
 * @param assignment The assignment; an invalid handle is an error, not a silent no-op.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_clustered_light_assignment_destroy(
    CNA_ClusteredLightAssignmentHandle assignment);

/**
 * @brief Owned handle for one clustered light upload buffer.
 *
 * The buffer owns three textures and **lends none of them**: the canonical class exposes no
 * accessor for them, only @ref cna_clustered_light_buffer_bind, so there is no borrow to count and
 * destruction is never refused for an outstanding view.
 */
typedef CNA_Handle CNA_ClusteredLightBufferHandle;

/**
 * @brief Creates a clustered light upload buffer.
 *
 * @param graphics_device The device to upload on.
 * @param out_buffer Receives the owned handle; set invalid on failure.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_clustered_light_buffer_create(
    CNA_Handle graphics_device,
    CNA_ClusteredLightBufferHandle* out_buffer);

/**
 * @brief Uploads a light set, a grid and an assignment as one consistent trio.
 *
 * The three must agree: the assignment's light indices are positions in the set and its cluster
 * indices are positions in the grid. A mismatched trio is **refused**, because uploading it would
 * light the wrong objects with the wrong lamps rather than fail visibly.
 *
 * @param buffer The buffer.
 * @param lights The light set.
 * @param grid The grid.
 * @param assignment The assignment.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` when the trio disagrees,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_clustered_light_buffer_upload(
    CNA_ClusteredLightBufferHandle buffer,
    CNA_ClusteredLightSetHandle lights,
    CNA_ClusteredLightGridHandle grid,
    CNA_ClusteredLightAssignmentHandle assignment);

/**
 * @brief Binds the uploaded buffer's three textures to consecutive units of an effect.
 *
 * @param buffer The buffer.
 * @param effect The shader effect to bind into.
 * @param first_unit The first of three consecutive texture units.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_STATE` when nothing has been uploaded — there
 * is no light list to bind — `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_clustered_light_buffer_bind(
    CNA_ClusteredLightBufferHandle buffer,
    CNA_EffectHandle effect,
    int32_t first_unit);

/**
 * @brief Reports whether anything has been uploaded yet.
 *
 * @param buffer The buffer.
 * @param out_uploaded Receives `CNA_TRUE` once an upload has succeeded.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_clustered_light_buffer_is_uploaded(
    CNA_ClusteredLightBufferHandle buffer,
    CNA_Bool* out_uploaded);

/**
 * @brief Returns how many lights the last upload carried.
 *
 * @param buffer The buffer.
 * @param out_count Receives the count.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_clustered_light_buffer_get_light_count(
    CNA_ClusteredLightBufferHandle buffer,
    int32_t* out_count);

/**
 * @brief Returns how many clusters the last upload carried.
 *
 * @param buffer The buffer.
 * @param out_count Receives the count.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_clustered_light_buffer_get_cluster_count(
    CNA_ClusteredLightBufferHandle buffer,
    int32_t* out_count);

/**
 * @brief Returns how many light references the last upload carried.
 *
 * @param buffer The buffer.
 * @param out_count Receives the count.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_clustered_light_buffer_get_reference_count(
    CNA_ClusteredLightBufferHandle buffer,
    int32_t* out_count);

/**
 * @brief Copies the GLSL a shader needs to read the uploaded light list.
 *
 * @param destination Caller-owned destination, or null only when @p capacity is zero.
 * @param capacity Destination capacity in bytes.
 * @param out_bytes Receives the required byte count without a terminator.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL`, `CNA_RESULT_NOT_SUPPORTED` without
 * the engine layer, or an error. No partial string is written.
 */
CNA_C_API CNA_Result cna_clustered_light_buffer_copy_light_lookup_glsl(
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

/**
 * @brief Releases the buffer and its textures.
 *
 * @param buffer The buffer; an invalid handle is an error, not a silent no-op.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_clustered_light_buffer_destroy(CNA_ClusteredLightBufferHandle buffer);

/* ---------------------------------------------------------------------------------------------
 * The clustered forward effect and the compute assignment
 * ------------------------------------------------------------------------------------------- */

/** @brief The most lights one fragment walks before the shader stops accumulating. */
#define CNA_CLUSTERED_FORWARD_MAX_LIGHTS_PER_FRAGMENT_EXT INT32_C(128)

/**
 * @brief Owned handle for one clustered forward effect.
 *
 * Release it with @ref cna_clustered_forward_effect_destroy. Its shader effect is a borrow that
 * keeps it alive, and destroying it is refused while one is outstanding.
 */
typedef CNA_Handle CNA_ClusteredForwardEffectHandle;

/**
 * @brief Creates a clustered forward effect.
 *
 * Creation succeeds on a renderer that cannot run it; ask
 * @ref cna_clustered_forward_effect_is_supported.
 *
 * @param graphics_device The device to compile on.
 * @param out_effect Receives the owned handle; set invalid on failure.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_clustered_forward_effect_create(
    CNA_Handle graphics_device,
    CNA_ClusteredForwardEffectHandle* out_effect);

/**
 * @brief Reports whether the effect's shader exists and links.
 *
 * @param effect The effect.
 * @param out_supported Receives `CNA_TRUE` when the effect can shade.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_clustered_forward_effect_is_supported(
    CNA_ClusteredForwardEffectHandle effect,
    CNA_Bool* out_supported);

/**
 * @brief Prepares the effect to shade with an uploaded light buffer.
 *
 * @param effect The effect.
 * @param world The world transform.
 * @param view The camera's view matrix.
 * @param projection The camera's projection matrix.
 * @param camera_position The camera's world-space position.
 * @param lights An uploaded clustered light buffer.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_STATE` when the buffer holds nothing — there is
 * no cluster table for the shader to walk — or when the material transmits and no opaque frame has
 * been given to refract against, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 *
 * The transmission case is a refusal rather than an approximation on purpose: a transmissive
 * material drawn without a frame to refract is not slightly wrong, it is an opaque object where a
 * glass one was asked for. See @ref cna_clustered_forward_effect_set_opaque_frame.
 */
CNA_C_API CNA_Result cna_clustered_forward_effect_begin(
    CNA_ClusteredForwardEffectHandle effect,
    const CNA_Matrix* world,
    const CNA_Matrix* view,
    const CNA_Matrix* projection,
    const CNA_Vector3* camera_position,
    CNA_ClusteredLightBufferHandle lights);

/**
 * @brief Returns the underlying shader effect, borrowed from this effect.
 *
 * @param effect The effect.
 * @param out_shader Receives the borrowed effect, or `CNA_INVALID_HANDLE` when unsupported.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_clustered_forward_effect_get_effect(
    CNA_ClusteredForwardEffectHandle effect,
    CNA_EffectHandle* out_shader);

/**
 * @brief Reports whether an area light is bound.
 *
 * @param effect The effect.
 * @param out_has Receives `CNA_TRUE` when one is bound.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_clustered_forward_effect_has_area_light(
    CNA_ClusteredForwardEffectHandle effect,
    CNA_Bool* out_has);

/**
 * @brief Unbinds any area light.
 *
 * @param effect The effect.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_clustered_forward_effect_clear_area_light(
    CNA_ClusteredForwardEffectHandle effect);

/**
 * @brief Reports whether a light probe is bound.
 *
 * @param effect The effect.
 * @param out_has Receives `CNA_TRUE` when one is bound.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_clustered_forward_effect_has_light_probe(
    CNA_ClusteredForwardEffectHandle effect,
    CNA_Bool* out_has);

/**
 * @brief Unbinds any light probe.
 *
 * @param effect The effect.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_clustered_forward_effect_clear_light_probe(
    CNA_ClusteredForwardEffectHandle effect);

/**
 * @brief Returns the material's base colour.
 *
 * @param effect The effect.
 * @param out_color Receives the colour.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_clustered_forward_effect_get_base_color(
    CNA_ClusteredForwardEffectHandle effect,
    CNA_Vector3* out_color);

/**
 * @brief Sets the material's base colour, clamping each channel to zero-to-one.
 *
 * @param effect The effect.
 * @param color The colour; each channel is **clamped** rather than refused.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_clustered_forward_effect_set_base_color(
    CNA_ClusteredForwardEffectHandle effect,
    const CNA_Vector3* color);

/**
 * @brief Returns how metallic the material is.
 *
 * @param effect The effect.
 * @param out_metallic Receives the value.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_clustered_forward_effect_get_metallic(
    CNA_ClusteredForwardEffectHandle effect,
    float* out_metallic);

/**
 * @brief Sets how metallic the material is, clamped to zero-to-one.
 *
 * @param effect The effect.
 * @param metallic The value; **clamped** rather than refused.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_clustered_forward_effect_set_metallic(
    CNA_ClusteredForwardEffectHandle effect,
    float metallic);

/**
 * @brief Returns the material's roughness.
 *
 * @param effect The effect.
 * @param out_roughness Receives the value.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_clustered_forward_effect_get_roughness(
    CNA_ClusteredForwardEffectHandle effect,
    float* out_roughness);

/**
 * @brief Sets the material's roughness, clamped to **0.04**-to-one.
 *
 * The floor is not zero and is not a typo: a perfectly smooth surface collapses the specular lobe
 * to a point the shader cannot integrate, so the canonical setter refuses to go below it by
 * clamping. This route preserves that exactly.
 *
 * @param effect The effect.
 * @param roughness The value; **clamped** rather than refused.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_clustered_forward_effect_set_roughness(
    CNA_ClusteredForwardEffectHandle effect,
    float roughness);

/**
 * @brief Returns the material's index of refraction.
 *
 * @param effect The effect.
 * @param out_ior Receives the value.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_clustered_forward_effect_get_ior(
    CNA_ClusteredForwardEffectHandle effect,
    float* out_ior);

/**
 * @brief Sets the material's index of refraction.
 *
 * @param effect The effect.
 * @param ior The value.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_clustered_forward_effect_set_ior(
    CNA_ClusteredForwardEffectHandle effect,
    float ior);

/**
 * @brief Returns the ambient term.
 *
 * @param effect The effect.
 * @param out_ambient Receives the term.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_clustered_forward_effect_get_ambient(
    CNA_ClusteredForwardEffectHandle effect,
    CNA_Vector3* out_ambient);

/**
 * @brief Sets the ambient term, flooring each channel at zero.
 *
 * @param effect The effect.
 * @param ambient The term; each channel is **floored at zero** rather than refused, because a
 * negative ambient would subtract light that was never added.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_clustered_forward_effect_set_ambient(
    CNA_ClusteredForwardEffectHandle effect,
    const CNA_Vector3* ambient);

/**
 * @brief Returns the opaque frame the effect refracts against.
 *
 * The handle is a fresh name for the same texture and does **not** keep it alive: the effect
 * borrows the frame rather than owning it, exactly as @ref cna_effect_get_shadow_map_ext does.
 *
 * @param effect The effect.
 * @param out_frame Receives the borrowed texture, or `CNA_INVALID_HANDLE` when none is bound.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_clustered_forward_effect_get_opaque_frame(
    CNA_ClusteredForwardEffectHandle effect,
    CNA_Handle* out_frame);

/**
 * @brief Gives the effect a copy of the opaque frame to refract against.
 *
 * @param effect The effect.
 * @param frame The texture, or `CNA_INVALID_HANDLE` to unbind; borrowed.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_clustered_forward_effect_set_opaque_frame(
    CNA_ClusteredForwardEffectHandle effect,
    CNA_Handle frame);

/**
 * @brief Computes the volume attenuation of a transmissive material.
 *
 * A pure function of its arguments, so it needs no effect.
 *
 * @param attenuation_color The colour light takes on as it travels.
 * @param attenuation_distance The distance over which that colour is reached.
 * @param thickness How far light travels through the volume.
 * @param out_attenuation Receives the attenuation.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_clustered_forward_effect_volume_attenuation(
    const CNA_Vector3* attenuation_color,
    float attenuation_distance,
    float thickness,
    CNA_Vector3* out_attenuation);

/**
 * @brief Computes one light's contribution to a surface point.
 *
 * The canonical overload defaults its last eight arguments; C has no defaults, so all of them are
 * explicit here. Pass zero for the effects you are not using and the documented neutral values for
 * the rest — `1.3` for the iridescence IOR, `400` for its thickness and `0.5` for the subsurface
 * wrap, which are the canonical defaults.
 *
 * @param light The light.
 * @param surface The surface point.
 * @param normal The surface normal.
 * @param camera_position The camera's world-space position.
 * @param base_color The material's base colour.
 * @param metallic How metallic the material is.
 * @param roughness The material's roughness.
 * @param clearcoat Clearcoat strength.
 * @param clearcoat_roughness Clearcoat roughness.
 * @param sheen_color Sheen colour.
 * @param sheen_roughness Sheen roughness.
 * @param iridescence Iridescence strength.
 * @param iridescence_ior Iridescence index of refraction.
 * @param iridescence_thickness Iridescence film thickness in nanometres.
 * @param subsurface_color Subsurface colour.
 * @param subsurface_wrap How far light wraps around the terminator.
 * @param out_contribution Receives the contribution.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for a null argument or an
 * uninitialized light, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_clustered_forward_effect_contribution(
    const CNA_ClusteredLightEXT* light,
    const CNA_Vector3* surface,
    const CNA_Vector3* normal,
    const CNA_Vector3* camera_position,
    const CNA_Vector3* base_color,
    float metallic,
    float roughness,
    float clearcoat,
    float clearcoat_roughness,
    const CNA_Vector3* sheen_color,
    float sheen_roughness,
    float iridescence,
    float iridescence_ior,
    float iridescence_thickness,
    const CNA_Vector3* subsurface_color,
    float subsurface_wrap,
    CNA_Vector3* out_contribution);

/**
 * @brief Releases the clustered forward effect.
 *
 * @param effect The effect; an invalid handle is an error, not a silent no-op.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_clustered_forward_effect_destroy(
    CNA_ClusteredForwardEffectHandle effect);

/** @brief The per-cluster capacity a compute assignment uses unless told otherwise. */
#define CNA_CLUSTERED_COMPUTE_DEFAULT_STRIDE_EXT INT32_C(64)

/**
 * @brief Owned handle for one GPU cluster-assignment program.
 *
 * **It degrades rather than refuses.** On a renderer without compute shaders the object still
 * works: @ref cna_clustered_light_compute_assign falls back to the CPU sort and produces the same
 * assignment, and @ref cna_clustered_light_compute_used_compute reports which path ran.
 */
typedef CNA_Handle CNA_ClusteredLightComputeHandle;

/**
 * @brief Creates a GPU cluster-assignment program.
 *
 * @param graphics_device The device to compile on.
 * @param stride The per-cluster light capacity; must be positive.
 * @param out_compute Receives the owned handle; set invalid on failure.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for a non-positive stride,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_clustered_light_compute_create(
    CNA_Handle graphics_device,
    int32_t stride,
    CNA_ClusteredLightComputeHandle* out_compute);

/**
 * @brief Reports whether the GPU path is available.
 *
 * A `CNA_FALSE` here is not a failure: assignment still works, on the CPU.
 *
 * @param compute The program.
 * @param out_supported Receives `CNA_TRUE` when the compute program compiled.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_clustered_light_compute_is_supported(
    CNA_ClusteredLightComputeHandle compute,
    CNA_Bool* out_supported);

/**
 * @brief Copies why the GPU path is unavailable, as UTF-8 bytes without a terminator.
 *
 * Empty when the program compiled.
 *
 * @param compute The program.
 * @param destination Caller-owned destination, or null only when @p capacity is zero.
 * @param capacity Destination capacity in bytes.
 * @param out_bytes Receives the required byte count without a terminator.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL`, `CNA_RESULT_NOT_SUPPORTED` without
 * the engine layer, or an error. No partial string is written.
 */
CNA_C_API CNA_Result cna_clustered_light_compute_copy_unsupported_reason(
    CNA_ClusteredLightComputeHandle compute,
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

/**
 * @brief Returns the per-cluster light capacity.
 *
 * @param compute The program.
 * @param out_stride Receives the capacity.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_clustered_light_compute_get_stride(
    CNA_ClusteredLightComputeHandle compute,
    int32_t* out_stride);

/**
 * @brief Sorts lights into clusters, on the GPU where it can and the CPU where it cannot.
 *
 * @param compute The program.
 * @param grid The grid to sort into.
 * @param view The camera's view matrix.
 * @param bounds Array of light bounding spheres, in light-index order.
 * @param bounds_count How many spheres.
 * @param out_assignment The assignment to fill.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for more lights than the assignment
 * accepts, `CNA_RESULT_INVALID_STATE` when the grid has no projection, `CNA_RESULT_NOT_SUPPORTED`
 * without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_clustered_light_compute_assign(
    CNA_ClusteredLightComputeHandle compute,
    CNA_ClusteredLightGridHandle grid,
    const CNA_Matrix* view,
    const CNA_BoundingSphere* bounds,
    uint64_t bounds_count,
    CNA_ClusteredLightAssignmentHandle out_assignment);

/**
 * @brief Reports whether the last assignment ran on the GPU.
 *
 * @param compute The program.
 * @param out_used Receives `CNA_TRUE` when the GPU path ran.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_clustered_light_compute_used_compute(
    CNA_ClusteredLightComputeHandle compute,
    CNA_Bool* out_used);

/**
 * @brief Reports whether the last assignment overflowed a cluster's capacity.
 *
 * A cluster holding more lights than the stride drops the excess, so this is the flag that says a
 * larger stride is needed rather than that anything failed.
 *
 * @param compute The program.
 * @param out_overflowed Receives `CNA_TRUE` when at least one cluster overflowed.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_clustered_light_compute_has_overflowed(
    CNA_ClusteredLightComputeHandle compute,
    CNA_Bool* out_overflowed);

/**
 * @brief Releases the compute program.
 *
 * @param compute The program; an invalid handle is an error, not a silent no-op.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_clustered_light_compute_destroy(
    CNA_ClusteredLightComputeHandle compute);

/**
 * @brief Scores a light set and selects which lights may cast shadows this frame.
 *
 * Completes the shadow-budget family `CBIND-085C1` bound: the policy could be configured and read
 * but not run, because scoring needs a light set.
 *
 * @param policy The policy.
 * @param lights The light set to score.
 * @param view The camera's view matrix.
 * @param projection The camera's projection matrix.
 * @param camera_position The camera's world-space position.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_clustered_shadow_policy_select(
    CNA_ClusteredShadowPolicyHandle policy,
    CNA_ClusteredLightSetHandle lights,
    const CNA_Matrix* view,
    const CNA_Matrix* projection,
    const CNA_Vector3* camera_position);

/* ---------------------------------------------------------------------------------------------
 * The PBR material extensions and thin-film iridescence
 * ------------------------------------------------------------------------------------------- */

/**
 * @brief Owned handle for one set of PBR material extensions.
 *
 * The canonical type is a **value** -- it compares, hashes and prints by content -- but it holds
 * nine borrowed `Texture2D` pointers, so it is bound as a handle rather than a C structure: a
 * plain structure would put raw texture pointers in caller-writable memory. It never owns a
 * texture given to it; the caller keeps that lifetime, exactly as an effect does.
 *
 * Release it with @ref cna_pbr_material_extensions_destroy.
 */
typedef CNA_Handle CNA_PbrMaterialExtensionsHandle;

/**
 * @brief Creates a neutral set of PBR material extensions.
 *
 * Every field starts at the canonical default, which is the state
 * @ref cna_pbr_material_extensions_is_neutral reports.
 *
 * @param out_extensions Receives the owned handle; set invalid on failure.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_pbr_material_extensions_create(CNA_PbrMaterialExtensionsHandle* out_extensions);

/**
 * @brief Releases the extensions.
 *
 * The nine textures are borrowed, so none of them is released here.
 *
 * @param extensions The extensions; an invalid handle is an error, not a silent no-op.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_pbr_material_extensions_destroy(CNA_PbrMaterialExtensionsHandle extensions);

/**
 * @brief Copies every field of one set of extensions over another.
 *
 * The canonical type is copyable and this is how a C caller copies it, since a handle cannot be
 * assigned. The texture pointers are copied as borrows, not duplicated.
 *
 * @param destination The extensions to overwrite.
 * @param source The extensions to copy from.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_pbr_material_extensions_copy_from(CNA_PbrMaterialExtensionsHandle destination, CNA_PbrMaterialExtensionsHandle source);

/**
 * @brief Returns the canonical `ClearcoatFactor`.
 *
 * @param extensions The extensions.
 * @param out_value Receives the value.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_pbr_material_extensions_get_clearcoat_factor(CNA_PbrMaterialExtensionsHandle extensions, float* out_value);

/**
 * @brief Sets the canonical `ClearcoatFactor`.
 *
 * @param extensions The extensions.
 * @param value The value, **clamped** to zero-to-one.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_pbr_material_extensions_set_clearcoat_factor(CNA_PbrMaterialExtensionsHandle extensions, float value);

/**
 * @brief Returns the canonical `ClearcoatRoughness`.
 *
 * @param extensions The extensions.
 * @param out_value Receives the value.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_pbr_material_extensions_get_clearcoat_roughness(CNA_PbrMaterialExtensionsHandle extensions, float* out_value);

/**
 * @brief Sets the canonical `ClearcoatRoughness`.
 *
 * @param extensions The extensions.
 * @param value The value, **clamped** to zero-to-one.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_pbr_material_extensions_set_clearcoat_roughness(CNA_PbrMaterialExtensionsHandle extensions, float value);

/**
 * @brief Returns the canonical `ClearcoatNormalScale`.
 *
 * @param extensions The extensions.
 * @param out_value Receives the value.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_pbr_material_extensions_get_clearcoat_normal_scale(CNA_PbrMaterialExtensionsHandle extensions, float* out_value);

/**
 * @brief Sets the canonical `ClearcoatNormalScale`.
 *
 * @param extensions The extensions.
 * @param value The value, **ignored when negative** -- the canonical setter guards the assignment rather than clamping, so a negative write leaves the previous value in place instead of forcing it to zero.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_pbr_material_extensions_set_clearcoat_normal_scale(CNA_PbrMaterialExtensionsHandle extensions, float value);

/**
 * @brief Returns the canonical `SheenRoughness`.
 *
 * @param extensions The extensions.
 * @param out_value Receives the value.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_pbr_material_extensions_get_sheen_roughness(CNA_PbrMaterialExtensionsHandle extensions, float* out_value);

/**
 * @brief Sets the canonical `SheenRoughness`.
 *
 * @param extensions The extensions.
 * @param value The value, **clamped** to zero-to-one.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_pbr_material_extensions_set_sheen_roughness(CNA_PbrMaterialExtensionsHandle extensions, float value);

/**
 * @brief Returns the canonical `TransmissionFactor`.
 *
 * @param extensions The extensions.
 * @param out_value Receives the value.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_pbr_material_extensions_get_transmission_factor(CNA_PbrMaterialExtensionsHandle extensions, float* out_value);

/**
 * @brief Sets the canonical `TransmissionFactor`.
 *
 * @param extensions The extensions.
 * @param value The value, **clamped** to zero-to-one.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_pbr_material_extensions_set_transmission_factor(CNA_PbrMaterialExtensionsHandle extensions, float value);

/**
 * @brief Returns the canonical `ThicknessFactor`.
 *
 * @param extensions The extensions.
 * @param out_value Receives the value.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_pbr_material_extensions_get_thickness_factor(CNA_PbrMaterialExtensionsHandle extensions, float* out_value);

/**
 * @brief Sets the canonical `ThicknessFactor`.
 *
 * @param extensions The extensions.
 * @param value The value, **ignored when negative** -- a guarded assignment, so a negative write leaves the previous thickness in place; the value is a distance and has no upper bound.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_pbr_material_extensions_set_thickness_factor(CNA_PbrMaterialExtensionsHandle extensions, float value);

/**
 * @brief Returns the canonical `AttenuationDistance`.
 *
 * @param extensions The extensions.
 * @param out_value Receives the value.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_pbr_material_extensions_get_attenuation_distance(CNA_PbrMaterialExtensionsHandle extensions, float* out_value);

/**
 * @brief Sets the canonical `AttenuationDistance`.
 *
 * @param extensions The extensions.
 * @param value The value, **floored at zero** -- unlike the guarded setters beside it this one writes zero rather than keeping the previous value, which is a third correction shape in the same class.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_pbr_material_extensions_set_attenuation_distance(CNA_PbrMaterialExtensionsHandle extensions, float value);

/**
 * @brief Returns the canonical `IridescenceFactor`.
 *
 * @param extensions The extensions.
 * @param out_value Receives the value.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_pbr_material_extensions_get_iridescence_factor(CNA_PbrMaterialExtensionsHandle extensions, float* out_value);

/**
 * @brief Sets the canonical `IridescenceFactor`.
 *
 * @param extensions The extensions.
 * @param value The value, **clamped** to zero-to-one.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_pbr_material_extensions_set_iridescence_factor(CNA_PbrMaterialExtensionsHandle extensions, float value);

/**
 * @brief Returns the canonical `IridescenceIor`.
 *
 * @param extensions The extensions.
 * @param out_value Receives the value.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_pbr_material_extensions_get_iridescence_ior(CNA_PbrMaterialExtensionsHandle extensions, float* out_value);

/**
 * @brief Sets the canonical `IridescenceIor`.
 *
 * @param extensions The extensions.
 * @param value The value, **ignored when below one**, not below zero -- an index of refraction under one describes a medium light speeds up in, which this film cannot be; a guarded assignment, so such a write leaves the previous value in place.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_pbr_material_extensions_set_iridescence_ior(CNA_PbrMaterialExtensionsHandle extensions, float value);

/**
 * @brief Returns the canonical `IridescenceThicknessMinimum`.
 *
 * @param extensions The extensions.
 * @param out_value Receives the value.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_pbr_material_extensions_get_iridescence_thickness_minimum(CNA_PbrMaterialExtensionsHandle extensions, float* out_value);

/**
 * @brief Sets the canonical `IridescenceThicknessMinimum`.
 *
 * @param extensions The extensions.
 * @param value The value, **ignored when negative** -- a guarded assignment; the value is a film thickness in nanometres and has no upper bound.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_pbr_material_extensions_set_iridescence_thickness_minimum(CNA_PbrMaterialExtensionsHandle extensions, float value);

/**
 * @brief Returns the canonical `IridescenceThicknessMaximum`.
 *
 * @param extensions The extensions.
 * @param out_value Receives the value.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_pbr_material_extensions_get_iridescence_thickness_maximum(CNA_PbrMaterialExtensionsHandle extensions, float* out_value);

/**
 * @brief Sets the canonical `IridescenceThicknessMaximum`.
 *
 * @param extensions The extensions.
 * @param value The value, **ignored when negative** -- a guarded assignment; the value is a film thickness in nanometres and has no upper bound.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_pbr_material_extensions_set_iridescence_thickness_maximum(CNA_PbrMaterialExtensionsHandle extensions, float value);

/**
 * @brief Returns the canonical `SubsurfaceWrap`.
 *
 * @param extensions The extensions.
 * @param out_value Receives the value.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_pbr_material_extensions_get_subsurface_wrap(CNA_PbrMaterialExtensionsHandle extensions, float* out_value);

/**
 * @brief Sets the canonical `SubsurfaceWrap`.
 *
 * @param extensions The extensions.
 * @param value The value, **clamped** to zero-to-one.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_pbr_material_extensions_set_subsurface_wrap(CNA_PbrMaterialExtensionsHandle extensions, float value);

/**
 * @brief Returns the sheen tint.
 *
 * @param extensions The extensions.
 * @param out_value Receives the value.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_pbr_material_extensions_get_sheen_color_factor(CNA_PbrMaterialExtensionsHandle extensions, CNA_Vector3* out_value);

/**
 * @brief Sets the sheen tint, clamping each channel to zero-to-one.
 *
 * @param extensions The extensions.
 * @param value The value; **each channel is clamped** rather than the value refused.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_pbr_material_extensions_set_sheen_color_factor(CNA_PbrMaterialExtensionsHandle extensions, const CNA_Vector3* value);

/**
 * @brief Returns the colour light takes on as it travels through the volume.
 *
 * @param extensions The extensions.
 * @param out_value Receives the value.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_pbr_material_extensions_get_attenuation_color(CNA_PbrMaterialExtensionsHandle extensions, CNA_Vector3* out_value);

/**
 * @brief Sets the colour light takes on as it travels through the volume, clamping each channel to zero-to-one.
 *
 * @param extensions The extensions.
 * @param value The value; **each channel is clamped** rather than the value refused.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_pbr_material_extensions_set_attenuation_color(CNA_PbrMaterialExtensionsHandle extensions, const CNA_Vector3* value);

/**
 * @brief Returns the subsurface tint.
 *
 * @param extensions The extensions.
 * @param out_value Receives the value.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_pbr_material_extensions_get_subsurface_color(CNA_PbrMaterialExtensionsHandle extensions, CNA_Vector3* out_value);

/**
 * @brief Sets the subsurface tint, clamping each channel to zero-to-one.
 *
 * @param extensions The extensions.
 * @param value The value; **each channel is clamped** rather than the value refused.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_pbr_material_extensions_set_subsurface_color(CNA_PbrMaterialExtensionsHandle extensions, const CNA_Vector3* value);

/**
 * @brief Returns the clearcoat strength map, borrowed.
 *
 * The handle is a fresh name for the same texture and does **not** keep it alive.
 *
 * @param extensions The extensions.
 * @param out_texture Receives the borrowed texture, or `CNA_INVALID_HANDLE` when none is bound.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_pbr_material_extensions_get_clearcoat_texture(CNA_PbrMaterialExtensionsHandle extensions, CNA_Handle* out_texture);

/**
 * @brief Binds the clearcoat strength map.
 *
 * @param extensions The extensions.
 * @param texture The texture, or `CNA_INVALID_HANDLE` to unbind; borrowed, never owned.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_pbr_material_extensions_set_clearcoat_texture(CNA_PbrMaterialExtensionsHandle extensions, CNA_Handle texture);

/**
 * @brief Returns the clearcoat roughness map, borrowed.
 *
 * The handle is a fresh name for the same texture and does **not** keep it alive.
 *
 * @param extensions The extensions.
 * @param out_texture Receives the borrowed texture, or `CNA_INVALID_HANDLE` when none is bound.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_pbr_material_extensions_get_clearcoat_roughness_texture(CNA_PbrMaterialExtensionsHandle extensions, CNA_Handle* out_texture);

/**
 * @brief Binds the clearcoat roughness map.
 *
 * @param extensions The extensions.
 * @param texture The texture, or `CNA_INVALID_HANDLE` to unbind; borrowed, never owned.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_pbr_material_extensions_set_clearcoat_roughness_texture(CNA_PbrMaterialExtensionsHandle extensions, CNA_Handle texture);

/**
 * @brief Returns the clearcoat normal map, borrowed.
 *
 * The handle is a fresh name for the same texture and does **not** keep it alive.
 *
 * @param extensions The extensions.
 * @param out_texture Receives the borrowed texture, or `CNA_INVALID_HANDLE` when none is bound.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_pbr_material_extensions_get_clearcoat_normal_texture(CNA_PbrMaterialExtensionsHandle extensions, CNA_Handle* out_texture);

/**
 * @brief Binds the clearcoat normal map.
 *
 * @param extensions The extensions.
 * @param texture The texture, or `CNA_INVALID_HANDLE` to unbind; borrowed, never owned.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_pbr_material_extensions_set_clearcoat_normal_texture(CNA_PbrMaterialExtensionsHandle extensions, CNA_Handle texture);

/**
 * @brief Returns the sheen colour map, borrowed.
 *
 * The handle is a fresh name for the same texture and does **not** keep it alive.
 *
 * @param extensions The extensions.
 * @param out_texture Receives the borrowed texture, or `CNA_INVALID_HANDLE` when none is bound.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_pbr_material_extensions_get_sheen_color_texture(CNA_PbrMaterialExtensionsHandle extensions, CNA_Handle* out_texture);

/**
 * @brief Binds the sheen colour map.
 *
 * @param extensions The extensions.
 * @param texture The texture, or `CNA_INVALID_HANDLE` to unbind; borrowed, never owned.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_pbr_material_extensions_set_sheen_color_texture(CNA_PbrMaterialExtensionsHandle extensions, CNA_Handle texture);

/**
 * @brief Returns the sheen roughness map, borrowed.
 *
 * The handle is a fresh name for the same texture and does **not** keep it alive.
 *
 * @param extensions The extensions.
 * @param out_texture Receives the borrowed texture, or `CNA_INVALID_HANDLE` when none is bound.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_pbr_material_extensions_get_sheen_roughness_texture(CNA_PbrMaterialExtensionsHandle extensions, CNA_Handle* out_texture);

/**
 * @brief Binds the sheen roughness map.
 *
 * @param extensions The extensions.
 * @param texture The texture, or `CNA_INVALID_HANDLE` to unbind; borrowed, never owned.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_pbr_material_extensions_set_sheen_roughness_texture(CNA_PbrMaterialExtensionsHandle extensions, CNA_Handle texture);

/**
 * @brief Returns the transmission map, borrowed.
 *
 * The handle is a fresh name for the same texture and does **not** keep it alive.
 *
 * @param extensions The extensions.
 * @param out_texture Receives the borrowed texture, or `CNA_INVALID_HANDLE` when none is bound.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_pbr_material_extensions_get_transmission_texture(CNA_PbrMaterialExtensionsHandle extensions, CNA_Handle* out_texture);

/**
 * @brief Binds the transmission map.
 *
 * @param extensions The extensions.
 * @param texture The texture, or `CNA_INVALID_HANDLE` to unbind; borrowed, never owned.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_pbr_material_extensions_set_transmission_texture(CNA_PbrMaterialExtensionsHandle extensions, CNA_Handle texture);

/**
 * @brief Returns the thickness map, borrowed.
 *
 * The handle is a fresh name for the same texture and does **not** keep it alive.
 *
 * @param extensions The extensions.
 * @param out_texture Receives the borrowed texture, or `CNA_INVALID_HANDLE` when none is bound.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_pbr_material_extensions_get_thickness_texture(CNA_PbrMaterialExtensionsHandle extensions, CNA_Handle* out_texture);

/**
 * @brief Binds the thickness map.
 *
 * @param extensions The extensions.
 * @param texture The texture, or `CNA_INVALID_HANDLE` to unbind; borrowed, never owned.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_pbr_material_extensions_set_thickness_texture(CNA_PbrMaterialExtensionsHandle extensions, CNA_Handle texture);

/**
 * @brief Returns the iridescence strength map, borrowed.
 *
 * The handle is a fresh name for the same texture and does **not** keep it alive.
 *
 * @param extensions The extensions.
 * @param out_texture Receives the borrowed texture, or `CNA_INVALID_HANDLE` when none is bound.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_pbr_material_extensions_get_iridescence_texture(CNA_PbrMaterialExtensionsHandle extensions, CNA_Handle* out_texture);

/**
 * @brief Binds the iridescence strength map.
 *
 * @param extensions The extensions.
 * @param texture The texture, or `CNA_INVALID_HANDLE` to unbind; borrowed, never owned.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_pbr_material_extensions_set_iridescence_texture(CNA_PbrMaterialExtensionsHandle extensions, CNA_Handle texture);

/**
 * @brief Returns the iridescence film-thickness map, borrowed.
 *
 * The handle is a fresh name for the same texture and does **not** keep it alive.
 *
 * @param extensions The extensions.
 * @param out_texture Receives the borrowed texture, or `CNA_INVALID_HANDLE` when none is bound.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_pbr_material_extensions_get_iridescence_thickness_texture(CNA_PbrMaterialExtensionsHandle extensions, CNA_Handle* out_texture);

/**
 * @brief Binds the iridescence film-thickness map.
 *
 * @param extensions The extensions.
 * @param texture The texture, or `CNA_INVALID_HANDLE` to unbind; borrowed, never owned.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_pbr_material_extensions_set_iridescence_thickness_texture(CNA_PbrMaterialExtensionsHandle extensions, CNA_Handle texture);

/**
 * @brief Reports whether any subsurface colour channel is above zero.
 *
 * @param extensions The extensions.
 * @param out_value Receives the answer.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_pbr_material_extensions_is_subsurface_enabled(CNA_PbrMaterialExtensionsHandle extensions, CNA_Bool* out_value);

/**
 * @brief Reports whether the iridescence factor is above zero.
 *
 * @param extensions The extensions.
 * @param out_value Receives the answer.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_pbr_material_extensions_is_iridescence_enabled(CNA_PbrMaterialExtensionsHandle extensions, CNA_Bool* out_value);

/**
 * @brief Reports whether the transmission factor is above zero.
 *
 * @param extensions The extensions.
 * @param out_value Receives the answer.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_pbr_material_extensions_is_transmission_enabled(CNA_PbrMaterialExtensionsHandle extensions, CNA_Bool* out_value);

/**
 * @brief Reports whether any sheen colour channel is above zero.
 *
 * @param extensions The extensions.
 * @param out_value Receives the answer.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_pbr_material_extensions_is_sheen_enabled(CNA_PbrMaterialExtensionsHandle extensions, CNA_Bool* out_value);

/**
 * @brief Reports whether no extension is active, so a renderer may take the plain PBR path.
 *
 * @param extensions The extensions.
 * @param out_value Receives the answer.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_pbr_material_extensions_is_neutral(CNA_PbrMaterialExtensionsHandle extensions, CNA_Bool* out_value);

/**
 * @brief Compares two sets of extensions by value across every field.
 *
 * The single route behind both canonical equality operators. Texture members compare by identity
 * -- two sets are equal when they point at the same textures, not at equal ones.
 *
 * @param first The first set.
 * @param second The second set.
 * @param out_equal Receives `CNA_TRUE` when every field matches.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_pbr_material_extensions_equals(
    CNA_PbrMaterialExtensionsHandle first, CNA_PbrMaterialExtensionsHandle second, CNA_Bool* out_equal);

/**
 * @brief Returns the canonical hash code.
 *
 * Equal extensions hash equally; the width is the C ABI's `uint64_t` rather than the canonical
 * `std::size_t`, which is the campaign's settled deviation for hash codes.
 *
 * @param extensions The extensions.
 * @param out_hash Receives the hash.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_pbr_material_extensions_get_hash_code(CNA_PbrMaterialExtensionsHandle extensions, uint64_t* out_hash);

/**
 * @brief Copies the canonical `ToString` text as UTF-8 bytes without a terminator.
 *
 * The text names only the extensions that are active, so a neutral set prints as `{}`.
 *
 * @param extensions The extensions.
 * @param destination Caller-owned destination, or null only when @p capacity is zero.
 * @param capacity Destination capacity in bytes.
 * @param out_bytes Receives the required byte count without a terminator.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL`, `CNA_RESULT_NOT_SUPPORTED` without
 * the engine layer, or an error. No partial string is written.
 */
CNA_C_API CNA_Result cna_pbr_material_extensions_copy_to_string(
    CNA_PbrMaterialExtensionsHandle extensions, char* destination, uint64_t capacity, uint64_t* out_bytes);

/**
 * @brief Evaluates thin-film iridescence for one viewing angle and film thickness.
 *
 * A pure function of its arguments, so it needs no handle.
 *
 * @param outside_ior The index of refraction of the medium the light comes from.
 * @param film_ior The index of refraction of the film.
 * @param cos_theta The cosine of the viewing angle; **clamped** to zero-to-one.
 * @param thickness_nm The film thickness in nanometres.
 * @param base_f0 The base reflectance at normal incidence.
 * @param out_value Receives the result, whose channels are floored at zero.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for a null vector,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_thin_film_iridescence_evaluate(
    float outside_ior,
    float film_ior,
    float cos_theta,
    float thickness_nm,
    const CNA_Vector3* base_f0,
    CNA_Vector3* out_value);

/**
 * @brief Copies the GLSL source of the thin-film term as UTF-8 bytes without a terminator.
 *
 * @param destination Caller-owned destination, or null only when @p capacity is zero.
 * @param capacity Destination capacity in bytes.
 * @param out_bytes Receives the required byte count without a terminator.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL`, `CNA_RESULT_NOT_SUPPORTED` without
 * the engine layer, or an error. No partial string is written.
 */
CNA_C_API CNA_Result cna_thin_film_iridescence_copy_glsl(
    char* destination, uint64_t capacity, uint64_t* out_bytes);

/**
 * @brief Returns the material extensions the clustered forward effect shades with, borrowed.
 *
 * `CBIND-086C` deferred this route because the type did not exist in C yet.
 *
 * @param effect The effect.
 * @param out_extensions Receives a borrowed handle onto the effect's own extensions.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_clustered_forward_effect_get_material_extensions(
    CNA_ClusteredForwardEffectHandle effect, CNA_PbrMaterialExtensionsHandle* out_extensions);

/**
 * @brief Gives the clustered forward effect a copy of the material extensions.
 *
 * @param effect The effect.
 * @param extensions The extensions to copy in.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_clustered_forward_effect_set_material_extensions(
    CNA_ClusteredForwardEffectHandle effect, CNA_PbrMaterialExtensionsHandle extensions);

/**
 * @brief Computes one light's contribution using a set of material extensions.
 *
 * The canonical overload that takes a `PbrMaterialExtensions` instead of the eight loose
 * extension scalars. This route closes `CBIND-086`'s last inventory row.
 *
 * @param light The light.
 * @param surface The surface point.
 * @param normal The surface normal.
 * @param camera_position The camera's world-space position.
 * @param base_color The material's base colour.
 * @param metallic How metallic the material is.
 * @param roughness The material's roughness.
 * @param extensions The material extensions.
 * @param out_contribution Receives the contribution.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for a null argument or an
 * uninitialized light, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_clustered_forward_effect_contribution_with_extensions(
    const CNA_ClusteredLightEXT* light,
    const CNA_Vector3* surface,
    const CNA_Vector3* normal,
    const CNA_Vector3* camera_position,
    const CNA_Vector3* base_color,
    float metallic,
    float roughness,
    CNA_PbrMaterialExtensionsHandle extensions,
    CNA_Vector3* out_contribution);

/* ---------------------------------------------------------------------------------------------
 * The PBR material's value semantics, its slot count and the transparency mode
 * ------------------------------------------------------------------------------------------- */

/**
 * @brief How many `Texture2D` slots a PBR material carries.
 *
 * The bound of `CNA_PBR_TEXTURE_BASE_COLOR` through `CNA_PBR_TEXTURE_SPECULAR_COLOR_EXT`, and the
 * length of `CNA_PbrMaterialEXT::texture_coordinate_sets` and `::texture_transforms`.
 */
#define CNA_PBR_TEXTURE_SLOT_COUNT INT32_C(7)

/** @brief Fixed-width identity for how a renderer resolves transparent geometry. */
typedef uint32_t CNA_TransparencyMode;
/** @brief Draw transparent geometry in submission order, resolving nothing. */
#define CNA_TRANSPARENCY_MODE_NONE UINT32_C(0)
/** @brief Sort transparent geometry back to front before drawing it. */
#define CNA_TRANSPARENCY_MODE_SORTED UINT32_C(1)
/** @brief Resolve transparent geometry with an order-independent weighted blend. */
#define CNA_TRANSPARENCY_MODE_ORDER_INDEPENDENT UINT32_C(2)

/**
 * @brief Compares two PBR materials by value across every field.
 *
 * The single route behind both canonical equality operators. Texture members compare by handle
 * identity -- two materials are equal when they name the same textures, not equal ones.
 *
 * The material itself is a value describable in either build, but this route needs the canonical
 * type to answer with the canonical rule, so it refuses without the engine layer. The same is
 * true of @ref cna_pbr_material_ext_get_hash_code and @ref cna_pbr_material_ext_copy_to_string:
 * all three are gated together rather than one of them being reimplemented field by field in C,
 * which would make equality answerable in a build where the hash consistent with it is not.
 *
 * @param first The first material.
 * @param second The second material.
 * @param out_equal Receives `CNA_TRUE` when every field matches.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for a null or malformed structure,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_pbr_material_ext_equals(
    const CNA_PbrMaterialEXT* first, const CNA_PbrMaterialEXT* second, CNA_Bool* out_equal);

/**
 * @brief Returns the canonical hash code of a PBR material.
 *
 * Equal materials hash equally; the width is the C ABI's `uint64_t` rather than the canonical
 * `std::size_t`, the campaign's settled deviation.
 *
 * @param material The material.
 * @param out_hash Receives the hash.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for a null or malformed structure,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_pbr_material_ext_get_hash_code(
    const CNA_PbrMaterialEXT* material, uint64_t* out_hash);

/**
 * @brief Copies the canonical `ToString` text as UTF-8 bytes without a terminator.
 *
 * @param material The material.
 * @param destination Caller-owned destination, or null only when @p capacity is zero.
 * @param capacity Destination capacity in bytes.
 * @param out_bytes Receives the required byte count without a terminator.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL`, `CNA_RESULT_INVALID_ARGUMENT` for a
 * null or malformed structure, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 * No partial string is written.
 */
CNA_C_API CNA_Result cna_pbr_material_ext_copy_to_string(
    const CNA_PbrMaterialEXT* material, char* destination, uint64_t capacity, uint64_t* out_bytes);

/* ---------------------------------------------------------------------------------------------
 * The glTF material bridge and the transparency pipeline
 * ------------------------------------------------------------------------------------------- */

/** @brief The textures an imported glTF material's core slots resolve to; any entry may be invalid. */
typedef struct CNA_GltfMaterialTexturesEXT {
    /** @brief Size of this caller-provided structure in bytes. */
    uint32_t struct_size;
    /** @brief Version of this caller-provided structure. */
    uint32_t struct_version;
    /** @brief One texture per `CNA_PBR_TEXTURE_*` slot, in slot order. */
    CNA_Handle slots[CNA_PBR_TEXTURE_SLOT_COUNT];
} CNA_GltfMaterialTexturesEXT;

/** @brief The textures an imported glTF material's extension slots resolve to; any may be invalid. */
typedef struct CNA_GltfMaterialExtensionTexturesEXT {
    /** @brief Size of this caller-provided structure in bytes. */
    uint32_t struct_size;
    /** @brief Version of this caller-provided structure. */
    uint32_t struct_version;
    /** @brief `KHR_materials_clearcoat.clearcoatTexture`. */
    CNA_Handle clearcoat;
    /** @brief `KHR_materials_clearcoat.clearcoatRoughnessTexture`. */
    CNA_Handle clearcoat_roughness;
    /** @brief `KHR_materials_clearcoat.clearcoatNormalTexture`. */
    CNA_Handle clearcoat_normal;
    /** @brief `KHR_materials_sheen.sheenColorTexture`. */
    CNA_Handle sheen_color;
    /** @brief `KHR_materials_sheen.sheenRoughnessTexture`. */
    CNA_Handle sheen_roughness;
    /** @brief `KHR_materials_transmission.transmissionTexture`. */
    CNA_Handle transmission;
    /** @brief `KHR_materials_volume.thicknessTexture`. */
    CNA_Handle thickness;
    /** @brief `KHR_materials_iridescence.iridescenceTexture`. */
    CNA_Handle iridescence;
    /** @brief `KHR_materials_iridescence.iridescenceThicknessTexture`. */
    CNA_Handle iridescence_thickness;
} CNA_GltfMaterialExtensionTexturesEXT;

/**
 * @brief One imported glTF material's core factors.
 *
 * The canonical bridge is written against a **concept**, not a type, so that the importer's own
 * record satisfies it without this layer knowing that record. A concept has no C form, so the C
 * bridge takes this structure instead: it names exactly the members the concept requires.
 */
typedef struct CNA_GltfMaterialSourceEXT {
    /** @brief Size of this caller-provided structure in bytes. */
    uint32_t struct_size;
    /** @brief Version of this caller-provided structure. */
    uint32_t struct_version;
    /** @brief `pbrMetallicRoughness.baseColorFactor`, four floats. */
    CNA_Vector4 base_color_factor;
    /** @brief `pbrMetallicRoughness.metallicFactor`. */
    float metallic_factor;
    /** @brief `pbrMetallicRoughness.roughnessFactor`. */
    float roughness_factor;
    /** @brief `emissiveFactor`. */
    CNA_Vector3 emissive_factor;
    /** @brief `normalTexture.scale`. */
    float normal_scale;
    /** @brief `occlusionTexture.strength`. */
    float occlusion_strength;
    /** @brief `KHR_materials_ior.ior`. */
    float ior_ext;
    /** @brief `KHR_materials_specular.specularFactor`. */
    float specular_factor_ext;
    /** @brief `KHR_materials_specular.specularColorFactor`. */
    CNA_Vector3 specular_color_factor_ext;
    /** @brief `alphaMode`. */
    CNA_AlphaModeEXT alpha_mode;
    /** @brief `alphaCutoff`. */
    float alpha_cutoff;
    /** @brief `doubleSided`. */
    CNA_Bool double_sided;
    /** @brief Padding; write zero. */
    uint8_t reserved[3];
    /** @brief `KHR_texture_transform` texture-coordinate set per slot. */
    int32_t texture_coordinate_sets_ext[CNA_PBR_TEXTURE_SLOT_COUNT];
    /** @brief `KHR_texture_transform` transform per slot. */
    CNA_TextureTransformEXT texture_transforms_ext[CNA_PBR_TEXTURE_SLOT_COUNT];
} CNA_GltfMaterialSourceEXT;

/** @brief One imported glTF material's extension factors, mirroring the extension concept. */
typedef struct CNA_GltfMaterialExtensionSourceEXT {
    /** @brief Size of this caller-provided structure in bytes. */
    uint32_t struct_size;
    /** @brief Version of this caller-provided structure. */
    uint32_t struct_version;
    /** @brief `KHR_materials_clearcoat.clearcoatFactor`. */
    float clearcoat_factor_ext;
    /** @brief `KHR_materials_clearcoat.clearcoatRoughnessFactor`. */
    float clearcoat_roughness_factor_ext;
    /** @brief `KHR_materials_sheen.sheenColorFactor`. */
    CNA_Vector3 sheen_color_factor_ext;
    /** @brief `KHR_materials_sheen.sheenRoughnessFactor`. */
    float sheen_roughness_factor_ext;
    /** @brief `KHR_materials_transmission.transmissionFactor`. */
    float transmission_factor_ext;
    /** @brief `KHR_materials_volume.thicknessFactor`. */
    float thickness_factor_ext;
    /** @brief `KHR_materials_volume.attenuationDistance`. */
    float attenuation_distance_ext;
    /** @brief `KHR_materials_volume.attenuationColor`. */
    CNA_Vector3 attenuation_color_ext;
    /** @brief `KHR_materials_iridescence.iridescenceFactor`. */
    float iridescence_factor_ext;
    /** @brief `KHR_materials_iridescence.iridescenceIor`. */
    float iridescence_ior_ext;
    /** @brief `KHR_materials_iridescence.iridescenceThicknessMinimum`. */
    float iridescence_thickness_minimum_ext;
    /** @brief `KHR_materials_iridescence.iridescenceThicknessMaximum`. */
    float iridescence_thickness_maximum_ext;
} CNA_GltfMaterialExtensionSourceEXT;

/**
 * @brief Fills a core glTF material source with the glTF specification's default factors.
 *
 * @param out_source Receives the defaults along with `struct_size` and `struct_version`.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_gltf_material_source_ext_init(CNA_GltfMaterialSourceEXT* out_source);

/**
 * @brief Fills an extension glTF material source with the specification's default factors.
 *
 * @param out_source Receives the defaults along with `struct_size` and `struct_version`.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_gltf_material_extension_source_ext_init(
    CNA_GltfMaterialExtensionSourceEXT* out_source);

/** @brief Fills a core texture set with invalid handles and its versioning fields. */
CNA_C_API CNA_Result cna_gltf_material_textures_ext_init(CNA_GltfMaterialTexturesEXT* out_textures);

/** @brief Fills an extension texture set with invalid handles and its versioning fields. */
CNA_C_API CNA_Result cna_gltf_material_extension_textures_ext_init(
    CNA_GltfMaterialExtensionTexturesEXT* out_textures);

/**
 * @brief Builds a PBR material from one imported glTF material and its resolved textures.
 *
 * **One value is not carried exactly, and that is the canonical behaviour rather than a limit of
 * this binding:** glTF's `baseColorFactor` is four floats and a material's albedo factor is a
 * `CNA_Color`, so it is quantised to eight bits per channel. Everything else round-trips.
 *
 * The textures are borrowed, never owned; the material records them as the canonical type does.
 *
 * @param source The material's core factors.
 * @param textures The textures its core slots resolved to.
 * @param out_material Receives the material.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for a null or malformed structure,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_gltf_material_bridge_build_material(
    const CNA_GltfMaterialSourceEXT* source,
    const CNA_GltfMaterialTexturesEXT* textures,
    CNA_PbrMaterialEXT* out_material);

/**
 * @brief Builds PBR material extensions from one imported glTF material's extension factors.
 *
 * @param source The material's extension factors.
 * @param textures The textures its extension slots resolved to.
 * @param out_extensions An existing extensions handle to fill.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for a null or malformed structure,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_gltf_material_bridge_build_extensions(
    const CNA_GltfMaterialExtensionSourceEXT* source,
    const CNA_GltfMaterialExtensionTexturesEXT* textures,
    CNA_PbrMaterialExtensionsHandle out_extensions);

/**
 * @brief Owned handle for one back-to-front transparent draw list.
 *
 * A pure CPU object: it holds bounding boxes and callbacks and touches no device.
 */
typedef CNA_Handle CNA_TransparentDrawListHandle;

/**
 * @brief Called once per submitted entry, in the order the list decides.
 *
 * @param context The pointer given to @ref cna_transparent_draw_list_submit.
 * @return `CNA_RESULT_SUCCESS`, or any documented result code to fail the draw that asked for it.
 */
typedef CNA_Result (*CNA_TransparentDrawCallback)(void* context);

/**
 * @brief Creates an empty transparent draw list.
 *
 * @param out_list Receives the owned handle; set invalid on failure.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_transparent_draw_list_create(CNA_TransparentDrawListHandle* out_list);

/**
 * @brief Releases the draw list.
 *
 * @param list The list; an invalid handle is an error, not a silent no-op.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_transparent_draw_list_destroy(CNA_TransparentDrawListHandle list);

/**
 * @brief Removes every entry.
 *
 * @param list The list.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_transparent_draw_list_clear(CNA_TransparentDrawListHandle list);

/**
 * @brief Adds one entry with the bounds that decide its place in the order.
 *
 * @param list The list.
 * @param bounds The entry's world-space bounds.
 * @param draw The callback to run when the entry's turn comes; must not be null, because an entry
 *        with nothing to draw is a caller mistake rather than an empty draw.
 * @param context Passed to @p draw unchanged; may be null.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for a null callback or bounds,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_transparent_draw_list_submit(
    CNA_TransparentDrawListHandle list,
    const CNA_BoundingBox* bounds,
    CNA_TransparentDrawCallback draw,
    void* context);

/**
 * @brief Returns how many entries the list holds.
 *
 * @param list The list.
 * @param out_count Receives the count.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_transparent_draw_list_get_count(
    CNA_TransparentDrawListHandle list, uint64_t* out_count);

/**
 * @brief Runs every entry's callback, farthest from the camera first.
 *
 * A callback that fails stops the draw and its result is returned, so a caller learns which draw
 * failed rather than finding a partly drawn frame.
 *
 * @param list The list.
 * @param view The camera's view matrix; the camera position is derived from it, matching the
 *        canonical signature rather than asking a caller to derive it twice.
 * @return `CNA_RESULT_SUCCESS`, the failing callback's result, `CNA_RESULT_INVALID_ARGUMENT` for a
 * null matrix, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_transparent_draw_list_draw_sorted(
    CNA_TransparentDrawListHandle list, const CNA_Matrix* view);

/**
 * @brief Copies the order `draw_sorted` would use, as indices into submission order.
 *
 * @param list The list.
 * @param view The camera's view matrix.
 * @param destination Caller-owned destination, or null only when @p capacity is zero.
 * @param capacity Destination capacity in elements.
 * @param out_count Receives the required element count.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL`, `CNA_RESULT_INVALID_ARGUMENT` for a
 * null matrix, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error. No partial result
 * is written.
 */
CNA_C_API CNA_Result cna_transparent_draw_list_copy_sorted_order_ext(
    CNA_TransparentDrawListHandle list,
    const CNA_Matrix* view,
    int32_t* destination,
    uint64_t capacity,
    uint64_t* out_count);

/**
 * @brief Returns the sort key for one entry's bounds, which is its distance from the camera.
 *
 * A pure function of its arguments. The distance is measured to the **nearest point of the box**,
 * so a camera inside the box sorts at zero rather than to the box's centre.
 *
 * @param bounds The bounds.
 * @param camera_position The camera's world-space position.
 * @param out_key Receives the key.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for a null argument,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_transparent_draw_list_sort_key(
    const CNA_BoundingBox* bounds, const CNA_Vector3* camera_position, float* out_key);

/**
 * @brief Returns the camera position implied by a view matrix.
 *
 * A pure function of its argument.
 *
 * @param view The view matrix.
 * @param out_position Receives the position.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for a null matrix,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_transparent_draw_list_camera_position_of(
    const CNA_Matrix* view, CNA_Vector3* out_position);

/**
 * @brief Owned handle for one weighted-blended order-independent transparency resolve.
 */
typedef CNA_Handle CNA_WeightedBlendedTransparencyHandle;

/**
 * @brief Creates a weighted-blended transparency resolve at a given target size.
 *
 * @param graphics_device The device to allocate targets on.
 * @param width Target width in pixels; must be positive.
 * @param height Target height in pixels; must be positive.
 * @param out_transparency Receives the owned handle; set invalid on failure.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for a non-positive size,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_weighted_blended_transparency_create(
    CNA_Handle graphics_device,
    int32_t width,
    int32_t height,
    CNA_WeightedBlendedTransparencyHandle* out_transparency);

/**
 * @brief Releases the resolve.
 *
 * @param transparency The resolve; an invalid handle is an error, not a silent no-op.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_weighted_blended_transparency_destroy(
    CNA_WeightedBlendedTransparencyHandle transparency);

/**
 * @brief Reports whether this renderer can run the resolve.
 *
 * @param transparency The resolve.
 * @param out_supported Receives `CNA_TRUE` when it can.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_weighted_blended_transparency_is_supported(
    CNA_WeightedBlendedTransparencyHandle transparency, CNA_Bool* out_supported);

/**
 * @brief Copies why the resolve is unavailable, as UTF-8 bytes without a terminator.
 *
 * @param transparency The resolve.
 * @param destination Caller-owned destination, or null only when @p capacity is zero.
 * @param capacity Destination capacity in bytes.
 * @param out_bytes Receives the required byte count without a terminator.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL`, `CNA_RESULT_NOT_SUPPORTED` without
 * the engine layer, or an error. No partial string is written.
 */
CNA_C_API CNA_Result cna_weighted_blended_transparency_copy_unsupported_reason(
    CNA_WeightedBlendedTransparencyHandle transparency,
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

/**
 * @brief Resizes both accumulation targets.
 *
 * @param transparency The resolve.
 * @param width New width in pixels; must be positive.
 * @param height New height in pixels; must be positive.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for a non-positive size,
 * `CNA_RESULT_INVALID_STATE` while accumulation is open, `CNA_RESULT_NOT_SUPPORTED` without the
 * engine layer, or an error. The two are kept apart because a bad size is a caller's argument
 * mistake and an open bracket is a caller's sequencing mistake.
 */
CNA_C_API CNA_Result cna_weighted_blended_transparency_resize(
    CNA_WeightedBlendedTransparencyHandle transparency, int32_t width, int32_t height);

/**
 * @brief Opens accumulation, binding and clearing both targets.
 *
 * **On a renderer that cannot run the resolve this succeeds without opening anything**, so
 * @ref cna_weighted_blended_transparency_is_accumulating still reports `CNA_FALSE` and a matching
 * @ref cna_weighted_blended_transparency_end refuses. That is the canonical behaviour, reproduced
 * rather than corrected; ask `is_supported` before bracketing, or treat `end`'s refusal on an
 * unsupported renderer as expected. See `plans/plan_binding.md` `CBIND-098`.
 *
 * @param transparency The resolve.
 * @param far_plane The camera's far plane; must be positive, because the weight divides by it.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for a non-positive far plane,
 * `CNA_RESULT_INVALID_STATE` when accumulation is already open, `CNA_RESULT_NOT_SUPPORTED`
 * without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_weighted_blended_transparency_begin(
    CNA_WeightedBlendedTransparencyHandle transparency, float far_plane);

/**
 * @brief Closes accumulation.
 *
 * @param transparency The resolve.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_STATE` when accumulation is not open,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_weighted_blended_transparency_end(
    CNA_WeightedBlendedTransparencyHandle transparency);

/**
 * @brief Resolves the accumulated transparency into the active target.
 *
 * @param transparency The resolve.
 * @param width Viewport width in pixels; must be positive.
 * @param height Viewport height in pixels; must be positive.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for a non-positive size,
 * `CNA_RESULT_INVALID_STATE` while accumulation is still open, `CNA_RESULT_NOT_SUPPORTED` without
 * the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_weighted_blended_transparency_resolve(
    CNA_WeightedBlendedTransparencyHandle transparency, int32_t width, int32_t height);

/**
 * @brief Reports whether accumulation is open.
 *
 * @param transparency The resolve.
 * @param out_accumulating Receives `CNA_TRUE` while it is.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_weighted_blended_transparency_is_accumulating(
    CNA_WeightedBlendedTransparencyHandle transparency, CNA_Bool* out_accumulating);

/**
 * @brief Returns the accumulation target, borrowed.
 *
 * @param transparency The resolve.
 * @param out_texture Receives the borrowed texture, or `CNA_INVALID_HANDLE` when unsupported.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_weighted_blended_transparency_get_accumulation_texture_ext(
    CNA_WeightedBlendedTransparencyHandle transparency, CNA_Handle* out_texture);

/**
 * @brief Returns the revealage target, borrowed.
 *
 * @param transparency The resolve.
 * @param out_texture Receives the borrowed texture, or `CNA_INVALID_HANDLE` when unsupported.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_weighted_blended_transparency_get_revealage_texture_ext(
    CNA_WeightedBlendedTransparencyHandle transparency, CNA_Handle* out_texture);

/**
 * @brief Copies the GLSL of the accumulation pass as UTF-8 bytes without a terminator.
 *
 * @param destination Caller-owned destination, or null only when @p capacity is zero.
 * @param capacity Destination capacity in bytes.
 * @param out_bytes Receives the required byte count without a terminator.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL`, `CNA_RESULT_NOT_SUPPORTED` without
 * the engine layer, or an error. No partial string is written.
 */
CNA_C_API CNA_Result cna_weighted_blended_transparency_copy_accumulation_glsl(
    char* destination, uint64_t capacity, uint64_t* out_bytes);

/**
 * @brief Returns the blending weight for one fragment's depth and coverage.
 *
 * A pure function of its arguments. The depth ratio is **clamped** to zero-to-one and the weight
 * itself to a finite range, because the curve is unbounded near zero depth and a weight that
 * overflows would poison the whole accumulation buffer rather than one fragment.
 *
 * @param view_depth The fragment's view-space depth.
 * @param alpha The fragment's coverage.
 * @param far_plane The camera's far plane.
 * @param out_weight Receives the weight.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_weighted_blended_transparency_weight(
    float view_depth, float alpha, float far_plane, float* out_weight);

/* ---------------------------------------------------------------------------------------------
 * The render pipeline settings, in full
 * ------------------------------------------------------------------------------------------- */

/** @brief The smallest gamma the settings will store; below it the frame comes back as infinities. */
#define CNA_RENDER_PIPELINE_MINIMUM_GAMMA_EXT 0.01F

/** @brief The smallest local contrast FXAA will treat as an edge. */
#define CNA_RENDER_PIPELINE_MINIMUM_FXAA_EDGE_THRESHOLD_EXT 0.001F

/**
 * @brief The canonical `CNA::Graphics::RenderPipelineSettings` in full, as an extensible value.
 *
 * CNA extension. The frozen ten-field @ref CNA_RenderPipelineSettings cannot grow within an ABI
 * major, so the complete shape arrives under a new name, exactly as @ref CNA_PbrMaterialEXT did.
 *
 * **Writing a field here is not the same as calling the canonical setter.** Thirty-one of the
 * forty-seven correct their input -- ten clamp to a two-sided range and twenty-one floor at a
 * minimum -- and a structure written by hand holds whatever the caller put in it. Every route that
 * hands these settings to the engine runs each field through its canonical setter first, and
 * @ref cna_render_pipeline_settings_ext_normalize does the same in place, so a caller can ask what
 * the engine will actually store rather than assume the structure is what it gets.
 *
 * Initialize with @ref cna_render_pipeline_settings_ext_init; fields added in a later minor
 * version are appended at the end.
 */
typedef struct CNA_RenderPipelineSettingsEXT {
    /** @brief Size of this caller-provided structure in bytes. */
    uint32_t struct_size;

    /** @brief Version of this caller-provided structure. */
    uint32_t struct_version;
    /** @brief HDREnabled. Stored as given -- the canonical setter corrects nothing here. */
    CNA_Bool hdr_enabled;
    /** @brief Exposure. **Floored** at 0.0 when it reaches the engine; a negative value here is a sign error rather than a look. */
    float exposure;
    /** @brief Gamma. **Floored** at `CNA_RENDER_PIPELINE_MINIMUM_GAMMA_EXT`; gamma is applied as a reciprocal power, so zero is a division by zero. */
    float gamma;
    /** @brief TonemappingMode. Stored as given -- the canonical setter corrects nothing here. */
    CNA_TonemappingMode tonemapping_mode;
    /** @brief BloomEnabled. Stored as given -- the canonical setter corrects nothing here. */
    CNA_Bool bloom_enabled;
    /** @brief BloomIntensity. **Floored** at 0.0 when it reaches the engine; a negative value here is a sign error rather than a look. */
    float bloom_intensity;
    /** @brief BloomThreshold. **Floored** at 0.0 when it reaches the engine; a negative value here is a sign error rather than a look. */
    float bloom_threshold;
    /** @brief BloomIterations. Stored as given -- the canonical setter corrects nothing here. */
    int32_t bloom_iterations;
    /** @brief SSAOEnabled. Stored as given -- the canonical setter corrects nothing here. */
    CNA_Bool ssao_enabled;
    /** @brief TransparencyMode. Stored as given -- the canonical setter corrects nothing here. */
    CNA_TransparencyMode transparency_mode;
    /** @brief SSAORadius. **Floored** at 0.0 when it reaches the engine; a negative value here is a sign error rather than a look. */
    float ssao_radius;
    /** @brief SSAOIntensity. **Floored** at 0.0 when it reaches the engine; a negative value here is a sign error rather than a look. */
    float ssao_intensity;
    /** @brief SSAOSampleCount. Stored as given -- the canonical setter corrects nothing here. */
    int32_t ssao_sample_count;
    /** @brief SSREnabled. Stored as given -- the canonical setter corrects nothing here. */
    CNA_Bool ssr_enabled;
    /** @brief SSRMaxDistance. **Floored** at 0.0 when it reaches the engine; a negative value here is a sign error rather than a look. */
    float ssr_max_distance;
    /** @brief SSRStepCount. Stored as given -- the canonical setter corrects nothing here. */
    int32_t ssr_step_count;
    /** @brief SSRThickness. **Floored** at 0.0 when it reaches the engine; a negative value here is a sign error rather than a look. */
    float ssr_thickness;
    /** @brief SSRDepthBias. **Floored** at 0.0 when it reaches the engine; a negative value here is a sign error rather than a look. */
    float ssr_depth_bias;
    /** @brief SSREdgeFade. **Clamped** to 0.0 through 0.5 when it reaches the engine. */
    float ssr_edge_fade;
    /** @brief VolumetricFogDensity. **Floored** at 0.0 when it reaches the engine; a negative value here is a sign error rather than a look. */
    float volumetric_fog_density;
    /** @brief LightShaftThreshold. **Floored** at 0.0 when it reaches the engine; a negative value here is a sign error rather than a look. */
    float light_shaft_threshold;
    /** @brief LightShaftIntensity. **Floored** at 0.0 when it reaches the engine; a negative value here is a sign error rather than a look. */
    float light_shaft_intensity;
    /** @brief LightShaftDecay. **Clamped** to 0.0 through 1.0 when it reaches the engine. */
    float light_shaft_decay;
    /** @brief HeightFogDensity. **Floored** at 0.0 when it reaches the engine; a negative value here is a sign error rather than a look. */
    float height_fog_density;
    /** @brief HeightFogFalloff. **Floored** at 0.0 when it reaches the engine; a negative value here is a sign error rather than a look. */
    float height_fog_falloff;
    /** @brief HeightFogBaseHeight. Stored as given -- the canonical setter corrects nothing here. */
    float height_fog_base_height;
    /** @brief MotionBlurStrength. **Clamped** to 0.0 through 1.0 when it reaches the engine. */
    float motion_blur_strength;
    /** @brief MotionBlurMaxDistance. **Clamped** to 0.0 through 0.25 when it reaches the engine. */
    float motion_blur_max_distance;
    /** @brief ChromaticAberrationStrength. **Clamped** to 0.0 through 0.1 when it reaches the engine. */
    float chromatic_aberration_strength;
    /** @brief FilmGrainIntensity. **Clamped** to 0.0 through 1.0 when it reaches the engine. */
    float film_grain_intensity;
    /** @brief LensFlareThreshold. **Floored** at 0.0 when it reaches the engine; a negative value here is a sign error rather than a look. */
    float lens_flare_threshold;
    /** @brief LensFlareIntensity. **Floored** at 0.0 when it reaches the engine; a negative value here is a sign error rather than a look. */
    float lens_flare_intensity;
    /** @brief LensFlareDispersal. **Clamped** to 0.0 through 1.0 when it reaches the engine. */
    float lens_flare_dispersal;
    /** @brief ColorGradeEnabled. Stored as given -- the canonical setter corrects nothing here. */
    CNA_Bool color_grade_enabled;
    /** @brief ColorGradeStrength. **Clamped** to 0.0 through 1.0 when it reaches the engine. */
    float color_grade_strength;
    /** @brief DOFEnabled. Stored as given -- the canonical setter corrects nothing here. */
    CNA_Bool dof_enabled;
    /** @brief DOFFocusDistance. **Floored** at 0.0 when it reaches the engine; a negative value here is a sign error rather than a look. */
    float dof_focus_distance;
    /** @brief DOFFocalLength. **Floored** at 0.0 when it reaches the engine; a negative value here is a sign error rather than a look. */
    float dof_focal_length;
    /** @brief DOFFNumber. **Floored** at 0.0 when it reaches the engine; a negative value here is a sign error rather than a look. */
    float doff_number;
    /** @brief DOFMaxRadius. **Clamped** to 0.0 through 0.25 when it reaches the engine. */
    float dof_max_radius;
    /** @brief SSRRoughnessBlur. **Clamped** to 0.0 through 0.25 when it reaches the engine. */
    float ssr_roughness_blur;
    /** @brief SSRIntensity. **Floored** at 0.0 when it reaches the engine; a negative value here is a sign error rather than a look. */
    float ssr_intensity;
    /** @brief FXAAEnabled. Stored as given -- the canonical setter corrects nothing here. */
    CNA_Bool fxaa_enabled;
    /** @brief FXAAEdgeThresholdEXT. **Floored** at `CNA_RENDER_PIPELINE_MINIMUM_FXAA_EDGE_THRESHOLD_EXT`. */
    float fxaa_edge_threshold_ext;
    /** @brief RenderQuality. Stored as given -- the canonical setter corrects nothing here. */
    CNA_RenderQuality render_quality;
    /** @brief ShadowQuality. Stored as given -- the canonical setter corrects nothing here. */
    CNA_ShadowQuality shadow_quality;
    /** @brief ShadowsEnabled. Stored as given -- the canonical setter corrects nothing here. */
    CNA_Bool shadows_enabled;
    /** @brief Padding; write zero. */
    uint8_t reserved[4];
} CNA_RenderPipelineSettingsEXT;

/**
 * @brief Fills the settings with the canonical constructor's defaults.
 *
 * @param out_settings Receives the defaults along with `struct_size` and `struct_version`.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_render_pipeline_settings_ext_init(
    CNA_RenderPipelineSettingsEXT* out_settings);

/**
 * @brief Rewrites the settings as the engine would store them.
 *
 * Runs every field through its canonical setter and reads it back, so the thirty-one corrections
 * become visible without needing a pipeline. A caller that wants to know what a value will
 * actually become calls this; one that just hands the settings to a pipeline gets the same
 * corrections applied on the way in either way.
 *
 * @param settings The settings to normalize in place.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for a null or malformed structure or
 * an undefined identity, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_render_pipeline_settings_ext_normalize(
    CNA_RenderPipelineSettingsEXT* settings);

/**
 * @brief Applies the quality preset named by the settings' own render quality.
 *
 * Derives the fields a quality dial has been decided for -- today bloom's pyramid level count and
 * the FXAA edge threshold. Passes whose quality mapping has not been settled are deliberately left
 * alone rather than given a guessed one.
 *
 * @param settings The settings to update in place.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for a null or malformed structure or
 * an undefined identity, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_render_pipeline_settings_ext_apply_render_quality_preset(
    CNA_RenderPipelineSettingsEXT* settings);

/**
 * @brief Applies serialized settings text, reporting how many fields were recognised.
 *
 * Unrecognised fields are skipped rather than refused, which is what makes the count meaningful:
 * a caller compares it against what it expected to set.
 *
 * @param settings The settings to update in place.
 * @param text The serialized settings as UTF-8 bytes; need not be null-terminated.
 * @param out_applied Receives how many fields were recognised and applied.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for a null or malformed structure,
 * `CNA_RESULT_ENCODING` when the text is not valid UTF-8, `CNA_RESULT_NOT_SUPPORTED` without the
 * engine layer, or an error.
 */
CNA_C_API CNA_Result cna_render_pipeline_settings_ext_apply_from_string(
    CNA_RenderPipelineSettingsEXT* settings,
    CNA_StringView text,
    int32_t* out_applied);

/* ---------------------------------------------------------------------------------------------
 * The render pipeline
 * ------------------------------------------------------------------------------------------- */

/** @brief What one frame of the pipeline actually did. */
typedef struct CNA_RenderPipelineFrameStatisticsEXT {
    /** @brief Size of this structure in bytes. */
    uint32_t struct_size;
    /** @brief Version of this structure. */
    uint32_t struct_version;
    /** @brief How many post-process passes ran. */
    int32_t passes_run;
    /** @brief How many times the render target changed. */
    int32_t target_switches;
    /** @brief `CNA_TRUE` when the frame rendered through an offscreen scene target. */
    CNA_Bool used_scene_target;
    /** @brief `CNA_TRUE` when the skybox drew. */
    CNA_Bool drew_skybox;
    /** @brief Padding; always zero. */
    uint8_t reserved[2];
    /** @brief Estimated bytes of GPU memory the pipeline's targets hold. */
    uint64_t gpu_memory_estimate_bytes;
} CNA_RenderPipelineFrameStatisticsEXT;

/** @brief Owned handle for one render pipeline. */
typedef CNA_Handle CNA_RenderPipelineHandle;

/**
 * @brief Called once per frame to draw the transparent or shadow-casting geometry.
 *
 * @param context The pointer given alongside the callback.
 * @return `CNA_RESULT_SUCCESS`, or any documented result code to fail the frame that asked for it.
 */
typedef CNA_Result (*CNA_RenderPipelineDrawCallback)(void* context);

/**
 * @brief Creates a render pipeline on a device.
 *
 * The pipeline has no size until @ref cna_render_pipeline_resize is called; beginning a frame
 * before that is refused, which is a different state from a frame already being open and carries
 * its own message.
 *
 * @param graphics_device The device to allocate targets on.
 * @param out_pipeline Receives the owned handle; set invalid on failure.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_render_pipeline_create(
    CNA_Handle graphics_device, CNA_RenderPipelineHandle* out_pipeline);

/**
 * @brief Releases the pipeline.
 *
 * @param pipeline The pipeline; an invalid handle is an error, not a silent no-op.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_render_pipeline_destroy(CNA_RenderPipelineHandle pipeline);

/**
 * @brief Copies the pipeline's settings out.
 *
 * The canonical getter returns a reference into the pipeline; the C form **copies**, so the result
 * stays correct after the pipeline changes and there is no view to dangle.
 *
 * @param pipeline The pipeline.
 * @param out_settings Receives the settings.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for a null output,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_render_pipeline_get_settings(
    CNA_RenderPipelineHandle pipeline, CNA_RenderPipelineSettingsEXT* out_settings);

/**
 * @brief Copies settings into the pipeline.
 *
 * Every field goes through its canonical setter, so the thirty-one corrections `CBIND-088A`
 * documented apply here exactly as they do to @ref cna_render_pipeline_settings_ext_normalize.
 *
 * @param pipeline The pipeline.
 * @param settings The settings to apply.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for a null or malformed structure or
 * an undefined identity, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_render_pipeline_set_settings(
    CNA_RenderPipelineHandle pipeline, const CNA_RenderPipelineSettingsEXT* settings);

/**
 * @brief Sizes the pipeline's targets.
 *
 * @param pipeline The pipeline.
 * @param width Width in pixels; must be positive.
 * @param height Height in pixels; must be positive.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for a non-positive size,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_render_pipeline_resize(
    CNA_RenderPipelineHandle pipeline, int32_t width, int32_t height);

/**
 * @brief Opens a frame, clearing to a colour.
 *
 * Unlike the transparency resolve of `CBIND-098`, this bracket opens on **every** renderer: the
 * canonical `begin` marks the frame open before any support check and `end` closes it before any
 * early return, so the pair is symmetric wherever the layer is compiled in. That was read from the
 * bodies rather than assumed, because two other classes in this phase are not.
 *
 * @param pipeline The pipeline.
 * @param clear_color The colour to clear to.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_STATE` when a frame is already open **or** the
 * pipeline has never been sized -- two distinct states with distinct messages -- 
 * `CNA_RESULT_INVALID_ARGUMENT` for a null colour, `CNA_RESULT_NOT_SUPPORTED` without the engine
 * layer, or an error.
 */
CNA_C_API CNA_Result cna_render_pipeline_begin(
    CNA_RenderPipelineHandle pipeline, const CNA_Color* clear_color);

/**
 * @brief Closes the frame, running the post-process chain.
 *
 * @param pipeline The pipeline.
 * @return `CNA_RESULT_SUCCESS`, the failing draw callback's result, `CNA_RESULT_INVALID_STATE`
 * when no frame is open, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_render_pipeline_end(CNA_RenderPipelineHandle pipeline);

/**
 * @brief Appends a caller-owned post-process pass to run after the built-in ones.
 *
 * The pass is **borrowed**: the pipeline records it and never owns it, so the caller keeps it
 * alive for as long as it is registered.
 *
 * @param pipeline The pipeline.
 * @param pass The pass to append.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for an invalid pass,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_render_pipeline_add_user_pass(
    CNA_RenderPipelineHandle pipeline, CNA_PostProcessPassHandle pass);

/**
 * @brief Removes every caller-owned pass.
 *
 * @param pipeline The pipeline.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_render_pipeline_clear_user_passes(CNA_RenderPipelineHandle pipeline);

/**
 * @brief Gives the pipeline the depth and normal buffers its passes read.
 *
 * Both are borrowed, never owned.
 *
 * @param pipeline The pipeline.
 * @param depth The depth texture, or `CNA_INVALID_HANDLE`.
 * @param normals The normal texture, or `CNA_INVALID_HANDLE`.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_render_pipeline_set_depth_normal_inputs(
    CNA_RenderPipelineHandle pipeline, CNA_Handle depth, CNA_Handle normals);

/**
 * @brief Gives the pipeline the velocity buffer motion blur reads.
 *
 * @param pipeline The pipeline.
 * @param velocity The velocity texture, or `CNA_INVALID_HANDLE`; borrowed.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_render_pipeline_set_velocity_input_ext(
    CNA_RenderPipelineHandle pipeline, CNA_Handle velocity);

/**
 * @brief Registers the callback that draws transparent geometry inside the frame.
 *
 * @param pipeline The pipeline.
 * @param draw The callback, or null to clear it.
 * @param context Passed to @p draw unchanged; may be null.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_render_pipeline_set_transparent_scene(
    CNA_RenderPipelineHandle pipeline, CNA_RenderPipelineDrawCallback draw, void* context);

/**
 * @brief Registers the shadow map, light and caster callback for the frame's shadow pass.
 *
 * The shadow map is borrowed, never owned.
 *
 * @param pipeline The pipeline.
 * @param shadow_map The shadow map, or `CNA_INVALID_HANDLE` to clear the shadow scene.
 * @param light The directional light to render from.
 * @param scene_bounds The bounds the light's projection must cover.
 * @param draw_casters The callback that draws the casters, or null.
 * @param context Passed to @p draw_casters unchanged; may be null.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for a null light or bounds,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_render_pipeline_set_shadow_scene(
    CNA_RenderPipelineHandle pipeline,
    CNA_ShadowMapHandle shadow_map,
    const CNA_DirectionalLightEXT* light,
    const CNA_BoundingBox* scene_bounds,
    CNA_RenderPipelineDrawCallback draw_casters,
    void* context);

/**
 * @brief Sets the camera the frame renders from.
 *
 * @param pipeline The pipeline.
 * @param view The view matrix.
 * @param projection The projection matrix.
 * @param near_plane The near plane; must be positive.
 * @param far_plane The far plane; must be greater than @p near_plane.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for a null matrix or an inverted or
 * non-positive plane pair, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_render_pipeline_set_camera(
    CNA_RenderPipelineHandle pipeline,
    const CNA_Matrix* view,
    const CNA_Matrix* projection,
    float near_plane,
    float far_plane);

/**
 * @brief Sets the camera the skybox draws with, when it differs from the scene camera.
 *
 * @param pipeline The pipeline.
 * @param view The view matrix.
 * @param projection The projection matrix.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for a null matrix,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_render_pipeline_set_skybox_camera(
    CNA_RenderPipelineHandle pipeline, const CNA_Matrix* view, const CNA_Matrix* projection);

/**
 * @brief Copies why the transparency mode fell back, as UTF-8 bytes without a terminator.
 *
 * Empty when nothing fell back.
 *
 * @param pipeline The pipeline.
 * @param destination Caller-owned destination, or null only when @p capacity is zero.
 * @param capacity Destination capacity in bytes.
 * @param out_bytes Receives the required byte count without a terminator.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL`, `CNA_RESULT_NOT_SUPPORTED` without
 * the engine layer, or an error. No partial string is written.
 */
CNA_C_API CNA_Result cna_render_pipeline_copy_transparency_fallback_reason_ext(
    CNA_RenderPipelineHandle pipeline, char* destination, uint64_t capacity, uint64_t* out_bytes);

/**
 * @brief Turns GPU timing on or off.
 *
 * A renderer without GPU timers accepts the request and reports `CNA_FALSE` afterwards rather than
 * refusing, so a caller asks @ref cna_render_pipeline_is_gpu_timing_enabled_ext what it got.
 *
 * @param pipeline The pipeline.
 * @param value `CNA_TRUE` to enable.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for a byte that is neither
 * `CNA_TRUE` nor `CNA_FALSE`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_render_pipeline_set_gpu_timing_enabled_ext(
    CNA_RenderPipelineHandle pipeline, CNA_Bool value);

/**
 * @brief Reports whether GPU timing is on.
 *
 * @param pipeline The pipeline.
 * @param out_enabled Receives the answer.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_render_pipeline_is_gpu_timing_enabled_ext(
    CNA_RenderPipelineHandle pipeline, CNA_Bool* out_enabled);

/**
 * @brief Reports whether the skybox drew during the last frame.
 *
 * @param pipeline The pipeline.
 * @param out_drew Receives the answer.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_render_pipeline_did_skybox_draw(
    CNA_RenderPipelineHandle pipeline, CNA_Bool* out_drew);

/**
 * @brief Reports whether the shadow pass ran during the last frame.
 *
 * @param pipeline The pipeline.
 * @param out_ran Receives the answer.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_render_pipeline_did_shadow_pass_run(
    CNA_RenderPipelineHandle pipeline, CNA_Bool* out_ran);

/**
 * @brief Returns the shadow map the pipeline was given, borrowed.
 *
 * @param pipeline The pipeline.
 * @param out_shadow_map Receives the borrowed map, or `CNA_INVALID_HANDLE` when none is set.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_render_pipeline_get_shadow_map(
    CNA_RenderPipelineHandle pipeline, CNA_ShadowMapHandle* out_shadow_map);

/**
 * @brief Returns the offscreen scene target, borrowed.
 *
 * @param pipeline The pipeline.
 * @param out_texture Receives the borrowed texture, or `CNA_INVALID_HANDLE` when the pipeline
 *        renders straight to the back buffer.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_render_pipeline_get_scene_target(
    CNA_RenderPipelineHandle pipeline, CNA_Handle* out_texture);

/**
 * @brief Returns the scene target's surface format.
 *
 * @param pipeline The pipeline.
 * @param out_format Receives the format.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_render_pipeline_get_scene_target_format(
    CNA_RenderPipelineHandle pipeline, CNA_SurfaceFormat* out_format);

/**
 * @brief Reports whether the pipeline renders through an offscreen target.
 *
 * @param pipeline The pipeline.
 * @param out_using Receives the answer.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_render_pipeline_is_using_scene_target(
    CNA_RenderPipelineHandle pipeline, CNA_Bool* out_using);

/**
 * @brief Returns how many passes ran in the last frame.
 *
 * @param pipeline The pipeline.
 * @param out_count Receives the count.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_render_pipeline_get_last_frame_pass_count(
    CNA_RenderPipelineHandle pipeline, int32_t* out_count);

/**
 * @brief Returns the estimated GPU memory the pipeline's targets hold.
 *
 * @param pipeline The pipeline.
 * @param out_bytes Receives the estimate in bytes.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_render_pipeline_get_gpu_memory_estimate_bytes(
    CNA_RenderPipelineHandle pipeline, uint64_t* out_bytes);

/**
 * @brief Returns what the last frame did.
 *
 * @param pipeline The pipeline.
 * @param out_statistics Receives the statistics; its versioning fields are filled here.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_render_pipeline_get_statistics(
    CNA_RenderPipelineHandle pipeline, CNA_RenderPipelineFrameStatisticsEXT* out_statistics);

/**
 * @brief Releases the pipeline's device resources without destroying it.
 *
 * @param pipeline The pipeline.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_STATE` while a frame is open -- releasing the
 * targets mid-frame would leave the frame drawing into freed memory -- `CNA_RESULT_NOT_SUPPORTED`
 * without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_render_pipeline_release_device_resources_ext(
    CNA_RenderPipelineHandle pipeline);

/* ---------------------------------------------------------------------------------------------
 * The post-process chain
 * ------------------------------------------------------------------------------------------- */

/** @brief How long one pass took, without its name; read the name with the matching route. */
typedef struct CNA_PassTimingEXT {
    /** @brief Size of this structure in bytes. */
    uint32_t struct_size;
    /** @brief Version of this structure. */
    uint32_t struct_version;
    /** @brief How many samples the average is over; zero when the pass has not been timed. */
    int32_t sample_count;
    /** @brief Padding; always zero. */
    uint8_t reserved[4];
    /** @brief Mean milliseconds the pass took on the GPU. */
    double milliseconds;
} CNA_PassTimingEXT;

/** @brief Owned handle for one post-process chain. */
typedef CNA_Handle CNA_PostProcessChainHandle;

/**
 * @brief Creates an empty post-process chain.
 *
 * @param graphics_device The device its intermediate targets come from.
 * @param out_chain Receives the owned handle; set invalid on failure.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_post_process_chain_create(
    CNA_Handle graphics_device, CNA_PostProcessChainHandle* out_chain);

/**
 * @brief Releases the chain.
 *
 * Passes added with @ref cna_post_process_chain_add_owned_pass are released with it; passes added
 * with @ref cna_post_process_chain_add_pass are not, because the chain never owned them.
 *
 * @param chain The chain; an invalid handle is an error, not a silent no-op.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_post_process_chain_destroy(CNA_PostProcessChainHandle chain);

/**
 * @brief Appends a pass the caller keeps owning.
 *
 * **Borrowed:** the caller must keep the pass alive for as long as it is in the chain, and must
 * release it afterwards.
 *
 * @param chain The chain.
 * @param pass The pass to append.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for an invalid pass,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_post_process_chain_add_pass(
    CNA_PostProcessChainHandle chain, CNA_PostProcessPassHandle pass);

/**
 * @brief Appends a pass and takes ownership of it.
 *
 * **The pass handle is consumed.** On success it is released from the runtime and must not be used
 * again -- the chain owns the object now, and a second release would be a double free. This is the
 * one route in the engine layer that invalidates a handle a caller still holds, which is why it is
 * named `add_owned_pass` rather than being a flag on @ref cna_post_process_chain_add_pass.
 *
 * @param chain The chain.
 * @param pass The pass to hand over; invalid on return whether or not the call succeeded.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for an invalid pass or one that is
 * lending its effect, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_post_process_chain_add_owned_pass(
    CNA_PostProcessChainHandle chain, CNA_PostProcessPassHandle pass);

/**
 * @brief Removes every pass, releasing the ones the chain owns.
 *
 * @param chain The chain.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_post_process_chain_clear(CNA_PostProcessChainHandle chain);

/**
 * @brief Returns how many passes the chain holds.
 *
 * @param chain The chain.
 * @param out_count Receives the count.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_post_process_chain_get_pass_count(
    CNA_PostProcessChainHandle chain, int32_t* out_count);

/**
 * @brief Runs every pass in order, ping-ponging between pooled targets.
 *
 * @param chain The chain.
 * @param context The frame's inputs and destination.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for a null or malformed context, a
 * context with no source, or a non-positive size -- the chain needs somewhere to read from and a
 * size to allocate its intermediates against -- `CNA_RESULT_NOT_SUPPORTED` without the engine
 * layer, or an error.
 */
CNA_C_API CNA_Result cna_post_process_chain_apply(
    CNA_PostProcessChainHandle chain, const CNA_PostProcessContext* context);

/**
 * @brief Releases the chain's pooled intermediate targets.
 *
 * @param chain The chain.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_post_process_chain_reset_targets(CNA_PostProcessChainHandle chain);

/**
 * @brief Returns the chain's render-target pool, borrowed.
 *
 * A counted borrow: destroying the chain is refused while the pool handle is outstanding.
 *
 * @param chain The chain.
 * @param out_pool Receives the borrowed pool.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_post_process_chain_get_target_pool(
    CNA_PostProcessChainHandle chain, CNA_RenderTargetPoolHandle* out_pool);

/**
 * @brief Reports whether GPU timing is on.
 *
 * @param chain The chain.
 * @param out_enabled Receives the answer.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_post_process_chain_is_gpu_timing_enabled(
    CNA_PostProcessChainHandle chain, CNA_Bool* out_enabled);

/**
 * @brief Turns GPU timing on or off.
 *
 * A renderer without GPU timers accepts the request and reports `CNA_FALSE` afterwards rather than
 * refusing, so ask @ref cna_post_process_chain_is_gpu_timing_enabled what it got.
 *
 * @param chain The chain.
 * @param value `CNA_TRUE` to enable.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for a byte that is neither
 * `CNA_TRUE` nor `CNA_FALSE`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_post_process_chain_set_gpu_timing_enabled(
    CNA_PostProcessChainHandle chain, CNA_Bool value);

/**
 * @brief Returns how many pass timings the chain recorded.
 *
 * Zero when GPU timing is off or unavailable, which is not a failure.
 *
 * @param chain The chain.
 * @param out_count Receives the count.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_post_process_chain_get_pass_timing_count(
    CNA_PostProcessChainHandle chain, uint64_t* out_count);

/**
 * @brief Returns one recorded pass timing, without its name.
 *
 * The canonical timing carries a name, a duration and a sample count; a C structure cannot own the
 * string, so the name is read separately by
 * @ref cna_post_process_chain_copy_pass_timing_name. Splitting them keeps the structure a plain
 * value with no lifetime of its own.
 *
 * @param chain The chain.
 * @param index Which timing, from zero.
 * @param out_timing Receives the timing; its versioning fields are filled here.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for an index the chain does not
 * hold, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_post_process_chain_get_pass_timing(
    CNA_PostProcessChainHandle chain, uint64_t index, CNA_PassTimingEXT* out_timing);

/**
 * @brief Copies one recorded timing's pass name as UTF-8 bytes without a terminator.
 *
 * @param chain The chain.
 * @param index Which timing, from zero.
 * @param destination Caller-owned destination, or null only when @p capacity is zero.
 * @param capacity Destination capacity in bytes.
 * @param out_bytes Receives the required byte count without a terminator.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL`, `CNA_RESULT_INVALID_ARGUMENT` for
 * an index the chain does not hold, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an
 * error. No partial string is written.
 */
CNA_C_API CNA_Result cna_post_process_chain_copy_pass_timing_name(
    CNA_PostProcessChainHandle chain,
    uint64_t index,
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

/**
 * @brief Returns how many pass timings the pipeline's own chain recorded.
 *
 * `CBIND-088B` deferred the pipeline's timings row here, because the timing type belongs to the
 * chain rather than to the pipeline.
 *
 * @param pipeline The pipeline.
 * @param out_count Receives the count.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_render_pipeline_get_pass_timing_count_ext(
    CNA_RenderPipelineHandle pipeline, uint64_t* out_count);

/**
 * @brief Returns one of the pipeline's recorded pass timings, without its name.
 *
 * @param pipeline The pipeline.
 * @param index Which timing, from zero.
 * @param out_timing Receives the timing.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for an index the pipeline does not
 * hold, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_render_pipeline_get_pass_timing_ext(
    CNA_RenderPipelineHandle pipeline, uint64_t index, CNA_PassTimingEXT* out_timing);

/**
 * @brief Copies one of the pipeline's recorded timings' pass names as UTF-8 bytes.
 *
 * @param pipeline The pipeline.
 * @param index Which timing, from zero.
 * @param destination Caller-owned destination, or null only when @p capacity is zero.
 * @param capacity Destination capacity in bytes.
 * @param out_bytes Receives the required byte count without a terminator.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL`, `CNA_RESULT_INVALID_ARGUMENT` for
 * an index the pipeline does not hold, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an
 * error. No partial string is written.
 */
CNA_C_API CNA_Result cna_render_pipeline_copy_pass_timing_name_ext(
    CNA_RenderPipelineHandle pipeline,
    uint64_t index,
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

/* ---------------------------------------------------------------------------------------------
 * The screen-space passes
 * ------------------------------------------------------------------------------------------- */

/** @brief The fewest ray-march steps the reflection trace will take, whatever it is told. */
#define CNA_SSR_PASS_MIN_STEP_COUNT_EXT INT32_C(4)

/** @brief The most ray-march steps the reflection trace will take, whatever it is told. */
#define CNA_SSR_PASS_MAX_STEP_COUNT_EXT INT32_C(64)

/** @brief The sensor height the depth-of-field pass computes its circle of confusion against. */
#define CNA_DEPTH_OF_FIELD_SENSOR_HEIGHT_MILLIMETRES_EXT 24.0F

/**
 * @brief Creates a screen-space reflection pass.
 *
 * Creation succeeds on a renderer that cannot run it; ask `cna_post_process_pass_is_supported`.
 * Every pass is released with `cna_post_process_pass_destroy` and driven through the shared
 * `CNA_PostProcessPassHandle` routes -- this slice adds only what the pass itself knows.
 *
 * @param graphics_device The device to compile on.
 * @param out_pass Receives the owned handle; set invalid on failure.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_ssr_pass_create(
    CNA_Handle graphics_device, CNA_PostProcessPassHandle* out_pass);

/**
 * @brief Returns the pass's MaxDistance.
 *
 * @param pass The pass.
 * @param out_value Receives the value.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` when the pass is not a SsrPass,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_ssr_pass_get_max_distance(
    CNA_PostProcessPassHandle pass, float* out_value);

/**
 * @brief Sets the pass's MaxDistance.
 *
 * @param pass The pass.
 * @param value The value, **ignored when not positive** -- the canonical setter guards the assignment, so a zero or negative write leaves the previous distance in place rather than disabling the trace.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` when the pass is not a SsrPass,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_ssr_pass_set_max_distance(
    CNA_PostProcessPassHandle pass, float value);

/**
 * @brief Returns the pass's StepCount.
 *
 * @param pass The pass.
 * @param out_value Receives the value.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` when the pass is not a SsrPass,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_ssr_pass_get_step_count(
    CNA_PostProcessPassHandle pass, int32_t* out_value);

/**
 * @brief Sets the pass's StepCount.
 *
 * @param pass The pass.
 * @param value The value, stored as given; the march clamps it to `CNA_SSR_PASS_MIN_STEP_COUNT_EXT`..`CNA_SSR_PASS_MAX_STEP_COUNT_EXT` **when it applies**, not when it is set, so a caller reads back what it wrote.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` when the pass is not a SsrPass,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_ssr_pass_set_step_count(
    CNA_PostProcessPassHandle pass, int32_t value);

/**
 * @brief Returns the pass's Thickness.
 *
 * @param pass The pass.
 * @param out_value Receives the value.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` when the pass is not a SsrPass,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_ssr_pass_get_thickness(
    CNA_PostProcessPassHandle pass, float* out_value);

/**
 * @brief Sets the pass's Thickness.
 *
 * @param pass The pass.
 * @param value The value, **ignored when not positive** -- a guarded assignment; a zero-thickness depth test matches nothing.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` when the pass is not a SsrPass,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_ssr_pass_set_thickness(
    CNA_PostProcessPassHandle pass, float value);

/**
 * @brief Returns the pass's DepthBias.
 *
 * @param pass The pass.
 * @param out_value Receives the value.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` when the pass is not a SsrPass,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_ssr_pass_get_depth_bias(
    CNA_PostProcessPassHandle pass, float* out_value);

/**
 * @brief Sets the pass's DepthBias.
 *
 * @param pass The pass.
 * @param value The value, **ignored when not positive** -- a guarded assignment.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` when the pass is not a SsrPass,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_ssr_pass_set_depth_bias(
    CNA_PostProcessPassHandle pass, float value);

/**
 * @brief Returns the pass's RoughnessBlur.
 *
 * @param pass The pass.
 * @param out_value Receives the value.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` when the pass is not a SsrPass,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_ssr_pass_get_roughness_blur(
    CNA_PostProcessPassHandle pass, float* out_value);

/**
 * @brief Sets the pass's RoughnessBlur.
 *
 * @param pass The pass.
 * @param value The value, **clamped** to zero through 0.25.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` when the pass is not a SsrPass,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_ssr_pass_set_roughness_blur(
    CNA_PostProcessPassHandle pass, float value);

/**
 * @brief Returns the pass's EdgeFade.
 *
 * @param pass The pass.
 * @param out_value Receives the value.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` when the pass is not a SsrPass,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_ssr_pass_get_edge_fade(
    CNA_PostProcessPassHandle pass, float* out_value);

/**
 * @brief Sets the pass's EdgeFade.
 *
 * @param pass The pass.
 * @param value The value, **clamped** to zero through 0.5 -- a different bound from the roughness blur beside it.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` when the pass is not a SsrPass,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_ssr_pass_set_edge_fade(
    CNA_PostProcessPassHandle pass, float value);

/**
 * @brief Returns the pass's Intensity.
 *
 * @param pass The pass.
 * @param out_value Receives the value.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` when the pass is not a SsrPass,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_ssr_pass_get_intensity(
    CNA_PostProcessPassHandle pass, float* out_value);

/**
 * @brief Sets the pass's Intensity.
 *
 * @param pass The pass.
 * @param value The value, stored as given -- the canonical setter corrects nothing here.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` when the pass is not a SsrPass,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_ssr_pass_set_intensity(
    CNA_PostProcessPassHandle pass, float value);

/**
 * @brief Creates a screen-space ambient occlusion pass.
 *
 * Creation succeeds on a renderer that cannot run it; ask `cna_post_process_pass_is_supported`.
 * Every pass is released with `cna_post_process_pass_destroy` and driven through the shared
 * `CNA_PostProcessPassHandle` routes -- this slice adds only what the pass itself knows.
 *
 * @param graphics_device The device to compile on.
 * @param out_pass Receives the owned handle; set invalid on failure.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_ssao_pass_create(
    CNA_Handle graphics_device, CNA_PostProcessPassHandle* out_pass);

/**
 * @brief Returns the pass's Radius.
 *
 * @param pass The pass.
 * @param out_value Receives the value.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` when the pass is not a SsaoPass,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_ssao_pass_get_radius(
    CNA_PostProcessPassHandle pass, float* out_value);

/**
 * @brief Sets the pass's Radius.
 *
 * @param pass The pass.
 * @param value The value, stored as given.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` when the pass is not a SsaoPass,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_ssao_pass_set_radius(
    CNA_PostProcessPassHandle pass, float value);

/**
 * @brief Returns the pass's Intensity.
 *
 * @param pass The pass.
 * @param out_value Receives the value.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` when the pass is not a SsaoPass,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_ssao_pass_get_intensity(
    CNA_PostProcessPassHandle pass, float* out_value);

/**
 * @brief Sets the pass's Intensity.
 *
 * @param pass The pass.
 * @param value The value, stored as given.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` when the pass is not a SsaoPass,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_ssao_pass_set_intensity(
    CNA_PostProcessPassHandle pass, float value);

/**
 * @brief Returns the pass's SampleCount.
 *
 * @param pass The pass.
 * @param out_value Receives the value.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` when the pass is not a SsaoPass,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_ssao_pass_get_sample_count(
    CNA_PostProcessPassHandle pass, int32_t* out_value);

/**
 * @brief Sets the pass's SampleCount.
 *
 * @param pass The pass.
 * @param value The value, stored as given.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` when the pass is not a SsaoPass,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_ssao_pass_set_sample_count(
    CNA_PostProcessPassHandle pass, int32_t value);

/**
 * @brief Returns the pass's HalfResolution.
 *
 * @param pass The pass.
 * @param out_value Receives the value.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` when the pass is not a SsaoPass,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_ssao_pass_get_half_resolution(
    CNA_PostProcessPassHandle pass, CNA_Bool* out_value);

/**
 * @brief Sets the pass's HalfResolution.
 *
 * @param pass The pass.
 * @param value The value, stored as given.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` when the pass is not a SsaoPass or the byte is neither `CNA_TRUE` nor `CNA_FALSE`,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_ssao_pass_set_half_resolution(
    CNA_PostProcessPassHandle pass, CNA_Bool value);

/**
 * @brief Creates a depth-of-field pass.
 *
 * Creation succeeds on a renderer that cannot run it; ask `cna_post_process_pass_is_supported`.
 * Every pass is released with `cna_post_process_pass_destroy` and driven through the shared
 * `CNA_PostProcessPassHandle` routes -- this slice adds only what the pass itself knows.
 *
 * @param graphics_device The device to compile on.
 * @param out_pass Receives the owned handle; set invalid on failure.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_depth_of_field_pass_create(
    CNA_Handle graphics_device, CNA_PostProcessPassHandle* out_pass);

/**
 * @brief Returns the pass's FocusDistance.
 *
 * @param pass The pass.
 * @param out_value Receives the value.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` when the pass is not a DepthOfFieldPass,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_depth_of_field_pass_get_focus_distance(
    CNA_PostProcessPassHandle pass, float* out_value);

/**
 * @brief Sets the pass's FocusDistance.
 *
 * @param pass The pass.
 * @param value The value, **ignored when not positive** -- a guarded assignment; focusing at zero distance has no meaning.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` when the pass is not a DepthOfFieldPass,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_depth_of_field_pass_set_focus_distance(
    CNA_PostProcessPassHandle pass, float value);

/**
 * @brief Returns the pass's FocalLength.
 *
 * @param pass The pass.
 * @param out_value Receives the value.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` when the pass is not a DepthOfFieldPass,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_depth_of_field_pass_get_focal_length(
    CNA_PostProcessPassHandle pass, float* out_value);

/**
 * @brief Sets the pass's FocalLength.
 *
 * @param pass The pass.
 * @param value The value, **ignored when not positive** -- a guarded assignment.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` when the pass is not a DepthOfFieldPass,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_depth_of_field_pass_set_focal_length(
    CNA_PostProcessPassHandle pass, float value);

/**
 * @brief Returns the pass's FNumber.
 *
 * @param pass The pass.
 * @param out_value Receives the value.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` when the pass is not a DepthOfFieldPass,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_depth_of_field_pass_get_f_number(
    CNA_PostProcessPassHandle pass, float* out_value);

/**
 * @brief Sets the pass's FNumber.
 *
 * @param pass The pass.
 * @param value The value, **ignored when not positive** -- a guarded assignment; the aperture divides by it.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` when the pass is not a DepthOfFieldPass,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_depth_of_field_pass_set_f_number(
    CNA_PostProcessPassHandle pass, float value);

/**
 * @brief Returns the pass's MaxRadius.
 *
 * @param pass The pass.
 * @param out_value Receives the value.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` when the pass is not a DepthOfFieldPass,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_depth_of_field_pass_get_max_radius(
    CNA_PostProcessPassHandle pass, float* out_value);

/**
 * @brief Sets the pass's MaxRadius.
 *
 * @param pass The pass.
 * @param value The value, **clamped** to zero through 0.25.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` when the pass is not a DepthOfFieldPass,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_depth_of_field_pass_set_max_radius(
    CNA_PostProcessPassHandle pass, float value);

/**
 * @brief Releases the ambient-occlusion pass's pooled intermediate targets.
 *
 * @param pass The pass.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` when the pass is not an SsaoPass,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_ssao_pass_reset_targets(CNA_PostProcessPassHandle pass);

/**
 * @brief Copies the pass's sampling kernel.
 *
 * @param pass The pass.
 * @param destination Caller-owned destination, or null only when @p capacity is zero.
 * @param capacity Destination capacity in elements.
 * @param out_count Receives the required element count.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL`, `CNA_RESULT_INVALID_ARGUMENT` when
 * the pass is not an SsaoPass, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 * No partial result is written.
 */
CNA_C_API CNA_Result cna_ssao_pass_copy_kernel(
    CNA_PostProcessPassHandle pass, CNA_Vector3* destination, uint64_t capacity,
    uint64_t* out_count);

/**
 * @brief Copies the occlusion GLSL as UTF-8 bytes without a terminator.
 *
 * A pure function of its argument, so it needs no pass.
 *
 * @param packed `CNA_TRUE` for the packed variant.
 * @param destination Caller-owned destination, or null only when @p capacity is zero.
 * @param capacity Destination capacity in bytes.
 * @param out_bytes Receives the required byte count without a terminator.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL`, `CNA_RESULT_INVALID_ARGUMENT` for a
 * byte that is neither `CNA_TRUE` nor `CNA_FALSE`, `CNA_RESULT_NOT_SUPPORTED` without the engine
 * layer, or an error. No partial string is written.
 */
CNA_C_API CNA_Result cna_ssao_pass_copy_occlusion_glsl(
    CNA_Bool packed, char* destination, uint64_t capacity, uint64_t* out_bytes);

/**
 * @brief Returns the sample count a quality preset asks for.
 *
 * A pure function of its argument.
 *
 * @param quality The preset.
 * @param out_count Receives the sample count.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for an undefined preset,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_ssao_pass_sample_count_for_quality(
    CNA_RenderQuality quality, int32_t* out_count);

/**
 * @brief Returns the circle of confusion for one depth, in millimetres.
 *
 * A pure function of its arguments, so it needs no pass.
 *
 * @param depth The surface's distance from the camera.
 * @param focus_distance The distance the lens is focused at.
 * @param focal_length The lens's focal length.
 * @param f_number The aperture's f-number.
 * @param out_millimetres Receives the diameter in millimetres.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_depth_of_field_pass_circle_of_confusion_millimetres(
    float depth, float focus_distance, float focal_length, float f_number,
    float* out_millimetres);

/* ---------------------------------------------------------------------------------------------
 * The atmospheric passes
 * ------------------------------------------------------------------------------------------- */

/** @brief How many froxel slices the volumetric fog marches. */
#define CNA_VOLUMETRIC_FOG_SLICE_COUNT_EXT INT32_C(32)

/** @brief The lateral resolution of each froxel slice. */
#define CNA_VOLUMETRIC_FOG_SLICE_RESOLUTION_EXT INT32_C(96)

/** @brief How many samples the light-shaft radial blur takes. */
#define CNA_LIGHT_SHAFT_STEP_COUNT_EXT INT32_C(24)

/**
 * @brief Creates a aerial-perspective pass.
 *
 * Creation succeeds on a renderer that cannot run it; ask `cna_post_process_pass_is_supported`.
 * Release it with `cna_post_process_pass_destroy`.
 *
 * @param graphics_device The device to compile on.
 * @param out_pass Receives the owned handle; set invalid on failure.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_aerial_perspective_pass_create(
    CNA_Handle graphics_device, CNA_PostProcessPassHandle* out_pass);

/**
 * @brief Returns the pass's SunDirection.
 *
 * @param pass The pass.
 * @param out_value Receives the value.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` when the pass is not a AerialPerspectivePass,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_aerial_perspective_pass_get_sun_direction(
    CNA_PostProcessPassHandle pass, CNA_Vector3* out_value);

/**
 * @brief Sets the pass's SunDirection.
 *
 * @param pass The pass.
 * @param value The value, stored as given -- the canonical setter corrects nothing here.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` when the pass is not a AerialPerspectivePass or the value is null,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_aerial_perspective_pass_set_sun_direction(
    CNA_PostProcessPassHandle pass, const CNA_Vector3* value);

/**
 * @brief Returns the pass's Turbidity.
 *
 * @param pass The pass.
 * @param out_value Receives the value.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` when the pass is not a AerialPerspectivePass,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_aerial_perspective_pass_get_turbidity(
    CNA_PostProcessPassHandle pass, float* out_value);

/**
 * @brief Sets the pass's Turbidity.
 *
 * @param pass The pass.
 * @param value The value, **floored at one**, not at zero: turbidity is a ratio against a perfectly clear atmosphere, so a value below one describes air clearer than vacuum.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` when the pass is not a AerialPerspectivePass,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_aerial_perspective_pass_set_turbidity(
    CNA_PostProcessPassHandle pass, const float value);

/**
 * @brief Returns the pass's Intensity.
 *
 * @param pass The pass.
 * @param out_value Receives the value.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` when the pass is not a AerialPerspectivePass,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_aerial_perspective_pass_get_intensity(
    CNA_PostProcessPassHandle pass, float* out_value);

/**
 * @brief Sets the pass's Intensity.
 *
 * @param pass The pass.
 * @param value The value, **ignored when negative**, but zero is accepted -- a guarded assignment whose bound admits zero, because no aerial perspective at all is a legitimate setting.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` when the pass is not a AerialPerspectivePass,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_aerial_perspective_pass_set_intensity(
    CNA_PostProcessPassHandle pass, const float value);

/**
 * @brief Returns the pass's ScaleHeight.
 *
 * @param pass The pass.
 * @param out_value Receives the value.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` when the pass is not a AerialPerspectivePass,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_aerial_perspective_pass_get_scale_height(
    CNA_PostProcessPassHandle pass, float* out_value);

/**
 * @brief Sets the pass's ScaleHeight.
 *
 * @param pass The pass.
 * @param value The value, **floored at 0.001** -- the air-mass integral divides by it, so zero is a division by zero rather than a thin atmosphere.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` when the pass is not a AerialPerspectivePass,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_aerial_perspective_pass_set_scale_height(
    CNA_PostProcessPassHandle pass, const float value);

/**
 * @brief Creates a volumetric-fog pass.
 *
 * Creation succeeds on a renderer that cannot run it; ask `cna_post_process_pass_is_supported`.
 * Release it with `cna_post_process_pass_destroy`.
 *
 * @param graphics_device The device to compile on.
 * @param out_pass Receives the owned handle; set invalid on failure.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_volumetric_fog_pass_create(
    CNA_Handle graphics_device, CNA_PostProcessPassHandle* out_pass);

/**
 * @brief Returns the pass's Density.
 *
 * @param pass The pass.
 * @param out_value Receives the value.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` when the pass is not a VolumetricFogPass,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_volumetric_fog_pass_get_density(
    CNA_PostProcessPassHandle pass, float* out_value);

/**
 * @brief Sets the pass's Density.
 *
 * @param pass The pass.
 * @param value The value, **ignored when negative**, but zero is accepted -- no fog is a legitimate setting.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` when the pass is not a VolumetricFogPass,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_volumetric_fog_pass_set_density(
    CNA_PostProcessPassHandle pass, const float value);

/**
 * @brief Returns the pass's Anisotropy.
 *
 * @param pass The pass.
 * @param out_value Receives the value.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` when the pass is not a VolumetricFogPass,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_volumetric_fog_pass_get_anisotropy(
    CNA_PostProcessPassHandle pass, float* out_value);

/**
 * @brief Sets the pass's Anisotropy.
 *
 * @param pass The pass.
 * @param value The value, **clamped to -0.95 through 0.95** -- the only two-sided clamp in this phase with a negative lower bound, because scattering runs from fully backward to fully forward and the poles are singular.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` when the pass is not a VolumetricFogPass,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_volumetric_fog_pass_set_anisotropy(
    CNA_PostProcessPassHandle pass, const float value);

/**
 * @brief Returns the pass's Range.
 *
 * @param pass The pass.
 * @param out_value Receives the value.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` when the pass is not a VolumetricFogPass,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_volumetric_fog_pass_get_range(
    CNA_PostProcessPassHandle pass, float* out_value);

/**
 * @brief Sets the pass's Range.
 *
 * @param pass The pass.
 * @param value The value, **ignored when not positive** -- a zero range has no volume to march through, so unlike the density beside it this guard rejects zero as well as negatives.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` when the pass is not a VolumetricFogPass,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_volumetric_fog_pass_set_range(
    CNA_PostProcessPassHandle pass, const float value);

/**
 * @brief Creates a height-fog pass.
 *
 * Creation succeeds on a renderer that cannot run it; ask `cna_post_process_pass_is_supported`.
 * Release it with `cna_post_process_pass_destroy`.
 *
 * @param graphics_device The device to compile on.
 * @param out_pass Receives the owned handle; set invalid on failure.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_height_fog_pass_create(
    CNA_Handle graphics_device, CNA_PostProcessPassHandle* out_pass);

/**
 * @brief Returns the pass's Color.
 *
 * @param pass The pass.
 * @param out_value Receives the value.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` when the pass is not a HeightFogPass,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_height_fog_pass_get_color(
    CNA_PostProcessPassHandle pass, CNA_Vector3* out_value);

/**
 * @brief Sets the pass's Color.
 *
 * @param pass The pass.
 * @param value The value, stored as given.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` when the pass is not a HeightFogPass or the value is null,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_height_fog_pass_set_color(
    CNA_PostProcessPassHandle pass, const CNA_Vector3* value);

/**
 * @brief Returns the pass's Density.
 *
 * @param pass The pass.
 * @param out_value Receives the value.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` when the pass is not a HeightFogPass,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_height_fog_pass_get_density(
    CNA_PostProcessPassHandle pass, float* out_value);

/**
 * @brief Sets the pass's Density.
 *
 * @param pass The pass.
 * @param value The value, **ignored when negative**, but zero is accepted -- no fog is a legitimate setting.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` when the pass is not a HeightFogPass,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_height_fog_pass_set_density(
    CNA_PostProcessPassHandle pass, const float value);

/**
 * @brief Returns the pass's Falloff.
 *
 * @param pass The pass.
 * @param out_value Receives the value.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` when the pass is not a HeightFogPass,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_height_fog_pass_get_falloff(
    CNA_PostProcessPassHandle pass, float* out_value);

/**
 * @brief Sets the pass's Falloff.
 *
 * @param pass The pass.
 * @param value The value, **ignored when not positive** -- the exponential divides by it, so zero is rejected as well as negatives.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` when the pass is not a HeightFogPass,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_height_fog_pass_set_falloff(
    CNA_PostProcessPassHandle pass, const float value);

/**
 * @brief Returns the pass's BaseHeight.
 *
 * @param pass The pass.
 * @param out_value Receives the value.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` when the pass is not a HeightFogPass,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_height_fog_pass_get_base_height(
    CNA_PostProcessPassHandle pass, float* out_value);

/**
 * @brief Sets the pass's BaseHeight.
 *
 * @param pass The pass.
 * @param value The value, stored as given -- a fog base below the origin is legitimate, so this one is not floored.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` when the pass is not a HeightFogPass,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_height_fog_pass_set_base_height(
    CNA_PostProcessPassHandle pass, const float value);

/**
 * @brief Creates a light-shaft pass.
 *
 * Creation succeeds on a renderer that cannot run it; ask `cna_post_process_pass_is_supported`.
 * Release it with `cna_post_process_pass_destroy`.
 *
 * @param graphics_device The device to compile on.
 * @param out_pass Receives the owned handle; set invalid on failure.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_light_shaft_pass_create(
    CNA_Handle graphics_device, CNA_PostProcessPassHandle* out_pass);

/**
 * @brief Returns the pass's LightScreenPosition.
 *
 * @param pass The pass.
 * @param out_value Receives the value.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` when the pass is not a LightShaftPass,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_light_shaft_pass_get_light_screen_position(
    CNA_PostProcessPassHandle pass, CNA_Vector2* out_value);

/**
 * @brief Sets the pass's LightScreenPosition.
 *
 * @param pass The pass.
 * @param value The value, stored as given -- a light off the edge of the screen still casts shafts across it.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` when the pass is not a LightShaftPass or the value is null,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_light_shaft_pass_set_light_screen_position(
    CNA_PostProcessPassHandle pass, const CNA_Vector2* value);

/**
 * @brief Returns the pass's Threshold.
 *
 * @param pass The pass.
 * @param out_value Receives the value.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` when the pass is not a LightShaftPass,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_light_shaft_pass_get_threshold(
    CNA_PostProcessPassHandle pass, float* out_value);

/**
 * @brief Sets the pass's Threshold.
 *
 * @param pass The pass.
 * @param value The value, **ignored when negative**, but zero is accepted.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` when the pass is not a LightShaftPass,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_light_shaft_pass_set_threshold(
    CNA_PostProcessPassHandle pass, const float value);

/**
 * @brief Returns the pass's Intensity.
 *
 * @param pass The pass.
 * @param out_value Receives the value.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` when the pass is not a LightShaftPass,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_light_shaft_pass_get_intensity(
    CNA_PostProcessPassHandle pass, float* out_value);

/**
 * @brief Sets the pass's Intensity.
 *
 * @param pass The pass.
 * @param value The value, **ignored when negative**, but zero is accepted.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` when the pass is not a LightShaftPass,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_light_shaft_pass_set_intensity(
    CNA_PostProcessPassHandle pass, const float value);

/**
 * @brief Returns the pass's Decay.
 *
 * @param pass The pass.
 * @param out_value Receives the value.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` when the pass is not a LightShaftPass,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_light_shaft_pass_get_decay(
    CNA_PostProcessPassHandle pass, float* out_value);

/**
 * @brief Sets the pass's Decay.
 *
 * @param pass The pass.
 * @param value The value, **clamped** to zero through one.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` when the pass is not a LightShaftPass,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_light_shaft_pass_set_decay(
    CNA_PostProcessPassHandle pass, const float value);

/**
 * @brief Copies why the aerial-perspective pass fell back, as UTF-8 bytes without a terminator.
 *
 * Empty when nothing fell back.
 *
 * @param pass The pass.
 * @param destination Caller-owned destination, or null only when @p capacity is zero.
 * @param capacity Destination capacity in bytes.
 * @param out_bytes Receives the required byte count without a terminator.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL`, `CNA_RESULT_INVALID_ARGUMENT` when
 * the pass is not an AerialPerspectivePass, `CNA_RESULT_NOT_SUPPORTED` without the engine layer,
 * or an error. No partial string is written.
 */
CNA_C_API CNA_Result cna_aerial_perspective_pass_copy_fallback_reason(
    CNA_PostProcessPassHandle pass, char* destination, uint64_t capacity, uint64_t* out_bytes);

/**
 * @brief Returns the air mass along a view ray over a distance.
 *
 * A pure function of its arguments, so it needs no pass.
 *
 * @param view_direction The direction the ray travels.
 * @param distance How far it travels.
 * @param scale_height The atmosphere's scale height.
 * @param out_air_mass Receives the air mass.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for a null direction,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_aerial_perspective_pass_air_mass_for_distance(
    const CNA_Vector3* view_direction, float distance, float scale_height, float* out_air_mass);

/**
 * @brief Returns the transmittance through a given air mass at a given turbidity.
 *
 * A pure function of its arguments.
 *
 * @param turbidity The atmosphere's turbidity.
 * @param air_mass The air mass the light crosses.
 * @param out_transmittance Receives the per-channel transmittance.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_aerial_perspective_pass_transmittance(
    float turbidity, float air_mass, CNA_Vector3* out_transmittance);

/**
 * @brief Returns the optical depth through height fog along a ray.
 *
 * A pure function of its arguments, so it needs no pass.
 *
 * @param camera_height The camera's height.
 * @param ray_height_step How much height the ray gains per unit of distance.
 * @param distance How far the ray travels.
 * @param density The fog's density.
 * @param falloff The fog's exponential falloff.
 * @param base_height The height the fog is densest at.
 * @param out_depth Receives the optical depth.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_height_fog_pass_optical_depth(
    float camera_height,
    float ray_height_step,
    float distance,
    float density,
    float falloff,
    float base_height,
    float* out_depth);

/**
 * @brief Gives the volumetric fog the light it marches against.
 *
 * The shadow map is **borrowed**, never owned; pass `CNA_INVALID_HANDLE` to march unshadowed.
 *
 * @param pass The pass.
 * @param shadow_map The shadow map, or `CNA_INVALID_HANDLE`.
 * @param light_direction The light's direction.
 * @param light_color The light's colour.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` when the pass is not a
 * VolumetricFogPass or a vector is null, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or
 * an error.
 */
CNA_C_API CNA_Result cna_volumetric_fog_pass_set_light(
    CNA_PostProcessPassHandle pass,
    CNA_ShadowMapHandle shadow_map,
    const CNA_Vector3* light_direction,
    const CNA_Vector3* light_color);

/* ---------------------------------------------------------------------------------------------
 * The remaining post-process passes
 * ------------------------------------------------------------------------------------------- */

/** @brief How many ghost images the lens-flare pass draws. */
#define CNA_LENS_FLARE_GHOST_COUNT_EXT INT32_C(4)

/** @brief How many samples the motion-blur pass takes along its velocity vector. */
#define CNA_MOTION_BLUR_SAMPLE_COUNT_EXT INT32_C(8)

/**
 * @brief Owned handle for one deferred-decal projector.
 *
 * **Not a post-process pass.** Despite its name, `DecalPass` does not derive from
 * `PostProcessPass`: it has no `apply`, and it is driven by `cna_decal_pass_draw` per decal rather
 * than by a chain. It therefore carries its own handle and its own destroy, and the shared
 * `cna_post_process_pass_*` routes do not accept it.
 */
typedef CNA_Handle CNA_DecalPassHandle;

/**
 * @brief Owned handle for one spatial upscaler.
 *
 * **Not a post-process pass**, for the same reason as @ref CNA_DecalPassHandle: it is driven by
 * `cna_spatial_upscale_pass_draw` with an explicit source and target size rather than by a chain.
 */
typedef CNA_Handle CNA_SpatialUpscalePassHandle;

/**
 * @brief Creates a bloom pass.
 *
 * Creation succeeds on a renderer that cannot run it; ask `cna_post_process_pass_is_supported`.
 * Release it with `cna_post_process_pass_destroy`.
 *
 * @param graphics_device The device to compile on.
 * @param out_pass Receives the owned handle; set invalid on failure.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_bloom_pass_create(
    CNA_Handle graphics_device, CNA_PostProcessPassHandle* out_pass);

/**
 * @brief Returns the pass's Threshold.
 *
 * @param pass The pass.
 * @param out_value Receives the value.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` when the pass is not a BloomPass,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_bloom_pass_get_threshold(
    CNA_PostProcessPassHandle pass, float* out_value);

/**
 * @brief Sets the pass's Threshold.
 *
 * @param pass The pass.
 * @param value The value, stored as given.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` when the pass is not a BloomPass,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_bloom_pass_set_threshold(
    CNA_PostProcessPassHandle pass, float value);

/**
 * @brief Returns the pass's Intensity.
 *
 * @param pass The pass.
 * @param out_value Receives the value.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` when the pass is not a BloomPass,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_bloom_pass_get_intensity(
    CNA_PostProcessPassHandle pass, float* out_value);

/**
 * @brief Sets the pass's Intensity.
 *
 * @param pass The pass.
 * @param value The value, stored as given.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` when the pass is not a BloomPass,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_bloom_pass_set_intensity(
    CNA_PostProcessPassHandle pass, float value);

/**
 * @brief Returns the pass's Iterations.
 *
 * @param pass The pass.
 * @param out_value Receives the value.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` when the pass is not a BloomPass,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_bloom_pass_get_iterations(
    CNA_PostProcessPassHandle pass, int32_t* out_value);

/**
 * @brief Sets the pass's Iterations.
 *
 * @param pass The pass.
 * @param value The value, stored as given; the pyramid clamps the count where it builds it.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` when the pass is not a BloomPass,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_bloom_pass_set_iterations(
    CNA_PostProcessPassHandle pass, int32_t value);

/**
 * @brief Creates a deferred-decal pass.
 *
 * Creation succeeds on a renderer that cannot run it; ask `cna_post_process_pass_is_supported`.
 * Release it with `cna_post_process_pass_destroy`.
 *
 * @param graphics_device The device to compile on.
 * @param out_pass Receives the owned handle; set invalid on failure.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_decal_pass_create(
    CNA_Handle graphics_device, CNA_DecalPassHandle* out_pass);

/**
 * @brief Releases the decal projector.
 *
 * @param pass The projector; an invalid handle is an error, not a silent no-op.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_decal_pass_destroy(CNA_DecalPassHandle pass);

/**
 * @brief Returns the pass's Opacity.
 *
 * @param pass The pass.
 * @param out_value Receives the value.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` when the pass is not a DecalPass,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_decal_pass_get_opacity(
    CNA_DecalPassHandle pass, float* out_value);

/**
 * @brief Sets the pass's Opacity.
 *
 * @param pass The pass.
 * @param value The value, **clamped** to zero through one.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` when the pass is not a DecalPass,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_decal_pass_set_opacity(
    CNA_DecalPassHandle pass, float value);

/**
 * @brief Returns the pass's Tint.
 *
 * @param pass The pass.
 * @param out_value Receives the value.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` when the pass is not a DecalPass,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_decal_pass_get_tint(
    CNA_DecalPassHandle pass, CNA_Vector3* out_value);

/**
 * @brief Sets the pass's Tint.
 *
 * @param pass The pass.
 * @param value The value, stored as given.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` when the pass is not a DecalPass,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_decal_pass_set_tint(
    CNA_DecalPassHandle pass, const CNA_Vector3* value);

/**
 * @brief Returns the pass's MaxSlopeAngle.
 *
 * @param pass The pass.
 * @param out_value Receives the value.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` when the pass is not a DecalPass,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_decal_pass_get_max_slope_angle(
    CNA_DecalPassHandle pass, float* out_value);

/**
 * @brief Sets the pass's MaxSlopeAngle.
 *
 * @param pass The pass.
 * @param value The value, **clamped** to zero through a right angle in radians -- a decal cannot project onto a surface facing further away than perpendicular.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` when the pass is not a DecalPass,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_decal_pass_set_max_slope_angle(
    CNA_DecalPassHandle pass, float value);

/**
 * @brief Creates a lens-flare pass.
 *
 * Creation succeeds on a renderer that cannot run it; ask `cna_post_process_pass_is_supported`.
 * Release it with `cna_post_process_pass_destroy`.
 *
 * @param graphics_device The device to compile on.
 * @param out_pass Receives the owned handle; set invalid on failure.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_lens_flare_pass_create(
    CNA_Handle graphics_device, CNA_PostProcessPassHandle* out_pass);

/**
 * @brief Returns the pass's Threshold.
 *
 * @param pass The pass.
 * @param out_value Receives the value.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` when the pass is not a LensFlarePass,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_lens_flare_pass_get_threshold(
    CNA_PostProcessPassHandle pass, float* out_value);

/**
 * @brief Sets the pass's Threshold.
 *
 * @param pass The pass.
 * @param value The value, **ignored when negative**, zero accepted.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` when the pass is not a LensFlarePass,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_lens_flare_pass_set_threshold(
    CNA_PostProcessPassHandle pass, float value);

/**
 * @brief Returns the pass's Intensity.
 *
 * @param pass The pass.
 * @param out_value Receives the value.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` when the pass is not a LensFlarePass,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_lens_flare_pass_get_intensity(
    CNA_PostProcessPassHandle pass, float* out_value);

/**
 * @brief Sets the pass's Intensity.
 *
 * @param pass The pass.
 * @param value The value, **ignored when negative**, zero accepted.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` when the pass is not a LensFlarePass,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_lens_flare_pass_set_intensity(
    CNA_PostProcessPassHandle pass, float value);

/**
 * @brief Returns the pass's Dispersal.
 *
 * @param pass The pass.
 * @param out_value Receives the value.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` when the pass is not a LensFlarePass,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_lens_flare_pass_get_dispersal(
    CNA_PostProcessPassHandle pass, float* out_value);

/**
 * @brief Sets the pass's Dispersal.
 *
 * @param pass The pass.
 * @param value The value, **clamped** to zero through one.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` when the pass is not a LensFlarePass,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_lens_flare_pass_set_dispersal(
    CNA_PostProcessPassHandle pass, float value);

/**
 * @brief Creates a motion-blur pass.
 *
 * Creation succeeds on a renderer that cannot run it; ask `cna_post_process_pass_is_supported`.
 * Release it with `cna_post_process_pass_destroy`.
 *
 * @param graphics_device The device to compile on.
 * @param out_pass Receives the owned handle; set invalid on failure.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_motion_blur_pass_create(
    CNA_Handle graphics_device, CNA_PostProcessPassHandle* out_pass);

/**
 * @brief Returns the pass's Strength.
 *
 * @param pass The pass.
 * @param out_value Receives the value.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` when the pass is not a MotionBlurPass,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_motion_blur_pass_get_strength(
    CNA_PostProcessPassHandle pass, float* out_value);

/**
 * @brief Sets the pass's Strength.
 *
 * @param pass The pass.
 * @param value The value, **clamped** to zero through one.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` when the pass is not a MotionBlurPass,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_motion_blur_pass_set_strength(
    CNA_PostProcessPassHandle pass, float value);

/**
 * @brief Returns the pass's MaxDistance.
 *
 * @param pass The pass.
 * @param out_value Receives the value.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` when the pass is not a MotionBlurPass,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_motion_blur_pass_get_max_distance(
    CNA_PostProcessPassHandle pass, float* out_value);

/**
 * @brief Sets the pass's MaxDistance.
 *
 * @param pass The pass.
 * @param value The value, **clamped** to zero through 0.25 -- a different bound from the strength beside it.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` when the pass is not a MotionBlurPass,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_motion_blur_pass_set_max_distance(
    CNA_PostProcessPassHandle pass, float value);

/**
 * @brief Creates a FXAA pass.
 *
 * Creation succeeds on a renderer that cannot run it; ask `cna_post_process_pass_is_supported`.
 * Release it with `cna_post_process_pass_destroy`.
 *
 * @param graphics_device The device to compile on.
 * @param out_pass Receives the owned handle; set invalid on failure.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_fxaa_pass_create(
    CNA_Handle graphics_device, CNA_PostProcessPassHandle* out_pass);

/**
 * @brief Returns the pass's EdgeThreshold.
 *
 * @param pass The pass.
 * @param out_value Receives the value.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` when the pass is not a FxaaPass,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_fxaa_pass_get_edge_threshold(
    CNA_PostProcessPassHandle pass, float* out_value);

/**
 * @brief Sets the pass's EdgeThreshold.
 *
 * @param pass The pass.
 * @param value The value, stored as given -- **the pass corrects nothing**, though the settings bag that can drive it floors the same value at `CNA_RENDER_PIPELINE_MINIMUM_FXAA_EDGE_THRESHOLD_EXT`; the two are different surfaces and only one of them corrects.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` when the pass is not a FxaaPass,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_fxaa_pass_set_edge_threshold(
    CNA_PostProcessPassHandle pass, float value);

/**
 * @brief Creates a spatial-upscale pass.
 *
 * Creation succeeds on a renderer that cannot run it; ask `cna_post_process_pass_is_supported`.
 * Release it with `cna_post_process_pass_destroy`.
 *
 * @param graphics_device The device to compile on.
 * @param out_pass Receives the owned handle; set invalid on failure.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_spatial_upscale_pass_create(
    CNA_Handle graphics_device, CNA_SpatialUpscalePassHandle* out_pass);

/**
 * @brief Releases the spatial upscaler.
 *
 * @param pass The upscaler; an invalid handle is an error, not a silent no-op.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_spatial_upscale_pass_destroy(CNA_SpatialUpscalePassHandle pass);

/**
 * @brief Returns the pass's Sharpness.
 *
 * @param pass The pass.
 * @param out_value Receives the value.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` when the pass is not a SpatialUpscalePass,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_spatial_upscale_pass_get_sharpness(
    CNA_SpatialUpscalePassHandle pass, float* out_value);

/**
 * @brief Sets the pass's Sharpness.
 *
 * @param pass The pass.
 * @param value The value, **clamped** to zero through one.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` when the pass is not a SpatialUpscalePass,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_spatial_upscale_pass_set_sharpness(
    CNA_SpatialUpscalePassHandle pass, float value);

/**
 * @brief Returns the pass's EdgeAdaptive.
 *
 * @param pass The pass.
 * @param out_value Receives the value.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` when the pass is not a SpatialUpscalePass,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_spatial_upscale_pass_get_edge_adaptive(
    CNA_SpatialUpscalePassHandle pass, CNA_Bool* out_value);

/**
 * @brief Sets the pass's EdgeAdaptive.
 *
 * @param pass The pass.
 * @param value The value, stored as given.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` when the pass is not a SpatialUpscalePass,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_spatial_upscale_pass_set_edge_adaptive(
    CNA_SpatialUpscalePassHandle pass, CNA_Bool value);

/**
 * @brief Creates a chromatic-aberration pass.
 *
 * Creation succeeds on a renderer that cannot run it; ask `cna_post_process_pass_is_supported`.
 * Release it with `cna_post_process_pass_destroy`.
 *
 * @param graphics_device The device to compile on.
 * @param out_pass Receives the owned handle; set invalid on failure.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_chromatic_aberration_pass_create(
    CNA_Handle graphics_device, CNA_PostProcessPassHandle* out_pass);

/**
 * @brief Returns the pass's Strength.
 *
 * @param pass The pass.
 * @param out_value Receives the value.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` when the pass is not a ChromaticAberrationPass,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_chromatic_aberration_pass_get_strength(
    CNA_PostProcessPassHandle pass, float* out_value);

/**
 * @brief Sets the pass's Strength.
 *
 * @param pass The pass.
 * @param value The value, **clamped** to zero through 0.1.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` when the pass is not a ChromaticAberrationPass,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_chromatic_aberration_pass_set_strength(
    CNA_PostProcessPassHandle pass, float value);

/**
 * @brief Creates a film-grain pass.
 *
 * Creation succeeds on a renderer that cannot run it; ask `cna_post_process_pass_is_supported`.
 * Release it with `cna_post_process_pass_destroy`.
 *
 * @param graphics_device The device to compile on.
 * @param out_pass Receives the owned handle; set invalid on failure.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_film_grain_pass_create(
    CNA_Handle graphics_device, CNA_PostProcessPassHandle* out_pass);

/**
 * @brief Returns the pass's Intensity.
 *
 * @param pass The pass.
 * @param out_value Receives the value.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` when the pass is not a FilmGrainPass,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_film_grain_pass_get_intensity(
    CNA_PostProcessPassHandle pass, float* out_value);

/**
 * @brief Sets the pass's Intensity.
 *
 * @param pass The pass.
 * @param value The value, **clamped** to zero through one.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` when the pass is not a FilmGrainPass,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_film_grain_pass_set_intensity(
    CNA_PostProcessPassHandle pass, float value);

/**
 * @brief Creates a ASCII pass.
 *
 * Creation succeeds on a renderer that cannot run it; ask `cna_post_process_pass_is_supported`.
 * Release it with `cna_post_process_pass_destroy`.
 *
 * @param graphics_device The device to compile on.
 * @param out_pass Receives the owned handle; set invalid on failure.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_ascii_pass_create(
    CNA_Handle graphics_device, CNA_PostProcessPassHandle* out_pass);

/**
 * @brief Releases the bloom pass's pooled pyramid targets.
 *
 * @param pass The pass.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` when the pass is not of that type, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an
 * error.
 */
CNA_C_API CNA_Result cna_bloom_pass_reset_targets(CNA_PostProcessPassHandle pass);

/**
 * @brief Returns how many pyramid levels a quality preset asks for.
 *
 * A pure function of its argument.
 *
 * @param quality The preset.
 * @param out_iterations Receives the level count.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for an undefined preset,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_bloom_pass_iterations_for_quality(
    CNA_RenderQuality quality, int32_t* out_iterations);

/**
 * @brief Returns how much of one channel survives the bright-pass threshold.
 *
 * A pure function of its arguments.
 *
 * @param value The channel's value.
 * @param threshold The bright-pass threshold.
 * @param out_extracted Receives what the bloom pyramid receives.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_bloom_pass_extract_channel(
    float value, float threshold, float* out_extracted);

/**
 * @brief Returns the FXAA fragment shader as UTF-8 bytes without a terminator.
 *
 * @param destination Caller-owned destination, or null only when @p capacity is zero.
 * @param capacity Destination capacity in bytes.
 * @param out_bytes Receives the required byte count without a terminator.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL`, `CNA_RESULT_NOT_SUPPORTED` without
 * the engine layer, or an error. No partial string is written.
 */
CNA_C_API CNA_Result cna_fxaa_pass_copy_fragment_glsl(
    char* destination, uint64_t capacity, uint64_t* out_bytes);

/**
 * @brief Returns the edge threshold a quality preset asks for.
 *
 * A pure function of its argument.
 *
 * @param quality The preset.
 * @param out_threshold Receives the threshold.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for an undefined preset,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_fxaa_pass_edge_threshold_for_quality(
    CNA_RenderQuality quality, float* out_threshold);

/**
 * @brief Gives the decal pass the depth and normal buffers it projects against.
 *
 * Both are borrowed, never owned.
 *
 * @param pass The pass.
 * @param depth The depth texture, or `CNA_INVALID_HANDLE`.
 * @param normals The normal texture, or `CNA_INVALID_HANDLE`.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` when the pass is not of that type, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an
 * error.
 */
CNA_C_API CNA_Result cna_decal_pass_set_prepass_inputs(
    CNA_DecalPassHandle pass, CNA_Handle depth, CNA_Handle normals);

/**
 * @brief Sets the camera the decal pass unprojects with.
 *
 * @param pass The pass.
 * @param view The view matrix.
 * @param projection The projection matrix.
 * @param far_plane The far plane; **ignored when not positive**, because the unprojection divides
 *        by it, so a bad value leaves the previous camera in place rather than breaking the pass.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for a null matrix or when the pass
 * is not a DecalPass, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_decal_pass_set_camera(
    CNA_DecalPassHandle pass,
    const CNA_Matrix* view,
    const CNA_Matrix* projection,
    float far_plane);

/**
 * @brief Projects one decal into the current target.
 *
 * @param pass The pass.
 * @param decal The decal texture; borrowed.
 * @param decal_world The decal box's world transform.
 * @param width Target width in pixels.
 * @param height Target height in pixels.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for a null matrix or when the pass
 * is not a DecalPass, `CNA_RESULT_INVALID_STATE` when the pass has no prepass inputs or no camera,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_decal_pass_draw(
    CNA_DecalPassHandle pass,
    CNA_Handle decal,
    const CNA_Matrix* decal_world,
    int32_t width,
    int32_t height);

/**
 * @brief Reports whether a point in the decal's local space falls inside its box.
 *
 * A pure function of its argument.
 *
 * @param decal_local_position The point, in the decal box's local space.
 * @param out_inside Receives the answer.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for a null point,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_decal_pass_is_inside_decal_box(
    const CNA_Vector3* decal_local_position, CNA_Bool* out_inside);

/**
 * @brief Upscales a source into the current target.
 *
 * @param pass The pass.
 * @param source The source texture; borrowed.
 * @param source_width Source width in pixels; must be positive.
 * @param source_height Source height in pixels; must be positive.
 * @param target_width Target width in pixels; must be positive.
 * @param target_height Target height in pixels; must be positive.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for a non-positive size, a null
 * source or when the pass is not a SpatialUpscalePass, `CNA_RESULT_NOT_SUPPORTED` without the
 * engine layer, or an error.
 */
CNA_C_API CNA_Result cna_spatial_upscale_pass_draw(
    CNA_SpatialUpscalePassHandle pass,
    CNA_Handle source,
    int32_t source_width,
    int32_t source_height,
    int32_t target_width,
    int32_t target_height);

/**
 * @brief Reports whether a source and target size need no scaling at all.
 *
 * A pure function of its arguments.
 *
 * @param source_width Source width in pixels.
 * @param source_height Source height in pixels.
 * @param target_width Target width in pixels.
 * @param target_height Target height in pixels.
 * @param out_identity Receives `CNA_TRUE` when the sizes match.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_spatial_upscale_pass_is_identity_scale(
    int32_t source_width,
    int32_t source_height,
    int32_t target_width,
    int32_t target_height,
    CNA_Bool* out_identity);

/**
 * @brief Returns the ASCII pass's effect, borrowed.
 *
 * The effect is the pass's own and does **not** keep it alive; releasing the returned handle does
 * not release the pass, and the pass must outlive it.
 *
 * @param pass The pass.
 * @param out_effect Receives the borrowed effect.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` when the pass is not of that type, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an
 * error.
 */
CNA_C_API CNA_Result cna_ascii_pass_get_effect(
    CNA_PostProcessPassHandle pass, CNA_AsciiPostProcessEffectHandle* out_effect);

/* ---------------------------------------------------------------------------------------------
 * HDR output, tonemapping and colour grading
 * ------------------------------------------------------------------------------------------- */

/**
 * @brief Uncharted 2 filmic tonemapping (Hable's curve), normalized against a white point.
 *
 * `CBIND-088A` found this enumerator declared in the canonical enum but absent from the C
 * identity, so a C caller could not select it and `cna_render_pipeline_settings_ext_normalize`
 * refused its ordinal. It is appended rather than inserted, because the preceding values are
 * stored in settings and compared by ordinal elsewhere. Unlike `FILMIC`, this curve does not bake
 * gamma into itself, so the pipeline's gamma step still applies to its output.
 */
#define CNA_TONEMAPPING_MODE_UNCHARTED2 UINT32_C(4)

/** @brief Fixed-width identity for how a LUT is sampled between its slices. */
typedef uint32_t CNA_LutInterpolation;
/** @brief Sample the eight surrounding entries and blend them. */
#define CNA_LUT_INTERPOLATION_TRILINEAR UINT32_C(0)
/** @brief Sample the four entries of the enclosing tetrahedron; sharper on steep gradients. */
#define CNA_LUT_INTERPOLATION_TETRAHEDRAL UINT32_C(1)

/** @brief Fixed-width identity for the colour space a display expects. */
typedef uint32_t CNA_DisplayColorSpace;
/** @brief Ordinary sRGB output. */
#define CNA_DISPLAY_COLOR_SPACE_SRGB UINT32_C(0)
/** @brief Extended-range linear sRGB, as scRGB displays expect. */
#define CNA_DISPLAY_COLOR_SPACE_SCRGB UINT32_C(1)
/** @brief Rec.2020 primaries with the PQ transfer function. */
#define CNA_DISPLAY_COLOR_SPACE_HDR10 UINT32_C(2)

/** @brief The largest slice count a colour-grading LUT strip may describe. */
#define CNA_COLOR_GRADE_MAX_LUT_SIZE_EXT INT32_C(64)

/** @brief The smallest cube LUT this layer accepts; below two nothing can be interpolated. */
#define CNA_CUBE_LUT_MIN_SIZE_EXT INT32_C(2)

/** @brief The largest cube LUT this layer accepts. */
#define CNA_CUBE_LUT_MAX_SIZE_EXT INT32_C(64)

/** @brief The luminance the pass treats as diffuse white unless told otherwise, in nits. */
#define CNA_HDR_DISPLAY_DEFAULT_PAPER_WHITE_NITS_EXT 200.0F

/** @brief The brightest luminance the pass will emit unless told otherwise, in nits. */
#define CNA_HDR_DISPLAY_DEFAULT_PEAK_NITS_EXT 1000.0F

/**
 * @brief Creates a tonemapping pass.
 *
 * @param graphics_device The device to compile on.
 * @param out_pass Receives the owned handle; set invalid on failure.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_tonemap_pass_create(
    CNA_Handle graphics_device, CNA_PostProcessPassHandle* out_pass);

/** @brief Returns the pass's tonemapping mode. */
CNA_C_API CNA_Result cna_tonemap_pass_get_mode(
    CNA_PostProcessPassHandle pass, CNA_TonemappingMode* out_mode);

/**
 * @brief Sets the pass's tonemapping mode.
 *
 * @param pass The pass.
 * @param mode The mode; an undefined identity is refused rather than cast through.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for an undefined mode or when the
 * pass is not a TonemapPass, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_tonemap_pass_set_mode(
    CNA_PostProcessPassHandle pass, CNA_TonemappingMode mode);

/** @brief Returns the pass's exposure. */
CNA_C_API CNA_Result cna_tonemap_pass_get_exposure(
    CNA_PostProcessPassHandle pass, float* out_value);

/** @brief Sets the pass's exposure; stored as given, the pass corrects nothing here. */
CNA_C_API CNA_Result cna_tonemap_pass_set_exposure(
    CNA_PostProcessPassHandle pass, float value);

/** @brief Returns the pass's gamma. */
CNA_C_API CNA_Result cna_tonemap_pass_get_gamma(
    CNA_PostProcessPassHandle pass, float* out_value);

/** @brief Sets the pass's gamma; stored as given, the pass corrects nothing here. */
CNA_C_API CNA_Result cna_tonemap_pass_set_gamma(
    CNA_PostProcessPassHandle pass, float value);

/** @brief Reports whether debanding is on. */
CNA_C_API CNA_Result cna_tonemap_pass_is_deband_enabled(
    CNA_PostProcessPassHandle pass, CNA_Bool* out_enabled);

/** @brief Turns debanding on or off. */
CNA_C_API CNA_Result cna_tonemap_pass_set_deband_enabled(
    CNA_PostProcessPassHandle pass, CNA_Bool value);

/** @brief Returns the deband strength. */
CNA_C_API CNA_Result cna_tonemap_pass_get_deband_strength(
    CNA_PostProcessPassHandle pass, float* out_value);

/** @brief Sets the deband strength. */
CNA_C_API CNA_Result cna_tonemap_pass_set_deband_strength(
    CNA_PostProcessPassHandle pass, float value);

/**
 * @brief Tonemaps one channel, exactly as the pass's shader would.
 *
 * A pure function of its arguments, so it needs no pass -- and the only way from C to check what
 * a mode does to a value without rendering a frame.
 *
 * @param mode The tonemapping mode.
 * @param value The channel's scene-linear value.
 * @param exposure The exposure multiplier.
 * @param gamma The gamma to encode with.
 * @param out_value Receives the tonemapped channel.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for an undefined mode,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_tonemap_pass_tonemap_channel(
    CNA_TonemappingMode mode, float value, float exposure, float gamma, float* out_value);

/**
 * @brief Creates a colour-grading pass.
 *
 * @param graphics_device The device to compile on.
 * @param out_pass Receives the owned handle; set invalid on failure.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_color_grade_pass_create(
    CNA_Handle graphics_device, CNA_PostProcessPassHandle* out_pass);

/** @brief Returns the strip LUT, borrowed, or `CNA_INVALID_HANDLE`. */
CNA_C_API CNA_Result cna_color_grade_pass_get_lut(
    CNA_PostProcessPassHandle pass, CNA_Handle* out_lut);

/**
 * @brief Binds a strip LUT, or unbinds the current one.
 *
 * @param pass The pass.
 * @param lut The LUT texture; borrowed. `CNA_INVALID_HANDLE` **unbinds**, which is what every
 *        other `set*` in this ABI does with an invalid handle.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` when the pass is not a
 * ColorGradePass **or the texture is not a valid strip** -- a strip is N slices of N by N, so its
 * width must be the square of its height, and one read at the wrong slice count would grade the
 * frame into colours nothing in the table names, which is why it is refused rather than sampled --
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_color_grade_pass_set_lut(
    CNA_PostProcessPassHandle pass, CNA_Handle lut);

/** @brief Returns the volume LUT, borrowed, or `CNA_INVALID_HANDLE`. */
CNA_C_API CNA_Result cna_color_grade_pass_get_volume_lut(
    CNA_PostProcessPassHandle pass, CNA_Handle* out_lut);

/**
 * @brief Binds a volume LUT, or unbinds the current one.
 *
 * @param pass The pass.
 * @param lut The LUT texture; borrowed. `CNA_INVALID_HANDLE` unbinds.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` when the pass is not a
 * ColorGradePass or the texture is not a cube with an edge between two and
 * `CNA_COLOR_GRADE_MAX_LUT_SIZE_EXT`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an
 * error.
 */
CNA_C_API CNA_Result cna_color_grade_pass_set_volume_lut(
    CNA_PostProcessPassHandle pass, CNA_Handle lut);

/** @brief Returns how the LUT is sampled between slices. */
CNA_C_API CNA_Result cna_color_grade_pass_get_interpolation(
    CNA_PostProcessPassHandle pass, CNA_LutInterpolation* out_value);

/** @brief Sets how the LUT is sampled; an undefined identity is refused. */
CNA_C_API CNA_Result cna_color_grade_pass_set_interpolation(
    CNA_PostProcessPassHandle pass, CNA_LutInterpolation value);

/** @brief Returns how strongly the grade is applied. */
CNA_C_API CNA_Result cna_color_grade_pass_get_strength(
    CNA_PostProcessPassHandle pass, float* out_value);

/** @brief Sets how strongly the grade is applied. */
CNA_C_API CNA_Result cna_color_grade_pass_set_strength(
    CNA_PostProcessPassHandle pass, float value);

/**
 * @brief Returns the slice count a strip of the given pixel size describes.
 *
 * A pure function of its arguments.
 *
 * @param width Strip width in pixels.
 * @param height Strip height in pixels.
 * @param out_size Receives the slice count, or zero when the size describes no valid strip.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_color_grade_pass_lut_size_for_strip(
    int32_t width, int32_t height, int32_t* out_size);

/**
 * @brief Creates an identity strip LUT, which grades nothing.
 *
 * @param graphics_device The device to allocate on.
 * @param size The slice count; must be between two and `CNA_COLOR_GRADE_MAX_LUT_SIZE_EXT`.
 * @param out_lut Receives an owned texture handle; set invalid on failure.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for a size outside the range,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_color_grade_pass_create_identity_lut(
    CNA_Handle graphics_device, int32_t size, CNA_Handle* out_lut);

/** @brief Owned handle for one HDR display output. Not a post-process pass. */
typedef CNA_Handle CNA_HdrDisplayOutputHandle;

/**
 * @brief Creates an HDR display output.
 *
 * @param graphics_device The device to compile on.
 * @param out_output Receives the owned handle; set invalid on failure.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_hdr_display_output_create(
    CNA_Handle graphics_device, CNA_HdrDisplayOutputHandle* out_output);

/** @brief Releases the HDR display output. */
CNA_C_API CNA_Result cna_hdr_display_output_destroy(CNA_HdrDisplayOutputHandle output);

/** @brief Reports whether this renderer can drive an HDR display. */
CNA_C_API CNA_Result cna_hdr_display_output_is_supported(
    CNA_HdrDisplayOutputHandle output, CNA_Bool* out_supported);

/** @brief Returns the colour space the output encodes for. */
CNA_C_API CNA_Result cna_hdr_display_output_get_color_space(
    CNA_HdrDisplayOutputHandle output, CNA_DisplayColorSpace* out_space);

/** @brief Sets the colour space; an undefined identity is refused. */
CNA_C_API CNA_Result cna_hdr_display_output_set_color_space(
    CNA_HdrDisplayOutputHandle output, CNA_DisplayColorSpace value);

/** @brief Returns the luminance treated as diffuse white, in nits. */
CNA_C_API CNA_Result cna_hdr_display_output_get_paper_white_nits(
    CNA_HdrDisplayOutputHandle output, float* out_nits);

/** @brief Sets the luminance treated as diffuse white, in nits. */
CNA_C_API CNA_Result cna_hdr_display_output_set_paper_white_nits(
    CNA_HdrDisplayOutputHandle output, float value);

/** @brief Returns the brightest luminance the output will emit, in nits. */
CNA_C_API CNA_Result cna_hdr_display_output_get_peak_nits(
    CNA_HdrDisplayOutputHandle output, float* out_nits);

/**
 * @brief Sets the brightest luminance the output will emit, in nits.
 *
 * **Floored at the current paper-white**, not at a constant: a peak below diffuse white would
 * make white brighter than the brightest thing the display can show. The bound therefore moves
 * with @ref cna_hdr_display_output_set_paper_white_nits, which is the only correction of this
 * shape in the engine layer.
 *
 * @param output The output.
 * @param value The peak luminance; **raised to the paper-white value** when below it.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_hdr_display_output_set_peak_nits(
    CNA_HdrDisplayOutputHandle output, float value);

/**
 * @brief Encodes a scene-linear frame for the display.
 *
 * @param output The output.
 * @param source The scene-linear source; borrowed.
 * @param destination The destination render target, or `CNA_INVALID_HANDLE` for the back buffer.
 * @param width Destination width in pixels; must be positive.
 * @param height Destination height in pixels; must be positive.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for a non-positive size or a null
 * source, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_hdr_display_output_draw(
    CNA_HdrDisplayOutputHandle output,
    CNA_Handle source,
    CNA_Handle destination,
    int32_t width,
    int32_t height);

/** @brief Encodes a luminance in nits with the PQ transfer function. Pure. */
CNA_C_API CNA_Result cna_hdr_display_output_encode_pq(float nits, float* out_encoded);

/** @brief Decodes a PQ-encoded value back to nits. Pure, and the inverse of the above. */
CNA_C_API CNA_Result cna_hdr_display_output_decode_pq(float encoded, float* out_nits);

/** @brief Converts a Rec.709 colour to Rec.2020 primaries. Pure. */
CNA_C_API CNA_Result cna_hdr_display_output_rec709_to_rec2020(
    const CNA_Vector3* color, CNA_Vector3* out_color);

/** @brief Rolls a luminance off towards a peak so highlights compress rather than clip. Pure. */
CNA_C_API CNA_Result cna_hdr_display_output_roll_off(
    float nits, float peak_nits, float* out_nits);

/**
 * @brief Encodes one scene-linear colour for a colour space. Pure.
 *
 * @param space The colour space.
 * @param scene_linear The scene-linear colour.
 * @param paper_white_nits The luminance of diffuse white.
 * @param peak_nits The brightest luminance the display can show.
 * @param out_color Receives the encoded colour.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for an undefined space or a null
 * colour, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_hdr_display_output_encode(
    CNA_DisplayColorSpace space,
    const CNA_Vector3* scene_linear,
    float paper_white_nits,
    float peak_nits,
    CNA_Vector3* out_color);

/**
 * @brief Owned handle for one automatic-exposure meter. Not a post-process pass.
 *
 * **It needs compute shaders**, and unlike everything else in this layer it says so at
 * construction rather than through an `is_supported` query: the canonical constructor builds a
 * compute program and a storage buffer, so where the renderer has neither there is no object to
 * create. Ask `cna_graphics_device_supports_capability` with
 * `CNA_GRAPHICS_CAPABILITY_COMPUTE_SHADERS` before creating one.
 */
typedef CNA_Handle CNA_AutoExposureHandle;

/**
 * @brief Creates an automatic-exposure meter.
 *
 * @param graphics_device The device to read the scene on.
 * @param out_auto_exposure Receives the owned handle; set invalid on failure.
 * @return `CNA_RESULT_SUCCESS`; `CNA_RESULT_NOT_SUPPORTED` without the engine layer **or on a
 * renderer without compute shaders**, which is a capability boundary rather than a failure; or an
 * error.
 */
CNA_C_API CNA_Result cna_auto_exposure_ext_create(
    CNA_Handle graphics_device, CNA_AutoExposureHandle* out_auto_exposure);

/** @brief Releases the meter. */
CNA_C_API CNA_Result cna_auto_exposure_ext_destroy(CNA_AutoExposureHandle auto_exposure);

/**
 * @brief Measures a scene's average luminance without adapting to it.
 *
 * @param auto_exposure The meter.
 * @param scene The scene texture; borrowed.
 * @param out_luminance Receives the average luminance.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for a null scene,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_auto_exposure_ext_measure_average_luminance(
    CNA_AutoExposureHandle auto_exposure, CNA_Handle scene, float* out_luminance);

/**
 * @brief Adapts the exposure towards a scene over a time step, and returns the new exposure.
 *
 * @param auto_exposure The meter.
 * @param scene The scene texture; borrowed.
 * @param delta_seconds How much time has passed.
 * @param out_exposure Receives the exposure after adapting.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for a null scene,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_auto_exposure_ext_update(
    CNA_AutoExposureHandle auto_exposure,
    CNA_Handle scene,
    float delta_seconds,
    float* out_exposure);

/**
 * @brief Writes the meter's exposure into a settings structure.
 *
 * @param auto_exposure The meter.
 * @param settings The settings to update in place.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for a null or malformed structure,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_auto_exposure_ext_apply_to(
    CNA_AutoExposureHandle auto_exposure, CNA_RenderPipelineSettingsEXT* settings);

/** @brief Returns the exposure as it stands. */
CNA_C_API CNA_Result cna_auto_exposure_ext_get_exposure(
    CNA_AutoExposureHandle auto_exposure, float* out_value);

/**
 * @brief Sets the exposure.
 *
 * @param auto_exposure The meter.
 * @param value The exposure; **must be positive**, and is then **clamped into the current
 *        exposure range** -- so a value inside the contract can still come back different.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for a value that is not positive,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_auto_exposure_ext_set_exposure(
    CNA_AutoExposureHandle auto_exposure, float value);

/** @brief Returns the key value the meter targets. */
CNA_C_API CNA_Result cna_auto_exposure_ext_get_key_value(
    CNA_AutoExposureHandle auto_exposure, float* out_value);

/**
 * @brief Sets the key value the meter targets.
 *
 * @param auto_exposure The meter.
 * @param value The key value; must be positive.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for a value that is not positive,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_auto_exposure_ext_set_key_value(
    CNA_AutoExposureHandle auto_exposure, float value);

/** @brief Returns how fast the meter adapts to a brighter scene. */
CNA_C_API CNA_Result cna_auto_exposure_ext_get_brightening_speed(
    CNA_AutoExposureHandle auto_exposure, float* out_value);

/** @brief Returns how fast the meter adapts to a darker scene. */
CNA_C_API CNA_Result cna_auto_exposure_ext_get_darkening_speed(
    CNA_AutoExposureHandle auto_exposure, float* out_value);

/**
 * @brief Sets both adaptation speeds.
 *
 * **The pair is validated as a pair**: if either speed is not positive the call is refused and
 * neither is written, so one good value and one bad changes nothing.
 *
 * @param auto_exposure The meter.
 * @param brightening_per_second How fast to adapt to a brighter scene; must be positive.
 * @param darkening_per_second How fast to adapt to a darker scene; must be positive.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` when either speed is not positive,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_auto_exposure_ext_set_adaptation_speeds(
    CNA_AutoExposureHandle auto_exposure,
    float brightening_per_second,
    float darkening_per_second);

/**
 * @brief Sets the range the exposure is kept within.
 *
 * Validated as a pair, exactly as the adaptation speeds are, and **the current exposure is
 * re-clamped into the new range** -- so setting a range can change the exposure a caller just set.
 *
 * @param auto_exposure The meter.
 * @param minimum The lowest exposure; must be positive.
 * @param maximum The highest exposure; must not be below @p minimum.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` when the minimum is not positive or
 * the maximum is below it, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_auto_exposure_ext_set_exposure_range(
    CNA_AutoExposureHandle auto_exposure, float minimum, float maximum);

/**
 * @brief Owned handle for one parsed `.cube` colour LUT.
 *
 * **This is the byte-facing surface of the engine layer.** Everything else in Phase B9 takes
 * numbers a caller chose; this takes text a caller did not necessarily write, so its refusals are
 * covered by an independent oracle and a fuzz target rather than by a smoke test alone -- the
 * release gate's rule for parser-like surfaces, set by `CBIND-040B`.
 */
typedef CNA_Handle CNA_CubeLutHandle;

/**
 * @brief Parses a `.cube` LUT from text.
 *
 * @param text The LUT text as UTF-8 bytes; need not be null-terminated.
 * @param out_lut Receives the owned handle; set invalid on failure.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for text the parser refuses --
 * a missing or unreadable size, a size outside `CNA_CUBE_LUT_MIN_SIZE_EXT`..
 * `CNA_CUBE_LUT_MAX_SIZE_EXT`, a malformed domain line, an entry line with fewer than three
 * numbers, or an entry count that disagrees with the declared size -- `CNA_RESULT_ENCODING` when
 * the text is not valid UTF-8, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_cube_lut_parse(CNA_StringView text, CNA_CubeLutHandle* out_lut);

/**
 * @brief Loads and parses a `.cube` LUT from a file.
 *
 * @param path The file path as UTF-8 bytes.
 * @param out_lut Receives the owned handle; set invalid on failure.
 * @return As @ref cna_cube_lut_parse, plus `CNA_RESULT_IO` when the file cannot be opened.
 */
CNA_C_API CNA_Result cna_cube_lut_load_from_file(
    CNA_StringView path, CNA_CubeLutHandle* out_lut);

/** @brief Releases the LUT. */
CNA_C_API CNA_Result cna_cube_lut_destroy(CNA_CubeLutHandle lut);

/** @brief Returns the table's slice count. */
CNA_C_API CNA_Result cna_cube_lut_get_size(CNA_CubeLutHandle lut, int32_t* out_size);

/** @brief Copies the table's title as UTF-8 bytes without a terminator. */
CNA_C_API CNA_Result cna_cube_lut_copy_title(
    CNA_CubeLutHandle lut, char* destination, uint64_t capacity, uint64_t* out_bytes);

/** @brief Returns the low corner of the table's input domain. */
CNA_C_API CNA_Result cna_cube_lut_get_domain_min(CNA_CubeLutHandle lut, CNA_Vector3* out_value);

/** @brief Returns the high corner of the table's input domain. */
CNA_C_API CNA_Result cna_cube_lut_get_domain_max(CNA_CubeLutHandle lut, CNA_Vector3* out_value);

/** @brief Reports whether the domain is the unit cube, which needs no rescaling. */
CNA_C_API CNA_Result cna_cube_lut_is_unit_domain(CNA_CubeLutHandle lut, CNA_Bool* out_unit);

/**
 * @brief Returns one entry of the table.
 *
 * @param lut The LUT.
 * @param red The red index, from zero.
 * @param green The green index, from zero.
 * @param blue The blue index, from zero.
 * @param out_color Receives the entry.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for an index outside the table --
 * refused rather than clamped, because a clamped index would silently read a different colour --
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_cube_lut_get_entry(
    CNA_CubeLutHandle lut, int32_t red, int32_t green, int32_t blue, CNA_Vector3* out_color);

/** @brief Builds a strip texture from the table; the caller owns the result. */
CNA_C_API CNA_Result cna_cube_lut_create_strip_texture(
    CNA_CubeLutHandle lut, CNA_Handle graphics_device, CNA_Handle* out_texture);

/** @brief Builds a volume texture from the table; the caller owns the result. */
CNA_C_API CNA_Result cna_cube_lut_create_volume_texture(
    CNA_CubeLutHandle lut, CNA_Handle graphics_device, CNA_Handle* out_texture);

/* ---------------------------------------------------------------------------------------------
 * Light probes and image-based lighting
 * ------------------------------------------------------------------------------------------- */

/** @brief How many spherical-harmonic coefficients one probe stores. */
#define CNA_LIGHT_PROBE_COEFFICIENT_COUNT_EXT INT32_C(9)

/** @brief How many directions one probe stores visibility for. */
#define CNA_LIGHT_PROBE_VISIBILITY_DIRECTIONS_EXT INT32_C(6)

/** @brief The most probes one volume may hold. */
#define CNA_LIGHT_PROBE_VOLUME_MAX_PROBES_EXT INT32_C(32768)

/**
 * @brief One image-based light: the three textures a PBR shader needs, and how bright they are.
 *
 * A caller fills this and hands it to an effect. The textures are **borrowed**; the structure
 * records them and never owns them.
 */
typedef struct CNA_ImageBasedLightEXT {
    /** @brief Size of this caller-provided structure in bytes. */
    uint32_t struct_size;
    /** @brief Version of this caller-provided structure. */
    uint32_t struct_version;
    /** @brief The irradiance cube, or `CNA_INVALID_HANDLE`. */
    CNA_Handle irradiance;
    /** @brief The prefiltered specular cube, or `CNA_INVALID_HANDLE`. */
    CNA_Handle prefiltered_specular;
    /** @brief The BRDF lookup texture, or `CNA_INVALID_HANDLE`. */
    CNA_Handle brdf_lut;
    /** @brief How many mip levels the prefiltered cube has; at least one. */
    int32_t prefiltered_mip_count;
    /** @brief Scalar multiplier on the light. */
    float intensity;
} CNA_ImageBasedLightEXT;

/**
 * @brief Fills an image-based light with its canonical defaults.
 *
 * @param out_light Receives the defaults along with `struct_size` and `struct_version`.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_image_based_light_ext_init(CNA_ImageBasedLightEXT* out_light);

/**
 * @brief Reports whether the light is complete enough to shade with.
 *
 * All three textures must be present and the mip count at least one. A light that is *nearly*
 * complete is the failure this answers: it does not look like a mismatch, it looks like a scene
 * lit slightly wrong.
 *
 * @param light The light.
 * @param out_valid Receives the answer.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for a null or malformed structure,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_image_based_light_ext_is_valid(
    const CNA_ImageBasedLightEXT* light, CNA_Bool* out_valid);

/**
 * @brief Owned handle for one light probe.
 *
 * A probe is a **value** -- it compares by content and is copied into a volume rather than
 * referenced by it -- but it carries nine coefficient vectors and twelve visibility scalars, so it
 * is bound as a handle rather than a structure a caller assembles by hand.
 */
typedef CNA_Handle CNA_LightProbeHandle;

/**
 * @brief Creates a light probe at the origin with no stored light.
 *
 * @param out_probe Receives the owned handle; set invalid on failure.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_light_probe_ext_create(CNA_LightProbeHandle* out_probe);

/**
 * @brief Creates a light probe at a position.
 *
 * @param position The probe's world-space position.
 * @param out_probe Receives the owned handle; set invalid on failure.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for a null position,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_light_probe_ext_create_at(
    const CNA_Vector3* position, CNA_LightProbeHandle* out_probe);

/** @brief Releases the probe. */
CNA_C_API CNA_Result cna_light_probe_ext_destroy(CNA_LightProbeHandle probe);

/** @brief Copies every field of one probe over another, since a handle cannot be assigned. */
CNA_C_API CNA_Result cna_light_probe_ext_copy_from(
    CNA_LightProbeHandle destination, CNA_LightProbeHandle source);

/** @brief Returns the probe's world-space position. */
CNA_C_API CNA_Result cna_light_probe_ext_get_position(
    CNA_LightProbeHandle probe, CNA_Vector3* out_position);

/** @brief Sets the probe's world-space position. */
CNA_C_API CNA_Result cna_light_probe_ext_set_position(
    CNA_LightProbeHandle probe, const CNA_Vector3* position);

/**
 * @brief Returns one spherical-harmonic coefficient.
 *
 * @param probe The probe.
 * @param index Which coefficient, from zero.
 * @param out_value Receives the coefficient.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for an index outside the table -- **refused rather than clamped**, because a clamped index
 * returns a different coefficient and the surface would light almost right -- `CNA_RESULT_NOT_SUPPORTED`
 * without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_light_probe_ext_get_coefficient(
    CNA_LightProbeHandle probe, int32_t index, CNA_Vector3* out_value);

/**
 * @brief Sets one spherical-harmonic coefficient.
 *
 * @param probe The probe.
 * @param index Which coefficient, from zero.
 * @param value The coefficient.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for an index outside the table, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an
 * error.
 */
CNA_C_API CNA_Result cna_light_probe_ext_set_coefficient(
    CNA_LightProbeHandle probe, int32_t index, const CNA_Vector3* value);

/**
 * @brief Copies every coefficient at once.
 *
 * @param probe The probe.
 * @param destination Caller-owned destination, or null only when @p capacity is zero.
 * @param capacity Destination capacity in elements.
 * @param out_count Receives the required element count.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL`, `CNA_RESULT_NOT_SUPPORTED` without
 * the engine layer, or an error. No partial result is written.
 */
CNA_C_API CNA_Result cna_light_probe_ext_copy_coefficients(
    CNA_LightProbeHandle probe, CNA_Vector3* destination, uint64_t capacity,
    uint64_t* out_count);

/**
 * @brief Returns the irradiance arriving on a surface with a given normal.
 *
 * This is irradiance, not outgoing radiance, and it is **never negative** -- the reconstruction
 * can go below zero and the canonical code floors it, because negative light is not a look.
 *
 * @param probe The probe.
 * @param normal The surface normal.
 * @param out_irradiance Receives the irradiance per channel.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for a null normal,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_light_probe_ext_irradiance(
    CNA_LightProbeHandle probe, const CNA_Vector3* normal, CNA_Vector3* out_irradiance);

/**
 * @brief Stores the mean and mean-squared occluder distance for one direction.
 *
 * @param probe The probe.
 * @param direction Which direction, from zero.
 * @param mean_distance The mean distance; **floored at zero**.
 * @param mean_squared_distance The mean squared distance; **floored at zero**.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for a direction outside the table,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_light_probe_ext_set_visibility(
    CNA_LightProbeHandle probe, int32_t direction, float mean_distance,
    float mean_squared_distance);

/** @brief Returns the mean occluder distance for one direction. */
CNA_C_API CNA_Result cna_light_probe_ext_get_visibility_mean(
    CNA_LightProbeHandle probe, int32_t direction, float* out_value);

/** @brief Returns the mean squared occluder distance for one direction. */
CNA_C_API CNA_Result cna_light_probe_ext_get_visibility_mean_squared(
    CNA_LightProbeHandle probe, int32_t direction, float* out_value);

/** @brief Reports whether any visibility has been stored. */
CNA_C_API CNA_Result cna_light_probe_ext_has_visibility(
    CNA_LightProbeHandle probe, CNA_Bool* out_has);

/**
 * @brief Returns how much of the probe's light reaches a point in a direction.
 *
 * **Answers one when the probe has no visibility data, and one when the distance is not
 * positive** -- both mean "nothing is known to be in the way", which is the safe answer rather
 * than an error, and is why this route has no refusal for either case.
 *
 * @param probe The probe.
 * @param direction The direction from the probe.
 * @param distance How far away the shaded point is.
 * @param out_weight Receives the weight, between zero and one.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for a null direction,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_light_probe_ext_visibility_weight(
    CNA_LightProbeHandle probe, const CNA_Vector3* direction, float distance,
    float* out_weight);

/** @brief Reports whether the probe stores no light at all. */
CNA_C_API CNA_Result cna_light_probe_ext_is_zero(
    CNA_LightProbeHandle probe, CNA_Bool* out_zero);

/** @brief Multiplies every coefficient by a factor. */
CNA_C_API CNA_Result cna_light_probe_ext_scale(CNA_LightProbeHandle probe, float factor);

/** @brief Compares two probes by value across every coefficient and visibility entry. */
CNA_C_API CNA_Result cna_light_probe_ext_equals(
    CNA_LightProbeHandle first, CNA_LightProbeHandle second, CNA_Bool* out_equal);

/** @brief Copies the GLSL that evaluates a probe, as UTF-8 bytes without a terminator. */
CNA_C_API CNA_Result cna_light_probe_ext_copy_evaluation_glsl(
    char* destination, uint64_t capacity, uint64_t* out_bytes);

/** @brief Owned handle for one grid of light probes. */
typedef CNA_Handle CNA_LightProbeVolumeHandle;

/**
 * @brief Creates a probe volume over a box.
 *
 * @param bounds The box the grid spans.
 * @param count_x Probes along X; at least one.
 * @param count_y Probes along Y; at least one.
 * @param count_z Probes along Z; at least one.
 * @param out_volume Receives the owned handle; set invalid on failure.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for a count below one, a product
 * above `CNA_LIGHT_PROBE_VOLUME_MAX_PROBES_EXT`, or a box whose maximum is below its minimum --
 * three distinct refusals with their own messages, because a caller fixes each differently --
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_light_probe_volume_ext_create(
    const CNA_BoundingBox* bounds, int32_t count_x, int32_t count_y, int32_t count_z,
    CNA_LightProbeVolumeHandle* out_volume);

/** @brief Releases the volume. */
CNA_C_API CNA_Result cna_light_probe_volume_ext_destroy(CNA_LightProbeVolumeHandle volume);

/** @brief Returns the box the grid spans. */
CNA_C_API CNA_Result cna_light_probe_volume_ext_get_bounds(
    CNA_LightProbeVolumeHandle volume, CNA_BoundingBox* out_bounds);

/** @brief Returns how many probes lie along X. */
CNA_C_API CNA_Result cna_light_probe_volume_ext_get_count_x(
    CNA_LightProbeVolumeHandle volume, int32_t* out_count);

/** @brief Returns how many probes lie along Y. */
CNA_C_API CNA_Result cna_light_probe_volume_ext_get_count_y(
    CNA_LightProbeVolumeHandle volume, int32_t* out_count);

/** @brief Returns how many probes lie along Z. */
CNA_C_API CNA_Result cna_light_probe_volume_ext_get_count_z(
    CNA_LightProbeVolumeHandle volume, int32_t* out_count);

/** @brief Returns how many probes the volume holds in total. */
CNA_C_API CNA_Result cna_light_probe_volume_ext_get_probe_count(
    CNA_LightProbeVolumeHandle volume, int32_t* out_count);

/**
 * @brief Returns the world-space position of one probe in the grid.
 *
 * @param volume The volume.
 * @param x The X index, from zero.
 * @param y The Y index, from zero.
 * @param z The Z index, from zero.
 * @param out_position Receives the position.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for an index outside the grid,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_light_probe_volume_ext_get_probe_position(
    CNA_LightProbeVolumeHandle volume, int32_t x, int32_t y, int32_t z,
    CNA_Vector3* out_position);

/**
 * @brief Copies one probe out of the grid into a caller's probe.
 *
 * The volume stores probes **by value**, so this copies rather than lending: the result stays
 * correct after the volume changes.
 *
 * @param volume The volume.
 * @param x The X index, from zero.
 * @param y The Y index, from zero.
 * @param z The Z index, from zero.
 * @param out_probe An existing probe handle to overwrite.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for an index outside the grid,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_light_probe_volume_ext_get_probe(
    CNA_LightProbeVolumeHandle volume, int32_t x, int32_t y, int32_t z,
    CNA_LightProbeHandle out_probe);

/** @brief Copies a probe into one cell of the grid. */
CNA_C_API CNA_Result cna_light_probe_volume_ext_set_probe(
    CNA_LightProbeVolumeHandle volume, int32_t x, int32_t y, int32_t z,
    CNA_LightProbeHandle probe);

/** @brief Reports whether a position lies inside the volume's box. */
CNA_C_API CNA_Result cna_light_probe_volume_ext_contains(
    CNA_LightProbeVolumeHandle volume, const CNA_Vector3* position, CNA_Bool* out_contains);

/**
 * @brief Interpolates the eight surrounding probes into one.
 *
 * The position is **clamped into the volume's box** rather than refused: a point just outside a
 * probe grid is an ordinary thing during rendering, and the nearest interpolation is what a
 * caller wants there.
 *
 * @param volume The volume.
 * @param position The world-space position.
 * @param out_probe An existing probe handle to overwrite.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for a null position,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_light_probe_volume_ext_sample_probe(
    CNA_LightProbeVolumeHandle volume, const CNA_Vector3* position,
    CNA_LightProbeHandle out_probe);

/** @brief Returns the irradiance at a point on a surface facing one way. */
CNA_C_API CNA_Result cna_light_probe_volume_ext_irradiance(
    CNA_LightProbeVolumeHandle volume, const CNA_Vector3* position, const CNA_Vector3* normal,
    CNA_Vector3* out_irradiance);

/** @brief Reports whether every probe in the volume stores no light. */
CNA_C_API CNA_Result cna_light_probe_volume_ext_is_zero(
    CNA_LightProbeVolumeHandle volume, CNA_Bool* out_zero);

/**
 * @brief Gives the clustered forward effect a light probe to shade ambient light with.
 *
 * `CBIND-086C` deferred this route because `LightProbeEXT` did not exist in C yet. The probe is
 * **copied**, not borrowed: the effect keeps its own, so the caller's handle stays theirs.
 *
 * @param effect The effect.
 * @param probe The probe to copy in.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_clustered_forward_effect_set_light_probe(
    CNA_ClusteredForwardEffectHandle effect, CNA_LightProbeHandle probe);

/**
 * @brief Gives the clustered forward effect a probe volume to sample, or clears it.
 *
 * Unlike the single probe, the volume is **borrowed**: the canonical setter takes a pointer and
 * keeps it, so the caller must outlive the effect's use of it. `CNA_INVALID_HANDLE` clears it.
 *
 * @param effect The effect.
 * @param volume The volume, or `CNA_INVALID_HANDLE`.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_clustered_forward_effect_set_light_probe_volume(
    CNA_ClusteredForwardEffectHandle effect, CNA_LightProbeVolumeHandle volume);

/**
 * @brief Returns the image-based light an effect shades with.
 *
 * `CBIND-087C` deferred this route because `ImageBasedLightEXT` had no C form. The three textures
 * come back as **fresh handles onto the effect's own textures**: each keeps the effect alive while
 * it exists, and releasing one releases only the handle, never the texture. A caller must release
 * all three, exactly as it would for any other borrow in this layer.
 *
 * @param effect A PbrEffect or SkinnedPbrEffect handle.
 * @param out_light Receives the light.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` when the effect does not carry one
 * or the output is null, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_effect_get_image_based_light_ext(
    CNA_EffectHandle effect, CNA_ImageBasedLightEXT* out_light);

/**
 * @brief Gives an effect an image-based light to shade with.
 *
 * The three textures are **borrowed**, never owned, exactly as the shadow map is.
 *
 * @param effect A PbrEffect or SkinnedPbrEffect handle.
 * @param light The light to apply.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` when the effect cannot take one, or
 * for a null or malformed structure, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an
 * error.
 */
CNA_C_API CNA_Result cna_effect_set_image_based_light_ext(
    CNA_EffectHandle effect, const CNA_ImageBasedLightEXT* light);

/**
 * @brief The default cube-face resolution a probe baker captures at.
 */
#define CNA_LIGHT_PROBE_BAKER_DEFAULT_FACE_SIZE 32

/**
 * @brief The number of cube faces a probe capture renders.
 */
#define CNA_LIGHT_PROBE_BAKER_FACE_COUNT 6

/**
 * @brief Draws the scene for one cube face of a probe capture.
 *
 * Called once per face, six times per probe, with the view and projection the baker chose. Draw
 * the scene and nothing else: the baker owns the render target, and binding another one inside
 * this callback loses the face being captured.
 *
 * @param view The view matrix for this face.
 * @param projection The projection matrix for this face.
 * @param context The pointer given to the bake route.
 */
typedef void (*CNA_LightProbeSceneDrawCallback)(
    const CNA_Matrix* view, const CNA_Matrix* projection, void* context);

/**
 * @brief Owned handle for a light-probe baker.
 *
 * **Whether a baker can bake is probed, not asked.** No renderer publishes "can bind an offscreen
 * target and read it back" as a capability, and the two do not come together -- the headless
 * renderer binds happily and refuses the readback -- so the canonical baker renders one probe
 * capture at construction and remembers whether it worked. @ref cna_light_probe_baker_is_supported
 * reports that measurement, and every bake route refuses when it is false.
 */
typedef CNA_Handle CNA_LightProbeBakerHandle;

/**
 * @brief Creates a probe baker at the default face size.
 *
 * @param graphics_device The device to capture with.
 * @param out_baker Receives the baker; invalid on failure.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for an invalid device or null
 * output, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_light_probe_baker_create(
    CNA_Handle graphics_device, CNA_LightProbeBakerHandle* out_baker);

/**
 * @brief Creates a probe baker at a chosen face size.
 *
 * @param graphics_device The device to capture with.
 * @param face_size The cube-face resolution; must be positive.
 * @param out_baker Receives the baker; invalid on failure.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for a face size below one, an
 * invalid device or a null output, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an
 * error.
 */
CNA_C_API CNA_Result cna_light_probe_baker_create_with_face_size(
    CNA_Handle graphics_device, int32_t face_size, CNA_LightProbeBakerHandle* out_baker);

/**
 * @brief Releases a probe baker.
 *
 * @param baker The baker.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for an invalid handle,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_light_probe_baker_destroy(CNA_LightProbeBakerHandle baker);

/**
 * @brief Reports whether this renderer can actually capture probes.
 *
 * @param baker The baker.
 * @param out_supported Receives the answer measured at construction.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_light_probe_baker_is_supported(
    CNA_LightProbeBakerHandle baker, CNA_Bool* out_supported);

/**
 * @brief Returns the cube-face resolution this baker captures at.
 *
 * @param baker The baker.
 * @param out_face_size Receives the size.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_light_probe_baker_get_face_size(
    CNA_LightProbeBakerHandle baker, int32_t* out_face_size);

/**
 * @brief Returns how many faces one capture renders.
 *
 * Constant, and the same for every baker; @ref CNA_LIGHT_PROBE_BAKER_FACE_COUNT is the same number
 * without a call.
 *
 * @param out_face_count Receives the count.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for a null output,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_light_probe_baker_face_count(int32_t* out_face_count);

/**
 * @brief Returns the near capture distance.
 *
 * @param baker The baker.
 * @param out_near Receives the distance.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_light_probe_baker_get_near_plane(
    CNA_LightProbeBakerHandle baker, float* out_near);

/**
 * @brief Returns the far capture distance.
 *
 * @param baker The baker.
 * @param out_far Receives the distance.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_light_probe_baker_get_far_plane(
    CNA_LightProbeBakerHandle baker, float* out_far);

/**
 * @brief Sets both capture distances at once.
 *
 * **Validated as a pair and refused as a pair.** A near distance that is not positive, or a far
 * distance that does not exceed it, leaves *both* unchanged -- there is no half-applied state, and
 * no clamping: a silently corrected capture range would give probes that look plausible and are
 * lit from the wrong depth.
 *
 * @param baker The baker.
 * @param near_plane The near distance; must be positive.
 * @param far_plane The far distance; must exceed the near one.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` when the pair is not ordered,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_light_probe_baker_set_planes(
    CNA_LightProbeBakerHandle baker, float near_plane, float far_plane);

/**
 * @brief Returns the view matrix one cube face is captured with.
 *
 * @param baker The baker.
 * @param face The face index, from zero to @ref CNA_LIGHT_PROBE_BAKER_FACE_COUNT minus one.
 * @param position The capture position.
 * @param out_view Receives the matrix.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for a face outside the six or a null
 * argument, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_light_probe_baker_face_view(
    CNA_LightProbeBakerHandle baker,
    int32_t face,
    const CNA_Vector3* position,
    CNA_Matrix* out_view);

/**
 * @brief Captures one probe by drawing the scene six times.
 *
 * @param baker The baker.
 * @param position Where to capture from.
 * @param draw The per-face scene callback; must not be null.
 * @param context Passed to the callback unchanged.
 * @param out_probe Receives a new owned probe; invalid on failure.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for a null callback, position or
 * output, `CNA_RESULT_INVALID_STATE` when this renderer cannot capture at all,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_light_probe_baker_bake_probe(
    CNA_LightProbeBakerHandle baker,
    const CNA_Vector3* position,
    CNA_LightProbeSceneDrawCallback draw,
    void* context,
    CNA_LightProbeHandle* out_probe);

/**
 * @brief Captures every probe of a volume.
 *
 * Whatever visibility each probe already carried is **kept**: light and visibility are two separate
 * bakes and either may be run without the other.
 *
 * @param baker The baker.
 * @param volume The volume to fill.
 * @param draw The per-face scene callback; must not be null.
 * @param context Passed to the callback unchanged.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_HANDLE` for a volume that is not one,
 * `CNA_RESULT_INVALID_ARGUMENT` for a null callback, `CNA_RESULT_INVALID_STATE` when this renderer
 * cannot capture at all, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_light_probe_baker_bake_light(
    CNA_LightProbeBakerHandle baker,
    CNA_LightProbeVolumeHandle volume,
    CNA_LightProbeSceneDrawCallback draw,
    void* context);

/**
 * @brief Captures the visibility distances of every probe of a volume.
 *
 * @param baker The baker.
 * @param volume The volume to fill.
 * @param draw The per-face scene callback; must not be null.
 * @param context Passed to the callback unchanged.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_HANDLE` for a volume that is not one,
 * `CNA_RESULT_INVALID_ARGUMENT` for a null callback, `CNA_RESULT_INVALID_STATE` when this renderer
 * cannot capture at all, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_light_probe_baker_bake_visibility(
    CNA_LightProbeBakerHandle baker,
    CNA_LightProbeVolumeHandle volume,
    CNA_LightProbeSceneDrawCallback draw,
    void* context);

/**
 * @brief Owned handle for the environment processor.
 *
 * A pure transformer: it has **no setters at all**, only operations that take an environment and
 * hand back a new texture. Every one of its nine *argument* refusals is therefore about a single
 * call rather than about accumulated state, which is why none of them is a state error.
 *
 * **Its generators are renderer-dependent, and unlike @ref CNA_LightProbeBakerHandle it publishes
 * no support flag to ask first.** They build a real `TextureCube` and write its faces, which a
 * renderer without cube storage refuses -- the headless renderer creates the cube and then refuses
 * the upload. That refusal reaches a caller as `CNA_RESULT_NOT_SUPPORTED`, which is the canonical
 * answer and is deliberately not rewritten here.
 *
 * That means `CNA_RESULT_NOT_SUPPORTED` from a generator has **two** possible causes: the library
 * was built without the engine layer, or this renderer cannot store a cube map. Tell them apart
 * with @ref cna_engine_layer_get_version -- a non-zero version means the layer is present and the
 * renderer is the reason.
 *
 * The split is by **output type**, not by "generator": the three routes that build a `TextureCube`
 * are the ones a cube-less renderer refuses, while @ref cna_environment_processor_generate_brdf_lut
 * builds a 2D table and works anyway. The five static maths routes touch no device at all and have
 * only the first cause.
 *
 * The three generators produce the three products of the split sum, and they must be generated
 * **together** -- pairing a prefiltered cube with a mip count from a different one is the failure
 * @ref CNA_ImageBasedLightEXT exists to prevent.
 */
typedef CNA_Handle CNA_EnvironmentProcessorHandle;

/**
 * @brief Creates an environment processor.
 *
 * @param graphics_device The device its outputs are created on.
 * @param out_processor Receives the processor; invalid on failure.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for an invalid device or null
 * output, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_environment_processor_create(
    CNA_Handle graphics_device, CNA_EnvironmentProcessorHandle* out_processor);

/**
 * @brief Releases an environment processor.
 *
 * The textures it produced are **not** released with it: each is an owned handle of its own and
 * outlives the processor that made it.
 *
 * @param processor The processor.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for an invalid handle,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_environment_processor_destroy(CNA_EnvironmentProcessorHandle processor);

/**
 * @brief Converts an equirectangular panorama into a cube map.
 *
 * @param processor The processor.
 * @param panorama The panorama; must be a valid texture with pixels.
 * @param face_size The cube-face resolution; must be positive.
 * @param out_environment Receives a new owned cube map; invalid on failure.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_HANDLE` for a panorama that is not a texture,
 * `CNA_RESULT_INVALID_ARGUMENT` for a panorama with no pixels, a face size below one or a null
 * output, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_environment_processor_convert_equirectangular(
    CNA_EnvironmentProcessorHandle processor,
    CNA_Handle panorama,
    int32_t face_size,
    CNA_Handle* out_environment);

/**
 * @brief Generates the cosine-convolved diffuse irradiance cube.
 *
 * @param processor The processor.
 * @param environment The source environment.
 * @param size The cube-face resolution; must be positive.
 * @param sample_count Samples per texel; must be positive.
 * @param out_irradiance Receives a new owned cube map; invalid on failure.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_HANDLE` for an environment that is not a cube
 * map, `CNA_RESULT_INVALID_ARGUMENT` for a size or sample count below one, or a null output,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_environment_processor_generate_irradiance(
    CNA_EnvironmentProcessorHandle processor,
    CNA_Handle environment,
    int32_t size,
    int32_t sample_count,
    CNA_Handle* out_irradiance);

/**
 * @brief Generates the GGX-prefiltered specular cube whose mips are a roughness ramp.
 *
 * @param processor The processor.
 * @param environment The source environment.
 * @param base_size The resolution of mip zero; must be positive.
 * @param mip_count How many mips to generate; must be positive, and is the number
 * @ref CNA_ImageBasedLightEXT must be given.
 * @param sample_count Samples per texel; must be positive.
 * @param out_specular Receives a new owned cube map; invalid on failure.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_HANDLE` for an environment that is not a cube
 * map, `CNA_RESULT_INVALID_ARGUMENT` for any of the three counts below one, or a null output,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_environment_processor_generate_prefiltered_specular(
    CNA_EnvironmentProcessorHandle processor,
    CNA_Handle environment,
    int32_t base_size,
    int32_t mip_count,
    int32_t sample_count,
    CNA_Handle* out_specular);

/**
 * @brief Projects an environment into one light probe.
 *
 * @param processor The processor.
 * @param environment The source environment.
 * @param position The position to record on the probe.
 * @param out_probe Receives a new owned probe; invalid on failure.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_HANDLE` for an environment that is not a cube
 * map, `CNA_RESULT_INVALID_ARGUMENT` for a null argument, `CNA_RESULT_NOT_SUPPORTED` without the
 * engine layer, or an error.
 */
CNA_C_API CNA_Result cna_environment_processor_generate_probe(
    CNA_EnvironmentProcessorHandle processor,
    CNA_Handle environment,
    const CNA_Vector3* position,
    CNA_LightProbeHandle* out_probe);

/**
 * @brief Generates the BRDF table indexed by (N·V across, roughness down).
 *
 * Depends on neither an environment nor a scene, so it can be generated once and shared by every
 * bundle.
 *
 * @param processor The processor.
 * @param size The table resolution; must be positive.
 * @param sample_count Samples per texel; must be positive.
 * @param out_lut Receives a new owned 2D texture; invalid on failure.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for a size or sample count below
 * one, or a null output, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_environment_processor_generate_brdf_lut(
    CNA_EnvironmentProcessorHandle processor,
    int32_t size,
    int32_t sample_count,
    CNA_Handle* out_lut);

/**
 * @brief Maps a roughness to the mip that carries it.
 *
 * **Answers rather than refuses.** A mip count of one or less has no ramp to index, so the answer
 * is mip zero; a roughness outside zero to one is clamped into it. This is the inverse of
 * @ref cna_environment_processor_roughness_for_mip.
 *
 * @param roughness The roughness.
 * @param mip_count The chain length the specular cube was generated with.
 * @param out_mip Receives the mip, as a fractional level.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for a null output,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_environment_processor_mip_for_roughness(
    float roughness, int32_t mip_count, float* out_mip);

/**
 * @brief Maps a mip back to the roughness it carries.
 *
 * Answers rather than refuses, on the same terms as its inverse.
 *
 * @param mip The mip level.
 * @param mip_count The chain length the specular cube was generated with.
 * @param out_roughness Receives the roughness, clamped to zero to one.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for a null output,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_environment_processor_roughness_for_mip(
    float mip, int32_t mip_count, float* out_roughness);

/**
 * @brief Returns one point of the Hammersley low-discrepancy sequence.
 *
 * @param index The point index.
 * @param count The sequence length.
 * @param out_x Receives the first coordinate.
 * @param out_y Receives the second coordinate.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for a null output,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_environment_processor_hammersley(
    int32_t index, int32_t count, float* out_x, float* out_y);

/**
 * @brief Turns one sequence point into a GGX half-vector around a normal.
 *
 * @param x The first sequence coordinate.
 * @param y The second sequence coordinate.
 * @param normal The surface normal to build the basis around.
 * @param roughness The roughness the lobe is shaped by.
 * @param out_direction Receives the sampled direction.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for a null argument,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_environment_processor_importance_sample_ggx(
    float x, float y, const CNA_Vector3* normal, float roughness, CNA_Vector3* out_direction);

/**
 * @brief Returns the direction one texel of one cube face looks along.
 *
 * @param face The cube face index.
 * @param u The horizontal texel coordinate.
 * @param v The vertical texel coordinate.
 * @param out_direction Receives the direction.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for a null output,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_environment_processor_face_direction(
    int32_t face, float u, float v, CNA_Vector3* out_direction);

/**
 * @brief Maps a direction to a panorama coordinate.
 *
 * @param direction The direction.
 * @param out_u Receives the horizontal coordinate.
 * @param out_v Receives the vertical coordinate.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for a null argument,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_environment_processor_direction_to_equirectangular(
    const CNA_Vector3* direction, float* out_u, float* out_v);

/**
 * @brief Owned handle for a skybox.
 *
 * Like the baker, **what it can do is probed rather than asked**: the canonical skybox tries to
 * compile its shader at construction and remembers the result.
 *
 * A skybox that cannot draw -- because the renderer refused the shader, or because no environment
 * is attached -- **skips silently** rather than failing. That is deliberate: a missing sky is a
 * scene without a sky, not a broken frame, and @ref cna_skybox_draw therefore succeeds in both
 * cases. Ask @ref cna_skybox_is_supported and @ref cna_skybox_get_environment to find out whether
 * anything was actually drawn.
 */
typedef CNA_Handle CNA_SkyboxHandle;

/**
 * @brief Creates a skybox over a borrowed environment.
 *
 * @param graphics_device The device to draw with.
 * @param environment The cube map to draw, or `CNA_INVALID_HANDLE` for none yet; **borrowed**.
 * @param out_skybox Receives the skybox; invalid on failure.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_HANDLE` for an environment handle that is
 * neither a cube map nor `CNA_INVALID_HANDLE`, `CNA_RESULT_INVALID_ARGUMENT` for an invalid device
 * or a null output, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_skybox_create(
    CNA_Handle graphics_device, CNA_Handle environment, CNA_SkyboxHandle* out_skybox);

/**
 * @brief Releases a skybox.
 *
 * A borrowed environment is untouched; an environment handed over with
 * @ref cna_skybox_set_owned_environment is released with the skybox.
 *
 * @param skybox The skybox.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for an invalid handle,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_skybox_destroy(CNA_SkyboxHandle skybox);

/**
 * @brief Reports whether this renderer could compile the sky shader.
 *
 * @param skybox The skybox.
 * @param out_supported Receives the answer measured at construction.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_skybox_is_supported(CNA_SkyboxHandle skybox, CNA_Bool* out_supported);

/**
 * @brief Draws the sky over whatever target is currently bound.
 *
 * Over the *current* target deliberately: the scene target inside a pipeline frame, the back buffer
 * outside one. Binding a destination here would mean the caller had to know which of the two it
 * currently was.
 *
 * @param skybox The skybox.
 * @param view The view matrix; only its rotation is used.
 * @param projection The projection matrix.
 * @param width The target width in pixels; must be positive.
 * @param height The target height in pixels; must be positive.
 * @return `CNA_RESULT_SUCCESS` -- including when the sky was skipped for want of support or an
 * environment -- `CNA_RESULT_INVALID_ARGUMENT` for a size below one or a null matrix,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_skybox_draw(
    CNA_SkyboxHandle skybox,
    const CNA_Matrix* view,
    const CNA_Matrix* projection,
    int32_t width,
    int32_t height);

/**
 * @brief Returns the attached environment.
 *
 * The handle **borrows**: it keeps the skybox alive while it exists, and releasing it releases only
 * the handle, never the cube map.
 *
 * @param skybox The skybox.
 * @param out_environment Receives the cube map, or `CNA_INVALID_HANDLE` when none is attached.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_skybox_get_environment(
    CNA_SkyboxHandle skybox, CNA_Handle* out_environment);

/**
 * @brief Attaches a borrowed environment.
 *
 * **Releases an owned environment first**, if one was handed over earlier: attaching a borrowed
 * cube over an owned one would otherwise keep the owned one alive with nothing referring to it.
 *
 * @param skybox The skybox.
 * @param environment The cube map, or `CNA_INVALID_HANDLE` to detach.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_HANDLE` for a handle that is neither valid nor
 * `CNA_INVALID_HANDLE`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_skybox_set_environment(CNA_SkyboxHandle skybox, CNA_Handle environment);

/**
 * @brief Attaches an environment and takes ownership of it.
 *
 * **The texture handle is consumed.** On success it is released from the runtime and must not be
 * used again -- the skybox owns the cube map now, and a second release would be a double free.
 * This follows @ref cna_post_process_chain_add_owned_pass, the campaign's settled shape for an
 * operation whose canonical form takes a `unique_ptr`, and is named for the transfer rather than
 * hiding it behind a flag on @ref cna_skybox_set_environment.
 *
 * @param skybox The skybox.
 * @param environment The cube map to hand over; invalid on return whether or not the call
 * succeeded.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_HANDLE` for a handle that is not a cube map --
 * including `CNA_INVALID_HANDLE`, since there is nothing to hand over --
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_skybox_set_owned_environment(
    CNA_SkyboxHandle skybox, CNA_Handle environment);

/**
 * @brief Returns the horizontal rotation applied to the sky.
 *
 * @param skybox The skybox.
 * @param out_radians Receives the angle.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_skybox_get_yaw(CNA_SkyboxHandle skybox, float* out_radians);

/**
 * @brief Rotates the sky horizontally.
 *
 * Assigned as given: any angle is meaningful, so there is nothing to clamp or refuse.
 *
 * @param skybox The skybox.
 * @param radians The angle.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_skybox_set_yaw(CNA_SkyboxHandle skybox, float radians);

/**
 * @brief Returns the brightness multiplier.
 *
 * @param skybox The skybox.
 * @param out_intensity Receives the multiplier.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_skybox_get_intensity(CNA_SkyboxHandle skybox, float* out_intensity);

/**
 * @brief Sets the brightness multiplier.
 *
 * **Floored at zero**, so a negative value reads back as zero. Note that this is *not* what
 * @ref cna_atmospheric_sky_set_intensity does with a negative value, despite the matching name:
 * that one keeps the previous intensity instead. The two canonical setters differ, and this
 * binding preserves the difference rather than making them agree.
 *
 * @param skybox The skybox.
 * @param intensity The multiplier.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_skybox_set_intensity(CNA_SkyboxHandle skybox, float intensity);

/**
 * @brief Returns the colour the sky is tinted by.
 *
 * @param skybox The skybox.
 * @param out_tint Receives the tint.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_skybox_get_tint(CNA_SkyboxHandle skybox, CNA_Vector3* out_tint);

/**
 * @brief Tints the sky.
 *
 * Assigned as given, with no clamp: a tint above one brightens, which is meaningful for an HDR sky.
 *
 * @param skybox The skybox.
 * @param tint The tint.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for a null tint,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_skybox_set_tint(CNA_SkyboxHandle skybox, const CNA_Vector3* tint);

/**
 * @brief Returns the world direction one screen point looks along, through a rotated sky.
 *
 * A pure function needing no skybox: it is how a caller reproduces the sky's own lookup, for
 * picking or for a CPU-side check of what the shader would have sampled. A degenerate ray answers
 * straight ahead rather than refusing.
 *
 * @param view The view matrix; only its rotation is used.
 * @param projection The projection matrix.
 * @param ndc_x The horizontal device coordinate, from minus one to one.
 * @param ndc_y The vertical device coordinate, from minus one to one.
 * @param yaw The sky rotation to apply.
 * @param out_direction Receives the unit direction.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for a null argument,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_skybox_compute_view_ray(
    const CNA_Matrix* view,
    const CNA_Matrix* projection,
    float ndc_x,
    float ndc_y,
    float yaw,
    CNA_Vector3* out_direction);

/**
 * @brief Owned handle for the analytic atmospheric sky.
 *
 * The alternative to @ref CNA_SkyboxHandle when there is no captured environment: the sky is
 * computed from a sun direction and a turbidity rather than sampled from a cube map. Support is
 * probed at construction here too, and an unsupported sky skips silently for the same reason.
 *
 * **Its three setters behave three different ways**, and the binding preserves each rather than
 * regularizing them: the turbidity is clamped, the intensity is a guarded assignment that keeps
 * the previous value, and the sun direction is a guarded assignment that also normalizes.
 */
typedef CNA_Handle CNA_AtmosphericSkyHandle;

/**
 * @brief Creates an atmospheric sky.
 *
 * @param graphics_device The device to draw with.
 * @param out_sky Receives the sky; invalid on failure.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for an invalid device or null
 * output, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_atmospheric_sky_create(
    CNA_Handle graphics_device, CNA_AtmosphericSkyHandle* out_sky);

/**
 * @brief Releases an atmospheric sky.
 *
 * @param sky The sky.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for an invalid handle,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_atmospheric_sky_destroy(CNA_AtmosphericSkyHandle sky);

/**
 * @brief Reports whether this renderer could compile the sky shader.
 *
 * @param sky The sky.
 * @param out_supported Receives the answer measured at construction.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_atmospheric_sky_is_supported(
    CNA_AtmosphericSkyHandle sky, CNA_Bool* out_supported);

/**
 * @brief Draws the sky over whatever target is currently bound.
 *
 * @param sky The sky.
 * @param view The view matrix; only its rotation is used.
 * @param projection The projection matrix.
 * @param width The target width in pixels; must be positive.
 * @param height The target height in pixels; must be positive.
 * @return `CNA_RESULT_SUCCESS` -- including when the sky was skipped for want of support --
 * `CNA_RESULT_INVALID_ARGUMENT` for a size below one or a null matrix,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_atmospheric_sky_draw(
    CNA_AtmosphericSkyHandle sky,
    const CNA_Matrix* view,
    const CNA_Matrix* projection,
    int32_t width,
    int32_t height);

/**
 * @brief Returns the direction the sun is in.
 *
 * Always a unit vector, whatever was set.
 *
 * @param sky The sky.
 * @param out_direction Receives the direction.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_atmospheric_sky_get_sun_direction(
    CNA_AtmosphericSkyHandle sky, CNA_Vector3* out_direction);

/**
 * @brief Points the sun.
 *
 * **Normalized on the way in**, so what reads back is a unit vector rather than what was written.
 * A vector too short to have a direction is a **silent no-op**: the previous sun direction stays.
 * That is not an error the canonical setter reports, and it is not one here either -- but it does
 * mean a caller cannot assume a successful call changed anything, which is why the getter exists.
 *
 * @param sky The sky.
 * @param direction The direction; need not be normalized.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for a null direction,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_atmospheric_sky_set_sun_direction(
    CNA_AtmosphericSkyHandle sky, const CNA_Vector3* direction);

/**
 * @brief Returns the atmospheric turbidity.
 *
 * @param sky The sky.
 * @param out_turbidity Receives the turbidity.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_atmospheric_sky_get_turbidity(
    CNA_AtmosphericSkyHandle sky, float* out_turbidity);

/**
 * @brief Sets the atmospheric turbidity.
 *
 * **Clamped to one through ten**, the range the model is defined over. Note that
 * @ref cna_atmospheric_sky_radiance does *not* clamp its own turbidity argument: the setter guards
 * a sky that will be drawn many times, the free function evaluates whatever it is handed.
 *
 * @param sky The sky.
 * @param turbidity The turbidity.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_atmospheric_sky_set_turbidity(
    CNA_AtmosphericSkyHandle sky, float turbidity);

/**
 * @brief Returns the brightness multiplier.
 *
 * @param sky The sky.
 * @param out_intensity Receives the multiplier.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_atmospheric_sky_get_intensity(
    CNA_AtmosphericSkyHandle sky, float* out_intensity);

/**
 * @brief Sets the brightness multiplier.
 *
 * A negative value is a **silent no-op** that keeps the previous intensity -- it is not clamped to
 * zero, which is what the identically named @ref cna_skybox_set_intensity does. The two canonical
 * setters genuinely differ and the binding preserves the difference.
 *
 * @param sky The sky.
 * @param intensity The multiplier.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_atmospheric_sky_set_intensity(
    CNA_AtmosphericSkyHandle sky, float intensity);

/**
 * @brief Copies the GLSL source of the sky model into a caller buffer.
 *
 * @param destination The buffer, or null to ask for the size.
 * @param capacity The buffer size in bytes.
 * @param out_bytes Receives the byte count, including the terminator.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL` with the needed size in
 * `out_bytes`, `CNA_RESULT_INVALID_ARGUMENT` for a null count,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_atmospheric_sky_copy_model_glsl(
    char* destination, uint64_t capacity, uint64_t* out_bytes);

/**
 * @brief Evaluates the sky model for one view direction, on the CPU.
 *
 * The same model the shader runs, available without a device -- for a CPU-side ambient term, or to
 * check what the sky will look like before drawing it. Degenerate directions fall back to straight
 * up rather than refusing, and **the turbidity is used as given**: unlike
 * @ref cna_atmospheric_sky_set_turbidity this does not clamp it into the model's range.
 *
 * @param view_direction The direction being looked along.
 * @param sun_direction The direction the sun is in.
 * @param turbidity The turbidity; not clamped.
 * @param out_radiance Receives the radiance.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for a null argument,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_atmospheric_sky_radiance(
    const CNA_Vector3* view_direction,
    const CNA_Vector3* sun_direction,
    float turbidity,
    CNA_Vector3* out_radiance);

/**
 * @brief Returns the skybox the pipeline draws, if any.
 *
 * The handle **borrows**: it keeps the pipeline alive while it exists and releases only itself.
 *
 * @param pipeline The pipeline.
 * @param out_skybox Receives the skybox, or `CNA_INVALID_HANDLE` when none is set.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_render_pipeline_get_skybox(
    CNA_RenderPipelineHandle pipeline, CNA_SkyboxHandle* out_skybox);

/**
 * @brief Gives the pipeline a skybox to draw.
 *
 * **Borrowed, not owned**: the caller keeps the skybox alive for as long as the pipeline draws it,
 * exactly as the canonical pointer requires.
 *
 * @param pipeline The pipeline.
 * @param skybox The skybox, or `CNA_INVALID_HANDLE` to draw none.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_HANDLE` for a handle that is neither valid nor
 * `CNA_INVALID_HANDLE`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_render_pipeline_set_skybox(
    CNA_RenderPipelineHandle pipeline, CNA_SkyboxHandle skybox);

/** @brief Fixed-width identity for the shape an area light emits from. */
typedef uint32_t CNA_AreaLightShapeEXT;
/** @brief A rectangle, described by its centre and two half-axes. */
#define CNA_AREA_LIGHT_SHAPE_RECTANGLE_EXT UINT32_C(0)
/** @brief A disc in the plane of the same two half-axes; approximated as a polygon. */
#define CNA_AREA_LIGHT_SHAPE_DISC_EXT UINT32_C(1)
/** @brief A capsule-like tube along the first half-axis, with the second as its radius. */
#define CNA_AREA_LIGHT_SHAPE_TUBE_EXT UINT32_C(2)

/**
 * @brief A light with a shape, as a lit effect receives one.
 *
 * The third kind of light, after XNA's directional one and CNA's `CNA_PunctualLightEXT`: a light
 * that is a *surface*, which is what almost every real light is. The difference is not brightness
 * but the shape of what the light does -- a window is a bright rectangle in a polished floor, and
 * that is not something a point light can be tuned into producing.
 *
 * The shape is a **centre and two half-axes** rather than four corners, so all three shapes share
 * one description and none can be given a non-planar or self-intersecting outline. `right_axis`
 * and `up_axis` are half-extents: their lengths are half the rectangle's width and height, and the
 * emitting side is the one they cross towards.
 *
 * **There are no area-light shadows.** A soft-edged shadow needs many samples of the light's
 * surface or a ray query, and this layer has neither, so an area light lights what faces it whether
 * or not anything stands in the way. Stated here because the failure looks like a shadow bug.
 *
 * Like @ref CNA_ImageBasedLightEXT, the canonical type lives in an always-compiled XNA header, so
 * @ref cna_area_light_ext_init and @ref cna_area_light_ext_is_valid work in **every** build.
 */
typedef struct CNA_AreaLightEXT {
    /** @brief Size of this caller-provided structure in bytes. */
    uint32_t struct_size;
    /** @brief Version of this caller-provided structure. */
    uint32_t struct_version;
    /** @brief Which shape the light emits from. */
    CNA_AreaLightShapeEXT shape;
    /** @brief Whether the light emits from both faces of its surface. */
    CNA_Bool two_sided;
    /** @brief Reserved bytes; must be zero. */
    uint8_t reserved0[3];
    /** @brief World-space centre of the emitting surface. */
    CNA_Vector3 position;
    /** @brief Half-axis across the surface; its length is half the width. */
    CNA_Vector3 right_axis;
    /** @brief Half-axis up the surface; half the height, or the tube's radius. */
    CNA_Vector3 up_axis;
    /** @brief Emitted colour, linear and unbounded -- values above one are meaningful in HDR. */
    CNA_Vector3 color;
    /** @brief Multiplier applied to @ref color. */
    float intensity;
    /** @brief Distance past which the light contributes nothing, measured from @ref position. */
    float range;
} CNA_AreaLightEXT;

/**
 * @brief Fills an area light with the canonical defaults.
 *
 * @param out_light Receives a rectangle at the origin, half a unit across each way, white, at
 * intensity one and range twenty.
 * @return `CNA_RESULT_SUCCESS` in every build, or `CNA_RESULT_INVALID_ARGUMENT` for a null output.
 */
CNA_C_API CNA_Result cna_area_light_ext_init(CNA_AreaLightEXT* out_light);

/**
 * @brief Reports whether the light describes a surface that can actually emit.
 *
 * **Seven arms, and one of them depends on the shape.** Every vector must be finite; the intensity
 * finite and not negative; the range finite and above zero; both axes long enough to have a
 * direction. Then: a **tube** is a line with a radius, so any two non-zero axes will do, while a
 * rectangle and a disc are *surfaces* and additionally need axes that are not parallel -- parallel
 * axes enclose no area, which the form factor answers with a division by zero rather than with
 * darkness. Applying the parallel-axis test to a tube would reject a perfectly good light.
 *
 * @param light The light.
 * @param out_valid Receives the answer.
 * @return `CNA_RESULT_SUCCESS` in every build, `CNA_RESULT_INVALID_ARGUMENT` for a null or
 * malformed structure.
 */
CNA_C_API CNA_Result cna_area_light_ext_is_valid(
    const CNA_AreaLightEXT* light, CNA_Bool* out_valid);

/** @brief The default edge length of a generated area-light BRDF table. */
#define CNA_AREA_LIGHT_BRDF_TABLE_DEFAULT_SIZE 32

/** @brief The default number of samples each of its entries integrates. */
#define CNA_AREA_LIGHT_BRDF_TABLE_DEFAULT_SAMPLE_COUNT 64

/**
 * @brief One entry of the area-light BRDF table.
 *
 * The four numbers a shader needs to turn a lobe into a coverage weight: how much energy the lobe
 * carries, how much of it is Fresnel, and the average direction it points, split into its tangent
 * and normal components.
 */
typedef struct CNA_AreaLightBrdfTerms {
    /** @brief Size of this caller-provided structure in bytes. */
    uint32_t struct_size;
    /** @brief Version of this caller-provided structure. */
    uint32_t struct_version;
    /** @brief Total energy of the lobe. */
    float magnitude;
    /** @brief The Fresnel-weighted share of it. */
    float fresnel;
    /** @brief Tangent component of the average direction. */
    float average_tangent;
    /** @brief Normal component of the average direction. */
    float average_normal;
} CNA_AreaLightBrdfTerms;

/**
 * @brief Owned handle for a generated area-light BRDF table.
 *
 * Generated once and shared: it depends on neither a scene nor an environment, only on the size
 * and sample count it was built with. The generation is measurable rather than merely slow --
 * @ref cna_area_light_brdf_table_get_generation_milliseconds reports what it actually cost.
 */
typedef CNA_Handle CNA_AreaLightBrdfTableHandle;

/**
 * @brief Creates a BRDF table at the default size and sample count.
 *
 * @param graphics_device The device the table's texture is created on.
 * @param out_table Receives the table; invalid on failure.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for an invalid device or null
 * output, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_area_light_brdf_table_create(
    CNA_Handle graphics_device, CNA_AreaLightBrdfTableHandle* out_table);

/**
 * @brief Creates a BRDF table at a chosen size and sample count.
 *
 * Both are validated **as a pair and refused as a pair**, matching the canonical constructor: a
 * table half-built at the wrong size is worse than no table.
 *
 * @param graphics_device The device the table's texture is created on.
 * @param size The edge length; must be positive.
 * @param sample_count Samples per entry; must be positive.
 * @param out_table Receives the table; invalid on failure.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` when either is below one, for an
 * invalid device or a null output, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an
 * error.
 */
CNA_C_API CNA_Result cna_area_light_brdf_table_create_with_size(
    CNA_Handle graphics_device,
    int32_t size,
    int32_t sample_count,
    CNA_AreaLightBrdfTableHandle* out_table);

/**
 * @brief Releases a BRDF table.
 *
 * @param table The table.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_HANDLE` for an invalid handle,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_area_light_brdf_table_destroy(CNA_AreaLightBrdfTableHandle table);

/**
 * @brief Returns the table's texture.
 *
 * The handle **borrows**: it keeps the table alive while it exists, and releasing it releases only
 * the handle, never the texture.
 *
 * @param table The table.
 * @param out_texture Receives the texture, or `CNA_INVALID_HANDLE` when the renderer could not
 * store one.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_area_light_brdf_table_get_texture(
    CNA_AreaLightBrdfTableHandle table, CNA_Handle* out_texture);

/**
 * @brief Returns the table's edge length.
 *
 * @param table The table.
 * @param out_size Receives the size.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_area_light_brdf_table_get_size(
    CNA_AreaLightBrdfTableHandle table, int32_t* out_size);

/**
 * @brief Returns how many samples each entry integrated.
 *
 * @param table The table.
 * @param out_sample_count Receives the count.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_area_light_brdf_table_get_sample_count(
    CNA_AreaLightBrdfTableHandle table, int32_t* out_sample_count);

/**
 * @brief Returns how long generating the table actually took.
 *
 * @param table The table.
 * @param out_milliseconds Receives the measurement.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_area_light_brdf_table_get_generation_milliseconds(
    CNA_AreaLightBrdfTableHandle table, double* out_milliseconds);

/**
 * @brief Evaluates one entry of the table without building one.
 *
 * The same integration a generated table stores, for a single (roughness, cos theta) pair -- which
 * is how a caller checks a shader's lookup against the CPU.
 *
 * **Both inputs are clamped, and not to the range an eye expects**: the roughness is clamped to
 * `0.02` through `1`, not zero through one, because a perfect mirror has a lobe of zero width and
 * no samples land in it; the cosine is clamped to `0.001` through `1` for the same reason at
 * grazing angles. The sample count is the one argument that is **refused** rather than clamped.
 *
 * @param roughness The surface roughness; clamped to `[0.02, 1]`.
 * @param cos_theta The cosine of the view angle; clamped to `[0.001, 1]`.
 * @param sample_count Samples to integrate; must be positive.
 * @param out_terms Receives the four terms.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for a sample count below one or a
 * null or malformed output, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_area_light_brdf_table_evaluate(
    float roughness, float cos_theta, int32_t sample_count, CNA_AreaLightBrdfTerms* out_terms);

/**
 * @brief Copies the GLSL that looks the table up into a caller buffer.
 *
 * @param destination The buffer, or null to ask for the size.
 * @param capacity The buffer size in bytes.
 * @param out_bytes Receives the byte count, including the terminator.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL` with the needed size in `out_bytes`,
 * `CNA_RESULT_INVALID_ARGUMENT` for a null count, `CNA_RESULT_NOT_SUPPORTED` without the engine
 * layer, or an error.
 */
CNA_C_API CNA_Result cna_area_light_brdf_table_copy_lookup_glsl(
    char* destination, uint64_t capacity, uint64_t* out_bytes);

/** @brief How many corners an area light's quad has. */
#define CNA_AREA_LIGHT_QUAD_CORNER_COUNT 4

/**
 * @brief Returns the four corners of the quad a light is integrated as.
 *
 * **Shape-dependent, and that is the point**: a rectangle uses its axes as they are, a disc scales
 * them so a polygon matches the disc's area, and a tube is *billboarded* -- turned so its face
 * points at the surface, because a cylinder looks like a rectangle from wherever it is seen. One
 * quad therefore serves all three shapes instead of a second integrator for tubes.
 *
 * @param light The light.
 * @param surface The world-space point being lit; only a tube's quad depends on it.
 * @param out_quad Receives @ref CNA_AREA_LIGHT_QUAD_CORNER_COUNT corners, counter-clockwise from
 * the lower left.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for a null or malformed argument,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_area_light_shading_quad_of(
    const CNA_AreaLightEXT* light, const CNA_Vector3* surface, CNA_Vector3* out_quad);

/**
 * @brief Returns how much of a shading lobe the quad covers.
 *
 * @param quad @ref CNA_AREA_LIGHT_QUAD_CORNER_COUNT corners, as @ref cna_area_light_shading_quad_of
 * produces them.
 * @param surface The world-space point being lit.
 * @param lobe_axis The direction the lobe points.
 * @param lobe_scale The lobe's width, from @ref cna_area_light_shading_lobe_scale_for.
 * @param two_sided Whether the light emits from both faces.
 * @param out_coverage Receives the coverage; zero when the quad is behind the surface.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for a null argument,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_area_light_shading_coverage(
    const CNA_Vector3* quad,
    const CNA_Vector3* surface,
    const CNA_Vector3* lobe_axis,
    float lobe_scale,
    CNA_Bool two_sided,
    float* out_coverage);

/**
 * @brief Returns one area light's whole contribution to one surface point.
 *
 * @param light The light.
 * @param surface The world-space point being lit.
 * @param normal The surface normal.
 * @param camera_position Where the surface is being viewed from.
 * @param base_color The surface's base colour.
 * @param metallic How metallic the surface is.
 * @param roughness How rough the surface is.
 * @param out_contribution Receives the contribution, unbounded above, and zero when the light
 * cannot reach the point.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for a null or malformed argument,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_area_light_shading_contribution(
    const CNA_AreaLightEXT* light,
    const CNA_Vector3* surface,
    const CNA_Vector3* normal,
    const CNA_Vector3* camera_position,
    const CNA_Vector3* base_color,
    float metallic,
    float roughness,
    CNA_Vector3* out_contribution);

/**
 * @brief Returns the lobe width a roughness implies.
 *
 * Clamps the roughness to zero through one, squares it, and then **floors the result at `0.02`**,
 * so a mirror still has a lobe with a width rather than a line -- a zero-width lobe covers nothing
 * and a polished surface would show no area light at all.
 *
 * @param roughness The surface roughness.
 * @param out_scale Receives the lobe width.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for a null output,
 * `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_area_light_shading_lobe_scale_for(float roughness, float* out_scale);

/**
 * @brief Copies the GLSL of the shading model into a caller buffer.
 *
 * The same arithmetic the routes above run, so a shader and a test can be checked against each
 * other rather than against two separate implementations.
 *
 * @param destination The buffer, or null to ask for the size.
 * @param capacity The buffer size in bytes.
 * @param out_bytes Receives the byte count, including the terminator.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL` with the needed size in `out_bytes`,
 * `CNA_RESULT_INVALID_ARGUMENT` for a null count, `CNA_RESULT_NOT_SUPPORTED` without the engine
 * layer, or an error.
 */
CNA_C_API CNA_Result cna_area_light_shading_copy_shading_glsl(
    char* destination, uint64_t capacity, uint64_t* out_bytes);

/**
 * @brief Gives the forward effect an area light and the table to shade it with.
 *
 * The light is **copied**; the table is **borrowed** and must outlive the effect's use of it. Both
 * halves are required, which is why this is one route rather than two: an effect given a light and
 * no table would have nothing to integrate the lobe against.
 *
 * **A degenerate light CLEARS the area light rather than being refused.** That is what the
 * canonical setter does -- a light with parallel axes or zero range would divide by zero in the
 * form factor, and unsetting it is how the effect stays drawable -- so this route succeeds and the
 * effect ends up with no area light at all. Call @ref cna_clustered_forward_effect_has_area_light
 * afterwards to find out which happened; a successful call is not evidence that a light was set.
 * A *malformed structure* is a different thing and is refused.
 *
 * @param effect The effect.
 * @param light The light to apply; a degenerate one clears instead of applying.
 * @param table The BRDF table to shade it with.
 * @return `CNA_RESULT_SUCCESS` -- including when a degenerate light cleared instead of applying --
 * `CNA_RESULT_INVALID_HANDLE` for an invalid table, `CNA_RESULT_INVALID_ARGUMENT` for a null or
 * malformed light structure, `CNA_RESULT_NOT_SUPPORTED` without the engine layer, or an error.
 */
CNA_C_API CNA_Result cna_clustered_forward_effect_set_area_light(
    CNA_ClusteredForwardEffectHandle effect,
    const CNA_AreaLightEXT* light,
    CNA_AreaLightBrdfTableHandle table);

#ifdef __cplusplus
}
#endif

#endif
