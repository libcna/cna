// SPDX-License-Identifier: MS-PL

#ifndef CNA_C_EFFECTS_H
#define CNA_C_EFFECTS_H

#include "CNA/C/core.h"
#include "CNA/C/math_values.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Fixed-width effect-parameter class identity. */
typedef uint32_t CNA_EffectParameterClass;
/** @brief Scalar effect parameter. */
#define CNA_EFFECT_PARAMETER_CLASS_SCALAR UINT32_C(0)
/** @brief Vector effect parameter. */
#define CNA_EFFECT_PARAMETER_CLASS_VECTOR UINT32_C(1)
/** @brief Matrix effect parameter. */
#define CNA_EFFECT_PARAMETER_CLASS_MATRIX UINT32_C(2)
/** @brief Object effect parameter, such as a texture or string. */
#define CNA_EFFECT_PARAMETER_CLASS_OBJECT UINT32_C(3)
/** @brief Structured effect parameter. */
#define CNA_EFFECT_PARAMETER_CLASS_STRUCT UINT32_C(4)

/** @brief Fixed-width effect-parameter storage-type identity. */
typedef uint32_t CNA_EffectParameterType;
/** @brief Void-pointer effect parameter. */
#define CNA_EFFECT_PARAMETER_TYPE_VOID UINT32_C(0)
/** @brief Boolean effect parameter. */
#define CNA_EFFECT_PARAMETER_TYPE_BOOL UINT32_C(1)
/** @brief Signed 32-bit integer effect parameter. */
#define CNA_EFFECT_PARAMETER_TYPE_INT32 UINT32_C(2)
/** @brief Single-precision floating-point effect parameter. */
#define CNA_EFFECT_PARAMETER_TYPE_SINGLE UINT32_C(3)
/** @brief String effect parameter. */
#define CNA_EFFECT_PARAMETER_TYPE_STRING UINT32_C(4)
/** @brief Texture effect parameter of unspecified dimension. */
#define CNA_EFFECT_PARAMETER_TYPE_TEXTURE UINT32_C(5)
/** @brief One-dimensional texture effect parameter. */
#define CNA_EFFECT_PARAMETER_TYPE_TEXTURE1D UINT32_C(6)
/** @brief Two-dimensional texture effect parameter. */
#define CNA_EFFECT_PARAMETER_TYPE_TEXTURE2D UINT32_C(7)
/** @brief Three-dimensional texture effect parameter. */
#define CNA_EFFECT_PARAMETER_TYPE_TEXTURE3D UINT32_C(8)
/** @brief Cube texture effect parameter. */
#define CNA_EFFECT_PARAMETER_TYPE_TEXTURE_CUBE UINT32_C(9)

/** @brief Owned handle for an immutable effect annotation. */
typedef CNA_Handle CNA_EffectAnnotationHandle;

/** @brief Owned handle for a mutable collection of copied effect annotations. */
typedef CNA_Handle CNA_EffectAnnotationCollectionHandle;

/** @brief Configures creation of an immutable effect annotation. */
typedef struct CNA_EffectAnnotationCreateInfo {
    /** @brief Size of this caller-provided structure in bytes. */
    uint32_t struct_size;
    /** @brief Version of this caller-provided structure; currently one. */
    uint32_t struct_version;
    /** @brief UTF-8 annotation name copied by the call. */
    CNA_StringView name;
    /** @brief UTF-8 semantic copied by the call. */
    CNA_StringView semantic;
    /** @brief Native row-count metadata preserved verbatim. */
    int32_t row_count;
    /** @brief Native column-count metadata preserved verbatim. */
    int32_t column_count;
    /** @brief Annotation parameter class. */
    CNA_EffectParameterClass parameter_class;
    /** @brief Annotation parameter storage type. */
    CNA_EffectParameterType parameter_type;
    /** @brief Raw native float storage copied by the call. */
    const float* data;
    /** @brief Number of floats at @ref data. */
    uint64_t data_count;
    /** @brief UTF-8 cached string value copied by the call. */
    CNA_StringView cached_string;
} CNA_EffectAnnotationCreateInfo;

/** @brief Reports immutable effect-annotation metadata. */
typedef struct CNA_EffectAnnotationInfo {
    /** @brief Size of this caller-provided structure in bytes. */
    uint32_t struct_size;
    /** @brief Version of this caller-provided structure; currently one. */
    uint32_t struct_version;
    /** @brief Native row-count metadata. */
    int32_t row_count;
    /** @brief Native column-count metadata. */
    int32_t column_count;
    /** @brief Annotation parameter class. */
    CNA_EffectParameterClass parameter_class;
    /** @brief Annotation parameter storage type. */
    CNA_EffectParameterType parameter_type;
} CNA_EffectAnnotationInfo;

/**
 * @brief Creates an owned immutable effect annotation from copied inputs.
 *
 * Integer and Boolean annotations use the native four-byte bit representation stored in the
 * first float slot. Other numeric annotations consume ordinary float values.
 *
 * @param create_info Versioned annotation metadata and values.
 * @param out_annotation Receives the owned annotation handle.
 * @return A CNA result code; failure leaves @p out_annotation invalid.
 */
CNA_C_API CNA_Result cna_effect_annotation_create(
    const CNA_EffectAnnotationCreateInfo* create_info,
    CNA_EffectAnnotationHandle* out_annotation);

/**
 * @brief Destroys an owned effect-annotation handle.
 *
 * @param annotation Owned annotation handle.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_effect_annotation_destroy(CNA_EffectAnnotationHandle annotation);

/**
 * @brief Gets immutable effect-annotation metadata.
 *
 * @param annotation Annotation handle.
 * @param out_info Receives the versioned metadata snapshot.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_effect_annotation_get_info(
    CNA_EffectAnnotationHandle annotation,
    CNA_EffectAnnotationInfo* out_info);

/**
 * @brief Gets the UTF-8 annotation-name byte count.
 *
 * @param annotation Annotation handle.
 * @param out_byte_count Receives the byte count without a terminator.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_effect_annotation_get_name_byte_count(
    CNA_EffectAnnotationHandle annotation,
    uint64_t* out_byte_count);

/**
 * @brief Copies the UTF-8 annotation name without a terminator.
 *
 * @param annotation Annotation handle.
 * @param destination Destination bytes, or null only for zero capacity.
 * @param capacity Destination capacity in bytes.
 * @param out_byte_count Receives the required byte count.
 * @return A CNA result code; insufficient capacity performs no partial write.
 */
CNA_C_API CNA_Result cna_effect_annotation_copy_name(
    CNA_EffectAnnotationHandle annotation,
    char* destination,
    uint64_t capacity,
    uint64_t* out_byte_count);

/**
 * @brief Gets the UTF-8 annotation-semantic byte count.
 *
 * @param annotation Annotation handle.
 * @param out_byte_count Receives the byte count without a terminator.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_effect_annotation_get_semantic_byte_count(
    CNA_EffectAnnotationHandle annotation,
    uint64_t* out_byte_count);

/**
 * @brief Copies the UTF-8 annotation semantic without a terminator.
 *
 * @param annotation Annotation handle.
 * @param destination Destination bytes, or null only for zero capacity.
 * @param capacity Destination capacity in bytes.
 * @param out_byte_count Receives the required byte count.
 * @return A CNA result code; insufficient capacity performs no partial write.
 */
CNA_C_API CNA_Result cna_effect_annotation_copy_semantic(
    CNA_EffectAnnotationHandle annotation,
    char* destination,
    uint64_t capacity,
    uint64_t* out_byte_count);

/**
 * @brief Gets the annotation value as a Boolean.
 *
 * @param annotation Annotation handle.
 * @param out_value Receives true or false.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_effect_annotation_get_value_boolean(
    CNA_EffectAnnotationHandle annotation,
    CNA_Bool* out_value);

/**
 * @brief Gets the annotation value as a signed 32-bit integer.
 *
 * @param annotation Annotation handle.
 * @param out_value Receives the integer value.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_effect_annotation_get_value_int32(
    CNA_EffectAnnotationHandle annotation,
    int32_t* out_value);

/**
 * @brief Gets the annotation value as a single-precision float.
 *
 * @param annotation Annotation handle.
 * @param out_value Receives the float value.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_effect_annotation_get_value_single(
    CNA_EffectAnnotationHandle annotation,
    float* out_value);

/**
 * @brief Gets the UTF-8 cached string-value byte count.
 *
 * @param annotation Annotation handle.
 * @param out_byte_count Receives the byte count without a terminator.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_effect_annotation_get_value_string_byte_count(
    CNA_EffectAnnotationHandle annotation,
    uint64_t* out_byte_count);

/**
 * @brief Copies the UTF-8 cached string value without a terminator.
 *
 * @param annotation Annotation handle.
 * @param destination Destination bytes, or null only for zero capacity.
 * @param capacity Destination capacity in bytes.
 * @param out_byte_count Receives the required byte count.
 * @return A CNA result code; insufficient capacity performs no partial write.
 */
CNA_C_API CNA_Result cna_effect_annotation_copy_value_string(
    CNA_EffectAnnotationHandle annotation,
    char* destination,
    uint64_t capacity,
    uint64_t* out_byte_count);

/**
 * @brief Gets the annotation value as a Vector2.
 *
 * @param annotation Annotation handle.
 * @param out_value Receives the vector.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_effect_annotation_get_value_vector2(
    CNA_EffectAnnotationHandle annotation,
    CNA_Vector2* out_value);

/**
 * @brief Gets the annotation value as a Vector3.
 *
 * @param annotation Annotation handle.
 * @param out_value Receives the vector.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_effect_annotation_get_value_vector3(
    CNA_EffectAnnotationHandle annotation,
    CNA_Vector3* out_value);

/**
 * @brief Gets the annotation value as a Vector4.
 *
 * @param annotation Annotation handle.
 * @param out_value Receives the vector.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_effect_annotation_get_value_vector4(
    CNA_EffectAnnotationHandle annotation,
    CNA_Vector4* out_value);

/**
 * @brief Gets the annotation value as a row-major Matrix.
 *
 * @param annotation Annotation handle.
 * @param out_value Receives the matrix.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_effect_annotation_get_value_matrix(
    CNA_EffectAnnotationHandle annotation,
    CNA_Matrix* out_value);

/**
 * @brief Creates an owned empty effect-annotation collection.
 *
 * @param out_collection Receives the owned collection handle.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_effect_annotation_collection_create(
    CNA_EffectAnnotationCollectionHandle* out_collection);

/**
 * @brief Destroys an owned effect-annotation collection.
 *
 * @param collection Owned collection handle.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_effect_annotation_collection_destroy(
    CNA_EffectAnnotationCollectionHandle collection);

/**
 * @brief Gets the number of annotations in a collection.
 *
 * @param collection Collection handle.
 * @param out_count Receives the annotation count.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_effect_annotation_collection_get_count(
    CNA_EffectAnnotationCollectionHandle collection,
    uint64_t* out_count);

/**
 * @brief Adds a value copy of an annotation to a collection.
 *
 * @param collection Collection handle.
 * @param annotation Source annotation handle.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_effect_annotation_collection_add(
    CNA_EffectAnnotationCollectionHandle collection,
    CNA_EffectAnnotationHandle annotation);

/**
 * @brief Gets an owned copy of the annotation at an index.
 *
 * @param collection Collection handle.
 * @param index Zero-based annotation index.
 * @param out_annotation Receives a new owned annotation handle.
 * @return A CNA result code; failure leaves @p out_annotation invalid.
 */
CNA_C_API CNA_Result cna_effect_annotation_collection_get_at(
    CNA_EffectAnnotationCollectionHandle collection,
    uint64_t index,
    CNA_EffectAnnotationHandle* out_annotation);

/**
 * @brief Finds the first annotation with an exact UTF-8 name.
 *
 * @param collection Collection handle.
 * @param name Exact UTF-8 name.
 * @param out_found Receives true when a match exists.
 * @param out_annotation Receives a new owned annotation copy, or an invalid handle when absent.
 * @return A CNA result code; absence is a successful false result.
 */
CNA_C_API CNA_Result cna_effect_annotation_collection_find(
    CNA_EffectAnnotationCollectionHandle collection,
    CNA_StringView name,
    CNA_Bool* out_found,
    CNA_EffectAnnotationHandle* out_annotation);

/** @brief Fixed-width tagged numeric effect-value identity. */
typedef uint32_t CNA_EffectValueType;
/** @brief `CNA_Bool` scalar or array values. */
#define CNA_EFFECT_VALUE_BOOLEAN UINT32_C(0)
/** @brief `int32_t` scalar or array values. */
#define CNA_EFFECT_VALUE_INT32 UINT32_C(1)
/** @brief `float` scalar or array values. */
#define CNA_EFFECT_VALUE_SINGLE UINT32_C(2)
/** @brief `CNA_Matrix` scalar or array values using ordinary effect storage. */
#define CNA_EFFECT_VALUE_MATRIX UINT32_C(3)
/** @brief `CNA_Matrix` scalar or array values using transposed effect storage. */
#define CNA_EFFECT_VALUE_MATRIX_TRANSPOSE UINT32_C(4)
/** @brief `CNA_Quaternion` scalar or array values. */
#define CNA_EFFECT_VALUE_QUATERNION UINT32_C(5)
/** @brief `CNA_Vector2` scalar or array values. */
#define CNA_EFFECT_VALUE_VECTOR2 UINT32_C(6)
/** @brief `CNA_Vector3` scalar or array values. */
#define CNA_EFFECT_VALUE_VECTOR3 UINT32_C(7)
/** @brief `CNA_Vector4` scalar or array values. */
#define CNA_EFFECT_VALUE_VECTOR4 UINT32_C(8)

/** @brief Fixed-width tagged effect-texture overload identity. */
typedef uint32_t CNA_EffectTextureType;
/** @brief Base Texture setter overload; no corresponding native getter exists. */
#define CNA_EFFECT_TEXTURE_BASE UINT32_C(0)
/** @brief Texture2D getter/setter overload. */
#define CNA_EFFECT_TEXTURE_2D UINT32_C(1)
/** @brief Texture3D getter/setter overload. */
#define CNA_EFFECT_TEXTURE_3D UINT32_C(2)
/** @brief TextureCube getter/setter overload. */
#define CNA_EFFECT_TEXTURE_CUBE UINT32_C(3)

/** @brief Owned handle for a mutable effect parameter or stable collection element view. */
typedef CNA_Handle CNA_EffectParameterHandle;

/** @brief Owned handle for a mutable effect-parameter collection view. */
typedef CNA_Handle CNA_EffectParameterCollectionHandle;

/** @brief Configures creation of an effect parameter. */
typedef struct CNA_EffectParameterCreateInfo {
    /** @brief Size of this caller-provided structure in bytes. */
    uint32_t struct_size;
    /** @brief Version of this caller-provided structure; currently one. */
    uint32_t struct_version;
    /** @brief UTF-8 parameter name copied by the call. */
    CNA_StringView name;
    /** @brief UTF-8 semantic copied by the call. */
    CNA_StringView semantic;
    /** @brief Native row-count metadata preserved verbatim. */
    int32_t row_count;
    /** @brief Native column-count metadata preserved verbatim. */
    int32_t column_count;
    /** @brief Effect-parameter class identity. */
    CNA_EffectParameterClass parameter_class;
    /** @brief Effect-parameter storage-type identity. */
    CNA_EffectParameterType parameter_type;
} CNA_EffectParameterCreateInfo;

/** @brief Reports immutable effect-parameter metadata. */
typedef struct CNA_EffectParameterInfo {
    /** @brief Size of this caller-provided structure in bytes. */
    uint32_t struct_size;
    /** @brief Version of this caller-provided structure; currently one. */
    uint32_t struct_version;
    /** @brief Native row-count metadata. */
    int32_t row_count;
    /** @brief Native column-count metadata. */
    int32_t column_count;
    /** @brief Effect-parameter class identity. */
    CNA_EffectParameterClass parameter_class;
    /** @brief Effect-parameter storage-type identity. */
    CNA_EffectParameterType parameter_type;
} CNA_EffectParameterInfo;

/**
 * @brief Creates an owned mutable effect parameter.
 * @param create_info Versioned copied parameter metadata.
 * @param out_parameter Receives the owned parameter handle.
 * @return A CNA result code; failure leaves @p out_parameter invalid.
 */
CNA_C_API CNA_Result cna_effect_parameter_create(
    const CNA_EffectParameterCreateInfo* create_info,
    CNA_EffectParameterHandle* out_parameter);

/**
 * @brief Destroys a standalone parameter or collection-element view handle.
 * @param parameter Owned parameter handle.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_effect_parameter_destroy(CNA_EffectParameterHandle parameter);

/**
 * @brief Gets immutable parameter metadata.
 * @param parameter Parameter handle.
 * @param out_info Receives the versioned metadata snapshot.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_effect_parameter_get_info(
    CNA_EffectParameterHandle parameter,
    CNA_EffectParameterInfo* out_info);

/**
 * @brief Gets the exact UTF-8 parameter-name byte count.
 * @param parameter Parameter handle.
 * @param out_byte_count Receives the byte count without a terminator.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_effect_parameter_get_name_byte_count(
    CNA_EffectParameterHandle parameter,
    uint64_t* out_byte_count);

/**
 * @brief Copies the UTF-8 parameter name without a terminator.
 * @param parameter Parameter handle.
 * @param destination Destination bytes, or null only for zero capacity.
 * @param capacity Destination capacity in bytes.
 * @param out_byte_count Receives the required byte count.
 * @return A CNA result code; insufficient capacity performs no partial write.
 */
CNA_C_API CNA_Result cna_effect_parameter_copy_name(
    CNA_EffectParameterHandle parameter,
    char* destination,
    uint64_t capacity,
    uint64_t* out_byte_count);

/**
 * @brief Gets the exact UTF-8 parameter-semantic byte count.
 * @param parameter Parameter handle.
 * @param out_byte_count Receives the byte count without a terminator.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_effect_parameter_get_semantic_byte_count(
    CNA_EffectParameterHandle parameter,
    uint64_t* out_byte_count);

/**
 * @brief Copies the UTF-8 parameter semantic without a terminator.
 * @param parameter Parameter handle.
 * @param destination Destination bytes, or null only for zero capacity.
 * @param capacity Destination capacity in bytes.
 * @param out_byte_count Receives the required byte count.
 * @return A CNA result code; insufficient capacity performs no partial write.
 */
CNA_C_API CNA_Result cna_effect_parameter_copy_semantic(
    CNA_EffectParameterHandle parameter,
    char* destination,
    uint64_t capacity,
    uint64_t* out_byte_count);

/**
 * @brief Gets a mutable view of the parameter's array-element collection.
 * @param parameter Parameter handle.
 * @param out_collection Receives an owned collection-view handle.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_effect_parameter_get_elements(
    CNA_EffectParameterHandle parameter,
    CNA_EffectParameterCollectionHandle* out_collection);

/**
 * @brief Gets a mutable view of the parameter's structure-member collection.
 * @param parameter Parameter handle.
 * @param out_collection Receives an owned collection-view handle.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_effect_parameter_get_structure_members(
    CNA_EffectParameterHandle parameter,
    CNA_EffectParameterCollectionHandle* out_collection);

/**
 * @brief Gets a mutable view of the parameter's annotation collection.
 * @param parameter Parameter handle.
 * @param out_collection Receives an owned annotation-collection view handle.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_effect_parameter_get_annotations(
    CNA_EffectParameterHandle parameter,
    CNA_EffectAnnotationCollectionHandle* out_collection);

/**
 * @brief Gets one tagged scalar value.
 *
 * @p out_value points to the C type documented by @p value_type. MatrixTranspose returns the
 * matching native transposed getter rather than changing the public C layout.
 *
 * @param parameter Parameter handle.
 * @param value_type Tagged scalar type.
 * @param out_value Receives the selected typed value.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_effect_parameter_get_value(
    CNA_EffectParameterHandle parameter,
    CNA_EffectValueType value_type,
    void* out_value);

/**
 * @brief Gets up to a requested number of tagged array values atomically.
 * @param parameter Parameter handle.
 * @param value_type Tagged array element type.
 * @param requested_count Maximum number requested from native storage.
 * @param destination Typed destination array, or null for a zero-capacity count query.
 * @param capacity Destination capacity in typed elements.
 * @param out_count Receives the actual number available within @p requested_count.
 * @return A CNA result code; insufficient capacity performs no partial write.
 */
CNA_C_API CNA_Result cna_effect_parameter_get_values(
    CNA_EffectParameterHandle parameter,
    CNA_EffectValueType value_type,
    uint64_t requested_count,
    void* destination,
    uint64_t capacity,
    uint64_t* out_count);

/**
 * @brief Sets one tagged scalar value through the matching native overload.
 * @param parameter Parameter handle.
 * @param value_type Tagged scalar type.
 * @param value Pointer to the selected C value.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_effect_parameter_set_value(
    CNA_EffectParameterHandle parameter,
    CNA_EffectValueType value_type,
    const void* value);

/**
 * @brief Sets a copied tagged value array through the matching native vector overload.
 * @param parameter Parameter handle.
 * @param value_type Tagged array element type.
 * @param values Typed source array, or null when @p count is zero.
 * @param count Number of typed elements.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_effect_parameter_set_values(
    CNA_EffectParameterHandle parameter,
    CNA_EffectValueType value_type,
    const void* values,
    uint64_t count);

/**
 * @brief Gets the exact UTF-8 string-value byte count.
 * @param parameter Parameter handle.
 * @param out_byte_count Receives the byte count without a terminator.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_effect_parameter_get_value_string_byte_count(
    CNA_EffectParameterHandle parameter,
    uint64_t* out_byte_count);

/**
 * @brief Copies the UTF-8 string value without a terminator.
 * @param parameter Parameter handle.
 * @param destination Destination bytes, or null only for zero capacity.
 * @param capacity Destination capacity in bytes.
 * @param out_byte_count Receives the required byte count.
 * @return A CNA result code; insufficient capacity performs no partial write.
 */
CNA_C_API CNA_Result cna_effect_parameter_copy_value_string(
    CNA_EffectParameterHandle parameter,
    char* destination,
    uint64_t capacity,
    uint64_t* out_byte_count);

/**
 * @brief Sets a copied UTF-8 string value.
 * @param parameter Parameter handle.
 * @param value UTF-8 string copied by the call.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_effect_parameter_set_value_string(
    CNA_EffectParameterHandle parameter,
    CNA_StringView value);

/**
 * @brief Gets the C handle stored by a typed texture getter.
 * @param parameter Parameter handle.
 * @param texture_type Texture2D, Texture3D or TextureCube getter identity.
 * @param out_texture Receives the retained handle or invalid handle for null.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_effect_parameter_get_value_texture(
    CNA_EffectParameterHandle parameter,
    CNA_EffectTextureType texture_type,
    CNA_Handle* out_texture);

/**
 * @brief Sets or clears one native texture-overload slot.
 *
 * A nonzero handle is retained against destruction until this slot is replaced, cleared or the
 * parameter handle hierarchy is destroyed. `CNA_INVALID_HANDLE` selects native null.
 *
 * @param parameter Parameter handle.
 * @param texture_type Base, Texture2D, Texture3D or TextureCube setter identity.
 * @param texture Matching texture handle or `CNA_INVALID_HANDLE`.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_effect_parameter_set_value_texture(
    CNA_EffectParameterHandle parameter,
    CNA_EffectTextureType texture_type,
    CNA_Handle texture);

/**
 * @brief Creates an owned empty effect-parameter collection.
 * @param out_collection Receives the owned collection handle.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_effect_parameter_collection_create(
    CNA_EffectParameterCollectionHandle* out_collection);

/**
 * @brief Destroys an owned effect-parameter collection view.
 * @param collection Owned collection handle.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_effect_parameter_collection_destroy(
    CNA_EffectParameterCollectionHandle collection);

/**
 * @brief Gets the parameter count.
 * @param collection Collection handle.
 * @param out_count Receives the count.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_effect_parameter_collection_get_count(
    CNA_EffectParameterCollectionHandle collection,
    uint64_t* out_count);

/**
 * @brief Constructs and adds a parameter, returning a stable mutable element view.
 * @param collection Collection handle.
 * @param create_info Versioned copied parameter metadata.
 * @param out_parameter Receives an owned stable element-view handle.
 * @return A CNA result code; failure leaves @p out_parameter invalid.
 */
CNA_C_API CNA_Result cna_effect_parameter_collection_add_create(
    CNA_EffectParameterCollectionHandle collection,
    const CNA_EffectParameterCreateInfo* create_info,
    CNA_EffectParameterHandle* out_parameter);

/**
 * @brief Gets a stable mutable parameter view by index.
 * @param collection Collection handle.
 * @param index Zero-based parameter index.
 * @param out_parameter Receives an owned element-view handle.
 * @return A CNA result code; failure leaves @p out_parameter invalid.
 */
CNA_C_API CNA_Result cna_effect_parameter_collection_get_at(
    CNA_EffectParameterCollectionHandle collection,
    uint64_t index,
    CNA_EffectParameterHandle* out_parameter);

/**
 * @brief Finds the first parameter with an exact UTF-8 name.
 * @param collection Collection handle.
 * @param name Exact UTF-8 name.
 * @param out_found Receives true when a match exists.
 * @param out_parameter Receives an owned element view, or invalid handle when absent.
 * @return A CNA result code; absence is a successful false result.
 */
CNA_C_API CNA_Result cna_effect_parameter_collection_find_name(
    CNA_EffectParameterCollectionHandle collection,
    CNA_StringView name,
    CNA_Bool* out_found,
    CNA_EffectParameterHandle* out_parameter);

/**
 * @brief Finds the first parameter with an exact UTF-8 semantic.
 * @param collection Collection handle.
 * @param semantic Exact UTF-8 semantic.
 * @param out_found Receives true when a match exists.
 * @param out_parameter Receives an owned element view, or invalid handle when absent.
 * @return A CNA result code; absence is a successful false result.
 */
CNA_C_API CNA_Result cna_effect_parameter_collection_find_semantic(
    CNA_EffectParameterCollectionHandle collection,
    CNA_StringView semantic,
    CNA_Bool* out_found,
    CNA_EffectParameterHandle* out_parameter);

#ifdef __cplusplus
}
#endif

#endif
