// SPDX-License-Identifier: MS-PL

#ifndef CNA_C_CORE_H
#define CNA_C_CORE_H

#include "CNA/C/abi.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Fixed-width category assigned to a native C API error. */
typedef uint32_t CNA_ErrorCategory;

/** @brief Indicates that no error category is available. */
#define CNA_ERROR_CATEGORY_NONE UINT32_C(0)

/** @brief Indicates an argument-contract error. */
#define CNA_ERROR_CATEGORY_ARGUMENT UINT32_C(1)

/** @brief Indicates a handle validation error. */
#define CNA_ERROR_CATEGORY_HANDLE UINT32_C(2)

/** @brief Indicates an object or runtime state error. */
#define CNA_ERROR_CATEGORY_STATE UINT32_C(3)

/** @brief Indicates a native memory allocation error. */
#define CNA_ERROR_CATEGORY_MEMORY UINT32_C(4)

/** @brief Indicates a file, stream, or storage error. */
#define CNA_ERROR_CATEGORY_IO UINT32_C(5)

/** @brief Indicates an unavailable CNA capability. */
#define CNA_ERROR_CATEGORY_NOT_SUPPORTED UINT32_C(6)

/** @brief Indicates a native platform-service error. */
#define CNA_ERROR_CATEGORY_PLATFORM UINT32_C(7)

/** @brief Indicates a thread-affinity or concurrency-contract error. */
#define CNA_ERROR_CATEGORY_THREAD UINT32_C(8)

/** @brief Indicates a C callback failure. */
#define CNA_ERROR_CATEGORY_CALLBACK UINT32_C(9)

/** @brief Indicates an overflow or output-capacity error. */
#define CNA_ERROR_CATEGORY_RANGE UINT32_C(10)

/** @brief Indicates a UTF-8 or other text-encoding error. */
#define CNA_ERROR_CATEGORY_ENCODING UINT32_C(11)

/** @brief Indicates an unexpected native implementation error. */
#define CNA_ERROR_CATEGORY_INTERNAL UINT32_C(12)

/** @brief Indicates that CNA runtime shutdown prevented the operation. */
#define CNA_ERROR_CATEGORY_SHUTTING_DOWN UINT32_C(13)

/**
 * @brief Describes the most recent native C API error on the calling thread.
 */
typedef struct CNA_ErrorInfo {
    /** @brief Size of this caller-provided structure in bytes. */
    uint32_t struct_size;

    /** @brief Version of this caller-provided structure. */
    uint32_t struct_version;

    /** @brief Result returned by the failed C API call. */
    CNA_Result result;

    /** @brief Stable error category associated with @ref result. */
    CNA_ErrorCategory category;

    /** @brief Number of UTF-8 bytes available from the last-error message copy API. */
    uint64_t message_byte_length;
} CNA_ErrorInfo;

/**
 * @brief Views UTF-8 bytes borrowed for the duration of a C API call.
 */
typedef struct CNA_StringView {
    /** @brief Pointer to UTF-8 bytes, or null only when @ref byte_length is zero. */
    const char* data;

    /** @brief Number of bytes beginning at @ref data. */
    uint64_t byte_length;
} CNA_StringView;

/**
 * @brief Represents an unpacked 8-bit-per-channel RGBA color value.
 */
typedef struct CNA_Color {
    /** @brief Red channel in the inclusive range 0 through 255. */
    uint8_t r;

    /** @brief Green channel in the inclusive range 0 through 255. */
    uint8_t g;

    /** @brief Blue channel in the inclusive range 0 through 255. */
    uint8_t b;

    /** @brief Alpha channel in the inclusive range 0 through 255. */
    uint8_t a;
} CNA_Color;

/**
 * @brief Represents a two-component single-precision vector value.
 */
typedef struct CNA_Vector2 {
    /** @brief X component. */
    float x;

    /** @brief Y component. */
    float y;
} CNA_Vector2;

/**
 * @brief Represents a three-component single-precision vector value.
 */
typedef struct CNA_Vector3 {
    /** @brief X component. */
    float x;

    /** @brief Y component. */
    float y;

    /** @brief Z component. */
    float z;
} CNA_Vector3;

/**
 * @brief Represents an integer rectangle as position and size.
 */
typedef struct CNA_Rectangle {
    /** @brief X coordinate of the top-left corner. */
    int32_t x;

    /** @brief Y coordinate of the top-left corner. */
    int32_t y;

    /** @brief Rectangle width. */
    int32_t width;

    /** @brief Rectangle height. */
    int32_t height;
} CNA_Rectangle;

/**
 * @brief Gets the most recent error information for the calling thread.
 *
 * @param out_info Caller-provided versioned structure to receive the error information.
 * @return `CNA_RESULT_SUCCESS` on success; otherwise an argument error without overwriting the
 * previous thread-local diagnostic.
 */
CNA_C_API CNA_Result cna_error_get_last_info(CNA_ErrorInfo* out_info);

/**
 * @brief Gets the UTF-8 byte count of the most recent error message for the calling thread.
 *
 * @param out_bytes Receives the exact message byte count without a terminator.
 * @return `CNA_RESULT_SUCCESS` on success; otherwise an argument error without overwriting the
 * previous thread-local diagnostic.
 */
CNA_C_API CNA_Result cna_error_get_last_message_size(uint64_t* out_bytes);

/**
 * @brief Copies the UTF-8 bytes of the most recent error message for the calling thread.
 *
 * @param destination Caller-owned destination bytes, or null only when @p capacity is zero.
 * @param capacity Capacity of @p destination in bytes.
 * @param out_bytes Receives the required message byte count without a terminator.
 * @return `CNA_RESULT_SUCCESS` if the complete message was copied,
 * `CNA_RESULT_BUFFER_TOO_SMALL` when capacity is insufficient, or an argument error. Error-query
 * calls never overwrite the prior thread-local diagnostic.
 */
CNA_C_API CNA_Result cna_error_copy_last_message(
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

#ifdef __cplusplus
}
#endif

#endif
