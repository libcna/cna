// SPDX-License-Identifier: MS-PL

#ifndef CNA_C_VERTEX_RESOURCES_H
#define CNA_C_VERTEX_RESOURCES_H

#include "CNA/C/vertex_values.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Owned handle for a vertex declaration. */
typedef CNA_Handle CNA_VertexDeclarationHandle;

/** @brief Future owned handle identity for a vertex buffer. */
typedef CNA_Handle CNA_VertexBufferHandle;

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

#ifdef __cplusplus
}
#endif

#endif
