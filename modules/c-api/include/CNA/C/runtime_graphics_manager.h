// SPDX-License-Identifier: MS-PL

#ifndef CNA_C_RUNTIME_GRAPHICS_MANAGER_H
#define CNA_C_RUNTIME_GRAPHICS_MANAGER_H

#include "CNA/C/display.h"
#include "CNA/C/runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Fixed-width identity of how a back buffer is presented into a window. */
typedef uint32_t CNA_PresentationMode;

/** @brief Preserve the aspect ratio with black bars. */
#define CNA_PRESENTATION_MODE_LETTERBOX UINT32_C(0)
/** @brief Preserve the aspect ratio by cropping. */
#define CNA_PRESENTATION_MODE_OVERSCAN UINT32_C(1)
/** @brief Fill the window, ignoring the aspect ratio. */
#define CNA_PRESENTATION_MODE_STRETCH UINT32_C(2)
/** @brief Present at the window's own size with no scaling. */
#define CNA_PRESENTATION_MODE_NATIVE_BACK_BUFFER UINT32_C(3)
/** @brief Keep the height and let the width follow the window. */
#define CNA_PRESENTATION_MODE_FIXED_HEIGHT_DYNAMIC_WIDTH UINT32_C(4)
/** @brief Highest defined presentation-mode identity. */
#define CNA_PRESENTATION_MODE_MAXIMUM CNA_PRESENTATION_MODE_FIXED_HEIGHT_DYNAMIC_WIDTH

/** @brief Default preferred back-buffer width of a graphics device manager. */
#define CNA_GRAPHICS_DEVICE_MANAGER_DEFAULT_BACK_BUFFER_WIDTH INT32_C(800)
/** @brief Default preferred back-buffer height of a graphics device manager. */
#define CNA_GRAPHICS_DEVICE_MANAGER_DEFAULT_BACK_BUFFER_HEIGHT INT32_C(480)

/**
 * @brief One candidate device configuration.
 *
 * The canonical value names an adapter by pointer; C names it by the **index** every adapter query
 * in this ABI already uses, because a pointer into the runtime's adapter list is nothing a C caller
 * could hold safely. A negative index means no adapter is selected.
 */
typedef struct CNA_GraphicsDeviceInformation {
    /** @brief Size of this caller-provided structure in bytes. */
    uint32_t struct_size;

    /** @brief Version of this caller-provided structure. */
    uint32_t struct_version;

    /** @brief Zero-based adapter index, or a negative value for no adapter. */
    int32_t adapter_index;

    /** @brief Requested `CNA_GRAPHICS_PROFILE_*` identity. */
    CNA_GraphicsProfile graphics_profile;

    /** @brief Requested presentation parameters. */
    CNA_PresentationParameters presentation_parameters;
} CNA_GraphicsDeviceInformation;

/** @brief Owned handle to one graphics device manager. */
typedef CNA_Handle CNA_GraphicsDeviceManagerHandle;

/** @brief Fixed-width identity of a graphics device manager event. */
typedef uint32_t CNA_GraphicsDeviceManagerEvent;

/** @brief The manager was disposed. */
#define CNA_GRAPHICS_DEVICE_MANAGER_EVENT_DISPOSED UINT32_C(0)
/** @brief A graphics device was created. */
#define CNA_GRAPHICS_DEVICE_MANAGER_EVENT_DEVICE_CREATED UINT32_C(1)
/** @brief The graphics device is about to be disposed. */
#define CNA_GRAPHICS_DEVICE_MANAGER_EVENT_DEVICE_DISPOSING UINT32_C(2)
/** @brief The graphics device finished resetting. */
#define CNA_GRAPHICS_DEVICE_MANAGER_EVENT_DEVICE_RESET UINT32_C(3)
/** @brief The graphics device is about to reset. */
#define CNA_GRAPHICS_DEVICE_MANAGER_EVENT_DEVICE_RESETTING UINT32_C(4)
/** @brief Highest defined manager-event identity that carries no data. */
#define CNA_GRAPHICS_DEVICE_MANAGER_EVENT_MAXIMUM CNA_GRAPHICS_DEVICE_MANAGER_EVENT_DEVICE_RESETTING

/**
 * @brief Handler invoked while device settings are being prepared.
 *
 * @param information The candidate configuration, borrowed for the duration of the call.
 * @param context The caller context supplied at subscription time.
 *
 * **This is an observation, not a veto, and that is a canonical limitation rather than a choice made
 * here.** In XNA this event is how an application overrides the settings before the device is
 * created; in this runtime the event-handler collection delivers its argument as a `const`
 * reference, so a C++ subscriber cannot reach the argument type's mutable accessor either. This ABI
 * reports what a subscriber can actually do rather than inventing a power the canonical event does
 * not grant. Change the settings through the manager's own preference routes and
 * `cna_graphics_device_manager_apply_changes` instead.
 */
typedef void (*CNA_PreparingDeviceSettingsCallback)(
    const CNA_GraphicsDeviceInformation* information,
    void* context);

/**
 * @brief Initializes a device configuration to the canonical default.
 *
 * @param out_information Receives a configuration with no adapter and default parameters.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null output.
 *
 * This pure POD operation touches no runtime state and may run on any thread.
 */
CNA_C_API CNA_Result cna_graphics_device_information_init(
    CNA_GraphicsDeviceInformation* out_information);

/**
 * @brief Copies a device configuration.
 *
 * @param information The configuration to copy.
 * @param out_information Receives the copy.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null or invalid argument.
 *
 * The canonical clone returns a value, and a C value copies itself; this route exists so the
 * canonical operation has a name in C and so the copy is validated on the way through.
 */
CNA_C_API CNA_Result cna_graphics_device_information_clone(
    const CNA_GraphicsDeviceInformation* information,
    CNA_GraphicsDeviceInformation* out_information);

/**
 * @brief Returns the byte count of the configuration type's .NET type name.
 *
 * @param out_bytes Receives the required byte count, without a terminator.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null output.
 */
CNA_C_API CNA_Result cna_graphics_device_information_get_type_name_size(uint64_t* out_bytes);

/**
 * @brief Copies the configuration type's fully-qualified .NET type name.
 *
 * @param destination Buffer receiving the UTF-8 bytes; may be null only when @p capacity is zero.
 * @param capacity Bytes available in @p destination.
 * @param out_bytes Always receives the required byte count, without a terminator.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL` with **no partial write**, or
 *         `CNA_RESULT_INVALID_ARGUMENT`.
 */
CNA_C_API CNA_Result cna_graphics_device_information_copy_type_name(
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

/**
 * @brief Creates the graphics device manager for a game.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param out_manager Receives an owned manager handle.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` when the game already has a manager,
 *         or a documented handle/thread/native failure.
 *
 * Creating the manager **registers it as the game's graphics device manager and graphics device
 * service**, which is what `cna_game_services_contains_ext` then reports. A game accepts exactly
 * one; the canonical constructor refuses a second. Release it before the game.
 */
CNA_C_API CNA_Result cna_graphics_device_manager_create(
    CNA_Handle game,
    CNA_GraphicsDeviceManagerHandle* out_manager);

/**
 * @brief Returns the requested graphics profile.
 *
 * @param manager Owned manager handle.
 * @param out_profile Receives one `CNA_GRAPHICS_PROFILE_*` identity.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_graphics_device_manager_get_graphics_profile(
    CNA_GraphicsDeviceManagerHandle manager,
    CNA_GraphicsProfile* out_profile);

/**
 * @brief Requests a graphics profile.
 *
 * @param manager Owned manager handle.
 * @param profile One `CNA_GRAPHICS_PROFILE_*` identity.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for an undefined identity, or a
 *         documented handle/thread failure.
 *
 * Every preference route here records a request; `cna_graphics_device_manager_apply_changes` is what
 * acts on it.
 */
CNA_C_API CNA_Result cna_graphics_device_manager_set_graphics_profile(
    CNA_GraphicsDeviceManagerHandle manager,
    CNA_GraphicsProfile profile);

/**
 * @brief Borrows the graphics device the manager manages.
 *
 * @param manager Owned manager handle.
 * @param out_graphics_device Receives the callback-scoped borrowed device handle.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_STATE` outside a lifecycle callback or when no
 *         device exists, or a documented argument/handle/thread failure.
 *
 * The device is the game's own, borrowed on the same terms as `cna_game_get_graphics_device`.
 */
CNA_C_API CNA_Result cna_graphics_device_manager_get_graphics_device(
    CNA_GraphicsDeviceManagerHandle manager,
    CNA_Handle* out_graphics_device);

/**
 * @brief Reports whether full-screen presentation is requested.
 *
 * @param manager Owned manager handle.
 * @param out_full_screen Receives `CNA_TRUE` when full screen is requested.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_graphics_device_manager_get_is_full_screen(
    CNA_GraphicsDeviceManagerHandle manager,
    CNA_Bool* out_full_screen);

/**
 * @brief Requests windowed or full-screen presentation.
 *
 * @param manager Owned manager handle.
 * @param full_screen Whether full screen is requested.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread failure.
 */
CNA_C_API CNA_Result cna_graphics_device_manager_set_is_full_screen(
    CNA_GraphicsDeviceManagerHandle manager,
    CNA_Bool full_screen);

/**
 * @brief Reports whether multisampling is preferred.
 *
 * @param manager Owned manager handle.
 * @param out_prefer Receives `CNA_TRUE` when multisampling is preferred.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_graphics_device_manager_get_prefer_multi_sampling(
    CNA_GraphicsDeviceManagerHandle manager,
    CNA_Bool* out_prefer);

/**
 * @brief Requests or declines multisampling.
 *
 * @param manager Owned manager handle.
 * @param prefer Whether multisampling is preferred.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread failure.
 */
CNA_C_API CNA_Result cna_graphics_device_manager_set_prefer_multi_sampling(
    CNA_GraphicsDeviceManagerHandle manager,
    CNA_Bool prefer);

/**
 * @brief Returns the preferred back-buffer color format.
 *
 * @param manager Owned manager handle.
 * @param out_format Receives one `CNA_SURFACE_FORMAT_*` identity.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_graphics_device_manager_get_preferred_back_buffer_format(
    CNA_GraphicsDeviceManagerHandle manager,
    CNA_SurfaceFormat* out_format);

/**
 * @brief Requests a back-buffer color format.
 *
 * @param manager Owned manager handle.
 * @param format One `CNA_SURFACE_FORMAT_*` identity.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for an undefined identity, or a
 *         documented handle/thread failure.
 */
CNA_C_API CNA_Result cna_graphics_device_manager_set_preferred_back_buffer_format(
    CNA_GraphicsDeviceManagerHandle manager,
    CNA_SurfaceFormat format);

/**
 * @brief Returns the preferred back-buffer width in pixels.
 *
 * @param manager Owned manager handle.
 * @param out_width Receives the width.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_graphics_device_manager_get_preferred_back_buffer_width(
    CNA_GraphicsDeviceManagerHandle manager,
    int32_t* out_width);

/**
 * @brief Requests a back-buffer width in pixels.
 *
 * @param manager Owned manager handle.
 * @param width Requested width.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread failure.
 *
 * The canonical setter records whatever it is given, including a value that no adapter can present;
 * negotiation happens when the changes are applied, not here.
 */
CNA_C_API CNA_Result cna_graphics_device_manager_set_preferred_back_buffer_width(
    CNA_GraphicsDeviceManagerHandle manager,
    int32_t width);

/**
 * @brief Returns the preferred back-buffer height in pixels.
 *
 * @param manager Owned manager handle.
 * @param out_height Receives the height.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_graphics_device_manager_get_preferred_back_buffer_height(
    CNA_GraphicsDeviceManagerHandle manager,
    int32_t* out_height);

/**
 * @brief Requests a back-buffer height in pixels.
 *
 * @param manager Owned manager handle.
 * @param height Requested height.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread failure.
 */
CNA_C_API CNA_Result cna_graphics_device_manager_set_preferred_back_buffer_height(
    CNA_GraphicsDeviceManagerHandle manager,
    int32_t height);

/**
 * @brief Returns the preferred depth/stencil format.
 *
 * @param manager Owned manager handle.
 * @param out_format Receives one `CNA_DEPTH_FORMAT_*` identity.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_graphics_device_manager_get_preferred_depth_stencil_format(
    CNA_GraphicsDeviceManagerHandle manager,
    CNA_DepthFormat* out_format);

/**
 * @brief Requests a depth/stencil format.
 *
 * @param manager Owned manager handle.
 * @param format One `CNA_DEPTH_FORMAT_*` identity.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for an undefined identity, or a
 *         documented handle/thread failure.
 */
CNA_C_API CNA_Result cna_graphics_device_manager_set_preferred_depth_stencil_format(
    CNA_GraphicsDeviceManagerHandle manager,
    CNA_DepthFormat format);

/**
 * @brief Reports whether presentation waits for the vertical retrace.
 *
 * @param manager Owned manager handle.
 * @param out_synchronize Receives `CNA_TRUE` when presentation waits.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_graphics_device_manager_get_synchronize_with_vertical_retrace(
    CNA_GraphicsDeviceManagerHandle manager,
    CNA_Bool* out_synchronize);

/**
 * @brief Requests waiting, or not waiting, for the vertical retrace.
 *
 * @param manager Owned manager handle.
 * @param synchronize Whether presentation waits.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread failure.
 */
CNA_C_API CNA_Result cna_graphics_device_manager_set_synchronize_with_vertical_retrace(
    CNA_GraphicsDeviceManagerHandle manager,
    CNA_Bool synchronize);

/**
 * @brief Returns the display orientations the game supports.
 *
 * @param manager Owned manager handle.
 * @param out_orientations Receives a `CNA_DISPLAY_ORIENTATION_*` bit set.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_graphics_device_manager_get_supported_orientations(
    CNA_GraphicsDeviceManagerHandle manager,
    CNA_DisplayOrientation* out_orientations);

/**
 * @brief Declares the display orientations the game supports.
 *
 * @param manager Owned manager handle.
 * @param orientations A `CNA_DISPLAY_ORIENTATION_*` bit set.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread failure.
 *
 * This is a **bit set**, so it is not validated against a single identity: the canonical setter
 * accepts any combination and the window applies what it can.
 */
CNA_C_API CNA_Result cna_graphics_device_manager_set_supported_orientations(
    CNA_GraphicsDeviceManagerHandle manager,
    CNA_DisplayOrientation orientations);

/**
 * @brief Returns how the back buffer is presented into the window.
 *
 * @param manager Owned manager handle.
 * @param out_mode Receives one `CNA_PRESENTATION_MODE_*` identity.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_graphics_device_manager_get_preferred_presentation_mode_ext(
    CNA_GraphicsDeviceManagerHandle manager,
    CNA_PresentationMode* out_mode);

/**
 * @brief Chooses how the back buffer is presented into the window.
 *
 * @param manager Owned manager handle.
 * @param mode One `CNA_PRESENTATION_MODE_*` identity.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for an undefined identity, or a
 *         documented handle/thread failure.
 */
CNA_C_API CNA_Result cna_graphics_device_manager_set_preferred_presentation_mode_ext(
    CNA_GraphicsDeviceManagerHandle manager,
    CNA_PresentationMode mode);

/**
 * @brief Applies every recorded preference to the device and the window.
 *
 * @param manager Owned manager handle.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_PLATFORM` when the platform refuses the reconfiguration,
 *         or a documented handle/thread failure.
 *
 * This is where a preference becomes a device reset and a window change. Applying with nothing
 * changed is a cheap no-op, which is the canonical behavior rather than a refusal.
 */
CNA_C_API CNA_Result cna_graphics_device_manager_apply_changes(
    CNA_GraphicsDeviceManagerHandle manager);

/**
 * @brief Switches between windowed and full-screen presentation.
 *
 * @param manager Owned manager handle.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_PLATFORM` when the platform refuses, or a documented
 *         handle/thread failure.
 */
CNA_C_API CNA_Result cna_graphics_device_manager_toggle_full_screen(
    CNA_GraphicsDeviceManagerHandle manager);

/**
 * @brief Creates or re-creates the managed device.
 *
 * @param manager Owned manager handle.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_PLATFORM` when the platform refuses, or a documented
 *         handle/thread failure.
 *
 * The game calls this itself while initializing, so a caller normally never does; it is here because
 * the canonical operation is public. In this runtime the game already owns its device, so this
 * re-applies the configuration rather than allocating a new device.
 */
CNA_C_API CNA_Result cna_graphics_device_manager_create_device(
    CNA_GraphicsDeviceManagerHandle manager);

/**
 * @brief Reports whether the frame may draw, from the manager's point of view.
 *
 * @param manager Owned manager handle.
 * @param out_should_draw Receives `CNA_TRUE` when drawing may proceed.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 *
 * The game calls this once per frame before drawing; a caller that drives its own loop can too.
 */
CNA_C_API CNA_Result cna_graphics_device_manager_begin_draw(
    CNA_GraphicsDeviceManagerHandle manager,
    CNA_Bool* out_should_draw);

/**
 * @brief Presents the frame the manager's device drew.
 *
 * @param manager Owned manager handle.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_PLATFORM` when the platform refuses, or a documented
 *         handle/thread failure.
 */
CNA_C_API CNA_Result cna_graphics_device_manager_end_draw(
    CNA_GraphicsDeviceManagerHandle manager);

/**
 * @brief Disposes the manager without releasing its handle.
 *
 * @param manager Owned manager handle.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread failure.
 *
 * Disposal unregisters both services and raises the disposed event once; a second disposal is a
 * no-op, which is this canonical type's own idempotence.
 */
CNA_C_API CNA_Result cna_graphics_device_manager_dispose(CNA_GraphicsDeviceManagerHandle manager);

/**
 * @brief Returns the byte count of the manager's .NET type name.
 *
 * @param manager Owned manager handle.
 * @param out_bytes Receives the required byte count, without a terminator.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_graphics_device_manager_get_type_name_size(
    CNA_GraphicsDeviceManagerHandle manager,
    uint64_t* out_bytes);

/**
 * @brief Copies the manager's fully-qualified .NET type name.
 *
 * @param manager Owned manager handle.
 * @param destination Buffer receiving the UTF-8 bytes; may be null only when @p capacity is zero.
 * @param capacity Bytes available in @p destination.
 * @param out_bytes Always receives the required byte count, without a terminator.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL` with **no partial write**,
 *         `CNA_RESULT_INVALID_ARGUMENT`, or a documented handle/thread failure.
 */
CNA_C_API CNA_Result cna_graphics_device_manager_copy_type_name(
    CNA_GraphicsDeviceManagerHandle manager,
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

/**
 * @brief Subscribes to one of the manager's data-free events.
 *
 * @param manager Owned manager handle.
 * @param event One `CNA_GRAPHICS_DEVICE_MANAGER_EVENT_*` identity.
 * @param callback Handler invoked when the event is raised.
 * @param context Caller context passed back to @p callback.
 * @param out_registration Receives an owned registration handle.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for an undefined identity or a null
 *         handler, or a documented handle/thread failure.
 *
 * The four device events are also the canonical graphics-device-service events, so subscribing here
 * is what a consumer of that service would observe. Release with `cna_game_unsubscribe`, which
 * releases every runtime registration in this ABI.
 */
CNA_C_API CNA_Result cna_graphics_device_manager_subscribe(
    CNA_GraphicsDeviceManagerHandle manager,
    CNA_GraphicsDeviceManagerEvent event,
    CNA_GameEventCallback callback,
    void* context,
    CNA_GameEventRegistrationHandle* out_registration);

/**
 * @brief Subscribes to the manager's device-settings event.
 *
 * @param manager Owned manager handle.
 * @param callback Handler invoked with the candidate configuration.
 * @param context Caller context passed back to @p callback.
 * @param out_registration Receives an owned registration handle.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for a null handler, or a documented
 *         handle/thread failure.
 *
 * This is the one manager event that carries data. See @ref CNA_PreparingDeviceSettingsCallback for
 * why the data is read-only: the canonical event delivers a `const` reference, so no subscriber in
 * this runtime can change the settings through it.
 */
CNA_C_API CNA_Result cna_graphics_device_manager_subscribe_preparing_device_settings(
    CNA_GraphicsDeviceManagerHandle manager,
    CNA_PreparingDeviceSettingsCallback callback,
    void* context,
    CNA_GameEventRegistrationHandle* out_registration);

/**
 * @brief Releases a manager handle.
 *
 * @param manager Owned manager handle.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread failure.
 *
 * Releasing disposes the manager, which unregisters both services, and then **keeps the object
 * alive until the game is destroyed**. That is not tidiness: the canonical game caches a raw pointer
 * to the graphics device service the first time it resolves one and never clears it, so destroying
 * the manager while its game still lives would leave the game dereferencing freed memory on its very
 * next frame. A disposed manager still answers that cached pointer correctly, because disposal does
 * not touch the game-owned device it points at. The handle is invalid immediately either way.
 */
CNA_C_API CNA_Result cna_graphics_device_manager_destroy(CNA_GraphicsDeviceManagerHandle manager);

#ifdef __cplusplus
}
#endif

#endif
