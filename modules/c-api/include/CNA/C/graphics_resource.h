// SPDX-License-Identifier: MS-PL

#ifndef CNA_C_GRAPHICS_RESOURCE_H
#define CNA_C_GRAPHICS_RESOURCE_H

#include "CNA/C/core.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief C-owned opaque metadata token associated with a graphics resource. */
typedef uint64_t CNA_GraphicsResourceTag;

/** @brief Owned handle for one graphics-resource event subscription. */
typedef CNA_Handle CNA_GraphicsResourceEventRegistrationHandle;

/**
 * @brief Receives synchronous notification immediately before explicit resource disposal.
 *
 * @param resource Resource handle supplied when the callback was registered.
 * @param context Caller-owned context supplied at registration.
 */
typedef void (*CNA_GraphicsResourceDisposingCallback)(
    CNA_Handle resource,
    void* context);

/**
 * @brief Gets the callback-scoped graphics device that owns a resource.
 *
 * A standalone resource returns success and CNA_INVALID_HANDLE. A device-owned resource returns
 * its existing borrowed handle only while the parent game is inside a lifecycle callback.
 *
 * @param resource Graphics-resource handle.
 * @param out_graphics_device Receives a borrowed callback-scoped device handle or invalid handle.
 * @return A CNA result code; failure leaves @p out_graphics_device invalid.
 */
CNA_C_API CNA_Result cna_graphics_resource_get_graphics_device(
    CNA_Handle resource,
    CNA_Handle* out_graphics_device);

/**
 * @brief Gets whether a graphics resource has been explicitly disposed.
 *
 * @param resource Graphics-resource handle.
 * @param out_is_disposed Receives the disposal state.
 * @return A CNA result code; failure does not overwrite @p out_is_disposed.
 */
CNA_C_API CNA_Result cna_graphics_resource_get_is_disposed(
    CNA_Handle resource,
    CNA_Bool* out_is_disposed);

/**
 * @brief Gets the exact UTF-8 byte count of a graphics resource's name.
 *
 * @param resource Graphics-resource handle.
 * @param out_byte_count Receives the byte count without a terminator.
 * @return A CNA result code; failure does not overwrite @p out_byte_count.
 */
CNA_C_API CNA_Result cna_graphics_resource_get_name_byte_count(
    CNA_Handle resource,
    uint64_t* out_byte_count);

/**
 * @brief Copies a graphics resource's name without a terminator.
 *
 * @param resource Graphics-resource handle.
 * @param destination Destination bytes, or null only for zero capacity.
 * @param capacity Destination capacity in bytes.
 * @param out_byte_count Receives the required byte count.
 * @return A CNA result code; insufficient capacity performs no partial write.
 */
CNA_C_API CNA_Result cna_graphics_resource_copy_name(
    CNA_Handle resource,
    char* destination,
    uint64_t capacity,
    uint64_t* out_byte_count);

/**
 * @brief Sets a graphics resource's name from validated UTF-8.
 *
 * @param resource Graphics-resource handle.
 * @param name UTF-8 name; embedded NUL bytes are rejected.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_graphics_resource_set_name(
    CNA_Handle resource,
    CNA_StringView name);

/**
 * @brief Gets the exact UTF-8 byte count of a graphics resource's string representation.
 *
 * @param resource Graphics-resource handle.
 * @param out_byte_count Receives the byte count without a terminator.
 * @return A CNA result code; failure does not overwrite @p out_byte_count.
 */
CNA_C_API CNA_Result cna_graphics_resource_get_string_byte_count(
    CNA_Handle resource,
    uint64_t* out_byte_count);

/**
 * @brief Copies a graphics resource's string representation without a terminator.
 *
 * @param resource Graphics-resource handle.
 * @param destination Destination bytes, or null only for zero capacity.
 * @param capacity Destination capacity in bytes.
 * @param out_byte_count Receives the required byte count.
 * @return A CNA result code; insufficient capacity performs no partial write.
 */
CNA_C_API CNA_Result cna_graphics_resource_copy_string(
    CNA_Handle resource,
    char* destination,
    uint64_t capacity,
    uint64_t* out_byte_count);

/**
 * @brief Gets a graphics resource's C-owned opaque tag token.
 *
 * @param resource Graphics-resource handle.
 * @param out_tag Receives the tag; zero is the default/null token.
 * @return A CNA result code; failure does not overwrite @p out_tag.
 */
CNA_C_API CNA_Result cna_graphics_resource_get_tag(
    CNA_Handle resource,
    CNA_GraphicsResourceTag* out_tag);

/**
 * @brief Sets a graphics resource's C-owned opaque tag token.
 *
 * @param resource Graphics-resource handle.
 * @param tag New tag token; zero represents null.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_graphics_resource_set_tag(
    CNA_Handle resource,
    CNA_GraphicsResourceTag tag);

/**
 * @brief Explicitly disposes a graphics resource without releasing its C handle.
 *
 * @param resource Graphics-resource handle.
 * @return A CNA result code. Repeated disposal is a successful no-op.
 */
CNA_C_API CNA_Result cna_graphics_resource_dispose(CNA_Handle resource);

/**
 * @brief Subscribes to a graphics resource's synchronous disposing event.
 *
 * The callback and context remain caller-owned until unregistration or resource destruction.
 *
 * @param resource Graphics-resource handle.
 * @param callback Non-null callback.
 * @param context Caller-owned callback context, which may be null.
 * @param out_registration Receives the owned subscription handle.
 * @return A CNA result code; failure leaves @p out_registration invalid.
 */
CNA_C_API CNA_Result cna_graphics_resource_subscribe_disposing(
    CNA_Handle resource,
    CNA_GraphicsResourceDisposingCallback callback,
    void* context,
    CNA_GraphicsResourceEventRegistrationHandle* out_registration);

/**
 * @brief Removes and destroys a graphics-resource disposing subscription.
 *
 * @param registration Owned subscription handle.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_graphics_resource_unsubscribe_disposing(
    CNA_GraphicsResourceEventRegistrationHandle registration);

#ifdef __cplusplus
}
#endif

#endif
