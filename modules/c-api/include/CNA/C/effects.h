// SPDX-License-Identifier: MS-PL

#ifndef CNA_C_EFFECTS_H
#define CNA_C_EFFECTS_H

#include "CNA/C/core.h"
#include "CNA/C/graphics_state.h"
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

/** @brief Owned handle for a graphics-device effect instance. */
typedef CNA_Handle CNA_EffectHandle;

/** @brief Maximum number of matrices in a SkinnedEffect bone palette. */
#define CNA_SKINNED_EFFECT_MAX_BONES UINT32_C(72)

/** @brief Owned standalone or stable effect-member view of a DirectionalLight. */
typedef CNA_Handle CNA_DirectionalLightHandle;

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

/** @brief Owned handle for an effect pass or stable collection element view. */
typedef CNA_Handle CNA_EffectPassHandle;

/** @brief Owned handle for a mutable effect-pass collection view. */
typedef CNA_Handle CNA_EffectPassCollectionHandle;

/** @brief Owned handle for an effect technique or stable collection element view. */
typedef CNA_Handle CNA_EffectTechniqueHandle;

/** @brief Owned handle for a mutable effect-technique collection view. */
typedef CNA_Handle CNA_EffectTechniqueCollectionHandle;

/**
 * @brief Creates an ownerless native pass with copied UTF-8 name and identity metadata.
 * @param name UTF-8 pass name copied by the call.
 * @param technique_identity Owning-technique identity metadata, or zero.
 * @param out_pass Receives the owned pass handle.
 * @return A CNA result code; failure leaves @p out_pass invalid.
 */
CNA_C_API CNA_Result cna_effect_pass_create(
    CNA_StringView name,
    uint64_t technique_identity,
    CNA_EffectPassHandle* out_pass);

/**
 * @brief Destroys a pass or stable pass-element view handle.
 * @param pass Owned pass handle.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_effect_pass_destroy(CNA_EffectPassHandle pass);

/**
 * @brief Gets the exact UTF-8 pass-name byte count.
 * @param pass Pass handle.
 * @param out_byte_count Receives the byte count without a terminator.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_effect_pass_get_name_byte_count(
    CNA_EffectPassHandle pass,
    uint64_t* out_byte_count);

/**
 * @brief Copies the UTF-8 pass name without a terminator.
 * @param pass Pass handle.
 * @param destination Destination bytes, or null only for zero capacity.
 * @param capacity Destination capacity in bytes.
 * @param out_byte_count Receives the required byte count.
 * @return A CNA result code; insufficient capacity performs no partial write.
 */
CNA_C_API CNA_Result cna_effect_pass_copy_name(
    CNA_EffectPassHandle pass,
    char* destination,
    uint64_t capacity,
    uint64_t* out_byte_count);

/**
 * @brief Gets a mutable view of the pass annotation collection.
 * @param pass Pass handle.
 * @param out_collection Receives an owned annotation-collection view handle.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_effect_pass_get_annotations(
    CNA_EffectPassHandle pass,
    CNA_EffectAnnotationCollectionHandle* out_collection);

/**
 * @brief Applies a pass through the canonical native dispatch.
 *
 * Ownerless passes are the native successful no-op. Effect-owned pass views introduced with the
 * effect lifecycle validate their technique against that effect's current technique.
 *
 * @param pass Pass handle.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_effect_pass_apply(CNA_EffectPassHandle pass);

/**
 * @brief Creates an owned empty pass collection.
 * @param out_collection Receives the owned collection handle.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_effect_pass_collection_create(
    CNA_EffectPassCollectionHandle* out_collection);

/**
 * @brief Destroys an owned pass-collection view.
 * @param collection Owned collection handle.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_effect_pass_collection_destroy(
    CNA_EffectPassCollectionHandle collection);

/**
 * @brief Gets the pass count.
 * @param collection Collection handle.
 * @param out_count Receives the count.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_effect_pass_collection_get_count(
    CNA_EffectPassCollectionHandle collection,
    uint64_t* out_count);

/**
 * @brief Constructs and adds a pass, returning a stable element view.
 * @param collection Collection handle.
 * @param name UTF-8 pass name copied by the call.
 * @param technique_identity Owning-technique identity metadata, or zero.
 * @param out_pass Receives an owned stable element-view handle.
 * @return A CNA result code; failure leaves @p out_pass invalid.
 */
CNA_C_API CNA_Result cna_effect_pass_collection_add_create(
    CNA_EffectPassCollectionHandle collection,
    CNA_StringView name,
    uint64_t technique_identity,
    CNA_EffectPassHandle* out_pass);

/**
 * @brief Gets a stable pass view by index.
 * @param collection Collection handle.
 * @param index Zero-based pass index.
 * @param out_pass Receives an owned stable element-view handle.
 * @return A CNA result code; failure leaves @p out_pass invalid.
 */
CNA_C_API CNA_Result cna_effect_pass_collection_get_at(
    CNA_EffectPassCollectionHandle collection,
    uint64_t index,
    CNA_EffectPassHandle* out_pass);

/**
 * @brief Finds the first pass with an exact UTF-8 name.
 * @param collection Collection handle.
 * @param name Exact UTF-8 name.
 * @param out_found Receives true when a match exists.
 * @param out_pass Receives an owned element view, or invalid handle when absent.
 * @return A CNA result code; absence is a successful false result.
 */
CNA_C_API CNA_Result cna_effect_pass_collection_find(
    CNA_EffectPassCollectionHandle collection,
    CNA_StringView name,
    CNA_Bool* out_found,
    CNA_EffectPassHandle* out_pass);

/**
 * @brief Creates the native default ownerless technique with empty name and no passes.
 * @param out_technique Receives the owned technique handle.
 * @return A CNA result code; failure leaves @p out_technique invalid.
 */
CNA_C_API CNA_Result cna_effect_technique_create_default(
    CNA_EffectTechniqueHandle* out_technique);

/**
 * @brief Creates a named ownerless technique with its canonical default `P0` pass.
 * @param name UTF-8 technique name copied by the call.
 * @param out_technique Receives the owned technique handle.
 * @return A CNA result code; failure leaves @p out_technique invalid.
 */
CNA_C_API CNA_Result cna_effect_technique_create_named(
    CNA_StringView name,
    CNA_EffectTechniqueHandle* out_technique);

/**
 * @brief Destroys a technique or stable technique-element view handle.
 * @param technique Owned technique handle.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_effect_technique_destroy(CNA_EffectTechniqueHandle technique);

/**
 * @brief Gets the exact UTF-8 technique-name byte count.
 * @param technique Technique handle.
 * @param out_byte_count Receives the byte count without a terminator.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_effect_technique_get_name_byte_count(
    CNA_EffectTechniqueHandle technique,
    uint64_t* out_byte_count);

/**
 * @brief Copies the UTF-8 technique name without a terminator.
 * @param technique Technique handle.
 * @param destination Destination bytes, or null only for zero capacity.
 * @param capacity Destination capacity in bytes.
 * @param out_byte_count Receives the required byte count.
 * @return A CNA result code; insufficient capacity performs no partial write.
 */
CNA_C_API CNA_Result cna_effect_technique_copy_name(
    CNA_EffectTechniqueHandle technique,
    char* destination,
    uint64_t capacity,
    uint64_t* out_byte_count);

/**
 * @brief Gets the technique's stable non-pointer identity token.
 * @param technique Technique handle.
 * @param out_identity Receives the nonzero native identity.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_effect_technique_get_identity(
    CNA_EffectTechniqueHandle technique,
    uint64_t* out_identity);

/**
 * @brief Gets a mutable view of the technique's pass collection.
 * @param technique Technique handle.
 * @param out_collection Receives an owned pass-collection view handle.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_effect_technique_get_passes(
    CNA_EffectTechniqueHandle technique,
    CNA_EffectPassCollectionHandle* out_collection);

/**
 * @brief Gets a mutable view of the technique annotation collection.
 * @param technique Technique handle.
 * @param out_collection Receives an owned annotation-collection view handle.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_effect_technique_get_annotations(
    CNA_EffectTechniqueHandle technique,
    CNA_EffectAnnotationCollectionHandle* out_collection);

/**
 * @brief Creates an owned empty technique collection.
 * @param out_collection Receives the owned collection handle.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_effect_technique_collection_create(
    CNA_EffectTechniqueCollectionHandle* out_collection);

/**
 * @brief Destroys an owned technique-collection view.
 * @param collection Owned collection handle.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_effect_technique_collection_destroy(
    CNA_EffectTechniqueCollectionHandle collection);

/**
 * @brief Gets the technique count.
 * @param collection Collection handle.
 * @param out_count Receives the count.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_effect_technique_collection_get_count(
    CNA_EffectTechniqueCollectionHandle collection,
    uint64_t* out_count);

/**
 * @brief Constructs and adds a default technique, returning a stable element view.
 * @param collection Collection handle.
 * @param out_technique Receives an owned stable element-view handle.
 * @return A CNA result code; failure leaves @p out_technique invalid.
 */
CNA_C_API CNA_Result cna_effect_technique_collection_add_default(
    CNA_EffectTechniqueCollectionHandle collection,
    CNA_EffectTechniqueHandle* out_technique);

/**
 * @brief Constructs and adds a named technique with its canonical `P0` pass.
 * @param collection Collection handle.
 * @param name UTF-8 technique name copied by the call.
 * @param out_technique Receives an owned stable element-view handle.
 * @return A CNA result code; failure leaves @p out_technique invalid.
 */
CNA_C_API CNA_Result cna_effect_technique_collection_add_named(
    CNA_EffectTechniqueCollectionHandle collection,
    CNA_StringView name,
    CNA_EffectTechniqueHandle* out_technique);

/**
 * @brief Gets a stable technique view by index.
 * @param collection Collection handle.
 * @param index Zero-based technique index.
 * @param out_technique Receives an owned stable element-view handle.
 * @return A CNA result code; failure leaves @p out_technique invalid.
 */
CNA_C_API CNA_Result cna_effect_technique_collection_get_at(
    CNA_EffectTechniqueCollectionHandle collection,
    uint64_t index,
    CNA_EffectTechniqueHandle* out_technique);

/**
 * @brief Finds the first technique with an exact UTF-8 name.
 * @param collection Collection handle.
 * @param name Exact UTF-8 name.
 * @param out_found Receives true when a match exists.
 * @param out_technique Receives an owned element view, or invalid handle when absent.
 * @return A CNA result code; absence is a successful false result.
 */
CNA_C_API CNA_Result cna_effect_technique_collection_find(
    CNA_EffectTechniqueCollectionHandle collection,
    CNA_StringView name,
    CNA_Bool* out_found,
    CNA_EffectTechniqueHandle* out_technique);

/**
 * @brief Creates the minimal concrete adapter for the native abstract Effect base class.
 * @param graphics_device Borrowed graphics-device handle from an active game callback.
 * @param out_effect Receives the owned effect handle.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_effect_create_empty(
    CNA_Handle graphics_device,
    CNA_EffectHandle* out_effect);

/**
 * @brief Attempts to create an Effect from compiled XNA `.fx` bytecode.
 * @param graphics_device Borrowed graphics-device handle from an active game callback.
 * @param effect_code Bytecode bytes copied during the call.
 * @param effect_code_count Number of bytes at @p effect_code.
 * @param out_effect Receives the owned effect handle on success.
 * @return `CNA_RESULT_NOT_SUPPORTED` while native CNA bytecode loading is unavailable.
 */
CNA_C_API CNA_Result cna_effect_create_compiled(
    CNA_Handle graphics_device,
    const uint8_t* effect_code,
    uint64_t effect_code_count,
    CNA_EffectHandle* out_effect);

/**
 * @brief Creates a source-based ShaderEffect.
 * @param graphics_device Borrowed graphics-device handle from an active game callback.
 * @param vertex_source UTF-8 vertex-shader source copied by the call.
 * @param fragment_source UTF-8 fragment-shader source copied by the call.
 * @param out_effect Receives the owned effect handle.
 * @return A CNA result code; renderer availability is reported separately.
 */
CNA_C_API CNA_Result cna_shader_effect_create(
    CNA_Handle graphics_device,
    CNA_StringView vertex_source,
    CNA_StringView fragment_source,
    CNA_EffectHandle* out_effect);

/**
 * @brief Creates an EffectMaterial using the source effect's graphics device.
 * @param clone_source Source effect handle.
 * @param out_effect Receives the owned material handle.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_effect_material_create(
    CNA_EffectHandle clone_source,
    CNA_EffectHandle* out_effect);

/**
 * @brief Creates the stock SpriteEffect.
 * @param graphics_device Borrowed graphics-device handle from an active game callback.
 * @param out_effect Receives the owned effect handle.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_sprite_effect_create(
    CNA_Handle graphics_device,
    CNA_EffectHandle* out_effect);

/** @brief Destroys an owned effect handle. */
CNA_C_API CNA_Result cna_effect_destroy(CNA_EffectHandle effect);

/**
 * @brief Creates an independent native clone of an effect.
 * @param effect Source effect.
 * @param out_clone Receives an owned clone of the same concrete native type.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_effect_clone(
    CNA_EffectHandle effect,
    CNA_EffectHandle* out_clone);

/** @brief Disposes an effect's graphics resources without releasing its handle. */
CNA_C_API CNA_Result cna_effect_dispose(CNA_EffectHandle effect);

/** @brief Applies the effect and selects it on its owning graphics device. */
CNA_C_API CNA_Result cna_effect_apply(CNA_EffectHandle effect);

/**
 * @brief Gets a mutable stable view of the effect parameter collection.
 * @param effect Effect handle.
 * @param out_collection Receives an owned collection-view handle.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_effect_get_parameters(
    CNA_EffectHandle effect,
    CNA_EffectParameterCollectionHandle* out_collection);

/**
 * @brief Gets a mutable stable view of the effect technique collection.
 * @param effect Effect handle.
 * @param out_collection Receives an owned collection-view handle.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_effect_get_techniques(
    CNA_EffectHandle effect,
    CNA_EffectTechniqueCollectionHandle* out_collection);

/**
 * @brief Gets the current technique as an owned stable view.
 * @param effect Effect handle.
 * @param out_technique Receives a technique view, or the invalid handle when null.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_effect_get_current_technique(
    CNA_EffectHandle effect,
    CNA_EffectTechniqueHandle* out_technique);

/**
 * @brief Selects a technique belonging to this effect, or clears it with the invalid handle.
 * @param effect Effect handle.
 * @param technique Technique view from this effect, or `CNA_INVALID_HANDLE`.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_effect_set_current_technique(
    CNA_EffectHandle effect,
    CNA_EffectTechniqueHandle technique);

/** @brief Gets the borrowed graphics-device handle that owns an effect. */
CNA_C_API CNA_Result cna_effect_get_graphics_device(
    CNA_EffectHandle effect,
    CNA_Handle* out_graphics_device);

/** @brief Gets the exact UTF-8 runtime type-name byte count. */
CNA_C_API CNA_Result cna_effect_get_type_name_byte_count(
    CNA_EffectHandle effect,
    uint64_t* out_byte_count);

/** @brief Copies the UTF-8 runtime type name without a terminator. */
CNA_C_API CNA_Result cna_effect_copy_type_name(
    CNA_EffectHandle effect,
    char* destination,
    uint64_t capacity,
    uint64_t* out_byte_count);

/** @brief Gets the exact UTF-8 vertex-source byte count. */
CNA_C_API CNA_Result cna_effect_get_vertex_source_byte_count(
    CNA_EffectHandle effect,
    uint64_t* out_byte_count);

/** @brief Copies the UTF-8 vertex source without a terminator. */
CNA_C_API CNA_Result cna_effect_copy_vertex_source(
    CNA_EffectHandle effect,
    char* destination,
    uint64_t capacity,
    uint64_t* out_byte_count);

/** @brief Gets the exact UTF-8 fragment-source byte count. */
CNA_C_API CNA_Result cna_effect_get_fragment_source_byte_count(
    CNA_EffectHandle effect,
    uint64_t* out_byte_count);

/** @brief Copies the UTF-8 fragment source without a terminator. */
CNA_C_API CNA_Result cna_effect_copy_fragment_source(
    CNA_EffectHandle effect,
    char* destination,
    uint64_t capacity,
    uint64_t* out_byte_count);

/** @brief Reports whether the effect exposes a live renderer-specific compiled program. */
CNA_C_API CNA_Result cna_effect_has_renderer(
    CNA_EffectHandle effect,
    CNA_Bool* out_has_renderer);

/** @brief Reports whether the effect is exactly the stock SpriteEffect runtime type. */
CNA_C_API CNA_Result cna_effect_is_exact_stock_sprite_effect(
    CNA_EffectHandle effect,
    CNA_Bool* out_is_exact);

/** @brief Reports whether a ShaderEffect has a valid compiled shader program. */
CNA_C_API CNA_Result cna_shader_effect_is_valid(
    CNA_EffectHandle effect,
    CNA_Bool* out_is_valid);

/** @brief Reports whether a ShaderEffect still owns a renderer program object. */
CNA_C_API CNA_Result cna_shader_effect_has_renderer(
    CNA_EffectHandle effect,
    CNA_Bool* out_has_renderer);

/** @brief Sets a named column-major 4-by-4 shader uniform. */
CNA_C_API CNA_Result cna_shader_effect_set_uniform_matrix(
    CNA_EffectHandle effect,
    CNA_StringView name,
    CNA_Matrix value);

/** @brief Sets a named vec4 shader uniform. */
CNA_C_API CNA_Result cna_shader_effect_set_uniform_vector4(
    CNA_EffectHandle effect,
    CNA_StringView name,
    CNA_Vector4 value);

/** @brief Sets a named vec3 shader uniform. */
CNA_C_API CNA_Result cna_shader_effect_set_uniform_vector3(
    CNA_EffectHandle effect,
    CNA_StringView name,
    CNA_Vector3 value);

/** @brief Sets a named vec2 shader uniform. */
CNA_C_API CNA_Result cna_shader_effect_set_uniform_vector2(
    CNA_EffectHandle effect,
    CNA_StringView name,
    CNA_Vector2 value);

/** @brief Sets a named scalar float shader uniform. */
CNA_C_API CNA_Result cna_shader_effect_set_uniform_float(
    CNA_EffectHandle effect,
    CNA_StringView name,
    float value);

/** @brief Sets a named signed integer shader uniform. */
CNA_C_API CNA_Result cna_shader_effect_set_uniform_int32(
    CNA_EffectHandle effect,
    CNA_StringView name,
    int32_t value);

/** @brief Sets a named array of scalar float shader uniforms. */
CNA_C_API CNA_Result cna_shader_effect_set_uniform_float_array(
    CNA_EffectHandle effect,
    CNA_StringView name,
    const float* values,
    uint64_t count);

/** @brief Sets a named array of vec2 shader uniforms. */
CNA_C_API CNA_Result cna_shader_effect_set_uniform_vector2_array(
    CNA_EffectHandle effect,
    CNA_StringView name,
    const CNA_Vector2* values,
    uint64_t count);

/** @brief Binds a Texture2D-compatible handle to a shader sampler unit. */
CNA_C_API CNA_Result cna_shader_effect_set_texture2d(
    CNA_EffectHandle effect,
    int32_t unit,
    CNA_Handle texture);

/** @brief Binds a TextureCube-compatible handle to a shader sampler unit. */
CNA_C_API CNA_Result cna_shader_effect_set_texture_cube(
    CNA_EffectHandle effect,
    int32_t unit,
    CNA_Handle texture);

/** @brief Binds a Texture3D handle to a shader sampler unit. */
CNA_C_API CNA_Result cna_shader_effect_set_texture3d(
    CNA_EffectHandle effect,
    int32_t unit,
    CNA_Handle texture);

/** @brief Gets the ShaderEffect world matrix. */
CNA_C_API CNA_Result cna_shader_effect_get_world(
    CNA_EffectHandle effect,
    CNA_Matrix* out_value);

/** @brief Sets the ShaderEffect world matrix. */
CNA_C_API CNA_Result cna_shader_effect_set_world(
    CNA_EffectHandle effect,
    CNA_Matrix value);

/** @brief Gets the ShaderEffect view matrix. */
CNA_C_API CNA_Result cna_shader_effect_get_view(
    CNA_EffectHandle effect,
    CNA_Matrix* out_value);

/** @brief Sets the ShaderEffect view matrix. */
CNA_C_API CNA_Result cna_shader_effect_set_view(
    CNA_EffectHandle effect,
    CNA_Matrix value);

/** @brief Gets the ShaderEffect projection matrix. */
CNA_C_API CNA_Result cna_shader_effect_get_projection(
    CNA_EffectHandle effect,
    CNA_Matrix* out_value);

/** @brief Sets the ShaderEffect projection matrix. */
CNA_C_API CNA_Result cna_shader_effect_set_projection(
    CNA_EffectHandle effect,
    CNA_Matrix value);

/** @brief Creates an owned default disabled DirectionalLight. */
CNA_C_API CNA_Result cna_directional_light_create(
    CNA_DirectionalLightHandle* out_light);

/** @brief Destroys a standalone or nested DirectionalLight view handle. */
CNA_C_API CNA_Result cna_directional_light_destroy(
    CNA_DirectionalLightHandle light);

/** @brief Gets a directional light's diffuse color. */
CNA_C_API CNA_Result cna_directional_light_get_diffuse_color(
    CNA_DirectionalLightHandle light,
    CNA_Vector3* out_value);

/** @brief Sets a directional light's diffuse color. */
CNA_C_API CNA_Result cna_directional_light_set_diffuse_color(
    CNA_DirectionalLightHandle light,
    CNA_Vector3 value);

/** @brief Gets the direction in which a directional light shines. */
CNA_C_API CNA_Result cna_directional_light_get_direction(
    CNA_DirectionalLightHandle light,
    CNA_Vector3* out_value);

/** @brief Sets the direction in which a directional light shines. */
CNA_C_API CNA_Result cna_directional_light_set_direction(
    CNA_DirectionalLightHandle light,
    CNA_Vector3 value);

/** @brief Gets a directional light's specular color. */
CNA_C_API CNA_Result cna_directional_light_get_specular_color(
    CNA_DirectionalLightHandle light,
    CNA_Vector3* out_value);

/** @brief Sets a directional light's specular color. */
CNA_C_API CNA_Result cna_directional_light_set_specular_color(
    CNA_DirectionalLightHandle light,
    CNA_Vector3 value);

/** @brief Gets whether a directional light is enabled. */
CNA_C_API CNA_Result cna_directional_light_get_enabled(
    CNA_DirectionalLightHandle light,
    CNA_Bool* out_value);

/** @brief Sets whether a directional light is enabled. */
CNA_C_API CNA_Result cna_directional_light_set_enabled(
    CNA_DirectionalLightHandle light,
    CNA_Bool value);

/**
 * @brief Creates an owned BasicEffect game child.
 * @param graphics_device Borrowed graphics-device handle from an active game callback.
 * @param out_effect Receives the owned BasicEffect handle.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_basic_effect_create(
    CNA_Handle graphics_device,
    CNA_EffectHandle* out_effect);

/** @brief Gets the world matrix through the native IEffectMatrices contract. */
CNA_C_API CNA_Result cna_effect_matrices_get_world(
    CNA_EffectHandle effect,
    CNA_Matrix* out_value);

/** @brief Sets the world matrix through the native IEffectMatrices contract. */
CNA_C_API CNA_Result cna_effect_matrices_set_world(
    CNA_EffectHandle effect,
    CNA_Matrix value);

/** @brief Gets the view matrix through the native IEffectMatrices contract. */
CNA_C_API CNA_Result cna_effect_matrices_get_view(
    CNA_EffectHandle effect,
    CNA_Matrix* out_value);

/** @brief Sets the view matrix through the native IEffectMatrices contract. */
CNA_C_API CNA_Result cna_effect_matrices_set_view(
    CNA_EffectHandle effect,
    CNA_Matrix value);

/** @brief Gets the projection matrix through the native IEffectMatrices contract. */
CNA_C_API CNA_Result cna_effect_matrices_get_projection(
    CNA_EffectHandle effect,
    CNA_Matrix* out_value);

/** @brief Sets the projection matrix through the native IEffectMatrices contract. */
CNA_C_API CNA_Result cna_effect_matrices_set_projection(
    CNA_EffectHandle effect,
    CNA_Matrix value);

/** @brief Gets the fog color through the native IEffectFog contract. */
CNA_C_API CNA_Result cna_effect_fog_get_color(
    CNA_EffectHandle effect,
    CNA_Vector3* out_value);

/** @brief Sets the fog color through the native IEffectFog contract. */
CNA_C_API CNA_Result cna_effect_fog_set_color(
    CNA_EffectHandle effect,
    CNA_Vector3 value);

/** @brief Gets whether fog is enabled through the native IEffectFog contract. */
CNA_C_API CNA_Result cna_effect_fog_get_enabled(
    CNA_EffectHandle effect,
    CNA_Bool* out_value);

/** @brief Sets whether fog is enabled through the native IEffectFog contract. */
CNA_C_API CNA_Result cna_effect_fog_set_enabled(
    CNA_EffectHandle effect,
    CNA_Bool value);

/** @brief Gets the fog start distance through the native IEffectFog contract. */
CNA_C_API CNA_Result cna_effect_fog_get_start(
    CNA_EffectHandle effect,
    float* out_value);

/** @brief Sets the fog start distance through the native IEffectFog contract. */
CNA_C_API CNA_Result cna_effect_fog_set_start(
    CNA_EffectHandle effect,
    float value);

/** @brief Gets the fog end distance through the native IEffectFog contract. */
CNA_C_API CNA_Result cna_effect_fog_get_end(
    CNA_EffectHandle effect,
    float* out_value);

/** @brief Sets the fog end distance through the native IEffectFog contract. */
CNA_C_API CNA_Result cna_effect_fog_set_end(
    CNA_EffectHandle effect,
    float value);

/** @brief Gets the ambient color through the native IEffectLights contract. */
CNA_C_API CNA_Result cna_effect_lights_get_ambient_color(
    CNA_EffectHandle effect,
    CNA_Vector3* out_value);

/** @brief Sets the ambient color through the native IEffectLights contract. */
CNA_C_API CNA_Result cna_effect_lights_set_ambient_color(
    CNA_EffectHandle effect,
    CNA_Vector3 value);

/**
 * @brief Gets one of the three stable directional-light member views.
 * @param effect Effect implementing IEffectLights.
 * @param index Light index in the inclusive range zero through two.
 * @param out_light Receives an owned stable member-view handle.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_effect_lights_get_directional_light(
    CNA_EffectHandle effect,
    uint32_t index,
    CNA_DirectionalLightHandle* out_light);

/** @brief Gets whether lighting is enabled through the native IEffectLights contract. */
CNA_C_API CNA_Result cna_effect_lights_get_enabled(
    CNA_EffectHandle effect,
    CNA_Bool* out_value);

/** @brief Sets whether lighting is enabled through the native IEffectLights contract. */
CNA_C_API CNA_Result cna_effect_lights_set_enabled(
    CNA_EffectHandle effect,
    CNA_Bool value);

/** @brief Applies the native standard three-point lighting preset. */
CNA_C_API CNA_Result cna_effect_lights_enable_default(
    CNA_EffectHandle effect);

/** @brief Gets whether BasicEffect consumes per-vertex color. */
CNA_C_API CNA_Result cna_basic_effect_get_vertex_color_enabled(
    CNA_EffectHandle effect,
    CNA_Bool* out_value);

/** @brief Sets whether BasicEffect consumes per-vertex color. */
CNA_C_API CNA_Result cna_basic_effect_set_vertex_color_enabled(
    CNA_EffectHandle effect,
    CNA_Bool value);

/** @brief Gets whether BasicEffect prefers per-pixel lighting. */
CNA_C_API CNA_Result cna_basic_effect_get_prefer_per_pixel_lighting(
    CNA_EffectHandle effect,
    CNA_Bool* out_value);

/** @brief Sets whether BasicEffect prefers per-pixel lighting. */
CNA_C_API CNA_Result cna_basic_effect_set_prefer_per_pixel_lighting(
    CNA_EffectHandle effect,
    CNA_Bool value);

/** @brief Gets the BasicEffect diffuse material color. */
CNA_C_API CNA_Result cna_basic_effect_get_diffuse_color(
    CNA_EffectHandle effect,
    CNA_Vector3* out_value);

/** @brief Sets the BasicEffect diffuse material color. */
CNA_C_API CNA_Result cna_basic_effect_set_diffuse_color(
    CNA_EffectHandle effect,
    CNA_Vector3 value);

/** @brief Gets the BasicEffect emissive material color. */
CNA_C_API CNA_Result cna_basic_effect_get_emissive_color(
    CNA_EffectHandle effect,
    CNA_Vector3* out_value);

/** @brief Sets the BasicEffect emissive material color. */
CNA_C_API CNA_Result cna_basic_effect_set_emissive_color(
    CNA_EffectHandle effect,
    CNA_Vector3 value);

/** @brief Gets the BasicEffect specular material color. */
CNA_C_API CNA_Result cna_basic_effect_get_specular_color(
    CNA_EffectHandle effect,
    CNA_Vector3* out_value);

/** @brief Sets the BasicEffect specular material color. */
CNA_C_API CNA_Result cna_basic_effect_set_specular_color(
    CNA_EffectHandle effect,
    CNA_Vector3 value);

/** @brief Gets the BasicEffect specular power. */
CNA_C_API CNA_Result cna_basic_effect_get_specular_power(
    CNA_EffectHandle effect,
    float* out_value);

/** @brief Sets the BasicEffect specular power. */
CNA_C_API CNA_Result cna_basic_effect_set_specular_power(
    CNA_EffectHandle effect,
    float value);

/** @brief Gets the BasicEffect alpha value. */
CNA_C_API CNA_Result cna_basic_effect_get_alpha(
    CNA_EffectHandle effect,
    float* out_value);

/** @brief Sets the BasicEffect alpha value. */
CNA_C_API CNA_Result cna_basic_effect_set_alpha(
    CNA_EffectHandle effect,
    float value);

/** @brief Gets whether BasicEffect texture mapping is enabled. */
CNA_C_API CNA_Result cna_basic_effect_get_texture_enabled(
    CNA_EffectHandle effect,
    CNA_Bool* out_value);

/** @brief Sets whether BasicEffect texture mapping is enabled. */
CNA_C_API CNA_Result cna_basic_effect_set_texture_enabled(
    CNA_EffectHandle effect,
    CNA_Bool value);

/**
 * @brief Gets the retained BasicEffect Texture2D handle.
 * @param effect BasicEffect handle.
 * @param out_has_texture Receives whether a texture is assigned.
 * @param out_texture Receives the assigned handle, or the invalid handle.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_basic_effect_get_texture(
    CNA_EffectHandle effect,
    CNA_Bool* out_has_texture,
    CNA_Handle* out_texture);

/**
 * @brief Assigns and retains a same-device Texture2D, or clears with the invalid handle.
 * @param effect BasicEffect handle.
 * @param texture Texture2D/RenderTarget2D handle or `CNA_INVALID_HANDLE`.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_basic_effect_set_texture(
    CNA_EffectHandle effect,
    CNA_Handle texture);

/**
 * @brief Creates an owned AlphaTestEffect for a borrowed graphics device.
 * @param graphics_device Callback-scoped graphics-device handle.
 * @param out_effect Receives the owned effect handle.
 * @return A CNA result code; failure leaves @p out_effect invalid.
 */
CNA_C_API CNA_Result cna_alpha_test_effect_create(
    CNA_Handle graphics_device,
    CNA_EffectHandle* out_effect);

/** @brief Gets the AlphaTestEffect diffuse material color. */
CNA_C_API CNA_Result cna_alpha_test_effect_get_diffuse_color(
    CNA_EffectHandle effect,
    CNA_Vector3* out_value);

/** @brief Sets the AlphaTestEffect diffuse material color. */
CNA_C_API CNA_Result cna_alpha_test_effect_set_diffuse_color(
    CNA_EffectHandle effect,
    CNA_Vector3 value);

/** @brief Gets the AlphaTestEffect alpha value. */
CNA_C_API CNA_Result cna_alpha_test_effect_get_alpha(
    CNA_EffectHandle effect,
    float* out_value);

/** @brief Sets the AlphaTestEffect alpha value without clamping. */
CNA_C_API CNA_Result cna_alpha_test_effect_set_alpha(
    CNA_EffectHandle effect,
    float value);

/**
 * @brief Gets the retained AlphaTestEffect Texture2D handle.
 * @param effect AlphaTestEffect handle.
 * @param out_has_texture Receives whether a texture is assigned.
 * @param out_texture Receives the assigned handle, or the invalid handle.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_alpha_test_effect_get_texture(
    CNA_EffectHandle effect,
    CNA_Bool* out_has_texture,
    CNA_Handle* out_texture);

/**
 * @brief Assigns and retains a same-device Texture2D, or clears with the invalid handle.
 * @param effect AlphaTestEffect handle.
 * @param texture Texture2D/RenderTarget2D handle or `CNA_INVALID_HANDLE`.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_alpha_test_effect_set_texture(
    CNA_EffectHandle effect,
    CNA_Handle texture);

/** @brief Gets whether AlphaTestEffect consumes per-vertex color. */
CNA_C_API CNA_Result cna_alpha_test_effect_get_vertex_color_enabled(
    CNA_EffectHandle effect,
    CNA_Bool* out_value);

/** @brief Sets whether AlphaTestEffect consumes per-vertex color. */
CNA_C_API CNA_Result cna_alpha_test_effect_set_vertex_color_enabled(
    CNA_EffectHandle effect,
    CNA_Bool value);

/** @brief Gets the AlphaTestEffect comparison function. */
CNA_C_API CNA_Result cna_alpha_test_effect_get_alpha_function(
    CNA_EffectHandle effect,
    CNA_CompareFunction* out_value);

/** @brief Sets the AlphaTestEffect comparison function. */
CNA_C_API CNA_Result cna_alpha_test_effect_set_alpha_function(
    CNA_EffectHandle effect,
    CNA_CompareFunction value);

/** @brief Gets the AlphaTestEffect reference alpha. */
CNA_C_API CNA_Result cna_alpha_test_effect_get_reference_alpha(
    CNA_EffectHandle effect,
    int32_t* out_value);

/** @brief Sets the AlphaTestEffect reference alpha without clamping. */
CNA_C_API CNA_Result cna_alpha_test_effect_set_reference_alpha(
    CNA_EffectHandle effect,
    int32_t value);

/**
 * @brief Creates an owned DualTextureEffect for a borrowed graphics device.
 * @param graphics_device Callback-scoped graphics-device handle.
 * @param out_effect Receives the owned effect handle.
 * @return A CNA result code; failure leaves @p out_effect invalid.
 */
CNA_C_API CNA_Result cna_dual_texture_effect_create(
    CNA_Handle graphics_device,
    CNA_EffectHandle* out_effect);

/** @brief Gets the DualTextureEffect diffuse material color. */
CNA_C_API CNA_Result cna_dual_texture_effect_get_diffuse_color(
    CNA_EffectHandle effect,
    CNA_Vector3* out_value);

/** @brief Sets the DualTextureEffect diffuse material color. */
CNA_C_API CNA_Result cna_dual_texture_effect_set_diffuse_color(
    CNA_EffectHandle effect,
    CNA_Vector3 value);

/** @brief Gets the DualTextureEffect alpha value. */
CNA_C_API CNA_Result cna_dual_texture_effect_get_alpha(
    CNA_EffectHandle effect,
    float* out_value);

/** @brief Sets the DualTextureEffect alpha value without clamping. */
CNA_C_API CNA_Result cna_dual_texture_effect_set_alpha(
    CNA_EffectHandle effect,
    float value);

/**
 * @brief Gets one retained DualTextureEffect Texture2D handle.
 * @param effect DualTextureEffect handle.
 * @param texture_index Texture layer index, zero or one.
 * @param out_has_texture Receives whether the layer has a texture.
 * @param out_texture Receives the assigned handle, or the invalid handle.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_dual_texture_effect_get_texture(
    CNA_EffectHandle effect,
    uint32_t texture_index,
    CNA_Bool* out_has_texture,
    CNA_Handle* out_texture);

/**
 * @brief Assigns and retains one same-device Texture2D layer, or clears it.
 * @param effect DualTextureEffect handle.
 * @param texture_index Texture layer index, zero or one.
 * @param texture Texture2D/RenderTarget2D handle or `CNA_INVALID_HANDLE`.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_dual_texture_effect_set_texture(
    CNA_EffectHandle effect,
    uint32_t texture_index,
    CNA_Handle texture);

/** @brief Gets whether DualTextureEffect consumes per-vertex color. */
CNA_C_API CNA_Result cna_dual_texture_effect_get_vertex_color_enabled(
    CNA_EffectHandle effect,
    CNA_Bool* out_value);

/** @brief Sets whether DualTextureEffect consumes per-vertex color. */
CNA_C_API CNA_Result cna_dual_texture_effect_set_vertex_color_enabled(
    CNA_EffectHandle effect,
    CNA_Bool value);

/**
 * @brief Creates an owned EnvironmentMapEffect for a borrowed graphics device.
 * @param graphics_device Callback-scoped graphics-device handle.
 * @param out_effect Receives the owned effect handle.
 * @return A CNA result code; failure leaves @p out_effect invalid.
 */
CNA_C_API CNA_Result cna_environment_map_effect_create(
    CNA_Handle graphics_device,
    CNA_EffectHandle* out_effect);

/** @brief Gets the EnvironmentMapEffect diffuse material color. */
CNA_C_API CNA_Result cna_environment_map_effect_get_diffuse_color(
    CNA_EffectHandle effect,
    CNA_Vector3* out_value);

/** @brief Sets the EnvironmentMapEffect diffuse material color. */
CNA_C_API CNA_Result cna_environment_map_effect_set_diffuse_color(
    CNA_EffectHandle effect,
    CNA_Vector3 value);

/** @brief Gets the EnvironmentMapEffect emissive material color. */
CNA_C_API CNA_Result cna_environment_map_effect_get_emissive_color(
    CNA_EffectHandle effect,
    CNA_Vector3* out_value);

/** @brief Sets the EnvironmentMapEffect emissive material color. */
CNA_C_API CNA_Result cna_environment_map_effect_set_emissive_color(
    CNA_EffectHandle effect,
    CNA_Vector3 value);

/** @brief Gets the EnvironmentMapEffect alpha value. */
CNA_C_API CNA_Result cna_environment_map_effect_get_alpha(
    CNA_EffectHandle effect,
    float* out_value);

/** @brief Sets the EnvironmentMapEffect alpha value without clamping. */
CNA_C_API CNA_Result cna_environment_map_effect_set_alpha(
    CNA_EffectHandle effect,
    float value);

/**
 * @brief Gets the retained EnvironmentMapEffect diffuse Texture2D handle.
 * @param effect EnvironmentMapEffect handle.
 * @param out_has_texture Receives whether a diffuse texture is assigned.
 * @param out_texture Receives the assigned handle, or the invalid handle.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_environment_map_effect_get_texture(
    CNA_EffectHandle effect,
    CNA_Bool* out_has_texture,
    CNA_Handle* out_texture);

/**
 * @brief Assigns and retains a same-device diffuse Texture2D, or clears it.
 * @param effect EnvironmentMapEffect handle.
 * @param texture Texture2D/RenderTarget2D handle or `CNA_INVALID_HANDLE`.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_environment_map_effect_set_texture(
    CNA_EffectHandle effect,
    CNA_Handle texture);

/**
 * @brief Gets the retained EnvironmentMapEffect TextureCube handle.
 * @param effect EnvironmentMapEffect handle.
 * @param out_has_environment_map Receives whether a cube map is assigned.
 * @param out_environment_map Receives the assigned handle, or the invalid handle.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_environment_map_effect_get_environment_map(
    CNA_EffectHandle effect,
    CNA_Bool* out_has_environment_map,
    CNA_Handle* out_environment_map);

/**
 * @brief Assigns and retains a same-device TextureCube, or clears it.
 * @param effect EnvironmentMapEffect handle.
 * @param environment_map TextureCube/RenderTargetCube handle or `CNA_INVALID_HANDLE`.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_environment_map_effect_set_environment_map(
    CNA_EffectHandle effect,
    CNA_Handle environment_map);

/** @brief Gets the EnvironmentMapEffect blend amount. */
CNA_C_API CNA_Result cna_environment_map_effect_get_amount(
    CNA_EffectHandle effect,
    float* out_value);

/** @brief Sets the EnvironmentMapEffect blend amount without clamping. */
CNA_C_API CNA_Result cna_environment_map_effect_set_amount(
    CNA_EffectHandle effect,
    float value);

/** @brief Gets the EnvironmentMapEffect specular tint. */
CNA_C_API CNA_Result cna_environment_map_effect_get_specular(
    CNA_EffectHandle effect,
    CNA_Vector3* out_value);

/** @brief Sets the EnvironmentMapEffect specular tint. */
CNA_C_API CNA_Result cna_environment_map_effect_set_specular(
    CNA_EffectHandle effect,
    CNA_Vector3 value);

/** @brief Gets the EnvironmentMapEffect Fresnel factor. */
CNA_C_API CNA_Result cna_environment_map_effect_get_fresnel_factor(
    CNA_EffectHandle effect,
    float* out_value);

/** @brief Sets the EnvironmentMapEffect Fresnel factor without clamping. */
CNA_C_API CNA_Result cna_environment_map_effect_set_fresnel_factor(
    CNA_EffectHandle effect,
    float value);

/**
 * @brief Creates an owned SkinnedEffect for a borrowed graphics device.
 * @param graphics_device Callback-scoped graphics-device handle.
 * @param out_effect Receives the owned effect handle.
 * @return A CNA result code; failure leaves @p out_effect invalid.
 */
CNA_C_API CNA_Result cna_skinned_effect_create(
    CNA_Handle graphics_device,
    CNA_EffectHandle* out_effect);

/** @brief Gets the SkinnedEffect diffuse material color. */
CNA_C_API CNA_Result cna_skinned_effect_get_diffuse_color(
    CNA_EffectHandle effect,
    CNA_Vector3* out_value);

/** @brief Sets the SkinnedEffect diffuse material color. */
CNA_C_API CNA_Result cna_skinned_effect_set_diffuse_color(
    CNA_EffectHandle effect,
    CNA_Vector3 value);

/** @brief Gets the SkinnedEffect emissive material color. */
CNA_C_API CNA_Result cna_skinned_effect_get_emissive_color(
    CNA_EffectHandle effect,
    CNA_Vector3* out_value);

/** @brief Sets the SkinnedEffect emissive material color. */
CNA_C_API CNA_Result cna_skinned_effect_set_emissive_color(
    CNA_EffectHandle effect,
    CNA_Vector3 value);

/** @brief Gets the SkinnedEffect specular material color. */
CNA_C_API CNA_Result cna_skinned_effect_get_specular_color(
    CNA_EffectHandle effect,
    CNA_Vector3* out_value);

/** @brief Sets the SkinnedEffect specular material color. */
CNA_C_API CNA_Result cna_skinned_effect_set_specular_color(
    CNA_EffectHandle effect,
    CNA_Vector3 value);

/** @brief Gets the SkinnedEffect specular power. */
CNA_C_API CNA_Result cna_skinned_effect_get_specular_power(
    CNA_EffectHandle effect,
    float* out_value);

/** @brief Sets the SkinnedEffect specular power without clamping. */
CNA_C_API CNA_Result cna_skinned_effect_set_specular_power(
    CNA_EffectHandle effect,
    float value);

/** @brief Gets the SkinnedEffect alpha value. */
CNA_C_API CNA_Result cna_skinned_effect_get_alpha(
    CNA_EffectHandle effect,
    float* out_value);

/** @brief Sets the SkinnedEffect alpha value without clamping. */
CNA_C_API CNA_Result cna_skinned_effect_set_alpha(
    CNA_EffectHandle effect,
    float value);

/** @brief Gets whether SkinnedEffect prefers per-pixel lighting. */
CNA_C_API CNA_Result cna_skinned_effect_get_prefer_per_pixel_lighting(
    CNA_EffectHandle effect,
    CNA_Bool* out_value);

/** @brief Sets whether SkinnedEffect prefers per-pixel lighting. */
CNA_C_API CNA_Result cna_skinned_effect_set_prefer_per_pixel_lighting(
    CNA_EffectHandle effect,
    CNA_Bool value);

/**
 * @brief Gets the retained SkinnedEffect Texture2D handle.
 * @param effect SkinnedEffect handle.
 * @param out_has_texture Receives whether a texture is assigned.
 * @param out_texture Receives the assigned handle, or the invalid handle.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_skinned_effect_get_texture(
    CNA_EffectHandle effect,
    CNA_Bool* out_has_texture,
    CNA_Handle* out_texture);

/**
 * @brief Assigns and retains a same-device Texture2D, or clears it.
 * @param effect SkinnedEffect handle.
 * @param texture Texture2D/RenderTarget2D handle or `CNA_INVALID_HANDLE`.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_skinned_effect_set_texture(
    CNA_EffectHandle effect,
    CNA_Handle texture);

/** @brief Gets the SkinnedEffect weights-per-vertex value. */
CNA_C_API CNA_Result cna_skinned_effect_get_weights_per_vertex(
    CNA_EffectHandle effect,
    int32_t* out_value);

/** @brief Sets weights per vertex to one, two, or four. */
CNA_C_API CNA_Result cna_skinned_effect_set_weights_per_vertex(
    CNA_EffectHandle effect,
    int32_t value);

/**
 * @brief Replaces the leading SkinnedEffect bone transforms from a copied array.
 * @param effect SkinnedEffect handle.
 * @param transforms Caller-owned row-major matrices.
 * @param transform_count Matrix count in the inclusive range 1 through 72.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_skinned_effect_set_bone_transforms(
    CNA_EffectHandle effect,
    const CNA_Matrix* transforms,
    uint64_t transform_count);

/**
 * @brief Copies the requested leading SkinnedEffect bone transforms atomically.
 * @param effect SkinnedEffect handle.
 * @param requested_count Number of transforms in the inclusive range 1 through 72.
 * @param destination Destination array, or null only for zero capacity.
 * @param capacity Destination capacity in matrices.
 * @param out_count Receives the requested count.
 * @return A CNA result code; insufficient capacity performs no partial write.
 */
CNA_C_API CNA_Result cna_skinned_effect_copy_bone_transforms(
    CNA_EffectHandle effect,
    uint64_t requested_count,
    CNA_Matrix* destination,
    uint64_t capacity,
    uint64_t* out_count);

/** @brief Gets the CNA extension that enables per-vertex color on SkinnedEffect. */
CNA_C_API CNA_Result cna_skinned_effect_get_vertex_color_enabled(
    CNA_EffectHandle effect,
    CNA_Bool* out_value);

/** @brief Sets the CNA extension that enables per-vertex color on SkinnedEffect. */
CNA_C_API CNA_Result cna_skinned_effect_set_vertex_color_enabled(
    CNA_EffectHandle effect,
    CNA_Bool value);

#ifdef __cplusplus
}
#endif

#endif
