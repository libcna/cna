// SPDX-License-Identifier: MS-PL

#ifndef CNA_C_ABI_H
#define CNA_C_ABI_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Marks a declaration as part of the exported CNA native C ABI. */
#if defined(CNA_C_API_STATIC)
#define CNA_C_API
#elif defined(_WIN32)
#if defined(CNA_C_API_BUILD)
#define CNA_C_API __declspec(dllexport)
#else
#define CNA_C_API __declspec(dllimport)
#endif
#elif defined(__GNUC__) || defined(__clang__)
#define CNA_C_API __attribute__((visibility("default")))
#else
#define CNA_C_API
#endif

/** @brief Encodes an ABI major, minor, and patch version into a `uint32_t`. */
#define CNA_ABI_VERSION_ENCODE(major, minor, patch) \
    ((((uint32_t)(major) & UINT32_C(0xFFFF)) << 16) | \
     (((uint32_t)(minor) & UINT32_C(0xFF)) << 8) | \
     ((uint32_t)(patch) & UINT32_C(0xFF)))

/** @brief Major component of the current experimental CNA C ABI version. */
#define CNA_ABI_VERSION_MAJOR UINT32_C(0)

/** @brief Minor component of the experimental CNA C ABI version. */
#define CNA_ABI_VERSION_MINOR UINT32_C(22)

/** @brief Patch component of the current experimental CNA C ABI version. */
#define CNA_ABI_VERSION_PATCH UINT32_C(0)

/** @brief Encoded value of the current experimental CNA C ABI version. */
#define CNA_ABI_VERSION CNA_ABI_VERSION_ENCODE( \
    CNA_ABI_VERSION_MAJOR, CNA_ABI_VERSION_MINOR, CNA_ABI_VERSION_PATCH)

/** @brief Fixed-width result code returned by every fallible C API operation. */
typedef uint32_t CNA_Result;

/** @brief Indicates that an operation completed successfully. */
#define CNA_RESULT_SUCCESS UINT32_C(0)

/** @brief Indicates that an argument violates the documented C API contract. */
#define CNA_RESULT_INVALID_ARGUMENT UINT32_C(1)

/** @brief Indicates that a handle is stale, invalid, from another runtime, or has the wrong kind. */
#define CNA_RESULT_INVALID_HANDLE UINT32_C(2)

/** @brief Indicates that an operation is invalid for the current object or runtime state. */
#define CNA_RESULT_INVALID_STATE UINT32_C(3)

/** @brief Indicates that a native allocation failed. */
#define CNA_RESULT_OUT_OF_MEMORY UINT32_C(4)

/** @brief Indicates that an input or output operation failed. */
#define CNA_RESULT_IO UINT32_C(5)

/** @brief Indicates that the selected renderer or platform does not support an operation. */
#define CNA_RESULT_NOT_SUPPORTED UINT32_C(6)

/** @brief Indicates that a native platform service failed. */
#define CNA_RESULT_PLATFORM UINT32_C(7)

/** @brief Indicates that an operation was invoked from a disallowed thread. */
#define CNA_RESULT_THREAD UINT32_C(8)

/** @brief Indicates that a registered callback returned a failure result. */
#define CNA_RESULT_CALLBACK UINT32_C(9)

/** @brief Indicates that a size or numeric conversion cannot be represented safely. */
#define CNA_RESULT_OVERFLOW UINT32_C(10)

/** @brief Indicates that input text is not valid for the required UTF-8 contract. */
#define CNA_RESULT_ENCODING UINT32_C(11)

/** @brief Indicates that CNA caught a native failure without a more specific public result. */
#define CNA_RESULT_INTERNAL UINT32_C(12)

/** @brief Indicates that runtime shutdown prevents an operation. */
#define CNA_RESULT_SHUTTING_DOWN UINT32_C(13)

/** @brief Indicates that a caller-owned output buffer cannot hold the required result. */
#define CNA_RESULT_BUFFER_TOO_SMALL UINT32_C(14)

/** @brief Fixed-width Boolean representation used by the C API. */
typedef uint8_t CNA_Bool;

/** @brief Boolean false value. */
#define CNA_FALSE ((CNA_Bool)0)

/** @brief Boolean true value. */
#define CNA_TRUE ((CNA_Bool)1)

/** @brief Opaque generation-checked CNA object handle. */
typedef uint64_t CNA_Handle;

/** @brief Invalid opaque CNA object handle. */
#define CNA_INVALID_HANDLE UINT64_C(0)

/**
 * @brief Gets the encoded version of the native CNA C ABI implemented by this library.
 *
 * @return Encoded ABI version created with `CNA_ABI_VERSION_ENCODE`.
 */
CNA_C_API uint32_t cna_get_abi_version(void);

#ifdef __cplusplus
}
#endif

#endif
