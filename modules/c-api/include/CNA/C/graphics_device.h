// SPDX-License-Identifier: MS-PL

#ifndef CNA_C_GRAPHICS_DEVICE_H
#define CNA_C_GRAPHICS_DEVICE_H

#include "CNA/C/display.h"
#include "CNA/C/effects.h"
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
     *
     * **There is deliberately no route from a native object back to a handle**, here or anywhere
     * else in this ABI, and that is worth stating because it is the reason a getter like this one
     * cannot always answer with a handle. A handle is a record this ABI *created* for an object a C
     * caller asked it to make; it is not an identity the object carries. Reversing the direction
     * would mean either a process-wide native-pointer-to-handle map, which would keep every object
     * a C caller ever saw alive forever and answer with a stale handle after any reuse of the
     * address, or a slot on every canonical graphics type for a C concept that has no business
     * being there. Neither is worth what it buys.
     *
     * The practical consequence, for a consumer whose own `Textures[i]` getter must return the
     * object it set: cache what you bind and answer from the cache, and use @ref bound to tell
     * "something else owns this slot now" from "the slot is empty". That is the case the cache
     * cannot cover, and reporting it is what this field is for.
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
/**
 * @brief Creates a GraphicsDevice this caller owns, outside any Game.
 *
 * Every other route in this ABI hands out the Game's own device, borrowed for the duration of a
 * callback. This one creates an independent device with XNA's own constructor arguments, and the
 * caller destroys it with @ref cna_graphics_device_destroy. Several may exist at once, and one may
 * be destroyed while another is still live.
 *
 * The returned handle is accepted everywhere a borrowed device handle is, so resources are created
 * on it exactly as they are on a Game's device. Resources remember which device made them: mixing
 * one device's resource into another device's call is refused, whether the devices are two
 * caller-created ones or a caller-created one and a Game's.
 *
 * Unlike a Game's resources, resources on a caller-created device do not gate
 * `cna_game_destroy` -- they belong to this device, not to a game, and are released with it.
 *
 * @param adapter_index Adapter to use, indexed as `cna_graphics_adapter_get_count` reports.
 * @param graphics_profile `CNA_GRAPHICS_PROFILE_REACH` or `CNA_GRAPHICS_PROFILE_HI_DEF`.
 * @param parameters Caller-provided versioned presentation parameters.
 * @param out_graphics_device Receives an owned device handle on success, `CNA_INVALID_HANDLE`
 *        otherwise.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for a null output, a malformed
 *         parameter structure, an unknown profile or an out-of-range adapter, or a documented
 *         native failure when the platform cannot supply a device.
 */
CNA_C_API CNA_Result cna_graphics_device_create(
    uint32_t adapter_index,
    uint32_t graphics_profile,
    const CNA_PresentationParameters* parameters,
    CNA_Handle* out_graphics_device);

/**
 * @brief Disposes and releases a caller-created GraphicsDevice.
 *
 * Only a handle from @ref cna_graphics_device_create is accepted; a Game's borrowed device is not
 * the caller's to destroy and is refused.
 *
 * @param graphics_device Owned graphics-device handle.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_HANDLE` when the handle is not a
 *         caller-created device, or a documented thread/native failure.
 */
CNA_C_API CNA_Result cna_graphics_device_destroy(CNA_Handle graphics_device);

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

/** @brief Identifies how a caller-supplied user-primitive vertex array is represented. */
typedef uint32_t CNA_UserVertexSource;

/** @brief Bytes that are already a GPU vertex stream at the declared stride. */
#define CNA_USER_VERTEX_SOURCE_RAW_STREAM UINT32_C(0)
/** @brief An array of `CNA_VertexPositionColor` values. */
#define CNA_USER_VERTEX_SOURCE_POSITION_COLOR UINT32_C(1)
/** @brief An array of `CNA_VertexPositionColorTexture` values. */
#define CNA_USER_VERTEX_SOURCE_POSITION_COLOR_TEXTURE UINT32_C(2)
/** @brief An array of `CNA_VertexPositionTexture` values. */
#define CNA_USER_VERTEX_SOURCE_POSITION_TEXTURE UINT32_C(3)
/** @brief An array of `CNA_VertexPositionNormalTexture` values. */
#define CNA_USER_VERTEX_SOURCE_POSITION_NORMAL_TEXTURE UINT32_C(4)

/**
 * @brief Describes one caller-supplied vertex array for a user-primitive draw.
 */
typedef struct CNA_UserPrimitives {
    /** @brief Size of this caller-provided structure in bytes. */
    uint32_t struct_size;

    /** @brief Version of this caller-provided structure. */
    uint32_t struct_version;

    /** @brief Primitive topology to draw. */
    CNA_PrimitiveType primitive_type;

    /** @brief One of the `CNA_USER_VERTEX_SOURCE_*` identities. */
    CNA_UserVertexSource vertex_source;

    /** @brief Caller-owned vertex array read during this call. */
    const void* vertex_data;

    /**
     * @brief Vertex declaration describing the stream, or `CNA_INVALID_HANDLE`.
     *
     * A raw stream without a declaration uses the canonical implicit
     * `VertexPositionColor` layout; a typed source without a declaration uses that type's own
     * declaration.
     */
    CNA_VertexDeclarationHandle vertex_declaration;

    /** @brief Offset into @ref vertex_data in vertices; must not be negative. */
    int32_t vertex_offset;

    /** @brief Number of vertices; used only by the indexed route. */
    int32_t num_vertices;

    /** @brief Number of primitives to draw; must be positive. */
    int32_t primitive_count;

    /** @brief Reserved for future use; callers must initialize this to zero. */
    uint32_t reserved;
} CNA_UserPrimitives;

/**
 * @brief Describes one caller-supplied index array for a user-primitive draw.
 */
typedef struct CNA_UserIndices {
    /** @brief Size of this caller-provided structure in bytes. */
    uint32_t struct_size;

    /** @brief Version of this caller-provided structure. */
    uint32_t struct_version;

    /** @brief Stored index width. */
    CNA_IndexElementSize index_element_size;

    /** @brief Offset into @ref index_data in indices; must not be negative. */
    int32_t index_offset;

    /** @brief Caller-owned index array read during this call. */
    const void* index_data;
} CNA_UserIndices;

/**
 * @brief Gets the vertex count required to draw a primitive count of one topology.
 *
 * @param primitive_type Primitive topology.
 * @param primitive_count Number of primitives.
 * @param out_vertex_count Receives the total vertex count.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for an unknown topology or a null
 * output, or `CNA_RESULT_INVALID_STATE` when the canonical helper rejects the topology.
 */
CNA_C_API CNA_Result cna_primitive_type_get_vertex_count(
    CNA_PrimitiveType primitive_type,
    int32_t primitive_count,
    int32_t* out_vertex_count);

/**
 * @brief Draws non-indexed primitives from the bound vertex buffer.
 *
 * @param graphics_device Callback-scoped borrowed graphics-device handle.
 * @param primitive_type Primitive topology.
 * @param vertex_start Index of the first vertex.
 * @param primitive_count Number of primitives.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` on a backend without the required
 * capability, or a documented argument/handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_graphics_device_draw_primitives(
    CNA_Handle graphics_device,
    CNA_PrimitiveType primitive_type,
    int32_t vertex_start,
    int32_t primitive_count);

/**
 * @brief Draws indexed primitives from the bound vertex and index buffers.
 *
 * @param graphics_device Callback-scoped borrowed graphics-device handle.
 * @param primitive_type Primitive topology.
 * @param base_vertex Offset added to each decoded index.
 * @param min_vertex_index Minimum referenced vertex index.
 * @param num_vertices Number of referenced vertices.
 * @param start_index First index element consumed.
 * @param primitive_count Number of primitives.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` on a backend without the required
 * capability, or a documented argument/handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_graphics_device_draw_indexed_primitives(
    CNA_Handle graphics_device,
    CNA_PrimitiveType primitive_type,
    int32_t base_vertex,
    int32_t min_vertex_index,
    int32_t num_vertices,
    int32_t start_index,
    int32_t primitive_count);

/**
 * @brief Draws several instances of one indexed geometry range.
 *
 * @param graphics_device Callback-scoped borrowed graphics-device handle.
 * @param primitive_type Primitive topology.
 * @param base_vertex Offset added to each decoded index.
 * @param min_vertex_index Minimum referenced vertex index.
 * @param num_vertices Number of referenced vertices.
 * @param start_index First index element consumed.
 * @param primitive_count Number of primitives per instance.
 * @param instance_count Number of instances.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` on a backend without instancing, or a
 * documented argument/handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_graphics_device_draw_instanced_primitives(
    CNA_Handle graphics_device,
    CNA_PrimitiveType primitive_type,
    int32_t base_vertex,
    int32_t min_vertex_index,
    int32_t num_vertices,
    int32_t start_index,
    int32_t primitive_count,
    int32_t instance_count);

/**
 * @brief Draws non-indexed primitives from a caller-supplied vertex array.
 *
 * @param graphics_device Callback-scoped borrowed graphics-device handle.
 * @param primitives Versioned vertex-array description.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` on a backend without the required
 * capability, or a documented argument/handle/thread/native failure.
 *
 * The vertex source and the optional declaration together select the canonical overload; no vertex
 * array is retained after the call returns.
 */
CNA_C_API CNA_Result cna_graphics_device_draw_user_primitives(
    CNA_Handle graphics_device,
    const CNA_UserPrimitives* primitives);

/**
 * @brief Draws indexed primitives from caller-supplied vertex and index arrays.
 *
 * @param graphics_device Callback-scoped borrowed graphics-device handle.
 * @param primitives Versioned vertex-array description.
 * @param indices Versioned index-array description.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` on a backend without the required
 * capability, or a documented argument/handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_graphics_device_draw_user_indexed_primitives(
    CNA_Handle graphics_device,
    const CNA_UserPrimitives* primitives,
    const CNA_UserIndices* indices);

/**
 * @brief Gets the number of live graphics resources the device currently tracks.
 *
 * @param graphics_device Callback-scoped borrowed graphics-device handle.
 * @param out_count Receives the tracked resource count.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_graphics_device_get_tracked_resource_count(
    CNA_Handle graphics_device,
    uint64_t* out_count);

/**
 * @brief Enables or disables depth testing.
 *
 * @param graphics_device Callback-scoped borrowed graphics-device handle.
 * @param enabled `CNA_TRUE` to enable depth testing.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for a non-Boolean value, or a
 * documented handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_graphics_device_set_depth_test_enabled(
    CNA_Handle graphics_device,
    CNA_Bool enabled);

/**
 * @brief Enables or disables blending.
 *
 * @param graphics_device Callback-scoped borrowed graphics-device handle.
 * @param enabled `CNA_TRUE` to enable blending.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for a non-Boolean value, or a
 * documented handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_graphics_device_set_blend_enabled(
    CNA_Handle graphics_device,
    CNA_Bool enabled);

/**
 * @brief Enables or disables depth writes.
 *
 * @param graphics_device Callback-scoped borrowed graphics-device handle.
 * @param enabled `CNA_TRUE` to enable depth writes.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for a non-Boolean value, or a
 * documented handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_graphics_device_set_depth_write_enabled(
    CNA_Handle graphics_device,
    CNA_Bool enabled);

/**
 * @brief Sets the graphics profile of an already constructed device.
 *
 * @param graphics_device Callback-scoped borrowed graphics-device handle.
 * @param profile `CNA_GRAPHICS_PROFILE_REACH` or `CNA_GRAPHICS_PROFILE_HI_DEF`.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for an unknown profile, or a
 * documented handle/thread/native failure.
 *
 * XNA fixes the profile at device construction; this CNA extension exists because the canonical
 * device is created before a game can request a profile. It does not re-validate existing
 * resources against the new profile.
 */
CNA_C_API CNA_Result cna_graphics_device_set_graphics_profile_ext(
    CNA_Handle graphics_device,
    CNA_GraphicsProfile profile);

/**
 * @brief Enables or disables graphics-context-loss recovery.
 *
 * @param graphics_device Callback-scoped borrowed graphics-device handle.
 * @param enabled `CNA_FALSE` to drop the CPU shadow copies that survive a context loss.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for a non-Boolean value, or a
 * documented handle/thread/native failure.
 *
 * The canonical contract requires this before the device is initialized; a later call has no
 * effect on resources that already exist.
 */
CNA_C_API CNA_Result cna_graphics_device_set_context_recovery_enabled(
    CNA_Handle graphics_device,
    CNA_Bool enabled);

/**
 * @brief Inserts a named debug marker into the GPU command stream.
 *
 * @param graphics_device Callback-scoped borrowed graphics-device handle.
 * @param marker UTF-8 marker text without a terminator.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_ENCODING` for invalid UTF-8, or a documented
 * argument/handle/thread/native failure. Backends without debug markers ignore it.
 */
CNA_C_API CNA_Result cna_graphics_device_set_string_marker_ext(
    CNA_Handle graphics_device,
    CNA_StringView marker);

/**
 * @brief Gets the active policy for unsupported 3D calls on a 2D-only renderer.
 *
 * @param graphics_device Callback-scoped borrowed graphics-device handle.
 * @param out_behavior Receives one of the `CNA_UNSUPPORTED_3D_GRAPHICS_CALL_BEHAVIOR_*` identities.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_graphics_device_get_unsupported_3d_call_behavior(
    CNA_Handle graphics_device,
    CNA_Unsupported3DGraphicsCallBehavior* out_behavior);

/**
 * @brief Sets the policy for unsupported 3D calls on a 2D-only renderer.
 *
 * @param graphics_device Callback-scoped borrowed graphics-device handle.
 * @param behavior One of the `CNA_UNSUPPORTED_3D_GRAPHICS_CALL_BEHAVIOR_*` identities.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for an unknown identity, or a
 * documented handle/thread/native failure.
 *
 * This does not change capability query results and does not suppress argument, lifetime or
 * driver errors.
 */
CNA_C_API CNA_Result cna_graphics_device_set_unsupported_3d_call_behavior(
    CNA_Handle graphics_device,
    CNA_Unsupported3DGraphicsCallBehavior behavior);

/**
 * @brief Sets the effect used by subsequent draw calls.
 *
 * @param graphics_device Callback-scoped borrowed graphics-device handle.
 * @param effect Owned effect handle, or `CNA_INVALID_HANDLE` to clear the current effect.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 *
 * The canonical device stores a borrowed pointer, so the C API keeps the assigned effect alive
 * until it is replaced, cleared, or the game is destroyed. Applying an effect natively also sets
 * this state without going through the C route.
 */
CNA_C_API CNA_Result cna_graphics_device_set_current_effect(
    CNA_Handle graphics_device,
    CNA_EffectHandle effect);

/**
 * @brief Rebuilds the active renderer with a new preferred multisample count.
 *
 * @param graphics_device Callback-scoped borrowed graphics-device handle.
 * @param multi_sample_count Requested sample count; must not be negative.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for a negative count, or a
 * documented handle/thread/native failure.
 *
 * This canonical extension destroys and replaces the renderer outright, so it is only safe before
 * any GPU resource exists on the current renderer. It is not a resource-preserving device reset.
 */
CNA_C_API CNA_Result cna_graphics_device_recreate_renderer_for_multi_sample_count_ext(
    CNA_Handle graphics_device,
    int32_t multi_sample_count);

/** @brief Owned handle for a GPU occlusion query. */
typedef CNA_Handle CNA_OcclusionQueryHandle;

/**
 * @brief Creates an owned occlusion query for the active game's graphics device.
 *
 * @param graphics_device Callback-scoped borrowed graphics-device handle.
 * @param out_occlusion_query Receives an owned occlusion-query handle on success.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` when the backend has no occlusion
 * queries, or a documented argument/handle/thread/native failure.
 *
 * The query is a child of the active game and must be destroyed before @ref cna_game_destroy.
 */
CNA_C_API CNA_Result cna_occlusion_query_create(
    CNA_Handle graphics_device,
    CNA_OcclusionQueryHandle* out_occlusion_query);

/**
 * @brief Begins counting visible pixels; every draw until the matching end is counted.
 *
 * @param occlusion_query Owned occlusion-query handle.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` when the backend has no query object,
 * or a documented handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_occlusion_query_begin(CNA_OcclusionQueryHandle occlusion_query);

/**
 * @brief Ends the query and submits it to the GPU for evaluation.
 *
 * @param occlusion_query Owned occlusion-query handle.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` when the backend has no query object,
 * or a documented handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_occlusion_query_end(CNA_OcclusionQueryHandle occlusion_query);

/**
 * @brief Gets whether the query result can be read without stalling the CPU.
 *
 * @param occlusion_query Owned occlusion-query handle.
 * @param out_is_complete Receives `CNA_TRUE` when the result is available.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` when the backend has no query object,
 * or a documented argument/handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_occlusion_query_get_is_complete(
    CNA_OcclusionQueryHandle occlusion_query,
    CNA_Bool* out_is_complete);

/**
 * @brief Gets the visible pixel count from the most recently completed query.
 *
 * @param occlusion_query Owned occlusion-query handle.
 * @param out_pixel_count Receives the visible pixel count.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` when the backend has no query object,
 * or a documented argument/handle/thread/native failure.
 *
 * Some backends report only zero or one rather than an exact sample count.
 */
CNA_C_API CNA_Result cna_occlusion_query_get_pixel_count(
    CNA_OcclusionQueryHandle occlusion_query,
    int32_t* out_pixel_count);

/**
 * @brief Gets whether the pixel count is a real per-fragment tally rather than a flag.
 *
 * @param occlusion_query Owned occlusion-query handle.
 * @param out_is_precise Receives whether the count is a genuine tally.
 * @return `CNA_RESULT_SUCCESS`, or a documented argument/handle/thread failure.
 *
 * True where the backend counts fragments the way XNA's own Direct3D 9 query does; false where it
 * can only answer "any" or "none" -- OpenGL ES 3.0 and WebGL 2, whose core query target is boolean.
 * **A coverage ratio computed from a boolean count is `1/area`, not a fraction**, so a game that
 * needs one has to be able to ask which it is holding rather than dividing and hoping. Ask this
 * before dividing @ref cna_occlusion_query_get_pixel_count by an area.
 */
CNA_C_API CNA_Result cna_occlusion_query_get_is_pixel_count_precise_ext(
    CNA_OcclusionQueryHandle occlusion_query,
    CNA_Bool* out_is_precise);

/**
 * @brief Gets whether the query still owns a live native query object.
 *
 * @param occlusion_query Owned occlusion-query handle.
 * @param out_has_renderer Receives `CNA_TRUE` while the native query object is alive.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_occlusion_query_has_renderer(
    CNA_OcclusionQueryHandle occlusion_query,
    CNA_Bool* out_has_renderer);

/**
 * @brief Disposes and releases an owned occlusion query.
 *
 * @param occlusion_query Owned occlusion-query handle.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread/native failure. A second destroy
 * returns `CNA_RESULT_INVALID_HANDLE`.
 */
CNA_C_API CNA_Result cna_occlusion_query_destroy(CNA_OcclusionQueryHandle occlusion_query);

/**
 * @brief Tells every content-losable resource on this device that its content is gone.
 *
 * Iterates a **snapshot** of the device's resources, because a subscriber is free to dispose the
 * resource it is told about and that would rewrite the list underneath the loop.
 *
 * @param graphics_device The device.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_HANDLE` for a device that is not one, or an
 * error.
 */
CNA_C_API CNA_Result cna_graphics_device_notify_content_lost_resources_ext(
    CNA_Handle graphics_device);

/**
 * @brief Reports whether a surface format can be used as a render target here.
 *
 * Asks exactly the question `RenderTarget2D`'s constructor asks, so the two can never disagree: a
 * format this accepts is one the constructor accepts, and one it refuses is one the constructor
 * refuses.
 *
 * @param graphics_device The device.
 * @param format The format.
 * @param out_supported Receives the answer.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_HANDLE` for a device that is not one,
 * `CNA_RESULT_INVALID_ARGUMENT` for a null output, or an error.
 */
CNA_C_API CNA_Result cna_graphics_device_supports_surface_format_as_render_target_ext(
    CNA_Handle graphics_device, CNA_SurfaceFormat format, CNA_Bool* out_supported);

/**
 * @brief Reports whether this renderer actually executes a shader effect's source.
 *
 * **Not the same question as the `CustomEffects` capability**, and this is the one that matters
 * before writing a shader: SOFTWARE and HEADLESS *accept* any source and keep rendering with their
 * own fixed path, and Vulkan takes SPIR-V rather than GLSL. Asking only the capability is how a
 * pass reports success while drawing nothing.
 *
 * @param graphics_device The device.
 * @param out_executes Receives the answer.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_HANDLE` for a device that is not one,
 * `CNA_RESULT_INVALID_ARGUMENT` for a null output, or an error.
 */
CNA_C_API CNA_Result cna_graphics_device_executes_shader_effect_source_ext(
    CNA_Handle graphics_device, CNA_Bool* out_executes);

/**
 * @brief Reports whether this renderer can shade with an image-based light.
 *
 * The companion query to `CNA_GRAPHICS_CAPABILITY_CUSTOM_EFFECTS` for the image-based-lighting
 * path, on the same reasoning as @ref cna_graphics_device_executes_shader_effect_source_ext.
 *
 * @param graphics_device The device.
 * @param out_supported Receives the answer.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_HANDLE` for a device that is not one,
 * `CNA_RESULT_INVALID_ARGUMENT` for a null output, or an error.
 */
CNA_C_API CNA_Result cna_graphics_device_supports_image_based_lighting_ext(
    CNA_Handle graphics_device, CNA_Bool* out_supported);

/**
 * @brief Returns the colour space the display is currently in.
 *
 * @param graphics_device The device.
 * @param out_space Receives the colour space.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_HANDLE` for a device that is not one,
 * `CNA_RESULT_INVALID_ARGUMENT` for a null output, or an error.
 */
CNA_C_API CNA_Result cna_graphics_device_get_display_color_space_ext(
    CNA_Handle graphics_device, uint32_t* out_space);

/**
 * @brief Asks the display to change colour space.
 *
 * **Reports whether it worked rather than refusing**: a display that cannot enter a space is an
 * ordinary answer, not a caller mistake, so this succeeds with `out_changed` false. Only an
 * undefined colour-space identity is refused.
 *
 * @param graphics_device The device.
 * @param space The colour space.
 * @param out_changed Receives whether the display changed.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_HANDLE` for a device that is not one,
 * `CNA_RESULT_INVALID_ARGUMENT` for an undefined space or a null output, or an error.
 */
CNA_C_API CNA_Result cna_graphics_device_set_display_color_space_ext(
    CNA_Handle graphics_device, uint32_t space, CNA_Bool* out_changed);

/**
 * @brief Reports whether the display can enter a colour space.
 *
 * **Answered by trying it and putting it back**, because there is no separate query on the renderer
 * boundary and inventing one would let the two answers drift apart. A caller watching the display
 * may therefore observe a momentary change; the space in force afterwards is the one that was in
 * force before.
 *
 * @param graphics_device The device.
 * @param space The colour space.
 * @param out_supported Receives the answer.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_HANDLE` for a device that is not one,
 * `CNA_RESULT_INVALID_ARGUMENT` for an undefined space or a null output, or an error.
 */
CNA_C_API CNA_Result cna_graphics_device_supports_display_color_space_ext(
    CNA_Handle graphics_device, uint32_t space, CNA_Bool* out_supported);

/**
 * @brief Returns the largest compute work-group count along one axis.
 *
 * **An axis outside zero to two answers zero rather than being refused**, which is the canonical
 * behaviour: the answer to "how many groups along axis 7" is none, and a caller looping over axes
 * gets a usable number instead of an error to special-case.
 *
 * @param graphics_device The device.
 * @param axis The axis, zero through two.
 * @param out_count Receives the count, or zero for an axis outside the range.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_HANDLE` for a device that is not one,
 * `CNA_RESULT_INVALID_ARGUMENT` for a null output, or an error.
 */
CNA_C_API CNA_Result cna_graphics_device_get_max_compute_work_group_count_ext(
    CNA_Handle graphics_device, int32_t axis, int32_t* out_count);

/**
 * @brief Returns the largest compute work-group size along one axis.
 *
 * An axis outside zero to two answers zero, as above.
 *
 * @param graphics_device The device.
 * @param axis The axis, zero through two.
 * @param out_size Receives the size, or zero for an axis outside the range.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_HANDLE` for a device that is not one,
 * `CNA_RESULT_INVALID_ARGUMENT` for a null output, or an error.
 */
CNA_C_API CNA_Result cna_graphics_device_get_max_compute_work_group_size_ext(
    CNA_Handle graphics_device, int32_t axis, int32_t* out_size);

/**
 * @brief Returns the largest total number of invocations one work group may have.
 *
 * Not the product of the three per-axis sizes: a group may be within every axis limit and still
 * exceed this one.
 *
 * @param graphics_device The device.
 * @param out_invocations Receives the count.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_HANDLE` for a device that is not one,
 * `CNA_RESULT_INVALID_ARGUMENT` for a null output, or an error.
 */
CNA_C_API CNA_Result cna_graphics_device_get_max_compute_work_group_invocations_ext(
    CNA_Handle graphics_device, int32_t* out_invocations);

#ifdef __cplusplus
}
#endif

#endif
