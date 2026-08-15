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

#ifdef __cplusplus
}
#endif

#endif
