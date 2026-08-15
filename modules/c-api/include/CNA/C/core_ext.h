// SPDX-License-Identifier: MS-PL

#ifndef CNA_C_CORE_EXT_H
#define CNA_C_CORE_EXT_H

#include "CNA/C/core.h"
#include "CNA/C/graphics.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Fixed-width severity level of a log message.
 *
 * The values are the canonical ordinals, which are explicit and deliberately not contiguous:
 * `CNA_LOG_LEVEL_EXPERIMENT` is 100, not 6.
 */
typedef uint32_t CNA_LogLevel;

/** @brief Critical issue: key functionality is not working. */
#define CNA_LOG_LEVEL_FATAL UINT32_C(0)
/** @brief One or more functionalities are not working properly. */
#define CNA_LOG_LEVEL_ERROR UINT32_C(1)
/** @brief Unexpected behavior occurred, but the application continues. */
#define CNA_LOG_LEVEL_WARN UINT32_C(2)
/** @brief Informational message about normal application events. */
#define CNA_LOG_LEVEL_INFO UINT32_C(3)
/** @brief Useful for debugging and troubleshooting. */
#define CNA_LOG_LEVEL_DEBUG UINT32_C(4)
/** @brief Fine-grained, highly detailed step-by-step tracing. */
#define CNA_LOG_LEVEL_TRACE UINT32_C(5)
/** @brief Used for experimental features and test-related logging. */
#define CNA_LOG_LEVEL_EXPERIMENT UINT32_C(100)

/** @brief Fixed-width functional category of a log message. */
typedef uint32_t CNA_LogCategory;

/** @brief General application messages. */
#define CNA_LOG_CATEGORY_APPLICATION UINT32_C(0)
/** @brief Error-condition messages. */
#define CNA_LOG_CATEGORY_ERROR UINT32_C(1)
/** @brief Operating system or low-level system messages. */
#define CNA_LOG_CATEGORY_SYSTEM UINT32_C(2)
/** @brief Audio subsystem messages. */
#define CNA_LOG_CATEGORY_AUDIO UINT32_C(3)
/** @brief Video and media subsystem messages. */
#define CNA_LOG_CATEGORY_VIDEO UINT32_C(4)
/** @brief Graphics rendering messages. */
#define CNA_LOG_CATEGORY_RENDER UINT32_C(5)
/** @brief Input subsystem messages. */
#define CNA_LOG_CATEGORY_INPUT UINT32_C(6)
/** @brief Test and unit-test messages. */
#define CNA_LOG_CATEGORY_TEST UINT32_C(7)
/** @brief GPU device and driver messages. */
#define CNA_LOG_CATEGORY_GPU UINT32_C(8)

/** @brief Fixed-width identity of a target platform. */
typedef uint32_t CNA_Platform;

/** @brief Windows, Linux, or macOS desktop. */
#define CNA_PLATFORM_DESKTOP UINT32_C(0)
/** @brief Android mobile. */
#define CNA_PLATFORM_ANDROID UINT32_C(1)
/** @brief Apple iOS. */
#define CNA_PLATFORM_IOS UINT32_C(2)
/** @brief Browser via Emscripten and WebAssembly. */
#define CNA_PLATFORM_WEB UINT32_C(3)

/** @brief Fixed-width identity of a desktop operating system. */
typedef uint32_t CNA_DesktopOS;

/** @brief Microsoft Windows. */
#define CNA_DESKTOP_OS_WINDOWS UINT32_C(0)
/** @brief GNU/Linux. */
#define CNA_DESKTOP_OS_LINUX UINT32_C(1)
/** @brief Apple macOS. */
#define CNA_DESKTOP_OS_MACOSX UINT32_C(2)
/** @brief Any other desktop operating system. */
#define CNA_DESKTOP_OS_OTHER UINT32_C(3)

/** @brief Fixed-width identity of the implementation technology a graphics backend uses. */
typedef uint32_t CNA_GraphicsBackendCategory;

/** @brief Compiled against exactly one fixed real graphics API, with no runtime negotiation. */
#define CNA_GRAPHICS_BACKEND_CATEGORY_NATIVE UINT32_C(0)
/** @brief An intermediate library picks the real backend, or reimplements another API's surface. */
#define CNA_GRAPHICS_BACKEND_CATEGORY_TRANSLATION_LAYER UINT32_C(1)
/** @brief Renders entirely on the CPU; no real GPU driver is involved. */
#define CNA_GRAPHICS_BACKEND_CATEGORY_SOFTWARE UINT32_C(2)
/** @brief Only available in a browser or Emscripten build. */
#define CNA_GRAPHICS_BACKEND_CATEGORY_WEB UINT32_C(3)
/** @brief Never renders a pixel; exists for testing, debugging or reference. */
#define CNA_GRAPHICS_BACKEND_CATEGORY_DIAGNOSTIC UINT32_C(4)

/** @brief Fixed-width identity of how confidently CNA recommends a graphics backend. */
typedef uint32_t CNA_GraphicsBackendMaturity;

/** @brief Fully recommended for regular use; broad, verified feature coverage. */
#define CNA_GRAPHICS_BACKEND_MATURITY_PRODUCTION UINT32_C(0)
/** @brief Functional and maintained, but not necessarily at full feature parity yet. */
#define CNA_GRAPHICS_BACKEND_MATURITY_SUPPORTED UINT32_C(1)
/** @brief Actively developed; public behavior may still change. */
#define CNA_GRAPHICS_BACKEND_MATURITY_EXPERIMENTAL UINT32_C(2)
/** @brief A legacy backend kept for compatibility, research or demonstration. */
#define CNA_GRAPHICS_BACKEND_MATURITY_HISTORICAL UINT32_C(3)
/** @brief No longer recommended; kept only until removal. */
#define CNA_GRAPHICS_BACKEND_MATURITY_DEPRECATED UINT32_C(4)

/**
 * @brief Logs a message with an explicit level, category and condition.
 *
 * @param level Severity level.
 * @param message UTF-8 message text; it does not need to be null-terminated.
 * @param category Functional category.
 * @param condition The message is emitted only when this is `CNA_TRUE`.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for an unknown level, an unknown
 *         category or an invalid message view.
 *
 * A message below the current minimum level is discarded, exactly as the canonical logger
 * discards it.
 */
CNA_C_API CNA_Result cna_logger_log(
    CNA_LogLevel level,
    CNA_StringView message,
    CNA_LogCategory category,
    CNA_Bool condition);

/**
 * @brief Logs a fatal message.
 *
 * @param message UTF-8 message text.
 * @param category Functional category.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for an unknown category or an
 *         invalid message view.
 */
CNA_C_API CNA_Result cna_logger_fatal(CNA_StringView message, CNA_LogCategory category);

/**
 * @brief Logs an error message.
 *
 * @param message UTF-8 message text.
 * @param category Functional category.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for an unknown category or an
 *         invalid message view.
 */
CNA_C_API CNA_Result cna_logger_error(CNA_StringView message, CNA_LogCategory category);

/**
 * @brief Logs a warning message.
 *
 * @param message UTF-8 message text.
 * @param category Functional category.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for an unknown category or an
 *         invalid message view.
 */
CNA_C_API CNA_Result cna_logger_warn(CNA_StringView message, CNA_LogCategory category);

/**
 * @brief Logs an informational message.
 *
 * @param message UTF-8 message text.
 * @param category Functional category.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for an unknown category or an
 *         invalid message view.
 */
CNA_C_API CNA_Result cna_logger_info(CNA_StringView message, CNA_LogCategory category);

/**
 * @brief Logs a debug message.
 *
 * @param message UTF-8 message text.
 * @param category Functional category.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for an unknown category or an
 *         invalid message view.
 */
CNA_C_API CNA_Result cna_logger_debug(CNA_StringView message, CNA_LogCategory category);

/**
 * @brief Logs a trace message.
 *
 * @param message UTF-8 message text.
 * @param category Functional category.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for an unknown category or an
 *         invalid message view.
 */
CNA_C_API CNA_Result cna_logger_trace(CNA_StringView message, CNA_LogCategory category);

/**
 * @brief Logs an experimental message.
 *
 * @param message UTF-8 message text.
 * @param category Functional category.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for an unknown category or an
 *         invalid message view.
 */
CNA_C_API CNA_Result cna_logger_experiment(CNA_StringView message, CNA_LogCategory category);

/**
 * @brief Logs a fatal message when the condition holds.
 *
 * @param message UTF-8 message text.
 * @param condition The message is emitted only when this is `CNA_TRUE`.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for an invalid message view.
 *
 * The canonical conditional overloads take no category and always use `APPLICATION`.
 */
CNA_C_API CNA_Result cna_logger_fatal_if(CNA_StringView message, CNA_Bool condition);

/**
 * @brief Logs an error message when the condition holds.
 *
 * @param message UTF-8 message text.
 * @param condition The message is emitted only when this is `CNA_TRUE`.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for an invalid message view.
 */
CNA_C_API CNA_Result cna_logger_error_if(CNA_StringView message, CNA_Bool condition);

/**
 * @brief Logs a warning message when the condition holds.
 *
 * @param message UTF-8 message text.
 * @param condition The message is emitted only when this is `CNA_TRUE`.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for an invalid message view.
 */
CNA_C_API CNA_Result cna_logger_warn_if(CNA_StringView message, CNA_Bool condition);

/**
 * @brief Logs an informational message when the condition holds.
 *
 * @param message UTF-8 message text.
 * @param condition The message is emitted only when this is `CNA_TRUE`.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for an invalid message view.
 */
CNA_C_API CNA_Result cna_logger_info_if(CNA_StringView message, CNA_Bool condition);

/**
 * @brief Logs a debug message when the condition holds.
 *
 * @param message UTF-8 message text.
 * @param condition The message is emitted only when this is `CNA_TRUE`.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for an invalid message view.
 */
CNA_C_API CNA_Result cna_logger_debug_if(CNA_StringView message, CNA_Bool condition);

/**
 * @brief Logs a trace message when the condition holds.
 *
 * @param message UTF-8 message text.
 * @param condition The message is emitted only when this is `CNA_TRUE`.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for an invalid message view.
 */
CNA_C_API CNA_Result cna_logger_trace_if(CNA_StringView message, CNA_Bool condition);

/**
 * @brief Logs an experimental message when the condition holds.
 *
 * @param message UTF-8 message text.
 * @param condition The message is emitted only when this is `CNA_TRUE`.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for an invalid message view.
 */
CNA_C_API CNA_Result cna_logger_experiment_if(CNA_StringView message, CNA_Bool condition);

/**
 * @brief Sets the minimum enabled log level.
 *
 * @param level New minimum level; messages below it are discarded.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for an unknown level.
 *
 * The canonical minimum level is process-wide static state, so this affects every thread and
 * every CNA subsystem, not just the caller.
 */
CNA_C_API CNA_Result cna_logger_set_minimum_level(CNA_LogLevel level);

/**
 * @brief Gets the current minimum enabled log level.
 *
 * @param out_level Receives the current minimum level.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null output.
 */
CNA_C_API CNA_Result cna_logger_get_minimum_level(CNA_LogLevel* out_level);

/**
 * @brief Reports the platform this build targets.
 *
 * @param out_platform Receives the platform identity.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null output.
 *
 * The canonical query is a compile-time constant, so the answer never changes for a given
 * binary.
 */
CNA_C_API CNA_Result cna_platform_get_current(CNA_Platform* out_platform);

/**
 * @brief Reports the desktop operating system this build runs on.
 *
 * @param out_os Receives the desktop operating-system identity.
 * @return `CNA_RESULT_SUCCESS`; `CNA_RESULT_INVALID_ARGUMENT` for a null output; or
 *         `CNA_RESULT_INVALID_STATE` with `CNA_ERROR_CATEGORY_STATE` when the platform is not
 *         `CNA_PLATFORM_DESKTOP`.
 *
 * The canonical function throws on a non-desktop platform rather than returning a fallback, so a
 * caller that may run on mobile or the web checks `cna_platform_get_current` first.
 */
CNA_C_API CNA_Result cna_desktop_os_get_current(CNA_DesktopOS* out_os);

/**
 * @brief Reports the implementation-technology category of a graphics renderer.
 *
 * @param type The renderer identity to classify; any of the public identities is accepted, not
 *        only the one compiled into this build.
 * @param out_category Receives the category identity.
 * @return `CNA_RESULT_SUCCESS`; `CNA_RESULT_INVALID_ARGUMENT` for a null output or for a renderer
 *         identity that is not one of the public ones.
 */
CNA_C_API CNA_Result cna_graphics_backend_get_category(
    CNA_GraphicsRendererType type,
    CNA_GraphicsBackendCategory* out_category);

/**
 * @brief Reports the implementation-technology category of the renderer compiled into this build.
 *
 * @param out_category Receives the category identity.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null output.
 */
CNA_C_API CNA_Result cna_graphics_backend_get_current_category(
    CNA_GraphicsBackendCategory* out_category);

/**
 * @brief Reports a backend category's name length without a terminator.
 *
 * @param category The category to name.
 * @param out_bytes Receives the required byte count without a terminator.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null output or an unknown
 *         category.
 */
CNA_C_API CNA_Result cna_graphics_backend_category_get_name_size(
    CNA_GraphicsBackendCategory category,
    uint64_t* out_bytes);

/**
 * @brief Copies a backend category's name without a terminator.
 *
 * @param category The category to name.
 * @param destination Caller-owned destination, or null only when @p capacity is zero.
 * @param capacity Destination capacity in bytes.
 * @param out_bytes Receives the required byte count without a terminator.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL`, or `CNA_RESULT_INVALID_ARGUMENT`
 * for a null output or an unknown category. No partial value is written.
 */
CNA_C_API CNA_Result cna_graphics_backend_category_copy_name(
    CNA_GraphicsBackendCategory category,
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

/**
 * @brief Reports the recommendation maturity of a graphics renderer.
 *
 * @param type The renderer identity to classify; any of the public identities is accepted, not
 *        only the one compiled into this build.
 * @param out_maturity Receives the maturity identity.
 * @return `CNA_RESULT_SUCCESS`; `CNA_RESULT_INVALID_ARGUMENT` for a null output or for a renderer
 *         identity that is not one of the public ones.
 */
CNA_C_API CNA_Result cna_graphics_backend_get_maturity(
    CNA_GraphicsRendererType type,
    CNA_GraphicsBackendMaturity* out_maturity);

/**
 * @brief Reports the recommendation maturity of the renderer compiled into this build.
 *
 * @param out_maturity Receives the maturity identity.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null output.
 */
CNA_C_API CNA_Result cna_graphics_backend_get_current_maturity(
    CNA_GraphicsBackendMaturity* out_maturity);

/**
 * @brief Reports a backend maturity's name length without a terminator.
 *
 * @param maturity The maturity to name.
 * @param out_bytes Receives the required byte count without a terminator.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null output or an unknown
 *         maturity.
 */
CNA_C_API CNA_Result cna_graphics_backend_maturity_get_name_size(
    CNA_GraphicsBackendMaturity maturity,
    uint64_t* out_bytes);

/**
 * @brief Copies a backend maturity's name without a terminator.
 *
 * @param maturity The maturity to name.
 * @param destination Caller-owned destination, or null only when @p capacity is zero.
 * @param capacity Destination capacity in bytes.
 * @param out_bytes Receives the required byte count without a terminator.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL`, or `CNA_RESULT_INVALID_ARGUMENT`
 * for a null output or an unknown maturity. No partial value is written.
 */
CNA_C_API CNA_Result cna_graphics_backend_maturity_copy_name(
    CNA_GraphicsBackendMaturity maturity,
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

/**
 * @brief Reports the renderer identity compiled into this build.
 *
 * @param out_type Receives the renderer identity.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null output.
 *
 * Unlike `cna_graphics_device_get_info`, this needs no device and no graphics initialization: the
 * canonical query is a compile-time constant. An identity is not a capability claim — probe the
 * behavior a consumer actually depends on rather than branching on this value.
 */
CNA_C_API CNA_Result cna_graphics_renderer_get_current_type(CNA_GraphicsRendererType* out_type);

/**
 * @brief Reports the compiled-in renderer's name length without a terminator.
 *
 * @param out_bytes Receives the required byte count without a terminator.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null output.
 */
CNA_C_API CNA_Result cna_graphics_renderer_get_current_name_size(uint64_t* out_bytes);

/**
 * @brief Copies the compiled-in renderer's name without a terminator.
 *
 * @param destination Caller-owned destination, or null only when @p capacity is zero.
 * @param capacity Destination capacity in bytes.
 * @param out_bytes Receives the required byte count without a terminator.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL`, or `CNA_RESULT_INVALID_ARGUMENT`
 * for a null output. No partial value is written.
 *
 * The name matches the `CNA_GRAPHICS_RENDERER` build option exactly, for example `"HEADLESS"` or
 * `"SDL_RENDERER"`.
 */
CNA_C_API CNA_Result cna_graphics_renderer_copy_current_name(
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

#ifdef __cplusplus
}
#endif

#endif // CNA_C_CORE_EXT_H
