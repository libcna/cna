// SPDX-License-Identifier: MS-PL

#ifndef CNA_C_ENGINE_LAYER_H
#define CNA_C_ENGINE_LAYER_H

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
} CNA_PostProcessContext;

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

#ifdef __cplusplus
}
#endif

#endif
