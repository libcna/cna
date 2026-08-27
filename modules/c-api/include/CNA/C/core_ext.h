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
 * @brief Reports whether this build targets an Apple platform.
 *
 * @param out_apple Receives the answer.
 * @return `CNA_RESULT_SUCCESS` or an argument failure.
 *
 * A compile-time fact, not a runtime probe: macOS and iOS both answer `CNA_TRUE`, and
 * @ref cna_platform_get_current is how a caller tells them apart.
 */
CNA_C_API CNA_Result cna_platform_get_is_apple_ext(CNA_Bool* out_apple);

/**
 * @brief Reports whether this build targets a mobile platform.
 *
 * @param out_mobile Receives the answer.
 * @return `CNA_RESULT_SUCCESS` or an argument failure.
 */
CNA_C_API CNA_Result cna_platform_get_is_mobile_ext(CNA_Bool* out_mobile);

/**
 * @brief Gets the byte count of the current platform's name.
 *
 * @param out_bytes Receives the byte count, without a terminator.
 * @return `CNA_RESULT_SUCCESS` or an argument failure.
 */
CNA_C_API CNA_Result cna_platform_get_current_name_size_ext(uint64_t* out_bytes);

/**
 * @brief Copies the current platform's name.
 *
 * @param destination Buffer receiving the UTF-8 bytes; may be null only when @p capacity is zero.
 * @param capacity Bytes available in @p destination.
 * @param out_bytes Always receives the required byte count, without a terminator.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL` with **no partial write**, or a
 *         documented argument failure.
 */
CNA_C_API CNA_Result cna_platform_copy_current_name_ext(
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

/**
 * @brief Receives one formatted log line.
 *
 * @param level One `CNA_LOG_LEVEL_*` identity.
 * @param category One `CNA_LOG_CATEGORY_*` identity.
 * @param message The formatted line as counted UTF-8 bytes, without a trailing newline.
 * @param context Caller-owned context supplied when the sink was installed.
 *
 * The bytes are borrowed for the duration of the call: copy them to keep them. A sink must not
 * call back into CNA, and must return normally -- see CALLBACKS_AND_THREADING.md.
 */
typedef void (*CNA_LogSinkCallback)(
    CNA_LogLevel level,
    CNA_LogCategory category,
    CNA_StringView message,
    void* context);

/**
 * @brief Replaces the destination log lines are written to.
 *
 * @param callback The sink, or null to restore the default.
 * @param context Caller-owned context passed back unchanged.
 * @return `CNA_RESULT_SUCCESS` or an argument failure.
 *
 * The default sink writes to **stderr**, deliberately never stdout: a terminal-hosted game draws
 * its frame on stdout, and a log line there would corrupt it. That makes the destination a
 * correctness matter rather than a preference, which is why a C sink is offered at all.
 */
CNA_C_API CNA_Result cna_logger_set_sink_ext(CNA_LogSinkCallback callback, void* context);

/**
 * @brief Restores the default stderr sink.
 *
 * @return `CNA_RESULT_SUCCESS`.
 */
CNA_C_API CNA_Result cna_logger_reset_sink_ext(void);

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
 * Unlike `cna_graphics_device_get_renderer_info`, this needs no device and no graphics
 * initialization: the
 * canonical query is a compile-time constant. An identity is not a capability claim — probe the
 * behavior a consumer actually depends on rather than branching on this value.
 */
/** @brief Fixed-width reason a renderer was passed over during selection. */
typedef uint32_t CNA_GraphicsRendererFallbackReason;

/** @brief The identity is not compiled into this build at all. */
#define CNA_GRAPHICS_RENDERER_FALLBACK_NOT_COMPILED_IN UINT32_C(0)

/** @brief The renderer's own availability probe reported it cannot run here. */
#define CNA_GRAPHICS_RENDERER_FALLBACK_PROBE_UNAVAILABLE UINT32_C(1)

/** @brief The renderer was attempted and its construction failed. */
#define CNA_GRAPHICS_RENDERER_FALLBACK_INITIALIZATION_FAILED UINT32_C(2)

/** @brief The renderer needs a different window kind and the window could not be recreated. */
#define CNA_GRAPHICS_RENDERER_FALLBACK_WINDOW_KIND_CONFLICT UINT32_C(3)

/** @brief Highest defined fallback reason. */
#define CNA_GRAPHICS_RENDERER_FALLBACK_MAXIMUM CNA_GRAPHICS_RENDERER_FALLBACK_WINDOW_KIND_CONFLICT

/**
 * @brief Describes one renderer that was tried and passed over.
 *
 * The diagnostic message is not carried here: it is a string of unbounded length, which this ABI
 * never puts in a fixed struct. `cna_graphics_renderer_fallback_get_message_size` and
 * `cna_graphics_renderer_fallback_copy_message` read it by the same count/copy pair every other
 * string in this ABI uses.
 */
typedef struct CNA_GraphicsRendererFallbackRecord {
    /** @brief Size of this caller-provided structure in bytes. */
    uint32_t struct_size;

    /** @brief Version of this caller-provided structure. */
    uint32_t struct_version;

    /** @brief The renderer identity that was tried and passed over. */
    CNA_GraphicsRendererType type;

    /** @brief Why it was passed over. */
    CNA_GraphicsRendererFallbackReason reason;
} CNA_GraphicsRendererFallbackRecord;

/**
 * @brief Requests the renderer CNA should attempt first.
 *
 * @param type One `CNA_GRAPHICS_RENDERER_*` identity.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for an undefined identity,
 *         `CNA_RESULT_INVALID_STATE` once the selection is latched or when the identity is not in
 *         this build and no fallback chain was configured.
 *
 * Process-wide and deliberately so: the choice must be made before the first graphics device
 * exists, which is before a game has anywhere natural to keep it. XNA had one renderer and no
 * notion of choosing, so this whole family is a CNA extension.
 */
CNA_C_API CNA_Result cna_graphics_renderer_set_preferred_ext(CNA_GraphicsRendererType type);

/**
 * @brief Requests the renderer by name, accepting the `CNA_GRAPHICS_RENDERER` spellings.
 *
 * @param name UTF-8 renderer name, matched case-insensitively, e.g. `"VULKAN"`.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` when the name is not a public
 *         renderer identity, `CNA_RESULT_INVALID_STATE` under the same conditions as above.
 */
CNA_C_API CNA_Result cna_graphics_renderer_set_preferred_by_name_ext(CNA_StringView name);

/**
 * @brief Gets the renderer CNA will attempt first.
 *
 * @param out_type Receives the identity.
 * @return `CNA_RESULT_SUCCESS` or an argument failure.
 */
CNA_C_API CNA_Result cna_graphics_renderer_get_selected_ext(CNA_GraphicsRendererType* out_type);

/**
 * @brief Gets the renderer that was actually created.
 *
 * @param out_type Receives the identity.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_STATE` while nothing has been created yet, or
 *         an argument failure.
 *
 * It equals @ref cna_graphics_renderer_get_selected_ext unless a configured fallback chain
 * substituted another renderer. Asking before the selection is latched is refused rather than
 * guessed: until something is created there is no honest answer to give, and
 * @ref cna_graphics_renderer_get_is_latched_ext is how a caller knows which it is.
 */
CNA_C_API CNA_Result cna_graphics_renderer_get_active_ext(CNA_GraphicsRendererType* out_type);

/**
 * @brief Reports whether the selection can still be changed.
 *
 * @param out_latched Receives `CNA_TRUE` once a renderer has been created.
 * @return `CNA_RESULT_SUCCESS` or an argument failure.
 */
CNA_C_API CNA_Result cna_graphics_renderer_get_is_latched_ext(CNA_Bool* out_latched);

/**
 * @brief Counts the renderer identities compiled into this build.
 *
 * @param out_count Receives the count.
 * @return `CNA_RESULT_SUCCESS` or an argument failure.
 */
CNA_C_API CNA_Result cna_graphics_renderer_get_available_count_ext(uint64_t* out_count);

/**
 * @brief Copies the renderer identities compiled into this build.
 *
 * @param destination Buffer receiving the identities; may be null only when @p capacity is zero.
 * @param capacity Elements available in @p destination.
 * @param out_count Always receives the required element count.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL` with **no partial write**, or an
 *         argument failure.
 */
CNA_C_API CNA_Result cna_graphics_renderer_copy_available_ext(
    CNA_GraphicsRendererType* destination,
    uint64_t capacity,
    uint64_t* out_count);

/**
 * @brief Reports whether one identity is compiled into this build.
 *
 * @param type One `CNA_GRAPHICS_RENDERER_*` identity.
 * @param out_available Receives the answer.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for an undefined identity.
 *
 * An identity that is enumerated is not a support claim: this is how a caller finds out which of
 * the 46 identities this particular build can actually produce.
 */
CNA_C_API CNA_Result cna_graphics_renderer_get_is_available_ext(
    CNA_GraphicsRendererType type,
    CNA_Bool* out_available);

/**
 * @brief Sets the order CNA tries renderers in when the preferred one cannot be used.
 *
 * @param types Identities in attempt order, or null only when @p count is zero.
 * @param count Number of identities.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for an undefined identity or an
 *         invalid array, or `CNA_RESULT_INVALID_STATE` once the selection is latched.
 */
CNA_C_API CNA_Result cna_graphics_renderer_set_fallback_chain_ext(
    const CNA_GraphicsRendererType* types,
    uint64_t count);

/**
 * @brief Enables or disables automatic fallback.
 *
 * @param enabled `CNA_TRUE` to let CNA try the chain, `CNA_FALSE` to fail instead.
 * @return `CNA_RESULT_SUCCESS` or `CNA_RESULT_INVALID_STATE` once latched.
 */
CNA_C_API CNA_Result cna_graphics_renderer_set_automatic_fallback_ext(CNA_Bool enabled);

/**
 * @brief Reports whether automatic fallback is enabled.
 *
 * @param out_enabled Receives the answer.
 * @return `CNA_RESULT_SUCCESS` or an argument failure.
 */
CNA_C_API CNA_Result cna_graphics_renderer_get_automatic_fallback_ext(CNA_Bool* out_enabled);

/**
 * @brief Counts the renderers that were tried and passed over.
 *
 * @param out_count Receives the count, which is zero on a build where the first choice worked.
 * @return `CNA_RESULT_SUCCESS` or an argument failure.
 */
CNA_C_API CNA_Result cna_graphics_renderer_get_fallback_count_ext(uint64_t* out_count);

/**
 * @brief Reads one fallback record.
 *
 * @param index Zero-based position in the history.
 * @param out_record Caller-provided versioned structure to receive the record.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for a position outside the history
 *         or an invalid structure.
 */
CNA_C_API CNA_Result cna_graphics_renderer_get_fallback_at_ext(
    uint64_t index,
    CNA_GraphicsRendererFallbackRecord* out_record);

/**
 * @brief Gets the byte count of one fallback record's diagnostic message.
 *
 * @param index Zero-based position in the history.
 * @param out_bytes Receives the byte count, without a terminator.
 * @return `CNA_RESULT_SUCCESS` or a documented argument failure.
 */
CNA_C_API CNA_Result cna_graphics_renderer_fallback_get_message_size_ext(
    uint64_t index,
    uint64_t* out_bytes);

/**
 * @brief Copies one fallback record's diagnostic message.
 *
 * @param index Zero-based position in the history.
 * @param destination Buffer receiving the UTF-8 bytes; may be null only when @p capacity is zero.
 * @param capacity Bytes available in @p destination.
 * @param out_bytes Always receives the required byte count, without a terminator.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL` with **no partial write**, or a
 *         documented argument failure.
 *
 * For an initialization failure this is the message the renderer's construction produced, verbatim;
 * for the other reasons a short explanatory sentence. Never empty.
 */
CNA_C_API CNA_Result cna_graphics_renderer_fallback_copy_message_ext(
    uint64_t index,
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

/**
 * @brief Gets the byte count of a fallback reason's stable name.
 *
 * @param reason One `CNA_GRAPHICS_RENDERER_FALLBACK_*` identity.
 * @param out_bytes Receives the byte count, without a terminator.
 * @return `CNA_RESULT_SUCCESS` or `CNA_RESULT_INVALID_ARGUMENT` for an undefined identity.
 */
CNA_C_API CNA_Result cna_graphics_renderer_fallback_reason_get_name_size_ext(
    CNA_GraphicsRendererFallbackReason reason,
    uint64_t* out_bytes);

/**
 * @brief Copies a fallback reason's stable name.
 *
 * @param reason One `CNA_GRAPHICS_RENDERER_FALLBACK_*` identity.
 * @param destination Buffer receiving the UTF-8 bytes; may be null only when @p capacity is zero.
 * @param capacity Bytes available in @p destination.
 * @param out_bytes Always receives the required byte count, without a terminator.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL` with **no partial write**, or a
 *         documented argument failure.
 */
CNA_C_API CNA_Result cna_graphics_renderer_fallback_reason_copy_name_ext(
    CNA_GraphicsRendererFallbackReason reason,
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

/**
 * @brief Parses a renderer name into its identity.
 *
 * @param name UTF-8 renderer name, matched case-insensitively.
 * @param out_type Receives the identity when the name is recognized.
 * @param out_recognized Receives whether it was.
 * @return `CNA_RESULT_SUCCESS` or a documented argument failure.
 *
 * An unrecognized name is an answer, not a failure: the route succeeds and reports `CNA_FALSE`.
 */
CNA_C_API CNA_Result cna_graphics_renderer_try_parse_name_ext(
    CNA_StringView name,
    CNA_GraphicsRendererType* out_type,
    CNA_Bool* out_recognized);

/**
 * @brief Returns the selection to its initial state.
 *
 * @return `CNA_RESULT_SUCCESS`.
 *
 * A test route: it un-latches the process-wide selection so a suite can drive it more than once.
 * It is named `_ext` like the rest of this family and is not something a game should call.
 */
CNA_C_API CNA_Result cna_graphics_renderer_reset_selection_for_tests_ext(void);

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

/**
 * @brief Declares the product title the runtime reports.
 *
 * The C++ side offers this two ways -- a call and an `AssemblyTitleAttributeEXT` declared at
 * namespace scope, which runs the same assignment at static initialization the way a C# assembly
 * attribute does. **C has no static-initialization form to bind**, so the attribute struct maps to
 * this call: a C caller sets the title from `main` instead of declaring it beside the code.
 *
 * The title is process-wide and unvalidated -- an empty title is a title.
 *
 * @param title The product title.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a malformed string view.
 */
CNA_C_API CNA_Result cna_assembly_set_title_ext(CNA_StringView title);

/**
 * @brief Copies the product title declared by @ref cna_assembly_set_title_ext.
 *
 * @param destination The buffer, or null to ask for the size.
 * @param capacity The buffer size in bytes.
 * @param out_bytes Receives the required byte count without a terminator.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL` with the needed size in `out_bytes`,
 * or `CNA_RESULT_INVALID_ARGUMENT` for a null count.
 */
CNA_C_API CNA_Result cna_assembly_copy_title_ext(
    char* destination, uint64_t capacity, uint64_t* out_bytes);

#ifdef __cplusplus
}
#endif

#endif // CNA_C_CORE_EXT_H
