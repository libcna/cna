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

#ifdef __cplusplus
}
#endif

#endif
