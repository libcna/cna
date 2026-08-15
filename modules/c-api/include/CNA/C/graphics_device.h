// SPDX-License-Identifier: MS-PL

#ifndef CNA_C_GRAPHICS_DEVICE_H
#define CNA_C_GRAPHICS_DEVICE_H

#include "CNA/C/display.h"
#include "CNA/C/graphics_state.h"
#include "CNA/C/index_resources.h"
#include "CNA/C/vertex_resources.h"
#include "CNA/C/math_values.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Fixed-width bit set selecting the buffers a device clear touches.
 *
 * The native declaration provides `|`, `&`, `~`, `|=` and `&=` operators over its scoped
 * enumeration. This fixed-width integer alias needs no adapter for them: C's own bitwise
 * operators apply directly to a `CNA_ClearOptions` value and produce the identical bit pattern.
 */
typedef uint32_t CNA_ClearOptions;

/** @brief Selects the color buffer. */
#define CNA_CLEAR_OPTION_TARGET UINT32_C(1)
/** @brief Selects the depth buffer. */
#define CNA_CLEAR_OPTION_DEPTH_BUFFER UINT32_C(2)
/** @brief Selects the stencil buffer. */
#define CNA_CLEAR_OPTION_STENCIL UINT32_C(4)

/** @brief Fixed-width identity describing the lifecycle status of a graphics device. */
typedef uint32_t CNA_GraphicsDeviceStatus;

/** @brief The device operates normally. */
#define CNA_GRAPHICS_DEVICE_STATUS_NORMAL UINT32_C(0)
/** @brief The device has been lost and cannot process commands. */
#define CNA_GRAPHICS_DEVICE_STATUS_LOST UINT32_C(1)
/** @brief The device has not been reset after being lost. */
#define CNA_GRAPHICS_DEVICE_STATUS_NOT_RESET UINT32_C(2)

/**
 * @brief Fixed-width identity of the policy a 2D-only renderer applies to unsupported 3D calls.
 */
typedef uint32_t CNA_Unsupported3DGraphicsCallBehavior;

/** @brief Preserves the renderer's established failure behavior. */
#define CNA_UNSUPPORTED_3D_GRAPHICS_CALL_BEHAVIOR_THROW UINT32_C(0)
/** @brief Logs one warning per operation and substitutes a safe no-op or null-object stub. */
#define CNA_UNSUPPORTED_3D_GRAPHICS_CALL_BEHAVIOR_WARN_AND_STUB UINT32_C(1)

/**
 * @brief Describes the view bounds for the render-target surface.
 *
 * The six fields are the complete public property set of the canonical `Viewport`; reading and
 * writing them directly is the C mapping of its getters and setters.
 */
typedef struct CNA_Viewport {
    /** @brief X coordinate of the upper-left corner in pixels. */
    int32_t x;

    /** @brief Y coordinate of the upper-left corner in pixels. */
    int32_t y;

    /** @brief Width of the viewport in pixels. */
    int32_t width;

    /** @brief Height of the viewport in pixels. */
    int32_t height;

    /** @brief Minimum depth of the clip volume. */
    float min_depth;

    /** @brief Maximum depth of the clip volume. */
    float max_depth;
} CNA_Viewport;

/**
 * @brief Initializes a viewport exactly as the canonical default constructor does.
 *
 * @param out_value Receives a viewport with zero position and size, `min_depth` 0 and
 * `max_depth` 1.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null output.
 */
CNA_C_API CNA_Result cna_viewport_init(CNA_Viewport* out_value);

/**
 * @brief Initializes a viewport from a position and size.
 *
 * @param x X coordinate of the upper-left corner in pixels.
 * @param y Y coordinate of the upper-left corner in pixels.
 * @param width Width of the viewport in pixels.
 * @param height Height of the viewport in pixels.
 * @param out_value Receives the viewport with `min_depth` 0 and `max_depth` 1.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null output.
 */
CNA_C_API CNA_Result cna_viewport_init_bounds(
    int32_t x,
    int32_t y,
    int32_t width,
    int32_t height,
    CNA_Viewport* out_value);

/**
 * @brief Initializes a viewport from a rectangle.
 *
 * @param bounds Rectangle that defines the location and size of the viewport.
 * @param out_value Receives the viewport with `min_depth` 0 and `max_depth` 1.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null output.
 */
CNA_C_API CNA_Result cna_viewport_init_from_rectangle(
    CNA_Rectangle bounds,
    CNA_Viewport* out_value);

/**
 * @brief Gets the aspect ratio of a viewport.
 *
 * @param value Viewport to measure.
 * @param out_aspect_ratio Receives width divided by height, or zero when either is zero.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null output.
 */
CNA_C_API CNA_Result cna_viewport_get_aspect_ratio(
    CNA_Viewport value,
    float* out_aspect_ratio);

/**
 * @brief Gets the viewport area as a rectangle.
 *
 * @param value Viewport to convert.
 * @param out_bounds Receives the position and size of @p value.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null output.
 */
CNA_C_API CNA_Result cna_viewport_get_bounds(
    CNA_Viewport value,
    CNA_Rectangle* out_bounds);

/**
 * @brief Sets the viewport position and size from a rectangle.
 *
 * @param viewport Viewport updated in place; depth range is preserved.
 * @param bounds Rectangle that defines the new viewport position and size.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null viewport.
 */
CNA_C_API CNA_Result cna_viewport_set_bounds(
    CNA_Viewport* viewport,
    CNA_Rectangle bounds);

/**
 * @brief Gets the subset of the viewport guaranteed to be visible on lower-quality displays.
 *
 * @param value Viewport to measure.
 * @param out_area Receives the title-safe rectangle.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null output.
 */
CNA_C_API CNA_Result cna_viewport_get_title_safe_area(
    CNA_Viewport value,
    CNA_Rectangle* out_area);

/**
 * @brief Projects a world-space point into screen space.
 *
 * @param value Viewport that defines the screen rectangle and depth range.
 * @param source World-space point to project.
 * @param projection Projection matrix.
 * @param view View matrix.
 * @param world World matrix.
 * @param out_value Receives the projected screen-space point.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null output.
 */
CNA_C_API CNA_Result cna_viewport_project(
    CNA_Viewport value,
    CNA_Vector3 source,
    CNA_Matrix projection,
    CNA_Matrix view,
    CNA_Matrix world,
    CNA_Vector3* out_value);

/**
 * @brief Unprojects a screen-space point back into world space.
 *
 * @param value Viewport that defines the screen rectangle and depth range.
 * @param source Screen-space point to unproject.
 * @param projection Projection matrix.
 * @param view View matrix.
 * @param world World matrix.
 * @param out_value Receives the unprojected world-space point.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null output.
 */
CNA_C_API CNA_Result cna_viewport_unproject(
    CNA_Viewport value,
    CNA_Vector3 source,
    CNA_Matrix projection,
    CNA_Matrix view,
    CNA_Matrix world,
    CNA_Vector3* out_value);

/**
 * @brief Gets the UTF-8 byte count of the canonical viewport string.
 *
 * @param value Viewport to format.
 * @param out_bytes Receives the required byte count without a terminator.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null output.
 */
CNA_C_API CNA_Result cna_viewport_get_string_size(
    CNA_Viewport value,
    uint64_t* out_bytes);

/**
 * @brief Copies the canonical viewport string as UTF-8 bytes without a terminator.
 *
 * @param value Viewport to format.
 * @param destination Caller-owned destination, or null only when @p capacity is zero.
 * @param capacity Destination capacity in bytes.
 * @param out_bytes Receives the required byte count without a terminator.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL`, or an argument error. No partial
 * string is written.
 */
CNA_C_API CNA_Result cna_viewport_copy_string(
    CNA_Viewport value,
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

/**
 * @brief Owned handle for one graphics-device event subscription.
 *
 * A registration is a C-owned resource of the active game. It must be released with
 * @ref cna_graphics_device_unsubscribe before @ref cna_game_destroy succeeds, which keeps the
 * canonical device alive for the whole life of the subscription.
 */
typedef CNA_Handle CNA_GraphicsDeviceEventRegistrationHandle;

/** @brief Fixed-width identity of a graphics-device event that carries no payload. */
typedef uint32_t CNA_GraphicsDeviceEvent;

/** @brief Raised when the device is disposed. */
#define CNA_GRAPHICS_DEVICE_EVENT_DISPOSING UINT32_C(0)
/** @brief Raised when the device is lost. */
#define CNA_GRAPHICS_DEVICE_EVENT_DEVICE_LOST UINT32_C(1)
/** @brief Raised after the device has been reset. */
#define CNA_GRAPHICS_DEVICE_EVENT_DEVICE_RESET UINT32_C(2)
/** @brief Raised before the device is reset. */
#define CNA_GRAPHICS_DEVICE_EVENT_DEVICE_RESETTING UINT32_C(3)

/**
 * @brief Describes the resource reported by a graphics-device resource-created event.
 *
 * The canonical event is raised from the graphics-resource base constructor, so the reported
 * object is still under construction: its concrete type does not exist yet and no member of it can
 * be queried. The C event therefore reports only that a resource was supplied, and no native
 * object pointer crosses the ABI.
 */
typedef struct CNA_ResourceCreatedEventInfo {
    /** @brief Size of this structure in bytes. */
    uint32_t struct_size;

    /** @brief Version of this structure. */
    uint32_t struct_version;

    /** @brief `CNA_TRUE` when the event reported a non-null resource. */
    CNA_Bool has_resource;

    /** @brief Reserved bytes; always zero. */
    uint8_t reserved[7];
} CNA_ResourceCreatedEventInfo;

/**
 * @brief Describes the resource reported by a graphics-device resource-destroyed event.
 *
 * @ref name borrows bytes that are valid only for the duration of the callback. The canonical
 * `System::Object*` tag is caller-owned native state, so it is reported as presence only.
 */
typedef struct CNA_ResourceDestroyedEventInfo {
    /** @brief Size of this structure in bytes. */
    uint32_t struct_size;

    /** @brief Version of this structure. */
    uint32_t struct_version;

    /** @brief `CNA_TRUE` when the destroyed resource carried a non-null native tag. */
    CNA_Bool has_tag;

    /** @brief Reserved bytes; always zero. */
    uint8_t reserved[7];

    /** @brief Callback-scoped UTF-8 resource name, which may be empty. */
    CNA_StringView name;
} CNA_ResourceDestroyedEventInfo;

/**
 * @brief Receives a graphics-device event that carries no payload.
 *
 * @param graphics_device Device handle supplied when the callback was registered.
 * @param context Caller-owned context supplied at registration.
 */
typedef void (*CNA_GraphicsDeviceEventCallback)(
    CNA_Handle graphics_device,
    void* context);

/**
 * @brief Receives a graphics-device resource-created event.
 *
 * @param graphics_device Device handle supplied when the callback was registered.
 * @param info Callback-scoped event description; the structure and its bytes expire on return.
 * @param context Caller-owned context supplied at registration.
 */
typedef void (*CNA_GraphicsDeviceResourceCreatedCallback)(
    CNA_Handle graphics_device,
    const CNA_ResourceCreatedEventInfo* info,
    void* context);

/**
 * @brief Receives a graphics-device resource-destroyed event.
 *
 * @param graphics_device Device handle supplied when the callback was registered.
 * @param info Callback-scoped event description; the structure and its bytes expire on return.
 * @param context Caller-owned context supplied at registration.
 */
typedef void (*CNA_GraphicsDeviceResourceDestroyedCallback)(
    CNA_Handle graphics_device,
    const CNA_ResourceDestroyedEventInfo* info,
    void* context);

/**
 * @brief Gets whether the graphics device has been disposed.
 *
 * @param graphics_device Callback-scoped borrowed graphics-device handle.
 * @param out_is_disposed Receives `CNA_TRUE` when the device has been disposed.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_graphics_device_get_is_disposed(
    CNA_Handle graphics_device,
    CNA_Bool* out_is_disposed);

/**
 * @brief Gets the current device lifecycle status.
 *
 * @param graphics_device Callback-scoped borrowed graphics-device handle.
 * @param out_status Receives one of the `CNA_GRAPHICS_DEVICE_STATUS_*` identities.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_graphics_device_get_status(
    CNA_Handle graphics_device,
    CNA_GraphicsDeviceStatus* out_status);

/**
 * @brief Gets the index of the adapter this device was created with.
 *
 * @param graphics_device Callback-scoped borrowed graphics-device handle.
 * @param out_adapter_index Receives an index usable with the `cna_graphics_adapter_*` queries.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_STATE` when the device's adapter is no longer
 * one of the currently enumerated adapters, or a documented argument/handle/thread/native failure.
 *
 * Adapter indices are point-in-time values; a later adapter change may renumber them.
 */
CNA_C_API CNA_Result cna_graphics_device_get_adapter_index(
    CNA_Handle graphics_device,
    uint32_t* out_adapter_index);

/**
 * @brief Gets the graphics profile this device was created with.
 *
 * @param graphics_device Callback-scoped borrowed graphics-device handle.
 * @param out_profile Receives `CNA_GRAPHICS_PROFILE_REACH` or `CNA_GRAPHICS_PROFILE_HI_DEF`.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_graphics_device_get_graphics_profile(
    CNA_Handle graphics_device,
    CNA_GraphicsProfile* out_profile);

/**
 * @brief Gets the current scissor rectangle.
 *
 * @param graphics_device Callback-scoped borrowed graphics-device handle.
 * @param out_scissor_rectangle Receives the scissor rectangle.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_graphics_device_get_scissor_rectangle(
    CNA_Handle graphics_device,
    CNA_Rectangle* out_scissor_rectangle);

/**
 * @brief Sets the scissor rectangle used for clipping.
 *
 * @param graphics_device Callback-scoped borrowed graphics-device handle.
 * @param scissor_rectangle Rectangle to apply.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_graphics_device_set_scissor_rectangle(
    CNA_Handle graphics_device,
    CNA_Rectangle scissor_rectangle);

/**
 * @brief Gets the current viewport.
 *
 * @param graphics_device Callback-scoped borrowed graphics-device handle.
 * @param out_viewport Receives the viewport.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_graphics_device_get_viewport(
    CNA_Handle graphics_device,
    CNA_Viewport* out_viewport);

/**
 * @brief Sets the viewport.
 *
 * @param graphics_device Callback-scoped borrowed graphics-device handle.
 * @param viewport Viewport to apply. Both depth values must be finite.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for a non-finite depth value, or a
 * documented handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_graphics_device_set_viewport(
    CNA_Handle graphics_device,
    CNA_Viewport viewport);

/**
 * @brief Gets the current blend factor color.
 *
 * @param graphics_device Callback-scoped borrowed graphics-device handle.
 * @param out_blend_factor Receives the blend factor.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_graphics_device_get_blend_factor(
    CNA_Handle graphics_device,
    CNA_Color* out_blend_factor);

/**
 * @brief Sets the blend factor color.
 *
 * @param graphics_device Callback-scoped borrowed graphics-device handle.
 * @param blend_factor Blend factor to apply.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_graphics_device_set_blend_factor(
    CNA_Handle graphics_device,
    CNA_Color blend_factor);

/**
 * @brief Gets the current multisample mask.
 *
 * @param graphics_device Callback-scoped borrowed graphics-device handle.
 * @param out_multi_sample_mask Receives the mask; the canonical default is -1.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_graphics_device_get_multi_sample_mask(
    CNA_Handle graphics_device,
    int32_t* out_multi_sample_mask);

/**
 * @brief Sets the multisample mask.
 *
 * @param graphics_device Callback-scoped borrowed graphics-device handle.
 * @param multi_sample_mask Bitmask for multisample anti-aliasing.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_graphics_device_set_multi_sample_mask(
    CNA_Handle graphics_device,
    int32_t multi_sample_mask);

/**
 * @brief Gets the current reference stencil value.
 *
 * @param graphics_device Callback-scoped borrowed graphics-device handle.
 * @param out_reference_stencil Receives the reference value; the canonical default is 0.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_graphics_device_get_reference_stencil(
    CNA_Handle graphics_device,
    int32_t* out_reference_stencil);

/**
 * @brief Sets the reference stencil value.
 *
 * @param graphics_device Callback-scoped borrowed graphics-device handle.
 * @param reference_stencil Reference value for stencil operations.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_graphics_device_set_reference_stencil(
    CNA_Handle graphics_device,
    int32_t reference_stencil);

/**
 * @brief Gets the UTF-8 byte count of the device's fully qualified .NET type name.
 *
 * @param graphics_device Callback-scoped borrowed graphics-device handle.
 * @param out_bytes Receives the byte count without a terminator.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_graphics_device_get_type_name_size(
    CNA_Handle graphics_device,
    uint64_t* out_bytes);

/**
 * @brief Copies the device's fully qualified .NET type name as UTF-8 bytes without a terminator.
 *
 * @param graphics_device Callback-scoped borrowed graphics-device handle.
 * @param destination Caller-owned destination, or null only when @p capacity is zero.
 * @param capacity Destination capacity in bytes.
 * @param out_bytes Receives the required byte count without a terminator.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL`, or a documented
 * argument/handle/thread/native failure. No partial name is written.
 */
CNA_C_API CNA_Result cna_graphics_device_copy_type_name(
    CNA_Handle graphics_device,
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

/**
 * @brief Reports that C cannot dispose the graphics device owned by the active game.
 *
 * @param graphics_device Callback-scoped borrowed graphics-device handle.
 * @return `CNA_RESULT_NOT_SUPPORTED` for a valid device handle, or a documented
 * argument/handle/thread failure.
 *
 * The canonical `GraphicsDevice` belongs to the running game, so disposing it through a borrowed
 * handle would leave that game drawing into a destroyed device. @ref cna_game_destroy performs the
 * canonical disposal and @ref cna_graphics_device_get_is_disposed observes the resulting state.
 */
CNA_C_API CNA_Result cna_graphics_device_dispose(CNA_Handle graphics_device);

/**
 * @brief Subscribes to one graphics-device event that carries no payload.
 *
 * @param graphics_device Callback-scoped borrowed graphics-device handle.
 * @param device_event One of the `CNA_GRAPHICS_DEVICE_EVENT_*` identities.
 * @param callback Non-null callback invoked synchronously on the game thread.
 * @param context Caller-owned callback context, which may be null.
 * @param out_registration Receives an owned registration handle on success.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 *
 * The callback and context remain caller-owned until unsubscription. The registration keeps the
 * game alive in the same way an owned graphics resource does.
 */
CNA_C_API CNA_Result cna_graphics_device_subscribe_event(
    CNA_Handle graphics_device,
    CNA_GraphicsDeviceEvent device_event,
    CNA_GraphicsDeviceEventCallback callback,
    void* context,
    CNA_GraphicsDeviceEventRegistrationHandle* out_registration);

/**
 * @brief Subscribes to the graphics-device resource-created event.
 *
 * @param graphics_device Callback-scoped borrowed graphics-device handle.
 * @param callback Non-null callback invoked synchronously on the game thread.
 * @param context Caller-owned callback context, which may be null.
 * @param out_registration Receives an owned registration handle on success.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_graphics_device_subscribe_resource_created(
    CNA_Handle graphics_device,
    CNA_GraphicsDeviceResourceCreatedCallback callback,
    void* context,
    CNA_GraphicsDeviceEventRegistrationHandle* out_registration);

/**
 * @brief Subscribes to the graphics-device resource-destroyed event.
 *
 * @param graphics_device Callback-scoped borrowed graphics-device handle.
 * @param callback Non-null callback invoked synchronously on the game thread.
 * @param context Caller-owned callback context, which may be null.
 * @param out_registration Receives an owned registration handle on success.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_graphics_device_subscribe_resource_destroyed(
    CNA_Handle graphics_device,
    CNA_GraphicsDeviceResourceDestroyedCallback callback,
    void* context,
    CNA_GraphicsDeviceEventRegistrationHandle* out_registration);

/**
 * @brief Unsubscribes and releases one graphics-device event registration.
 *
 * @param registration Owned registration handle.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread failure. A second release returns
 * `CNA_RESULT_INVALID_HANDLE`.
 */
CNA_C_API CNA_Result cna_graphics_device_unsubscribe(
    CNA_GraphicsDeviceEventRegistrationHandle registration);

/** @brief Number of texture sampler slots in each device texture collection. */
#define CNA_TEXTURE_COLLECTION_MAX_TEXTURES UINT32_C(16)

/**
 * @brief Describes one texture sampler slot of a device texture collection.
 */
typedef struct CNA_TextureSlotInfo {
    /** @brief Size of this caller-provided structure in bytes. */
    uint32_t struct_size;

    /** @brief Version of this caller-provided structure. */
    uint32_t struct_version;

    /** @brief `CNA_TRUE` when a native texture currently occupies the slot. */
    CNA_Bool bound;

    /** @brief Reserved bytes; always zero. */
    uint8_t reserved[7];

    /**
     * @brief Handle of the texture bound through this API, or `CNA_INVALID_HANDLE`.
     *
     * A slot filled by canonical CNA code — a SpriteBatch flush, for example — reports
     * @ref bound as `CNA_TRUE` with an invalid handle, because no C resource owns that texture.
     */
    CNA_Handle texture;
} CNA_TextureSlotInfo;

/**
 * @brief Reads one texture sampler slot of a device texture collection.
 *
 * @param graphics_device Callback-scoped borrowed graphics-device handle.
 * @param stage `CNA_SHADER_STAGE_PIXEL` or `CNA_SHADER_STAGE_VERTEX`.
 * @param slot Slot index below @ref CNA_TEXTURE_COLLECTION_MAX_TEXTURES.
 * @param out_info Caller-provided versioned structure to receive the slot description.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for an unknown stage, an
 * out-of-range slot or an invalid structure, or a documented handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_graphics_device_get_texture(
    CNA_Handle graphics_device,
    CNA_ShaderStage stage,
    uint32_t slot,
    CNA_TextureSlotInfo* out_info);

/**
 * @brief Binds a texture to one sampler slot of a device texture collection.
 *
 * @param graphics_device Callback-scoped borrowed graphics-device handle.
 * @param stage `CNA_SHADER_STAGE_PIXEL` or `CNA_SHADER_STAGE_VERTEX`.
 * @param slot Slot index below @ref CNA_TEXTURE_COLLECTION_MAX_TEXTURES.
 * @param texture Owned Texture2D, Texture3D, TextureCube or render-target handle, or
 * `CNA_INVALID_HANDLE` to leave the slot empty.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_STATE` when the texture is disposed or is
 * currently bound as a render target, `CNA_RESULT_INVALID_ARGUMENT` for an unknown stage or an
 * out-of-range slot, or a documented handle/thread/native failure.
 *
 * Binding stores no ownership: a texture destroyed through its own C route unbinds itself from
 * every sampler slot, exactly as canonical disposal does.
 */
CNA_C_API CNA_Result cna_graphics_device_set_texture(
    CNA_Handle graphics_device,
    CNA_ShaderStage stage,
    uint32_t slot,
    CNA_Handle texture);

/**
 * @brief Clears every sampler slot of both device collections that holds the given texture.
 *
 * @param graphics_device Callback-scoped borrowed graphics-device handle.
 * @param texture Owned texture handle to unbind.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure. Unbinding a
 * texture that occupies no slot succeeds and changes nothing.
 */
CNA_C_API CNA_Result cna_graphics_device_unbind_texture(
    CNA_Handle graphics_device,
    CNA_Handle texture);

/**
 * @brief Describes one back-buffer readback window.
 */
typedef struct CNA_BackBufferReadback {
    /** @brief Size of this caller-provided structure in bytes. */
    uint32_t struct_size;

    /** @brief Version of this caller-provided structure. */
    uint32_t struct_version;

    /** @brief `CNA_TRUE` to read @ref source_rectangle instead of the whole back buffer. */
    CNA_Bool has_source_rectangle;

    /** @brief Reserved bytes; callers must initialize them to zero. */
    uint8_t reserved[3];

    /** @brief Source region in back-buffer pixels; ignored unless @ref has_source_rectangle. */
    CNA_Rectangle source_rectangle;

    /** @brief First destination element written, in pixels. */
    uint64_t start_index;

    /** @brief Number of pixels to read. */
    uint64_t element_count;
} CNA_BackBufferReadback;

/**
 * @brief Clears the back buffer from four floating-point channels.
 *
 * @param graphics_device Callback-scoped borrowed graphics-device handle.
 * @param r Red channel in the inclusive range 0 through 1.
 * @param g Green channel in the inclusive range 0 through 1.
 * @param b Blue channel in the inclusive range 0 through 1.
 * @param a Alpha channel in the inclusive range 0 through 1.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for a non-finite channel, or a
 * documented handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_graphics_device_clear_rgba(
    CNA_Handle graphics_device,
    float r,
    float g,
    float b,
    float a);

/**
 * @brief Clears the color and depth buffers.
 *
 * @param graphics_device Callback-scoped borrowed graphics-device handle.
 * @param color Color value for the color buffer.
 * @param depth Depth value in the inclusive range 0 through 1. Must be finite.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for a non-finite depth,
 * `CNA_RESULT_NOT_SUPPORTED` when the backend has no depth buffer, or another documented
 * handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_graphics_device_clear_color_depth(
    CNA_Handle graphics_device,
    CNA_Color color,
    float depth);

/**
 * @brief Clears the buffers selected by a `CNA_ClearOptions` mask.
 *
 * @param graphics_device Callback-scoped borrowed graphics-device handle.
 * @param options Zero or more `CNA_CLEAR_OPTION_*` bits.
 * @param color Color value for the color buffer.
 * @param depth Depth value in the inclusive range 0 through 1. Must be finite.
 * @param stencil Stencil value for the stencil buffer.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for an unknown option bit or a
 * non-finite depth, `CNA_RESULT_NOT_SUPPORTED` when the backend cannot clear a selected buffer, or
 * another documented handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_graphics_device_clear_options(
    CNA_Handle graphics_device,
    CNA_ClearOptions options,
    CNA_Color color,
    float depth,
    int32_t stencil);

/**
 * @brief Presents the rendered frame to the display.
 *
 * @param graphics_device Callback-scoped borrowed graphics-device handle.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` when the backend cannot present, or a
 * documented handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_graphics_device_present(CNA_Handle graphics_device);

/**
 * @brief Resets the device using its current presentation parameters.
 *
 * @param graphics_device Callback-scoped borrowed graphics-device handle.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` when the backend refuses the reset, or a
 * documented handle/thread/native failure.
 *
 * A successful reset raises the device's resetting and reset events in that order.
 */
CNA_C_API CNA_Result cna_graphics_device_reset(CNA_Handle graphics_device);

/**
 * @brief Resets the device with new presentation parameters and an optional adapter.
 *
 * @param graphics_device Callback-scoped borrowed graphics-device handle.
 * @param parameters Caller-provided versioned presentation parameters.
 * @param adapter_index Adapter index to switch to, or null to keep the current adapter.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for invalid parameters or an unknown
 * adapter index, `CNA_RESULT_NOT_SUPPORTED` when the backend refuses the reset, or a documented
 * handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_graphics_device_reset_with_parameters(
    CNA_Handle graphics_device,
    const CNA_PresentationParameters* parameters,
    const uint32_t* adapter_index);

/**
 * @brief Reads a window of the back buffer into a caller-owned RGBA8 array.
 *
 * @param graphics_device Callback-scoped borrowed graphics-device handle.
 * @param readback Versioned window description.
 * @param destination Caller-owned output pixels, or null only when @p capacity is zero.
 * @param capacity Capacity of @p destination measured in pixels.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL` when @p capacity cannot hold
 * `start_index + element_count` pixels, `CNA_RESULT_NOT_SUPPORTED` when the active renderer has no
 * honest back-buffer readback, or another documented argument/handle/thread/native failure. No
 * partial pixel array is written.
 */
CNA_C_API CNA_Result cna_graphics_device_get_backbuffer_data_window(
    CNA_Handle graphics_device,
    const CNA_BackBufferReadback* readback,
    CNA_Color* destination,
    uint64_t capacity);

/**
 * @brief Binds one vertex buffer at vertex offset zero.
 *
 * @param graphics_device Callback-scoped borrowed graphics-device handle.
 * @param vertex_buffer Owned vertex-buffer handle, or `CNA_INVALID_HANDLE` to unbind.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_graphics_device_set_vertex_buffer(
    CNA_Handle graphics_device,
    CNA_VertexBufferHandle vertex_buffer);

/**
 * @brief Binds one vertex buffer at an explicit vertex offset.
 *
 * @param graphics_device Callback-scoped borrowed graphics-device handle.
 * @param vertex_buffer Owned vertex-buffer handle, or `CNA_INVALID_HANDLE` to unbind.
 * @param vertex_offset Offset in vertices; must not be negative.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for a negative offset, or a
 * documented handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_graphics_device_set_vertex_buffer_offset(
    CNA_Handle graphics_device,
    CNA_VertexBufferHandle vertex_buffer,
    int32_t vertex_offset);

/**
 * @brief Binds several vertex buffers at once.
 *
 * @param graphics_device Callback-scoped borrowed graphics-device handle.
 * @param bindings Caller-owned bindings copied during this call, or null only when
 * @p binding_count is zero. An empty array unbinds every vertex buffer.
 * @param binding_count Number of bindings beginning at @p bindings.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for a negative offset or frequency,
 * or a documented handle/thread/native failure. Every binding is validated before any is applied.
 */
CNA_C_API CNA_Result cna_graphics_device_set_vertex_buffers(
    CNA_Handle graphics_device,
    const CNA_VertexBufferBinding* bindings,
    uint64_t binding_count);

/**
 * @brief Gets the number of currently bound vertex-buffer bindings.
 *
 * @param graphics_device Callback-scoped borrowed graphics-device handle.
 * @param out_count Receives the binding count.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_graphics_device_get_vertex_buffer_count(
    CNA_Handle graphics_device,
    uint64_t* out_count);

/**
 * @brief Copies the currently bound vertex-buffer bindings.
 *
 * @param graphics_device Callback-scoped borrowed graphics-device handle.
 * @param destination Caller-owned destination, or null only when @p capacity is zero.
 * @param capacity Capacity of @p destination in elements.
 * @param out_count Receives the required binding count.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL`, or a documented
 * argument/handle/thread/native failure. No partial array is written.
 *
 * A binding applied by canonical CNA code reports `CNA_INVALID_HANDLE` in its
 * @ref CNA_VertexBufferBinding::vertex_buffer field, because no C resource owns that buffer.
 */
CNA_C_API CNA_Result cna_graphics_device_copy_vertex_buffers(
    CNA_Handle graphics_device,
    CNA_VertexBufferBinding* destination,
    uint64_t capacity,
    uint64_t* out_count);

/**
 * @brief Gets the vertex buffer bound in the first slot.
 *
 * @param graphics_device Callback-scoped borrowed graphics-device handle.
 * @param out_vertex_buffer Receives the owning C handle, or `CNA_INVALID_HANDLE` when the slot is
 * empty or was bound by canonical CNA code.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_graphics_device_get_vertex_buffer(
    CNA_Handle graphics_device,
    CNA_VertexBufferHandle* out_vertex_buffer);

/**
 * @brief Binds the index buffer used by indexed draw calls.
 *
 * @param graphics_device Callback-scoped borrowed graphics-device handle.
 * @param index_buffer Owned index-buffer handle, or `CNA_INVALID_HANDLE` to unbind.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_graphics_device_set_index_buffer(
    CNA_Handle graphics_device,
    CNA_IndexBufferHandle index_buffer);

/**
 * @brief Gets the currently bound index buffer.
 *
 * @param graphics_device Callback-scoped borrowed graphics-device handle.
 * @param out_index_buffer Receives the owning C handle, or `CNA_INVALID_HANDLE` when no buffer is
 * bound or it was bound by canonical CNA code.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_graphics_device_get_index_buffer(
    CNA_Handle graphics_device,
    CNA_IndexBufferHandle* out_index_buffer);

#ifdef __cplusplus
}
#endif

#endif
