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

/** @brief Maximum number of matrices in a SkinnedPbrEffect bone palette. */
#define CNA_SKINNED_PBR_EFFECT_MAX_BONES UINT32_C(72)

/** @brief Row-major 4-by-4 color transform used by ColorMatrixEffect. */
typedef struct CNA_ColorMatrix4x4 {
    /** @brief Sixteen row-major matrix elements. */
    float values[16];
} CNA_ColorMatrix4x4;

/** @brief Fixed-width PBR Texture2D slot identity. */
typedef uint32_t CNA_PbrTextureSlot;
/** @brief Base-color (albedo) texture slot. */
#define CNA_PBR_TEXTURE_BASE_COLOR UINT32_C(0)
/** @brief Tangent-space normal-map texture slot. */
#define CNA_PBR_TEXTURE_NORMAL UINT32_C(1)
/** @brief glTF metallic-roughness texture slot. */
#define CNA_PBR_TEXTURE_METALLIC_ROUGHNESS UINT32_C(2)
/** @brief Emissive texture slot. */
#define CNA_PBR_TEXTURE_EMISSIVE UINT32_C(3)
/** @brief Occlusion texture slot. */
#define CNA_PBR_TEXTURE_OCCLUSION UINT32_C(4)
/** @brief `KHR_materials_specular` scalar strength map slot; sampled from the alpha channel. */
#define CNA_PBR_TEXTURE_SPECULAR_EXT UINT32_C(5)
/** @brief `KHR_materials_specular` colour map slot; sRGB encoded by default. */
#define CNA_PBR_TEXTURE_SPECULAR_COLOR_EXT UINT32_C(6)
/** @brief Highest defined PBR texture slot identity. */
#define CNA_PBR_TEXTURE_MAXIMUM CNA_PBR_TEXTURE_SPECULAR_COLOR_EXT

/**
 * @brief Fixed-width glTF alpha-coverage identity.
 *
 * CNA extension: XNA has no material alpha mode. The values match
 * `Microsoft::Xna::Framework::Graphics::AlphaModeEXT`.
 */
typedef uint32_t CNA_AlphaModeEXT;
/** @brief The rendered output is fully opaque and any alpha is ignored. */
#define CNA_ALPHA_MODE_OPAQUE_EXT UINT32_C(0)
/** @brief Alpha is compared against the cutoff and the fragment is kept or discarded. */
#define CNA_ALPHA_MODE_MASK_EXT UINT32_C(1)
/** @brief Alpha blends the fragment with what is already there. */
#define CNA_ALPHA_MODE_BLEND_EXT UINT32_C(2)
/** @brief Highest defined alpha-mode identity. */
#define CNA_ALPHA_MODE_MAXIMUM_EXT CNA_ALPHA_MODE_BLEND_EXT

/**
 * @brief A texture coordinate's scale-rotate-translate transform, as `KHR_texture_transform`.
 *
 * CNA extension. The transform is independent of which packed UV channel a slot samples: the
 * selected coordinate is scaled, then rotated, then translated, and the result is sampled.
 */
typedef struct CNA_TextureTransformEXT {
    /** @brief Size of this caller-provided structure in bytes. */
    uint32_t struct_size;

    /** @brief Version of this caller-provided structure. */
    uint32_t struct_version;

    /** @brief Translation applied after scaling and rotation; zero by default. */
    CNA_Vector2 offset;

    /** @brief Per-axis scale; one by default. */
    CNA_Vector2 scale;

    /** @brief Counter-clockwise rotation in radians; zero by default. */
    float rotation;
} CNA_TextureTransformEXT;

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
 * @brief Creates an ownerless native pass that also states its runtime index.
 *
 * The companion to @ref cna_effect_pass_create for the shape a compiled effect's reflection
 * produces. The two trailing values are separate facts: `technique_identity` says which technique
 * owns the pass, `pass_index` says where the pass sits inside it.
 *
 * @param name UTF-8 pass name copied by the call.
 * @param technique_identity Owning-technique identity metadata, or zero.
 * @param pass_index Zero-based runtime index this pass reports.
 * @param out_pass Receives the owned pass handle.
 * @return A CNA result code; failure leaves @p out_pass invalid.
 */
CNA_C_API CNA_Result cna_effect_pass_create_indexed_ext(
    CNA_StringView name,
    uint64_t technique_identity,
    uint32_t pass_index,
    CNA_EffectPassHandle* out_pass);

/**
 * @brief Gets the pass's zero-based runtime index within a compiled effect.
 *
 * A pass built by @ref cna_effect_pass_create, or the canonical default `P0` pass, reports zero.
 *
 * @param pass Pass handle.
 * @param out_index Receives the zero-based runtime index.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_effect_pass_get_index_ext(
    CNA_EffectPassHandle pass,
    uint32_t* out_index);

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
 * @brief Gets the technique's zero-based runtime index within a compiled effect.
 *
 * Distinct from @ref cna_effect_technique_get_identity: the identity is unique per constructed
 * technique and says nothing about ordering, while this is the position the compiled effect's own
 * reflection assigned. A technique built by @ref cna_effect_technique_create_default or
 * @ref cna_effect_technique_create_named reports zero, because it belongs to no compiled effect.
 *
 * @param technique Technique handle.
 * @param out_index Receives the zero-based runtime index.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_effect_technique_get_index_ext(
    CNA_EffectTechniqueHandle technique,
    uint32_t* out_index);

/**
 * @brief Creates an ownerless technique carrying a compiled effect's reflected index.
 *
 * The companion to @ref cna_effect_technique_create_named for the shape a compiled effect's
 * reflection produces: the technique states its own runtime index, and says whether it starts with
 * the canonical default `P0` pass or with an empty pass list that reflected passes are appended to.
 *
 * @param name UTF-8 technique name copied by the call.
 * @param technique_index Zero-based runtime index this technique reports.
 * @param add_default_pass `CNA_TRUE` to start with the canonical default pass, `CNA_FALSE` to
 * start with no passes at all.
 * @param out_technique Receives the owned technique handle.
 * @return A CNA result code; failure leaves @p out_technique invalid.
 */
CNA_C_API CNA_Result cna_effect_technique_create_reflected_ext(
    CNA_StringView name,
    uint32_t technique_index,
    CNA_Bool add_default_pass,
    CNA_EffectTechniqueHandle* out_technique);

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
 * @brief Creates an Effect from compiled XNA/FNA Effect Framework bytecode.
 *
 * @param graphics_device Borrowed graphics-device handle from an active game callback.
 * @param effect_code Bytecode bytes copied during the call; the caller keeps its buffer.
 * @param effect_code_count Number of bytes at @p effect_code; must be positive.
 * @param out_effect Receives the owned effect handle on success, destroyed with
 *        `cna_effect_destroy`. Every failure leaves it `CNA_INVALID_HANDLE`.
 * @return `CNA_RESULT_SUCCESS`, or one of the failures below.
 *
 * ### What this accepts
 *
 * The Direct3D 9 Effect Framework binary an XNA or FNA game ships as content -- a `.fxb`,
 * including the extra wrapper the XNA 4 effect compiler prepends -- and the identical `Effect`
 * payload carried inside an XNB asset. The reflected object graph is then reachable through
 * `cna_effect_get_parameters`, `cna_effect_get_techniques` and their collections, and a pass
 * applies and draws like any other effect.
 *
 * Three things are deliberately **not** accepted, each refused by name rather than guessed at:
 * MonoGame's `MGFX`/`.mgfxo` container, which is a different format; HLSL `.fx` **source**, since
 * this runtime embeds no HLSL compiler and the XNA/FNA toolchain must compile it first; and
 * GLSL/SPIR-V/Metal source pairs, which are `cna_shader_effect_create`'s subject, a separate API
 * behind a separate capability.
 *
 * ### Which builds accept it
 *
 * Support is a renderer property, not a property of this ABI, and
 * `cna_graphics_device_supports_capability` with `CNA_GRAPHICS_CAPABILITY_COMPILED_EFFECTS`
 * answers it for the running build. It is true for the `FNA3D` renderer always, and for the
 * `SDL_GPU`, `VULKAN` and EasyGL-family (`OPENGLES2`, `OPENGLES3`, `OPENGL33`, `WEBGL1`,
 * `WEBGL2`) renderers when their build option is on -- the effect runtime is a fetched dependency
 * those families do not otherwise need, so the capability never claims more than the binary
 * actually contains. Every other renderer identity reports false and refuses the bytecode rather
 * than quietly drawing with a stock shader, because a silent fallback makes a porting bug look
 * like an art bug. `docs/fx-compiled-effects.md` is the full matrix, including which limitations
 * are renderer-wide and which are specific to compiled effects.
 *
 * ### Failures
 *
 * - `CNA_RESULT_INVALID_ARGUMENT` -- a null output, an empty buffer, a buffer whose pointer and
 *   count disagree, or bytes without a structurally valid Effect Framework header. An argument is
 *   judged before any renderer is consulted, so these hold in every build.
 * - `CNA_RESULT_NOT_SUPPORTED` -- a recognized container this constructor does not accept (the
 *   `MGFX` case above), or a renderer whose `CNA_GRAPHICS_CAPABILITY_COMPILED_EFFECTS` is false.
 *   The message names which of the two it was.
 * - `CNA_RESULT_INVALID_STATE`, `CNA_RESULT_OVERFLOW`, `CNA_RESULT_OUT_OF_MEMORY` -- a
 *   structurally valid binary whose reflected graph is inconsistent, exceeds a documented bound
 *   (the payload is capped at 64 MiB), or cannot be allocated.
 *
 * Treat a binary from outside the application as untrusted input: it is bounded and
 * arithmetic-checked and has been fuzzed hard, which is a measured bound rather than a proof.
 */
CNA_C_API CNA_Result cna_effect_create_compiled(
    CNA_Handle graphics_device,
    const uint8_t* effect_code,
    uint64_t effect_code_count,
    CNA_EffectHandle* out_effect);

/**
 * @brief Loads an Effect asset as an owned effect handle.
 *
 * @param content_manager Owned content-manager handle.
 * @param asset_name UTF-8 logical asset name, with or without its extension.
 * @param out_effect Receives an owned effect handle on success, destroyed with
 *        `cna_effect_destroy`. Failure leaves it `CNA_INVALID_HANDLE`.
 * @return `CNA_RESULT_SUCCESS`; `CNA_RESULT_IO` for a missing, malformed or wrongly-typed asset;
 *         `CNA_RESULT_NOT_SUPPORTED` when the asset is compiled Effect Framework bytecode and the
 *         active renderer's `CNA_GRAPHICS_CAPABILITY_COMPILED_EFFECTS` is false, or when it names a
 *         shader the renderer cannot compile; or a documented argument/handle/thread failure.
 *
 * This maps the canonical `Load<Effect>` specialization, which is the route an XNA game's
 * `ContentManager.Load<Effect>` takes, and it reads all three shapes CNA supports: a compiled
 * `.xnb` Effect asset, a `.cnj` descriptor naming one of the stock effects, and a `.cnj` descriptor
 * carrying custom shader source. What comes back is an ordinary effect handle -- parameters,
 * techniques, passes and `cna_effect_apply` all behave as they do for an effect built by hand.
 *
 * Which of the three an asset is decides which failures are possible, so branch on the result
 * rather than on the file name: only the compiled shape depends on the compiled-effect capability,
 * and `cna_graphics_device_supports_capability` answers that in advance.
 */
CNA_C_API CNA_Result cna_content_manager_load_effect(
    CNA_Handle content_manager,
    CNA_StringView asset_name,
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

/**
 * @brief Gets whether an effect carries a compiled Effect Framework runtime.
 *
 * The canonical accessor hands back the runtime object itself, which is renderer-owned
 * implementation a C caller can neither construct nor call into, so what crosses the ABI is the
 * one fact a caller can act on: whether this effect came from compiled bytecode. `CNA_TRUE` means
 * its parameters, techniques and passes were reflected out of that bytecode rather than built by
 * hand, and that @ref cna_effect_clone will clone the runtime with it.
 *
 * @param effect Effect handle.
 * @param out_is_compiled Receives `CNA_TRUE` when a compiled runtime is present.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_effect_get_is_compiled_ext(
    CNA_EffectHandle effect,
    CNA_Bool* out_is_compiled);

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

/**
 * @brief Declares the std140 uniform block this effect's parameters live in.
 *
 * @param effect Owned ShaderEffect handle.
 * @param block_size_bytes Size of the whole block in bytes, std140-padded; must not be negative.
 * @param names Array of @p count UTF-8 member names, copied by the call. May be null only when
 *        @p count is zero.
 * @param offsets Array of @p count byte offsets from the start of the block, one per name. May be
 *        null only when @p count is zero.
 * @param count Number of members; zero clears any previous declaration.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for a malformed array pair, a name
 *         that is not valid UTF-8, or a count above the canonical range, or a documented
 *         handle/thread failure.
 *
 * Required on a renderer whose shading dialect has no loose (non-block) uniforms -- every SPIR-V
 * target -- and harmlessly ignored everywhere else, so the same call can sit unconditionally
 * beside the effect's construction. Ask `cna_graphics_device_get_shader_dialect_ext` which dialect
 * the active renderer wants.
 */
CNA_C_API CNA_Result cna_shader_effect_declare_uniform_block_ext(
    CNA_EffectHandle effect,
    int32_t block_size_bytes,
    const CNA_StringView* names,
    const int32_t* offsets,
    uint64_t count);

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

/**
 * @brief Creates an owned ColorMatrixEffect for a borrowed graphics device.
 * @param graphics_device Callback-scoped graphics-device handle.
 * @param out_effect Receives the owned effect handle.
 * @return A CNA result code; failure leaves @p out_effect invalid.
 */
CNA_C_API CNA_Result cna_color_matrix_effect_create(
    CNA_Handle graphics_device,
    CNA_EffectHandle* out_effect);

/** @brief Gets the ColorMatrixEffect row-major transform matrix. */
CNA_C_API CNA_Result cna_color_matrix_effect_get_matrix(
    CNA_EffectHandle effect,
    CNA_ColorMatrix4x4* out_value);

/** @brief Sets a finite row-major ColorMatrixEffect transform matrix. */
CNA_C_API CNA_Result cna_color_matrix_effect_set_matrix(
    CNA_EffectHandle effect,
    CNA_ColorMatrix4x4 value);

/** @brief Gets the ColorMatrixEffect post-transform RGBA offset. */
CNA_C_API CNA_Result cna_color_matrix_effect_get_offset(
    CNA_EffectHandle effect,
    CNA_Vector4* out_value);

/** @brief Sets a finite ColorMatrixEffect post-transform RGBA offset. */
CNA_C_API CNA_Result cna_color_matrix_effect_set_offset(
    CNA_EffectHandle effect,
    CNA_Vector4 value);

/** @brief Selects the Rec. 709 grayscale transform and zero offset. */
CNA_C_API CNA_Result cna_color_matrix_effect_set_grayscale(CNA_EffectHandle effect);

/** @brief Restores the identity color transform and zero offset. */
CNA_C_API CNA_Result cna_color_matrix_effect_reset(CNA_EffectHandle effect);

/**
 * @brief Creates an owned PbrEffect for a borrowed graphics device.
 * @param graphics_device Callback-scoped graphics-device handle.
 * @param out_effect Receives the owned effect handle.
 * @return A CNA result code; failure leaves @p out_effect invalid.
 */
CNA_C_API CNA_Result cna_pbr_effect_create(
    CNA_Handle graphics_device,
    CNA_EffectHandle* out_effect);

/**
 * @brief Creates an owned SkinnedPbrEffect for a borrowed graphics device.
 * @param graphics_device Callback-scoped graphics-device handle.
 * @param out_effect Receives the owned effect handle.
 * @return A CNA result code; failure leaves @p out_effect invalid.
 */
CNA_C_API CNA_Result cna_skinned_pbr_effect_create(
    CNA_Handle graphics_device,
    CNA_EffectHandle* out_effect);

/** @brief Gets the base-color factor from a PbrEffect or SkinnedPbrEffect. */
CNA_C_API CNA_Result cna_pbr_effect_get_diffuse_color(
    CNA_EffectHandle effect,
    CNA_Vector3* out_value);

/** @brief Sets the base-color factor on a PbrEffect or SkinnedPbrEffect. */
CNA_C_API CNA_Result cna_pbr_effect_set_diffuse_color(
    CNA_EffectHandle effect,
    CNA_Vector3 value);

/** @brief Gets material opacity from a PbrEffect or SkinnedPbrEffect. */
CNA_C_API CNA_Result cna_pbr_effect_get_alpha(
    CNA_EffectHandle effect,
    float* out_value);

/** @brief Sets material opacity without clamping on a PbrEffect or SkinnedPbrEffect. */
CNA_C_API CNA_Result cna_pbr_effect_set_alpha(
    CNA_EffectHandle effect,
    float value);

/**
 * @brief Gets one retained PBR Texture2D handle.
 * @param effect PbrEffect or SkinnedPbrEffect handle.
 * @param slot One of the five `CNA_PBR_TEXTURE_*` identities.
 * @param out_has_texture Receives whether the slot has a texture.
 * @param out_texture Receives the assigned handle, or the invalid handle.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_pbr_effect_get_texture(
    CNA_EffectHandle effect,
    CNA_PbrTextureSlot slot,
    CNA_Bool* out_has_texture,
    CNA_Handle* out_texture);

/**
 * @brief Assigns and retains one same-device PBR Texture2D, or clears it.
 * @param effect PbrEffect or SkinnedPbrEffect handle.
 * @param slot One of the five `CNA_PBR_TEXTURE_*` identities.
 * @param texture Texture2D/RenderTarget2D handle or `CNA_INVALID_HANDLE`.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_pbr_effect_set_texture(
    CNA_EffectHandle effect,
    CNA_PbrTextureSlot slot,
    CNA_Handle texture);

/** @brief Gets the metallic factor from a PbrEffect or SkinnedPbrEffect. */
CNA_C_API CNA_Result cna_pbr_effect_get_metallic_factor(
    CNA_EffectHandle effect,
    float* out_value);

/** @brief Sets the metallic factor without clamping. */
CNA_C_API CNA_Result cna_pbr_effect_set_metallic_factor(
    CNA_EffectHandle effect,
    float value);

/** @brief Gets the roughness factor from a PbrEffect or SkinnedPbrEffect. */
CNA_C_API CNA_Result cna_pbr_effect_get_roughness_factor(
    CNA_EffectHandle effect,
    float* out_value);

/** @brief Sets the roughness factor without clamping. */
CNA_C_API CNA_Result cna_pbr_effect_set_roughness_factor(
    CNA_EffectHandle effect,
    float value);

/** @brief Gets the emissive factor from a PbrEffect or SkinnedPbrEffect. */
CNA_C_API CNA_Result cna_pbr_effect_get_emissive_factor(
    CNA_EffectHandle effect,
    CNA_Vector3* out_value);

/** @brief Sets the emissive factor on a PbrEffect or SkinnedPbrEffect. */
CNA_C_API CNA_Result cna_pbr_effect_set_emissive_factor(
    CNA_EffectHandle effect,
    CNA_Vector3 value);

/**
 * @brief Fills a texture transform with its documented defaults.
 *
 * Identity: no offset, unit scale, no rotation. Call this before setting individual fields so the
 * size and version headers are correct for the library that was built against them.
 *
 * @param out_transform Destination transform.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` when @p out_transform is null.
 */
CNA_C_API CNA_Result cna_texture_transform_ext_init(CNA_TextureTransformEXT* out_transform);

/**
 * @brief Compares two texture transforms field by field.
 * @param left First transform.
 * @param right Second transform.
 * @param out_equal Receives whether every field is equal.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null or malformed argument.
 */
CNA_C_API CNA_Result cna_texture_transform_ext_equals(
    const CNA_TextureTransformEXT* left,
    const CNA_TextureTransformEXT* right,
    CNA_Bool* out_equal);

/** @brief Gets `KHR_materials_ior`'s index of refraction; 1.5 by default. */
CNA_C_API CNA_Result cna_pbr_effect_get_ior_ext(CNA_EffectHandle effect, float* out_value);

/** @brief Sets the dielectric index of refraction without clamping. */
CNA_C_API CNA_Result cna_pbr_effect_set_ior_ext(CNA_EffectHandle effect, float value);

/** @brief Gets `KHR_materials_specular`'s reflection strength; 1 by default. */
CNA_C_API CNA_Result cna_pbr_effect_get_specular_factor_ext(
    CNA_EffectHandle effect,
    float* out_value);

/** @brief Sets the dielectric specular strength without clamping. */
CNA_C_API CNA_Result cna_pbr_effect_set_specular_factor_ext(CNA_EffectHandle effect, float value);

/** @brief Gets the linear-RGB F0 colour factor; white by default. */
CNA_C_API CNA_Result cna_pbr_effect_get_specular_color_factor_ext(
    CNA_EffectHandle effect,
    CNA_Vector3* out_value);

/** @brief Sets the dielectric F0 colour factor. */
CNA_C_API CNA_Result cna_pbr_effect_set_specular_color_factor_ext(
    CNA_EffectHandle effect,
    CNA_Vector3 value);

/** @brief Gets how far the bound normal map perturbs the surface; 1 by default. */
CNA_C_API CNA_Result cna_pbr_effect_get_normal_scale_ext(
    CNA_EffectHandle effect,
    float* out_value);

/** @brief Sets the normal scale; 0 flattens the map, 1 is as authored. */
CNA_C_API CNA_Result cna_pbr_effect_set_normal_scale_ext(CNA_EffectHandle effect, float value);

/** @brief Gets how far the bound occlusion map darkens; 1 by default. */
CNA_C_API CNA_Result cna_pbr_effect_get_occlusion_strength_ext(
    CNA_EffectHandle effect,
    float* out_value);

/** @brief Sets the occlusion strength; 0 disables occlusion, 1 applies the map as authored. */
CNA_C_API CNA_Result cna_pbr_effect_set_occlusion_strength_ext(
    CNA_EffectHandle effect,
    float value);

/**
 * @brief Gets the packed vertex UV channel one texture slot samples.
 * @param effect PbrEffect or SkinnedPbrEffect handle.
 * @param slot One of the `CNA_PBR_TEXTURE_*` identities.
 * @param out_value Receives the packed UV channel, 0 or 1.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_pbr_effect_get_texture_coordinate_set_ext(
    CNA_EffectHandle effect,
    CNA_PbrTextureSlot slot,
    int32_t* out_value);

/**
 * @brief Selects the packed vertex UV channel for one texture slot.
 * @param effect PbrEffect or SkinnedPbrEffect handle.
 * @param slot One of the `CNA_PBR_TEXTURE_*` identities.
 * @param value Packed UV channel, either 0 or 1.
 * @return `CNA_RESULT_INVALID_ARGUMENT` for an undefined slot or a channel outside [0,1].
 */
CNA_C_API CNA_Result cna_pbr_effect_set_texture_coordinate_set_ext(
    CNA_EffectHandle effect,
    CNA_PbrTextureSlot slot,
    int32_t value);

/**
 * @brief Gets one texture slot's scale-rotate-translate transform.
 * @param effect PbrEffect or SkinnedPbrEffect handle.
 * @param slot One of the `CNA_PBR_TEXTURE_*` identities.
 * @param out_transform Receives the transform; its size and version headers must be set.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_pbr_effect_get_texture_transform_ext(
    CNA_EffectHandle effect,
    CNA_PbrTextureSlot slot,
    CNA_TextureTransformEXT* out_transform);

/**
 * @brief Sets one texture slot's scale-rotate-translate transform.
 * @param effect PbrEffect or SkinnedPbrEffect handle.
 * @param slot One of the `CNA_PBR_TEXTURE_*` identities.
 * @param transform The new transform.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_pbr_effect_set_texture_transform_ext(
    CNA_EffectHandle effect,
    CNA_PbrTextureSlot slot,
    const CNA_TextureTransformEXT* transform);

/**
 * @brief Gets whether one texture slot's samples are sRGB encoded.
 *
 * Only the three colour-carrying slots have this flag: base colour, emissive and specular colour.
 * The others are linear data by definition, and asking about them is refused rather than answered
 * with a value that would mean nothing.
 *
 * @param effect PbrEffect or SkinnedPbrEffect handle.
 * @param slot `CNA_PBR_TEXTURE_BASE_COLOR`, `CNA_PBR_TEXTURE_EMISSIVE` or
 *             `CNA_PBR_TEXTURE_SPECULAR_COLOR_EXT`.
 * @param out_value Receives whether the slot's samples require sRGB decoding.
 * @return `CNA_RESULT_INVALID_ARGUMENT` for any other slot.
 */
CNA_C_API CNA_Result cna_pbr_effect_get_texture_is_srgb_ext(
    CNA_EffectHandle effect,
    CNA_PbrTextureSlot slot,
    CNA_Bool* out_value);

/**
 * @brief Sets whether one texture slot's samples require sRGB decoding.
 * @param effect PbrEffect or SkinnedPbrEffect handle.
 * @param slot One of the three colour-carrying slots documented by the getter.
 * @param value Whether the samples are sRGB encoded.
 * @return `CNA_RESULT_INVALID_ARGUMENT` for any other slot.
 */
CNA_C_API CNA_Result cna_pbr_effect_set_texture_is_srgb_ext(
    CNA_EffectHandle effect,
    CNA_PbrTextureSlot slot,
    CNA_Bool value);

/** @brief Gets whether the shader encodes its own output to sRGB. */
CNA_C_API CNA_Result cna_pbr_effect_get_encode_output_to_srgb_ext(
    CNA_EffectHandle effect,
    CNA_Bool* out_value);

/** @brief Sets whether the shader encodes its own output to sRGB. */
CNA_C_API CNA_Result cna_pbr_effect_set_encode_output_to_srgb_ext(
    CNA_EffectHandle effect,
    CNA_Bool value);

/** @brief Gets the material's alpha-coverage mode. */
CNA_C_API CNA_Result cna_pbr_effect_get_alpha_mode_ext(
    CNA_EffectHandle effect,
    CNA_AlphaModeEXT* out_value);

/**
 * @brief Sets the material's alpha-coverage mode.
 * @param effect PbrEffect or SkinnedPbrEffect handle.
 * @param value One `CNA_ALPHA_MODE_*_EXT` identity.
 * @return `CNA_RESULT_INVALID_ARGUMENT` for an undefined identity.
 */
CNA_C_API CNA_Result cna_pbr_effect_set_alpha_mode_ext(
    CNA_EffectHandle effect,
    CNA_AlphaModeEXT value);

/** @brief Gets the alpha threshold used by `CNA_ALPHA_MODE_MASK_EXT`. */
CNA_C_API CNA_Result cna_pbr_effect_get_alpha_cutoff_ext(
    CNA_EffectHandle effect,
    float* out_value);

/** @brief Sets the alpha threshold used by `CNA_ALPHA_MODE_MASK_EXT`. */
CNA_C_API CNA_Result cna_pbr_effect_set_alpha_cutoff_ext(CNA_EffectHandle effect, float value);

/** @brief Gets whether the material is rendered from both sides. */
CNA_C_API CNA_Result cna_pbr_effect_get_double_sided_ext(
    CNA_EffectHandle effect,
    CNA_Bool* out_value);

/** @brief Sets whether the material is rendered from both sides. */
CNA_C_API CNA_Result cna_pbr_effect_set_double_sided_ext(CNA_EffectHandle effect, CNA_Bool value);

/** @brief Gets the SkinnedPbrEffect weights-per-vertex value. */
CNA_C_API CNA_Result cna_skinned_pbr_effect_get_weights_per_vertex(
    CNA_EffectHandle effect,
    int32_t* out_value);

/** @brief Sets SkinnedPbrEffect weights per vertex to one, two, or four. */
CNA_C_API CNA_Result cna_skinned_pbr_effect_set_weights_per_vertex(
    CNA_EffectHandle effect,
    int32_t value);

/**
 * @brief Replaces the leading SkinnedPbrEffect bone transforms from a copied array.
 * @param effect SkinnedPbrEffect handle.
 * @param transforms Caller-owned row-major matrices.
 * @param transform_count Matrix count in the inclusive range 1 through 72.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_skinned_pbr_effect_set_bone_transforms(
    CNA_EffectHandle effect,
    const CNA_Matrix* transforms,
    uint64_t transform_count);

/**
 * @brief Copies the requested leading SkinnedPbrEffect bone transforms atomically.
 * @param effect SkinnedPbrEffect handle.
 * @param requested_count Number of transforms in the inclusive range 1 through 72.
 * @param destination Destination array, or null only for zero capacity.
 * @param capacity Destination capacity in matrices.
 * @param out_count Receives the requested count.
 * @return A CNA result code; insufficient capacity performs no partial write.
 */
CNA_C_API CNA_Result cna_skinned_pbr_effect_copy_bone_transforms(
    CNA_EffectHandle effect,
    uint64_t requested_count,
    CNA_Matrix* destination,
    uint64_t capacity,
    uint64_t* out_count);

#ifdef __cplusplus
}
#endif

#endif
