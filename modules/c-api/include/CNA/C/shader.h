// SPDX-License-Identifier: MS-PL
#ifndef CNA_C_SHADER_H
#define CNA_C_SHADER_H
#include "CNA/C/graphics.h"
#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Owned shader CPU snapshots, with creator-thread access and one typed destroy each.
 *
 * Inputs are copied; snapshots and child code copies survive source/package/device release.
 * Text is validated UTF-8, with embedded NUL refused. Text copies have no terminator and no
 * partial writes. Capacities/counts use uint64_t. Pure identity helpers allow any thread.
 * CNAEXT-disabled builds keep the symbols and refuse code/package operations.
 */
/** @brief Owned immutable ShaderDiagnostics snapshot; copying its bits acquires no ownership. */
typedef CNA_Handle CNA_ShaderDiagnosticsHandleEXT;
/** @brief Owned immutable ShaderCode snapshot; copying its bits acquires no ownership. */
typedef CNA_Handle CNA_ShaderCodeHandleEXT;
/** @brief Owned immutable ShaderPackage snapshot; copying its bits acquires no ownership. */
typedef CNA_Handle CNA_ShaderPackageHandleEXT;
/** @brief Owned immutable ShaderSelection snapshot; copying its bits acquires no ownership. */
typedef CNA_Handle CNA_ShaderSelectionHandleEXT;
/** @brief ShaderDiagnosticInfoEXT version 1 descriptor. */
typedef struct CNA_ShaderDiagnosticInfoEXT {
    /** @brief Size in bytes. */
    uint32_t struct_size;
    /** @brief Version, must be 1. */
    uint32_t struct_version;
    /** @brief Severity identity. */
    CNA_ShaderDiagnosticSeverityEXT severity;
    /** @brief Stage identity, including unknown. */
    CNA_ShaderStageEXT stage;
    /** @brief One-based line, or zero. */
    int32_t line;
    /** @brief One-based column, or zero. */
    int32_t column;
} CNA_ShaderDiagnosticInfoEXT;

/** @brief ShaderCodeInfoEXT version 1 descriptor. */
typedef struct CNA_ShaderCodeInfoEXT {
    /** @brief Size in bytes. */
    uint32_t struct_size;
    /** @brief Version, must be 1. */
    uint32_t struct_version;
    /** @brief Payload language. */
    CNA_ShaderLanguageEXT language;
    /** @brief Stage implemented. */
    CNA_ShaderStageEXT stage;
    /** @brief Exact payload bytes. */
    uint64_t payload_byte_count;
    /** @brief True for text, false for binary. */
    CNA_Bool is_text;
    /** @brief Zero bytes. */
    uint8_t reserved[7];
} CNA_ShaderCodeInfoEXT;

/** @brief Portable shader binding kind. */
typedef uint32_t CNA_ShaderBindingTypeEXT;
/** @brief Sampled texture2d binding identity. */
#define CNA_SHADER_BINDING_SAMPLED_TEXTURE2D_EXT UINT32_C(0)
/** @brief Sampled texture cube binding identity. */
#define CNA_SHADER_BINDING_SAMPLED_TEXTURE_CUBE_EXT UINT32_C(1)
/** @brief Sampled texture3d binding identity. */
#define CNA_SHADER_BINDING_SAMPLED_TEXTURE3D_EXT UINT32_C(2)
/** @brief Count binding identity. */
#define CNA_SHADER_BINDING_COUNT_EXT UINT32_C(3)
/** @brief ShaderBindingRequirementEXT version 1 descriptor. */
typedef struct CNA_ShaderBindingRequirementEXT {
    /** @brief Size in bytes. */
    uint32_t struct_size;
    /** @brief Version, must be 1. */
    uint32_t struct_version;
    /** @brief Non-empty logical name, copied during package creation. */
    CNA_StringView name;
    /** @brief Non-negative binding index. */
    int32_t binding;
    /** @brief Portable resource type. */
    CNA_ShaderBindingTypeEXT type;
    /** @brief Consuming vertex or fragment stage. */
    CNA_ShaderStageEXT stage;
} CNA_ShaderBindingRequirementEXT;

/**
 * @brief Queries the native binding contract on any thread.
 *
 * @param language Declared identity, including Unknown; Count is refused.
 * @param out_uses Receives descriptor contract membership.
 * @return A CNA result code; failure preserves outputs except where documented above.
 */
CNA_C_API CNA_Result cna_shader_language_uses_descriptor_binding_contract_ext(
    CNA_ShaderLanguageEXT language,
    CNA_Bool* out_uses);

/**
 * @brief Queries an explicit shader language and stage.
 *
 * @param graphics_device Live owned device or valid callback loan; owner thread.
 * @param language Known identity; Unknown is accepted as unsupported.
 * @param stage Known identity; Unknown is accepted as unsupported.
 * @param out_supported Receives language/stage support, independent of compile success.
 * @return A CNA result code; failure preserves outputs except where documented above.
 */
CNA_C_API CNA_Result cna_graphics_device_supports_shader_language_ext(
    CNA_Handle graphics_device,
    CNA_ShaderLanguageEXT language,
    CNA_ShaderStageEXT stage,
    CNA_Bool* out_supported);

/**
 * @brief Parses compiler log lines into an independently owned diagnostic snapshot.
 *
 * @param log UTF-8 log, copied during call; empty is accepted.
 * @param fallback_stage Fallback stage, including Unknown.
 * @param source_label Copied diagnostic label.
 * @param out_diagnostics Receives owned immutable list; invalid on failure.
 * @return A CNA result code; failure preserves outputs except where documented above.
 */
CNA_C_API CNA_Result cna_shader_diagnostics_parse_ext(
    CNA_StringView log,
    CNA_ShaderStageEXT fallback_stage,
    CNA_StringView source_label,
    CNA_ShaderDiagnosticsHandleEXT* out_diagnostics);

/**
 * @brief Creates one validated diagnostic.
 *
 * @param info Versioned severity/stage/location record.
 * @param source_label Copied label.
 * @param message Non-empty copied message.
 * @param out_diagnostics Receives owned one-record list; invalid on failure.
 * @return A CNA result code; failure preserves outputs except where documented above.
 */
CNA_C_API CNA_Result cna_shader_diagnostics_create_ext(
    const CNA_ShaderDiagnosticInfoEXT* info,
    CNA_StringView source_label,
    CNA_StringView message,
    CNA_ShaderDiagnosticsHandleEXT* out_diagnostics);

/**
 * @brief Queries the snapshot record count.
 *
 * @param diagnostics Live owned snapshot, accessed on its creator thread.
 * @param out_count Receives record count.
 * @return A CNA result code; failure preserves outputs except where documented above.
 */
CNA_C_API CNA_Result cna_shader_diagnostics_get_count_ext(
    CNA_ShaderDiagnosticsHandleEXT diagnostics,
    uint64_t* out_count);

/**
 * @brief Copies one diagnostic record without string pointers.
 *
 * @param diagnostics Live owned snapshot, accessed on its creator thread.
 * @param index Zero-based diagnostic index.
 * @param out_info Receives fields; initialize size/version first.
 * @return A CNA result code; failure preserves outputs except where documented above.
 */
CNA_C_API CNA_Result cna_shader_diagnostics_get_info_ext(
    CNA_ShaderDiagnosticsHandleEXT diagnostics,
    uint64_t index,
    CNA_ShaderDiagnosticInfoEXT* out_info);

/**
 * @brief Copies diagnostic source label as UTF-8.
 *
 * @param diagnostics Live owned snapshot, accessed on its creator thread.
 * @param index Zero-based diagnostic index.
 * @param destination Caller buffer; null only at zero capacity.
 * @param capacity Capacity in bytes; short buffers receive no prefix.
 * @param out_byte_count Receives full required byte count, also on short capacity.
 * @return A CNA result code; failure preserves outputs except where documented above.
 */
CNA_C_API CNA_Result cna_shader_diagnostics_copy_source_label_ext(
    CNA_ShaderDiagnosticsHandleEXT diagnostics,
    uint64_t index,
    char* destination,
    uint64_t capacity,
    uint64_t* out_byte_count);

/**
 * @brief Copies diagnostic message as UTF-8.
 *
 * @param diagnostics Live owned snapshot, accessed on its creator thread.
 * @param index Zero-based diagnostic index.
 * @param destination Caller buffer; null only at zero capacity.
 * @param capacity Capacity in bytes; short buffers receive no prefix.
 * @param out_byte_count Receives full required byte count, also on short capacity.
 * @return A CNA result code; failure preserves outputs except where documented above.
 */
CNA_C_API CNA_Result cna_shader_diagnostics_copy_message_ext(
    CNA_ShaderDiagnosticsHandleEXT diagnostics,
    uint64_t index,
    char* destination,
    uint64_t capacity,
    uint64_t* out_byte_count);

/**
 * @brief Releases the snapshot and stales its token.
 *
 * @param diagnostics Live owned snapshot, accessed on its creator thread.
 * @return A CNA result code; failure preserves outputs except where documented above.
 */
CNA_C_API CNA_Result cna_shader_diagnostics_destroy_ext(
    CNA_ShaderDiagnosticsHandleEXT diagnostics);

/**
 * @brief Creates explicit text shader code, copying all inputs.
 *
 * @param language Explicit text or binary language; Unknown/Count are refused.
 * @param stage Vertex or fragment stage.
 * @param entry_point Non-empty copied entry point.
 * @param source_label Copied diagnostic label.
 * @param source_text Non-empty copied source text.
 * @param out_code Receives owned code; invalid on failure.
 * @return A CNA result code; failure preserves outputs except where documented above.
 */
CNA_C_API CNA_Result cna_shader_code_create_text_ext(
    CNA_ShaderLanguageEXT language,
    CNA_ShaderStageEXT stage,
    CNA_StringView entry_point,
    CNA_StringView source_label,
    CNA_StringView source_text,
    CNA_ShaderCodeHandleEXT* out_code);

/**
 * @brief Creates explicit binary shader code, copying all inputs.
 *
 * @param language Explicit text or binary language; Unknown/Count are refused.
 * @param stage Vertex or fragment stage.
 * @param entry_point Non-empty copied entry point.
 * @param source_label Copied diagnostic label.
 * @param code Non-empty binary bytes; SPIR-V requires whole 32-bit words.
 * @param byte_count Readable byte count.
 * @param out_code Receives owned code; invalid on failure.
 * @return A CNA result code; failure preserves outputs except where documented above.
 */
CNA_C_API CNA_Result cna_shader_code_create_binary_ext(
    CNA_ShaderLanguageEXT language,
    CNA_ShaderStageEXT stage,
    CNA_StringView entry_point,
    CNA_StringView source_label,
    const uint8_t* code,
    uint64_t byte_count,
    CNA_ShaderCodeHandleEXT* out_code);

/**
 * @brief Queries immutable code metadata.
 *
 * @param code Live owned snapshot, accessed on its creator thread.
 * @param out_info Receives payload metadata; initialize size/version first.
 * @return A CNA result code; failure preserves outputs except where documented above.
 */
CNA_C_API CNA_Result cna_shader_code_get_info_ext(
    CNA_ShaderCodeHandleEXT code,
    CNA_ShaderCodeInfoEXT* out_info);

/**
 * @brief Copies entry point.
 *
 * @param code Live owned snapshot, accessed on its creator thread.
 * @param destination Caller buffer; null only at zero capacity.
 * @param capacity Capacity in bytes; short buffers receive no prefix.
 * @param out_byte_count Receives full required byte count, also on short capacity.
 * @return A CNA result code; failure preserves outputs except where documented above.
 */
CNA_C_API CNA_Result cna_shader_code_copy_entry_point_ext(
    CNA_ShaderCodeHandleEXT code,
    char* destination,
    uint64_t capacity,
    uint64_t* out_byte_count);

/**
 * @brief Copies source label.
 *
 * @param code Live owned snapshot, accessed on its creator thread.
 * @param destination Caller buffer; null only at zero capacity.
 * @param capacity Capacity in bytes; short buffers receive no prefix.
 * @param out_byte_count Receives full required byte count, also on short capacity.
 * @return A CNA result code; failure preserves outputs except where documented above.
 */
CNA_C_API CNA_Result cna_shader_code_copy_source_label_ext(
    CNA_ShaderCodeHandleEXT code,
    char* destination,
    uint64_t capacity,
    uint64_t* out_byte_count);

/**
 * @brief Copies text; binary code refuses text access.
 *
 * @param code Live owned snapshot, accessed on its creator thread.
 * @param destination Caller buffer; null only at zero capacity.
 * @param capacity Capacity in bytes; short buffers receive no prefix.
 * @param out_byte_count Receives full required byte count, also on short capacity.
 * @return A CNA result code; failure preserves outputs except where documented above.
 */
CNA_C_API CNA_Result cna_shader_code_copy_text_ext(
    CNA_ShaderCodeHandleEXT code,
    char* destination,
    uint64_t capacity,
    uint64_t* out_byte_count);

/**
 * @brief Copies binary payload; text code refuses binary access.
 *
 * @param code Live owned snapshot, accessed on its creator thread.
 * @param destination Caller buffer; null only at zero capacity.
 * @param capacity Capacity in bytes; no prefix on short capacity.
 * @param out_byte_count Receives required bytes.
 * @return A CNA result code; failure preserves outputs except where documented above.
 */
CNA_C_API CNA_Result cna_shader_code_copy_bytes_ext(
    CNA_ShaderCodeHandleEXT code,
    uint8_t* destination,
    uint64_t capacity,
    uint64_t* out_byte_count);

/**
 * @brief Releases a code snapshot.
 *
 * @param code Live owned snapshot, accessed on its creator thread.
 * @return A CNA result code; failure preserves outputs except where documented above.
 */
CNA_C_API CNA_Result cna_shader_code_destroy_ext(
    CNA_ShaderCodeHandleEXT code);

/**
 * @brief Validates and copies shader variants, required stages and binding requirements.
 *
 * @param variants Code handles copied into package; no token retained.
 * @param variant_count Nonzero number of handles.
 * @param required_stages Unique required stages, copied.
 * @param stage_count Nonzero number of required stages.
 * @param bindings Versioned binding records, copied; null only with zero count.
 * @param binding_count Binding record count.
 * @param out_package Receives immutable package; invalid on failure.
 * @return A CNA result code; failure preserves outputs except where documented above.
 */
CNA_C_API CNA_Result cna_shader_package_create_ext(
    const CNA_ShaderCodeHandleEXT* variants,
    uint64_t variant_count,
    const CNA_ShaderStageEXT* required_stages,
    uint64_t stage_count,
    const CNA_ShaderBindingRequirementEXT* bindings,
    uint64_t binding_count,
    CNA_ShaderPackageHandleEXT* out_package);

/**
 * @brief Queries the package variant count.
 *
 * @param package Live owned snapshot, accessed on its creator thread.
 * @param out_count Receives item count.
 * @return A CNA result code; failure preserves outputs except where documented above.
 */
CNA_C_API CNA_Result cna_shader_package_get_variant_count_ext(
    CNA_ShaderPackageHandleEXT package,
    uint64_t* out_count);

/**
 * @brief Queries the package required stage count.
 *
 * @param package Live owned snapshot, accessed on its creator thread.
 * @param out_count Receives item count.
 * @return A CNA result code; failure preserves outputs except where documented above.
 */
CNA_C_API CNA_Result cna_shader_package_get_required_stage_count_ext(
    CNA_ShaderPackageHandleEXT package,
    uint64_t* out_count);

/**
 * @brief Queries the package binding count.
 *
 * @param package Live owned snapshot, accessed on its creator thread.
 * @param out_count Receives item count.
 * @return A CNA result code; failure preserves outputs except where documented above.
 */
CNA_C_API CNA_Result cna_shader_package_get_binding_count_ext(
    CNA_ShaderPackageHandleEXT package,
    uint64_t* out_count);

/**
 * @brief Copies a variant into independent owned code.
 *
 * @param package Live owned snapshot, accessed on its creator thread.
 * @param index Variant index.
 * @param out_code Receives independent owned code copy; destroy separately.
 * @return A CNA result code; failure preserves outputs except where documented above.
 */
CNA_C_API CNA_Result cna_shader_package_get_variant_ext(
    CNA_ShaderPackageHandleEXT package,
    uint64_t index,
    CNA_ShaderCodeHandleEXT* out_code);

/**
 * @brief Queries a required stage.
 *
 * @param package Live owned snapshot, accessed on its creator thread.
 * @param index Required-stage index.
 * @param out_stage Receives stage identity.
 * @return A CNA result code; failure preserves outputs except where documented above.
 */
CNA_C_API CNA_Result cna_shader_package_get_required_stage_ext(
    CNA_ShaderPackageHandleEXT package,
    uint64_t index,
    CNA_ShaderStageEXT* out_stage);

/**
 * @brief Queries package stage membership.
 *
 * @param package Live owned snapshot, accessed on its creator thread.
 * @param stage Stage identity; invalid values return false as in native API.
 * @param out_required Receives stage membership.
 * @return A CNA result code; failure preserves outputs except where documented above.
 */
CNA_C_API CNA_Result cna_shader_package_requires_stage_ext(
    CNA_ShaderPackageHandleEXT package,
    CNA_ShaderStageEXT stage,
    CNA_Bool* out_required);

/**
 * @brief Copies binding metadata without exposing native string pointers.
 *
 * @param package Live owned snapshot, accessed on its creator thread.
 * @param index Binding index.
 * @param out_binding Receives metadata with empty name view; use name copy route. Initialize size/version first.
 * @return A CNA result code; failure preserves outputs except where documented above.
 */
CNA_C_API CNA_Result cna_shader_package_get_binding_ext(
    CNA_ShaderPackageHandleEXT package,
    uint64_t index,
    CNA_ShaderBindingRequirementEXT* out_binding);

/**
 * @brief Copies a package binding name.
 *
 * @param package Live owned snapshot, accessed on its creator thread.
 * @param index Binding index.
 * @param destination Caller buffer; null only at zero capacity.
 * @param capacity Capacity in bytes; short buffers receive no prefix.
 * @param out_byte_count Receives full required byte count, also on short capacity.
 * @return A CNA result code; failure preserves outputs except where documented above.
 */
CNA_C_API CNA_Result cna_shader_package_copy_binding_name_ext(
    CNA_ShaderPackageHandleEXT package,
    uint64_t index,
    char* destination,
    uint64_t capacity,
    uint64_t* out_byte_count);

/**
 * @brief Selects native code for a device; unusable results are successful snapshots with diagnostics.
 *
 * @param package Live owned snapshot, accessed on its creator thread.
 * @param graphics_device Live device/loan; owner thread.
 * @param out_selection Receives independent selection, including unusable selections; invalid on call failure.
 * @return A CNA result code; failure preserves outputs except where documented above.
 */
CNA_C_API CNA_Result cna_shader_package_select_ext(
    CNA_ShaderPackageHandleEXT package,
    CNA_Handle graphics_device,
    CNA_ShaderSelectionHandleEXT* out_selection);

/**
 * @brief Releases package; selections and copied variants remain valid.
 *
 * @param package Live owned snapshot, accessed on its creator thread.
 * @return A CNA result code; failure preserves outputs except where documented above.
 */
CNA_C_API CNA_Result cna_shader_package_destroy_ext(
    CNA_ShaderPackageHandleEXT package);

/**
 * @brief Queries selection usability.
 *
 * @param selection Live owned snapshot, accessed on its creator thread.
 * @param out_usable Receives whether complete usable code was selected.
 * @return A CNA result code; failure preserves outputs except where documented above.
 */
CNA_C_API CNA_Result cna_shader_selection_get_is_usable_ext(
    CNA_ShaderSelectionHandleEXT selection,
    CNA_Bool* out_usable);

/**
 * @brief Queries selected language.
 *
 * @param selection Live owned snapshot, accessed on its creator thread.
 * @param out_language Receives language or Unknown after refusal.
 * @return A CNA result code; failure preserves outputs except where documented above.
 */
CNA_C_API CNA_Result cna_shader_selection_get_language_ext(
    CNA_ShaderSelectionHandleEXT selection,
    CNA_ShaderLanguageEXT* out_language);

/**
 * @brief Queries selected code count.
 *
 * @param selection Live owned snapshot, accessed on its creator thread.
 * @param out_count Receives selected code count.
 * @return A CNA result code; failure preserves outputs except where documented above.
 */
CNA_C_API CNA_Result cna_shader_selection_get_code_count_ext(
    CNA_ShaderSelectionHandleEXT selection,
    uint64_t* out_count);

/**
 * @brief Copies selected code by index.
 *
 * @param selection Live owned snapshot, accessed on its creator thread.
 * @param index Selected-code index.
 * @param out_code Receives independent owned copy.
 * @return A CNA result code; failure preserves outputs except where documented above.
 */
CNA_C_API CNA_Result cna_shader_selection_get_code_ext(
    CNA_ShaderSelectionHandleEXT selection,
    uint64_t index,
    CNA_ShaderCodeHandleEXT* out_code);

/**
 * @brief Finds and copies a stage; absence succeeds with invalid code and false.
 *
 * @param selection Live owned snapshot, accessed on its creator thread.
 * @param stage Stage to find; invalid identities yield no match.
 * @param out_code Independent owned copy or invalid when absent.
 * @param out_found Receives whether stage was present.
 * @return A CNA result code; failure preserves outputs except where documented above.
 */
CNA_C_API CNA_Result cna_shader_selection_find_stage_ext(
    CNA_ShaderSelectionHandleEXT selection,
    CNA_ShaderStageEXT stage,
    CNA_ShaderCodeHandleEXT* out_code,
    CNA_Bool* out_found);

/**
 * @brief Copies the deterministic native selection diagnostic.
 *
 * @param selection Live owned snapshot, accessed on its creator thread.
 * @param destination Caller buffer; null only at zero capacity.
 * @param capacity Capacity in bytes; short buffers receive no prefix.
 * @param out_byte_count Receives full required byte count, also on short capacity.
 * @return A CNA result code; failure preserves outputs except where documented above.
 */
CNA_C_API CNA_Result cna_shader_selection_copy_diagnostic_ext(
    CNA_ShaderSelectionHandleEXT selection,
    char* destination,
    uint64_t capacity,
    uint64_t* out_byte_count);

/**
 * @brief Releases a selection without affecting package or code copies.
 *
 * @param selection Live owned snapshot, accessed on its creator thread.
 * @return A CNA result code; failure preserves outputs except where documented above.
 */
CNA_C_API CNA_Result cna_shader_selection_destroy_ext(
    CNA_ShaderSelectionHandleEXT selection);

#ifdef __cplusplus
}
#endif
#endif
