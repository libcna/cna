// SPDX-License-Identifier: MS-PL

#ifndef CNA_C_INDEX_RESOURCES_H
#define CNA_C_INDEX_RESOURCES_H

#include "CNA/C/graphics3d.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Owned handle for a static or dynamic index buffer. */
typedef CNA_Handle CNA_IndexBufferHandle;

/** @brief Owned handle for one dynamic index-buffer ContentLost subscription. */
typedef CNA_Handle CNA_IndexBufferEventRegistrationHandle;

/** @brief Fully qualified native type name represented by either index-buffer kind. */
#define CNA_INDEX_BUFFER_TYPE_NAME "Microsoft.Xna.Framework.Graphics.IndexBuffer"

/**
 * @brief Receives a dynamic index-buffer ContentLost notification.
 *
 * CNA currently never raises this event, but registration preserves the public event contract.
 *
 * @param index_buffer Dynamic index-buffer handle supplied at registration.
 * @param context Caller-owned context supplied at registration.
 */
typedef void (*CNA_IndexBufferContentLostCallback)(
    CNA_IndexBufferHandle index_buffer,
    void* context);

/** @brief Configures creation of a static or dynamic index buffer. */
typedef struct CNA_IndexBufferCreateInfo {
    /** @brief Size of this caller-provided structure in bytes. */
    uint32_t struct_size;
    /** @brief Version of this caller-provided structure; currently one. */
    uint32_t struct_version;
    /** @brief Nonnegative logical capacity measured in indices. */
    int32_t index_count;
    /** @brief Stored index width. */
    CNA_IndexElementSize index_element_size;
    /** @brief Buffer usage identity. */
    CNA_BufferUsage buffer_usage;
    /** @brief True to construct DynamicIndexBuffer; false to construct IndexBuffer. */
    CNA_Bool dynamic;
    /** @brief Reserved bytes; callers must initialize them to zero. */
    uint8_t reserved[3];
} CNA_IndexBufferCreateInfo;

/** @brief Reports immutable index-buffer metadata and current renderer state. */
typedef struct CNA_IndexBufferInfo {
    /** @brief Size of this caller-provided structure in bytes. */
    uint32_t struct_size;
    /** @brief Version of this caller-provided structure; currently one. */
    uint32_t struct_version;
    /** @brief Logical capacity measured in indices. */
    int32_t index_count;
    /** @brief Stored index width. */
    CNA_IndexElementSize index_element_size;
    /** @brief Buffer usage identity. */
    CNA_BufferUsage buffer_usage;
    /** @brief True when this handle owns a DynamicIndexBuffer. */
    CNA_Bool dynamic;
    /** @brief Dynamic content-loss state; currently always false. */
    CNA_Bool is_content_lost;
    /** @brief True while the native renderer allocation is present. */
    CNA_Bool has_renderer;
    /** @brief Reserved byte; initialized to zero. */
    uint8_t reserved;
} CNA_IndexBufferInfo;

/** @brief Selects an index width, streaming option and caller-array window. */
typedef struct CNA_IndexBufferTransfer {
    /** @brief Size of this caller-provided structure in bytes. */
    uint32_t struct_size;
    /** @brief Version of this caller-provided structure; currently one. */
    uint32_t struct_version;
    /** @brief Width of the pointed-to uint16_t or uint32_t elements. */
    CNA_IndexElementSize index_element_size;
    /** @brief Streaming option; non-None values require a dynamic buffer. */
    CNA_SetDataOptions options;
    /** @brief Index of the first element in the caller's source or destination array. */
    uint64_t start_index;
    /** @brief Number of indices to upload or read back. */
    uint64_t element_count;
} CNA_IndexBufferTransfer;

/**
 * @brief Creates a game-owned static or dynamic index buffer.
 *
 * @param graphics_device Callback-scoped borrowed graphics-device handle.
 * @param create_info Versioned creation configuration.
 * @param out_index_buffer Receives the owned buffer handle.
 * @return A CNA result code; failure leaves @p out_index_buffer invalid.
 */
CNA_C_API CNA_Result cna_index_buffer_create(
    CNA_Handle graphics_device,
    const CNA_IndexBufferCreateInfo* create_info,
    CNA_IndexBufferHandle* out_index_buffer);

/**
 * @brief Destroys an owned static or dynamic index-buffer handle.
 *
 * @param index_buffer Owned index-buffer handle.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_index_buffer_destroy(CNA_IndexBufferHandle index_buffer);

/**
 * @brief Gets index-buffer metadata and current renderer/content-loss state.
 *
 * @param index_buffer Static or dynamic index-buffer handle.
 * @param out_info Receives the versioned metadata snapshot.
 * @return A CNA result code; failure does not overwrite fields after the descriptor prefix.
 */
CNA_C_API CNA_Result cna_index_buffer_get_info(
    CNA_IndexBufferHandle index_buffer,
    CNA_IndexBufferInfo* out_info);

/**
 * @brief Gets the exact byte count of the native index-buffer type name.
 *
 * @param index_buffer Static or dynamic index-buffer handle.
 * @param out_byte_count Receives the UTF-8 byte count without a terminator.
 * @return A CNA result code; failure does not overwrite @p out_byte_count.
 */
CNA_C_API CNA_Result cna_index_buffer_get_type_name_byte_count(
    CNA_IndexBufferHandle index_buffer,
    uint64_t* out_byte_count);

/**
 * @brief Copies the native index-buffer type name without a terminator.
 *
 * @param index_buffer Static or dynamic index-buffer handle.
 * @param destination Destination bytes, or null only for zero capacity.
 * @param capacity Destination capacity in bytes.
 * @param out_byte_count Receives the required UTF-8 byte count.
 * @return A CNA result code; insufficient capacity performs no partial write.
 */
CNA_C_API CNA_Result cna_index_buffer_copy_type_name(
    CNA_IndexBufferHandle index_buffer,
    char* destination,
    uint64_t capacity,
    uint64_t* out_byte_count);

/**
 * @brief Uploads 16-bit or 32-bit indices from a caller-array window.
 *
 * @p data must point to uint16_t or uint32_t elements selected by the descriptor. A zero-element
 * window may use a null pointer and zero capacity.
 *
 * @param index_buffer Static or dynamic index-buffer handle.
 * @param transfer Versioned width, option and source-window descriptor.
 * @param data Typed source array.
 * @param capacity Source capacity measured in selected index elements.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_index_buffer_set_data(
    CNA_IndexBufferHandle index_buffer,
    const CNA_IndexBufferTransfer* transfer,
    const void* data,
    uint64_t capacity);

/**
 * @brief Uploads indices into a window of the buffer, leaving the rest alone.
 *
 * @param index_buffer Static or dynamic index-buffer handle.
 * @param buffer_offset_in_bytes Byte offset into **the buffer**, a multiple of its element size.
 * @param transfer Versioned width, option and source-window descriptor; `start_index` continues to
 *        index the caller's array, as it does everywhere else in this ABI.
 * @param data Typed source array.
 * @param capacity Source capacity measured in selected index elements.
 * @return A CNA result code.
 *
 * `cna_index_buffer_set_data` replaces the buffer's whole contents, which is what XNA's
 * `SetData(T[], int, int)` does; this adds the `offsetInBytes` overload beside it, so one slice of
 * a large dynamic index buffer can be rewritten per frame. Indices outside the window keep
 * whatever they held; indices never written by any upload read as zero.
 *
 * **Documented deviation, about cost rather than about result.** The renderer contract underneath
 * replaces whole-buffer contents, so the window is composed on the CPU side and the whole buffer is
 * re-uploaded. The indices end up exactly where XNA puts them; the transfer is not smaller.
 */
CNA_C_API CNA_Result cna_index_buffer_set_data_at(
    CNA_IndexBufferHandle index_buffer,
    uint64_t buffer_offset_in_bytes,
    const CNA_IndexBufferTransfer* transfer,
    const void* data,
    uint64_t capacity);

/**
 * @brief Reads 16-bit or 32-bit indices into a caller-array window atomically.
 *
 * Native readback begins at index zero and writes into `transfer->start_index` in the caller's
 * typed destination array, matching the documented XNA overload contract.
 *
 * @param index_buffer Static or dynamic index-buffer handle.
 * @param transfer Versioned width and destination-window descriptor; options must be None.
 * @param destination Typed destination array, or null only for zero capacity.
 * @param capacity Destination capacity measured in selected index elements.
 * @param out_element_count Receives the required element count.
 * @return A CNA result code; insufficient capacity or readback failure performs no partial write.
 */
CNA_C_API CNA_Result cna_index_buffer_get_data(
    CNA_IndexBufferHandle index_buffer,
    const CNA_IndexBufferTransfer* transfer,
    void* destination,
    uint64_t capacity,
    uint64_t* out_element_count);

/**
 * @brief Subscribes to a dynamic index buffer's ContentLost event.
 *
 * CNA currently never raises ContentLost. The callback and context remain caller-owned until
 * unregistration or buffer destruction.
 *
 * @param index_buffer Dynamic index-buffer handle.
 * @param callback Non-null callback.
 * @param context Caller-owned callback context, which may be null.
 * @param out_registration Receives the owned subscription handle.
 * @return A CNA result code; failure leaves @p out_registration invalid.
 */
CNA_C_API CNA_Result cna_index_buffer_subscribe_content_lost(
    CNA_IndexBufferHandle index_buffer,
    CNA_IndexBufferContentLostCallback callback,
    void* context,
    CNA_IndexBufferEventRegistrationHandle* out_registration);

/**
 * @brief Removes and destroys a dynamic index-buffer ContentLost subscription.
 *
 * @param registration Owned subscription handle.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_index_buffer_unsubscribe_content_lost(
    CNA_IndexBufferEventRegistrationHandle registration);

#ifdef __cplusplus
}
#endif

#endif
