// SPDX-License-Identifier: MS-PL

#ifndef CNA_C_DISPLAY_H
#define CNA_C_DISPLAY_H

#include "CNA/C/render_target.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Fully qualified native type name represented by @ref CNA_DisplayMode. */
#define CNA_DISPLAY_MODE_TYPE_NAME "Microsoft.Xna.Framework.Graphics.DisplayMode"
/** @brief Fully qualified native type name represented by adapter index/query functions. */
#define CNA_GRAPHICS_ADAPTER_TYPE_NAME "Microsoft.Xna.Framework.Graphics.GraphicsAdapter"
/** @brief Fully qualified native type name represented by @ref CNA_PresentationParameters. */
#define CNA_PRESENTATION_PARAMETERS_TYPE_NAME \
    "Microsoft.Xna.Framework.Graphics.PresentationParameters"

/** @brief Fixed-width graphics-profile identity. */
typedef uint32_t CNA_GraphicsProfile;
/** @brief Widest-compatibility graphics profile. */
#define CNA_GRAPHICS_PROFILE_REACH UINT32_C(0)
/** @brief Highest-feature graphics profile. */
#define CNA_GRAPHICS_PROFILE_HI_DEF UINT32_C(1)

/** @brief Fixed-width presentation-interval identity. */
typedef uint32_t CNA_PresentInterval;
/** @brief Uses the platform default, equivalent to one retrace. */
#define CNA_PRESENT_INTERVAL_DEFAULT UINT32_C(0)
/** @brief Waits for one vertical retrace. */
#define CNA_PRESENT_INTERVAL_ONE UINT32_C(1)
/** @brief Waits for two vertical retraces. */
#define CNA_PRESENT_INTERVAL_TWO UINT32_C(2)
/** @brief Presents without waiting for retrace. */
#define CNA_PRESENT_INTERVAL_IMMEDIATE UINT32_C(3)

/** @brief Fixed-width display-orientation bit set. */
typedef uint32_t CNA_DisplayOrientation;
/** @brief Default display orientation. */
#define CNA_DISPLAY_ORIENTATION_DEFAULT UINT32_C(0)
/** @brief Counter-clockwise landscape orientation. */
#define CNA_DISPLAY_ORIENTATION_LANDSCAPE_LEFT UINT32_C(1)
/** @brief Clockwise landscape orientation. */
#define CNA_DISPLAY_ORIENTATION_LANDSCAPE_RIGHT UINT32_C(2)
/** @brief Portrait orientation. */
#define CNA_DISPLAY_ORIENTATION_PORTRAIT UINT32_C(4)

/** @brief Safe fixed-width placeholder for a native IntPtr that is never disclosed by this ABI. */
typedef uint64_t CNA_NativeHandleValue;

/** @brief Value representation of a native DisplayMode. */
typedef struct CNA_DisplayMode {
    /** @brief Size of this structure in bytes. */
    uint32_t struct_size;
    /** @brief Version of this structure. */
    uint32_t struct_version;
    /** @brief Display width in pixels. */
    int32_t width;
    /** @brief Display height in pixels. */
    int32_t height;
    /** @brief Width divided by height, or zero when height is zero. */
    float aspect_ratio;
    /** @brief Display surface format. */
    CNA_SurfaceFormat format;
} CNA_DisplayMode;

/** @brief Point-in-time graphics-adapter metadata. */
typedef struct CNA_GraphicsAdapterInfo {
    /** @brief Size of this structure in bytes. */
    uint32_t struct_size;
    /** @brief Version of this structure. */
    uint32_t struct_version;
    /** @brief Zero-based adapter index used by all adapter query functions. */
    uint32_t adapter_index;
    /** @brief Whether this is the default adapter. */
    CNA_Bool is_default_adapter;
    /** @brief Whether its current mode is wider than 4:3. */
    CNA_Bool is_wide_screen;
    /** @brief Current null-device selection flag. */
    CNA_Bool use_null_device;
    /** @brief Current reference-device selection flag. */
    CNA_Bool use_reference_device;
    /** @brief PCI vendor identifier, or zero when unavailable. */
    int32_t vendor_id;
    /** @brief PCI device identifier, or zero when unavailable. */
    int32_t device_id;
    /** @brief Adapter revision; current CNA returns zero. */
    int32_t revision;
    /** @brief Adapter subsystem identifier; current CNA returns zero. */
    int32_t subsystem_id;
    /** @brief UTF-8 description byte count without a terminator. */
    uint64_t description_byte_length;
    /** @brief UTF-8 device-name byte count without a terminator. */
    uint64_t device_name_byte_length;
} CNA_GraphicsAdapterInfo;

/** @brief Result of an adapter backbuffer/render-target format negotiation. */
typedef struct CNA_GraphicsFormatSelection {
    /** @brief Size of this structure in bytes. */
    uint32_t struct_size;
    /** @brief Version of this structure. */
    uint32_t struct_version;
    /** @brief Whether every requested value was accepted without substitution. */
    CNA_Bool exact_match;
    /** @brief Reserved bytes; returned as zero. */
    uint8_t reserved[3];
    /** @brief Selected color format. */
    CNA_SurfaceFormat format;
    /** @brief Selected depth/stencil format. */
    CNA_DepthFormat depth_format;
    /** @brief Selected multisample count. */
    int32_t multi_sample_count;
} CNA_GraphicsFormatSelection;

/** @brief C-safe value representation of native PresentationParameters. */
typedef struct CNA_PresentationParameters {
    /** @brief Size of this structure in bytes. */
    uint32_t struct_size;
    /** @brief Version of this structure. */
    uint32_t struct_version;
    /** @brief Backbuffer color format. */
    CNA_SurfaceFormat back_buffer_format;
    /** @brief Backbuffer width in pixels. */
    int32_t back_buffer_width;
    /** @brief Backbuffer height in pixels. */
    int32_t back_buffer_height;
    /** @brief Backbuffer depth/stencil format. */
    CNA_DepthFormat depth_stencil_format;
    /** @brief Requested multisample count. */
    int32_t multi_sample_count;
    /** @brief Presentation interval. */
    CNA_PresentInterval presentation_interval;
    /** @brief Display-orientation bit set. */
    CNA_DisplayOrientation display_orientation;
    /** @brief Backbuffer preservation policy. */
    CNA_RenderTargetUsage render_target_usage;
    /** @brief Whether fullscreen presentation is requested. */
    CNA_Bool is_full_screen;
    /** @brief CNA extension requesting off-screen/no-window operation. */
    CNA_Bool headless_ext;
    /** @brief Reserved bytes; must be zero. */
    uint8_t reserved[2];
} CNA_PresentationParameters;

/**
 * @brief Initializes a DisplayMode value and computes its aspect ratio.
 *
 * @param width Display width in pixels.
 * @param height Display height in pixels.
 * @param format Surface format.
 * @param out_mode Receives a complete version-one value.
 * @return `CNA_RESULT_SUCCESS` or `CNA_RESULT_INVALID_ARGUMENT`.
 */
CNA_C_API CNA_Result cna_display_mode_init(
    int32_t width,
    int32_t height,
    CNA_SurfaceFormat format,
    CNA_DisplayMode* out_mode);

/**
 * @brief Compares two DisplayMode values by width, height and format.
 *
 * @param left First versioned value.
 * @param right Second versioned value.
 * @param out_equal Receives true when equal.
 * @return `CNA_RESULT_SUCCESS` or `CNA_RESULT_INVALID_ARGUMENT`.
 */
CNA_C_API CNA_Result cna_display_mode_equals(
    const CNA_DisplayMode* left,
    const CNA_DisplayMode* right,
    CNA_Bool* out_equal);

/**
 * @brief Gets the number of currently enumerated graphics adapters.
 *
 * @param graphics_device Callback-scoped device proving active runtime/thread context.
 * @param out_count Receives the adapter count.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_graphics_adapter_get_count(
    CNA_Handle graphics_device,
    uint64_t* out_count);

/**
 * @brief Gets the current metadata for one adapter index.
 *
 * @param graphics_device Callback-scoped graphics-device handle.
 * @param adapter_index Zero-based adapter index.
 * @param out_info Caller-initialized versioned output structure.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_graphics_adapter_get_info(
    CNA_Handle graphics_device,
    uint32_t adapter_index,
    CNA_GraphicsAdapterInfo* out_info);

/**
 * @brief Copies an adapter's UTF-8 description without a terminator.
 *
 * @param graphics_device Callback-scoped graphics-device handle.
 * @param adapter_index Zero-based adapter index.
 * @param destination Caller-owned bytes, or null only when @p capacity is zero.
 * @param capacity Destination capacity in bytes.
 * @param out_bytes Receives the exact required byte count.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL`, or a documented failure.
 */
CNA_C_API CNA_Result cna_graphics_adapter_copy_description(
    CNA_Handle graphics_device,
    uint32_t adapter_index,
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

/**
 * @brief Copies an adapter's UTF-8 device/display name without a terminator.
 *
 * @param graphics_device Callback-scoped graphics-device handle.
 * @param adapter_index Zero-based adapter index.
 * @param destination Caller-owned bytes, or null only when @p capacity is zero.
 * @param capacity Destination capacity in bytes.
 * @param out_bytes Receives the exact required byte count.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL`, or a documented failure.
 */
CNA_C_API CNA_Result cna_graphics_adapter_copy_device_name(
    CNA_Handle graphics_device,
    uint32_t adapter_index,
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

/**
 * @brief Gets one adapter's current display mode.
 *
 * @param graphics_device Callback-scoped graphics-device handle.
 * @param adapter_index Zero-based adapter index.
 * @param out_mode Caller-initialized versioned output structure.
 * @return `CNA_RESULT_SUCCESS` or a documented failure.
 */
CNA_C_API CNA_Result cna_graphics_adapter_get_current_display_mode(
    CNA_Handle graphics_device,
    uint32_t adapter_index,
    CNA_DisplayMode* out_mode);

/**
 * @brief Gets one adapter's supported display-mode count, optionally filtered by format.
 *
 * @param graphics_device Callback-scoped graphics-device handle.
 * @param adapter_index Zero-based adapter index.
 * @param filter_by_format Whether to apply @p format.
 * @param format Surface format used when filtering.
 * @param out_count Receives the exact count.
 * @return `CNA_RESULT_SUCCESS` or a documented failure.
 */
CNA_C_API CNA_Result cna_graphics_adapter_get_display_mode_count(
    CNA_Handle graphics_device,
    uint32_t adapter_index,
    CNA_Bool filter_by_format,
    CNA_SurfaceFormat format,
    uint64_t* out_count);

/**
 * @brief Copies one adapter's supported display modes, optionally filtered by format.
 *
 * @param graphics_device Callback-scoped graphics-device handle.
 * @param adapter_index Zero-based adapter index.
 * @param filter_by_format Whether to apply @p format.
 * @param format Surface format used when filtering.
 * @param destination Caller-owned array, or null only when @p capacity is zero.
 * @param capacity Destination capacity in display-mode elements.
 * @param out_count Receives the exact required count.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL`, or a documented failure.
 */
CNA_C_API CNA_Result cna_graphics_adapter_copy_display_modes(
    CNA_Handle graphics_device,
    uint32_t adapter_index,
    CNA_Bool filter_by_format,
    CNA_SurfaceFormat format,
    CNA_DisplayMode* destination,
    uint64_t capacity,
    uint64_t* out_count);

/**
 * @brief Gets or changes the adapter's null/reference device flags.
 *
 * @param graphics_device Callback-scoped graphics-device handle.
 * @param adapter_index Zero-based adapter index.
 * @param use_null_device New null-device flag.
 * @param use_reference_device New reference-device flag.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_graphics_adapter_set_device_preferences(
    CNA_Handle graphics_device,
    uint32_t adapter_index,
    CNA_Bool use_null_device,
    CNA_Bool use_reference_device);

/**
 * @brief Queries graphics-profile support for one adapter.
 *
 * @param graphics_device Callback-scoped graphics-device handle.
 * @param adapter_index Zero-based adapter index.
 * @param profile Requested profile.
 * @param out_supported Receives true when supported.
 * @return `CNA_RESULT_SUCCESS` or a documented failure.
 */
CNA_C_API CNA_Result cna_graphics_adapter_is_profile_supported(
    CNA_Handle graphics_device,
    uint32_t adapter_index,
    CNA_GraphicsProfile profile,
    CNA_Bool* out_supported);

/**
 * @brief Negotiates a render-target format through the native adapter.
 *
 * @param graphics_device Callback-scoped graphics-device handle.
 * @param adapter_index Zero-based adapter index.
 * @param profile Requested graphics profile.
 * @param format Requested color format.
 * @param depth_format Requested depth format.
 * @param multi_sample_count Requested multisample count.
 * @param out_selection Caller-initialized output structure.
 * @return `CNA_RESULT_SUCCESS` or a documented failure.
 */
CNA_C_API CNA_Result cna_graphics_adapter_query_render_target_format(
    CNA_Handle graphics_device,
    uint32_t adapter_index,
    CNA_GraphicsProfile profile,
    CNA_SurfaceFormat format,
    CNA_DepthFormat depth_format,
    int32_t multi_sample_count,
    CNA_GraphicsFormatSelection* out_selection);

/**
 * @brief Negotiates a backbuffer format through the native adapter.
 *
 * @param graphics_device Callback-scoped graphics-device handle.
 * @param adapter_index Zero-based adapter index.
 * @param profile Requested graphics profile.
 * @param format Requested color format.
 * @param depth_format Requested depth format.
 * @param multi_sample_count Requested multisample count.
 * @param out_selection Caller-initialized output structure.
 * @return `CNA_RESULT_SUCCESS` or a documented failure.
 */
CNA_C_API CNA_Result cna_graphics_adapter_query_backbuffer_format(
    CNA_Handle graphics_device,
    uint32_t adapter_index,
    CNA_GraphicsProfile profile,
    CNA_SurfaceFormat format,
    CNA_DepthFormat depth_format,
    int32_t multi_sample_count,
    CNA_GraphicsFormatSelection* out_selection);

/**
 * @brief Reports the native-monitor-handle mapping as unavailable at the stable C boundary.
 *
 * @param graphics_device Callback-scoped graphics-device handle.
 * @param adapter_index Zero-based adapter index.
 * @param out_value Receives zero.
 * @return `CNA_RESULT_NOT_SUPPORTED` after validating context and adapter index.
 */
CNA_C_API CNA_Result cna_graphics_adapter_get_native_monitor_handle(
    CNA_Handle graphics_device,
    uint32_t adapter_index,
    CNA_NativeHandleValue* out_value);

/**
 * @brief Reports global adapter-cache invalidation as unavailable while a C game owns a device.
 *
 * @param graphics_device Callback-scoped graphics-device handle.
 * @return `CNA_RESULT_NOT_SUPPORTED`; invalidating native adapter objects would leave the active
 * GraphicsDevice's native adapter reference dangling.
 */
CNA_C_API CNA_Result cna_graphics_adapters_refresh(CNA_Handle graphics_device);

/**
 * @brief Initializes PresentationParameters with XNA-compatible defaults.
 *
 * @param out_parameters Receives a complete version-one value.
 * @return `CNA_RESULT_SUCCESS` or `CNA_RESULT_INVALID_ARGUMENT`.
 */
CNA_C_API CNA_Result cna_presentation_parameters_init(
    CNA_PresentationParameters* out_parameters);

/**
 * @brief Clones PresentationParameters through the native Clone implementation.
 *
 * @param source Valid versioned source value.
 * @param out_parameters Receives the clone.
 * @return `CNA_RESULT_SUCCESS` or `CNA_RESULT_INVALID_ARGUMENT`.
 */
CNA_C_API CNA_Result cna_presentation_parameters_clone(
    const CNA_PresentationParameters* source,
    CNA_PresentationParameters* out_parameters);

/**
 * @brief Gets the rectangle derived from presentation backbuffer dimensions.
 *
 * @param parameters Valid versioned presentation value.
 * @param out_bounds Receives origin zero plus width and height.
 * @return `CNA_RESULT_SUCCESS` or `CNA_RESULT_INVALID_ARGUMENT`.
 */
CNA_C_API CNA_Result cna_presentation_parameters_get_bounds(
    const CNA_PresentationParameters* parameters,
    CNA_Rectangle* out_bounds);

/**
 * @brief Gets the active device's applied presentation parameters.
 *
 * @param graphics_device Callback-scoped graphics-device handle.
 * @param out_parameters Caller-initialized versioned output structure.
 * @return `CNA_RESULT_SUCCESS` or a documented failure.
 */
CNA_C_API CNA_Result cna_graphics_device_get_presentation_parameters(
    CNA_Handle graphics_device,
    CNA_PresentationParameters* out_parameters);

/**
 * @brief Applies C-safe presentation parameters while retaining the native window association.
 *
 * @param graphics_device Callback-scoped graphics-device handle.
 * @param parameters Valid versioned source value.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED`, or a documented failure.
 */
CNA_C_API CNA_Result cna_graphics_device_set_presentation_parameters(
    CNA_Handle graphics_device,
    const CNA_PresentationParameters* parameters);

/**
 * @brief Gets the graphics device's current display mode.
 *
 * @param graphics_device Callback-scoped graphics-device handle.
 * @param out_mode Caller-initialized versioned output structure.
 * @return `CNA_RESULT_SUCCESS` or a documented failure.
 */
CNA_C_API CNA_Result cna_graphics_device_get_display_mode(
    CNA_Handle graphics_device,
    CNA_DisplayMode* out_mode);

/**
 * @brief Reports the native-window-handle mapping as unavailable at the stable C boundary.
 *
 * @param graphics_device Callback-scoped graphics-device handle.
 * @param out_value Receives zero.
 * @return `CNA_RESULT_NOT_SUPPORTED` after validating the device context.
 */
CNA_C_API CNA_Result cna_graphics_device_get_native_window_handle(
    CNA_Handle graphics_device,
    CNA_NativeHandleValue* out_value);

#ifdef __cplusplus
}
#endif

#endif
