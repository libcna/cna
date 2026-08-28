// SPDX-License-Identifier: MS-PL

#ifndef CNA_C_VERTEX_RESOURCES_H
#define CNA_C_VERTEX_RESOURCES_H

#include "CNA/C/vertex_values.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Owned handle for a vertex declaration. */
typedef CNA_Handle CNA_VertexDeclarationHandle;

/** @brief Owned handle for a static or dynamic vertex buffer. */
typedef CNA_Handle CNA_VertexBufferHandle;

/** @brief Owned handle for one dynamic vertex-buffer ContentLost subscription. */
typedef CNA_Handle CNA_VertexBufferEventRegistrationHandle;

/** @brief Fully qualified native type name represented by either vertex-buffer kind. */
#define CNA_VERTEX_BUFFER_TYPE_NAME "Microsoft.Xna.Framework.Graphics.VertexBuffer"

/**
 * @brief Receives a dynamic vertex-buffer ContentLost notification.
 *
 * CNA raises this on the renderers whose API can actually lose a device (DirectX9,
 * Direct2D, Skia); families that cannot lose one never raise it.
 *
 * @param vertex_buffer Dynamic vertex-buffer handle supplied at registration.
 * @param context Caller-owned context supplied at registration.
 */
typedef void (*CNA_VertexBufferContentLostCallback)(
    CNA_VertexBufferHandle vertex_buffer,
    void* context);

/** @brief Configures creation of a static or dynamic vertex buffer. */
typedef struct CNA_VertexBufferCreateInfo {
    /** @brief Size of this caller-provided structure in bytes. */
    uint32_t struct_size;
    /** @brief Version of this caller-provided structure; currently one. */
    uint32_t struct_version;
    /** @brief Declaration copied into the buffer, or invalid for the CNA empty-declaration extension. */
    CNA_VertexDeclarationHandle vertex_declaration;
    /** @brief Nonnegative logical capacity measured in vertices. */
    int32_t vertex_count;
    /** @brief Buffer usage identity. */
    CNA_BufferUsage buffer_usage;
    /** @brief True to construct DynamicVertexBuffer; false to construct VertexBuffer. */
    CNA_Bool dynamic;
    /** @brief Reserved bytes; callers must initialize them to zero. */
    uint8_t reserved[7];
} CNA_VertexBufferCreateInfo;

/** @brief Reports immutable vertex-buffer metadata and current renderer state. */
typedef struct CNA_VertexBufferInfo {
    /** @brief Size of this caller-provided structure in bytes. */
    uint32_t struct_size;
    /** @brief Version of this caller-provided structure; currently one. */
    uint32_t struct_version;
    /** @brief Logical capacity measured in vertices. */
    int32_t vertex_count;
    /** @brief Buffer usage identity. */
    CNA_BufferUsage buffer_usage;
    /** @brief True when this handle owns a DynamicVertexBuffer. */
    CNA_Bool dynamic;
    /** @brief Dynamic content-loss state; currently always false. */
    CNA_Bool is_content_lost;
    /** @brief True while the native renderer allocation is present. */
    CNA_Bool has_renderer;
    /** @brief Reserved byte; initialized to zero. */
    uint8_t reserved0;
    /** @brief Vertex stride copied from the associated declaration. */
    int32_t vertex_stride;
    /** @brief Number of elements in the associated declaration. */
    uint64_t vertex_element_count;
} CNA_VertexBufferInfo;

/** @brief Selects a built-in vertex type and a caller-array window for typed transfer. */
typedef struct CNA_VertexBufferTransfer {
    /** @brief Size of this caller-provided structure in bytes. */
    uint32_t struct_size;
    /** @brief Version of this caller-provided structure; currently one. */
    uint32_t struct_version;
    /** @brief Built-in vertex value identity and pointed-to element type. */
    CNA_VertexType vertex_type;
    /** @brief Streaming option; non-None values require a supported dynamic-buffer overload. */
    CNA_SetDataOptions options;
    /** @brief Index of the first element in the caller's source or destination array. */
    uint64_t start_index;
    /** @brief Number of elements to upload or read back. */
    uint64_t element_count;
} CNA_VertexBufferTransfer;

/** @brief Describes one vertex-buffer binding without exposing a native pointer. */
typedef struct CNA_VertexBufferBinding {
    /** @brief Vertex-buffer handle, or CNA_INVALID_HANDLE for a default binding. */
    CNA_VertexBufferHandle vertex_buffer;
    /** @brief Offset in vertices from the beginning of the buffer. */
    int32_t vertex_offset;
    /** @brief Instance frequency, where zero disables instancing. */
    int32_t instance_frequency;
} CNA_VertexBufferBinding;

/**
 * @brief Creates an empty vertex declaration with zero stride and no elements.
 *
 * @param out_declaration Receives the owned declaration handle.
 * @return A CNA result code; failure leaves @p out_declaration invalid.
 */
CNA_C_API CNA_Result cna_vertex_declaration_create_empty(
    CNA_VertexDeclarationHandle* out_declaration);

/**
 * @brief Creates a vertex declaration and computes its stride from the elements.
 *
 * @param elements Source elements; the array is copied.
 * @param element_count Number of source elements; must be positive.
 * @param out_declaration Receives the owned declaration handle.
 * @return A CNA result code; failure leaves @p out_declaration invalid.
 */
CNA_C_API CNA_Result cna_vertex_declaration_create(
    const CNA_VertexElement* elements,
    uint64_t element_count,
    CNA_VertexDeclarationHandle* out_declaration);

/**
 * @brief Creates a vertex declaration with an explicit positive stride.
 *
 * @param vertex_stride Size in bytes of one vertex; must be positive.
 * @param elements Source elements; the array is copied.
 * @param element_count Number of source elements; must be positive.
 * @param out_declaration Receives the owned declaration handle.
 * @return A CNA result code; failure leaves @p out_declaration invalid.
 */
CNA_C_API CNA_Result cna_vertex_declaration_create_with_stride(
    int32_t vertex_stride,
    const CNA_VertexElement* elements,
    uint64_t element_count,
    CNA_VertexDeclarationHandle* out_declaration);

/**
 * @brief Destroys an owned vertex declaration.
 *
 * @param declaration Owned declaration handle.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_vertex_declaration_destroy(
    CNA_VertexDeclarationHandle declaration);

/**
 * @brief Gets a vertex declaration's stride.
 *
 * @param declaration Vertex declaration handle.
 * @param out_vertex_stride Receives the stride in bytes.
 * @return A CNA result code; failure does not overwrite @p out_vertex_stride.
 */
CNA_C_API CNA_Result cna_vertex_declaration_get_stride(
    CNA_VertexDeclarationHandle declaration,
    int32_t* out_vertex_stride);

/**
 * @brief Copies a vertex declaration's elements.
 *
 * @param declaration Vertex declaration handle.
 * @param destination Destination array, or null only for zero capacity.
 * @param capacity Destination capacity measured in elements.
 * @param out_element_count Receives the required element count.
 * @return A CNA result code; insufficient capacity performs no partial write.
 */
CNA_C_API CNA_Result cna_vertex_declaration_copy_elements(
    CNA_VertexDeclarationHandle declaration,
    CNA_VertexElement* destination,
    uint64_t capacity,
    uint64_t* out_element_count);

/**
 * @brief Gets the exact byte count of a declaration's fully-qualified type name.
 *
 * @param declaration Vertex declaration handle.
 * @param out_byte_count Receives the UTF-8 byte count without a terminator.
 * @return A CNA result code; failure does not overwrite @p out_byte_count.
 */
CNA_C_API CNA_Result cna_vertex_declaration_get_type_name_byte_count(
    CNA_VertexDeclarationHandle declaration,
    uint64_t* out_byte_count);

/**
 * @brief Copies a declaration's fully-qualified type name without a terminator.
 *
 * @param declaration Vertex declaration handle.
 * @param destination Destination bytes, or null only for zero capacity.
 * @param capacity Destination capacity in bytes.
 * @param out_byte_count Receives the required UTF-8 byte count.
 * @return A CNA result code; insufficient capacity performs no partial write.
 */
CNA_C_API CNA_Result cna_vertex_declaration_copy_type_name(
    CNA_VertexDeclarationHandle declaration,
    char* destination,
    uint64_t capacity,
    uint64_t* out_byte_count);

/**
 * @brief Initializes a structurally valid vertex-buffer binding descriptor.
 *
 * The buffer token must be nonzero. Its generation and object kind are validated when an
 * operation consumes the binding.
 *
 * @param vertex_buffer Nonzero vertex-buffer handle token.
 * @param vertex_offset Nonnegative offset measured in vertices.
 * @param instance_frequency Nonnegative instance frequency.
 * @param out_binding Receives the binding descriptor.
 * @return A CNA result code; failure does not overwrite @p out_binding.
 */
CNA_C_API CNA_Result cna_vertex_buffer_binding_init(
    CNA_VertexBufferHandle vertex_buffer,
    int32_t vertex_offset,
    int32_t instance_frequency,
    CNA_VertexBufferBinding* out_binding);

/**
 * @brief Creates a game-owned static or dynamic vertex buffer.
 *
 * An invalid declaration selects CNA's static empty-declaration constructor and requires
 * `dynamic == CNA_FALSE` plus `buffer_usage == CNA_BUFFER_USAGE_NONE`.
 *
 * @param graphics_device Callback-scoped borrowed graphics-device handle.
 * @param create_info Versioned creation configuration.
 * @param out_vertex_buffer Receives the owned buffer handle.
 * @return A CNA result code; failure leaves @p out_vertex_buffer invalid.
 */
CNA_C_API CNA_Result cna_vertex_buffer_create(
    CNA_Handle graphics_device,
    const CNA_VertexBufferCreateInfo* create_info,
    CNA_VertexBufferHandle* out_vertex_buffer);

/**
 * @brief Destroys an owned static or dynamic vertex-buffer handle.
 *
 * @param vertex_buffer Owned vertex-buffer handle.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_vertex_buffer_destroy(
    CNA_VertexBufferHandle vertex_buffer);

/**
 * @brief Gets vertex-buffer metadata and current renderer/content-loss state.
 *
 * @param vertex_buffer Static or dynamic vertex-buffer handle.
 * @param out_info Receives the versioned metadata snapshot.
 * @return A CNA result code; failure does not overwrite fields after the descriptor prefix.
 */
CNA_C_API CNA_Result cna_vertex_buffer_get_info(
    CNA_VertexBufferHandle vertex_buffer,
    CNA_VertexBufferInfo* out_info);

/**
 * @brief Copies the buffer's associated vertex-declaration elements atomically.
 *
 * @param vertex_buffer Static or dynamic vertex-buffer handle.
 * @param destination Destination array, or null only for zero capacity.
 * @param capacity Destination capacity measured in elements.
 * @param out_element_count Receives the required element count.
 * @return A CNA result code; insufficient capacity performs no partial write.
 */
CNA_C_API CNA_Result cna_vertex_buffer_copy_declaration_elements(
    CNA_VertexBufferHandle vertex_buffer,
    CNA_VertexElement* destination,
    uint64_t capacity,
    uint64_t* out_element_count);

/**
 * @brief Gets the exact byte count of the native vertex-buffer type name.
 *
 * @param vertex_buffer Static or dynamic vertex-buffer handle.
 * @param out_byte_count Receives the UTF-8 byte count without a terminator.
 * @return A CNA result code; failure does not overwrite @p out_byte_count.
 */
CNA_C_API CNA_Result cna_vertex_buffer_get_type_name_byte_count(
    CNA_VertexBufferHandle vertex_buffer,
    uint64_t* out_byte_count);

/**
 * @brief Copies the native vertex-buffer type name without a terminator.
 *
 * @param vertex_buffer Static or dynamic vertex-buffer handle.
 * @param destination Destination bytes, or null only for zero capacity.
 * @param capacity Destination capacity in bytes.
 * @param out_byte_count Receives the required UTF-8 byte count.
 * @return A CNA result code; insufficient capacity performs no partial write.
 */
CNA_C_API CNA_Result cna_vertex_buffer_copy_type_name(
    CNA_VertexBufferHandle vertex_buffer,
    char* destination,
    uint64_t capacity,
    uint64_t* out_byte_count);

/**
 * @brief Uploads built-in vertex values from a typed caller-array window.
 *
 * @p data must point to an array of the C structure selected by `transfer->vertex_type`.
 * A zero-element window may use a null pointer and zero capacity.
 *
 * @param vertex_buffer Static or dynamic vertex-buffer handle.
 * @param transfer Versioned type, option and array-window descriptor.
 * @param data Source array with the element type selected by the descriptor.
 * @param capacity Source capacity measured in selected vertex elements.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_vertex_buffer_set_data(
    CNA_VertexBufferHandle vertex_buffer,
    const CNA_VertexBufferTransfer* transfer,
    const void* data,
    uint64_t capacity);

/**
 * @brief Reads built-in vertex values into a typed caller-array window atomically.
 *
 * @p destination must point to an array of the C structure selected by
 * `transfer->vertex_type`. The start index selects the destination array window; native buffer
 * readback begins at vertex zero, matching the documented XNA overload contract.
 *
 * @param vertex_buffer Static or dynamic vertex-buffer handle.
 * @param transfer Versioned type and destination-window descriptor; options must be None.
 * @param destination Destination array, or null only for zero capacity.
 * @param capacity Destination capacity measured in selected vertex elements.
 * @param out_element_count Receives the required element count.
 * @return A CNA result code; insufficient capacity or readback failure performs no partial write.
 */
CNA_C_API CNA_Result cna_vertex_buffer_get_data(
    CNA_VertexBufferHandle vertex_buffer,
    const CNA_VertexBufferTransfer* transfer,
    void* destination,
    uint64_t capacity,
    uint64_t* out_element_count);

/**
 * @brief Uploads raw vertex bytes with an explicit per-vertex stride.
 *
 * @param vertex_buffer Static or dynamic vertex-buffer handle.
 * @param data Source bytes; null is valid only for a zero-vertex upload.
 * @param data_byte_count Accessible source byte count.
 * @param vertex_count Number of vertices to upload.
 * @param vertex_stride Positive byte size of one source vertex.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_vertex_buffer_set_data_raw(
    CNA_VertexBufferHandle vertex_buffer,
    const void* data,
    uint64_t data_byte_count,
    uint64_t vertex_count,
    uint32_t vertex_stride);

/**
 * @brief Uploads raw vertex bytes into a window of the buffer, leaving the rest alone.
 *
 * @param vertex_buffer Static or dynamic vertex-buffer handle.
 * @param buffer_offset_in_bytes Byte offset into **the buffer**, a multiple of @p vertex_stride.
 * @param data Source bytes; null is valid only for a zero-vertex upload.
 * @param data_byte_count Accessible source byte count.
 * @param vertex_count Number of vertices to write.
 * @param vertex_stride Positive byte size of one source vertex.
 * @return A CNA result code.
 *
 * The offset every other transfer route here carries indexes the **caller's array**; this one
 * indexes the buffer, which is what XNA's `offsetInBytes` means and what a consumer needs to
 * rewrite one slice of a large dynamic buffer per frame -- the pattern particle systems and
 * streaming terrain are built on. Bytes outside the window keep whatever they held; bytes never
 * written by any upload read as zero.
 *
 * **Documented deviation, about cost rather than about result.** The renderer contract underneath
 * replaces a buffer's whole contents -- no renderer family here takes a destination offset -- so
 * the window is composed on the CPU side and the whole buffer is re-uploaded. The bytes end up
 * exactly where XNA puts them; what this does not buy is a smaller transfer.
 */
CNA_C_API CNA_Result cna_vertex_buffer_set_data_raw_at(
    CNA_VertexBufferHandle vertex_buffer,
    uint64_t buffer_offset_in_bytes,
    const void* data,
    uint64_t data_byte_count,
    uint64_t vertex_count,
    uint32_t vertex_stride);

/**
 * @brief Reads raw vertex bytes back from a window of the buffer.
 *
 * @param vertex_buffer Static or dynamic vertex-buffer handle created readable.
 * @param buffer_offset_in_bytes Byte offset into **the buffer**.
 * @param destination Destination bytes; null is valid only for a zero-vertex read.
 * @param destination_byte_count Writable destination byte count.
 * @param vertex_count Number of vertices to read.
 * @param vertex_stride Positive byte size of one vertex.
 * @return A CNA result code; `CNA_RESULT_NOT_SUPPORTED` for a write-only buffer, and insufficient
 *         capacity performs no partial write.
 *
 * The counterpart of `cna_vertex_buffer_set_data_raw`, and the reason it exists: typed readback
 * covers the built-in `CNA_VertexType` layouts, so a buffer *written* with a custom layout through
 * the raw route could never be read back. That asymmetry had no reason behind it -- the bytes are
 * held either way.
 */
/**
 * @brief Uploads raw vertex bytes with an explicit streaming hint.
 *
 * The options-carrying counterpart of @ref cna_vertex_buffer_set_data_raw, and what a
 * caller-defined vertex type reaches: a type this ABI has no `CNA_VertexType` for is uploaded
 * exactly as it sits in memory, so there is nothing to pack and the stride is the type's own.
 *
 * @param vertex_buffer Owned vertex-buffer handle.
 * @param data Source vertex bytes.
 * @param data_byte_count Number of bytes readable from @p data.
 * @param vertex_count Number of vertices to upload.
 * @param vertex_stride Size of one vertex in bytes.
 * @param options Streaming hint. `CNA_SET_DATA_NONE` matches
 *        @ref cna_vertex_buffer_set_data_raw, so this route is the wider one rather than a second
 *        spelling of it.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for a null source, a zero stride, a
 *         byte count smaller than the vertices described, or an undefined option, or a documented
 *         handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_vertex_buffer_set_data_raw_with_options(
    CNA_VertexBufferHandle vertex_buffer,
    const void* data,
    uint64_t data_byte_count,
    uint64_t vertex_count,
    uint32_t vertex_stride,
    CNA_SetDataOptions options);

/**
 * @brief Uploads raw vertex bytes into a window of the buffer with an explicit streaming hint.
 *
 * @param vertex_buffer Owned vertex-buffer handle.
 * @param buffer_offset_in_bytes Byte offset into **this buffer**, a multiple of @p vertex_stride.
 *        This is the one offset in the family that indexes the buffer rather than the caller's
 *        array, which is what XNA's `offsetInBytes` means.
 * @param data Source vertex bytes.
 * @param data_byte_count Number of bytes readable from @p data.
 * @param vertex_count Number of vertices to write.
 * @param vertex_stride Size of one vertex in bytes.
 * @param options Streaming hint.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for a null source, a zero stride, a
 *         misaligned offset, a byte count smaller than the vertices described, or an undefined
 *         option, or a documented handle/thread/native failure.
 *
 * **Documented deviation, about cost rather than result** -- the same one
 * @ref cna_vertex_buffer_set_data_raw_at carries: a windowed upload cannot keep
 * `CNA_SET_DATA_NO_OVERWRITE`'s promise that nothing the GPU may still be reading is
 * touched, so the renderer receives the whole buffer. The result is correct and merely slower than
 * XNA's.
 */
CNA_C_API CNA_Result cna_vertex_buffer_set_data_raw_at_with_options(
    CNA_VertexBufferHandle vertex_buffer,
    uint64_t buffer_offset_in_bytes,
    const void* data,
    uint64_t data_byte_count,
    uint64_t vertex_count,
    uint32_t vertex_stride,
    CNA_SetDataOptions options);

CNA_C_API CNA_Result cna_vertex_buffer_get_data_raw(
    CNA_VertexBufferHandle vertex_buffer,
    uint64_t buffer_offset_in_bytes,
    void* destination,
    uint64_t destination_byte_count,
    uint64_t vertex_count,
    uint32_t vertex_stride);

/**
 * @brief Subscribes to a dynamic vertex buffer's ContentLost event.
 *
 * CNA raises this on the renderers whose API can actually lose a device (DirectX9,
 * Direct2D, Skia); families that cannot lose one never raise it. The callback and context remain caller-owned
 * until unregistration or buffer destruction.
 *
 * @param vertex_buffer Dynamic vertex-buffer handle.
 * @param callback Non-null callback.
 * @param context Caller-owned callback context, which may be null.
 * @param out_registration Receives the owned subscription handle.
 * @return A CNA result code; failure leaves @p out_registration invalid.
 */
CNA_C_API CNA_Result cna_vertex_buffer_subscribe_content_lost(
    CNA_VertexBufferHandle vertex_buffer,
    CNA_VertexBufferContentLostCallback callback,
    void* context,
    CNA_VertexBufferEventRegistrationHandle* out_registration);

/**
 * @brief Removes and destroys a dynamic vertex-buffer ContentLost subscription.
 *
 * @param registration Owned subscription handle.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_vertex_buffer_unsubscribe_content_lost(
    CNA_VertexBufferEventRegistrationHandle registration);

#ifdef __cplusplus
}
#endif

#endif
