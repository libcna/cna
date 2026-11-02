// SPDX-License-Identifier: MS-PL
#ifndef CNA_C_API_CONTENT_VALUES_H
#define CNA_C_API_CONTENT_VALUES_H
#include "CNA/C/content_readers.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Callbacks return normally: no exceptions or longjmp may cross the ABI. Do not reenter
   an active reader or manager from its callbacks. Function/context storage remains caller-owned. */
/** @brief Owned native content-value snapshot; destroy exactly once. Foreign pointees remain caller-owned. */
typedef CNA_Handle CNA_ContentValueHandle;
/** @brief Closed target registry, independent of reader factory keys; stateful calls use the owner thread. */
typedef uint32_t CNA_ContentValueKind;
/** @brief Empty serialized reference. */
#define CNA_CONTENT_VALUE_EMPTY UINT32_C(0)
/** @brief Canonical CNA_Bool. */
#define CNA_CONTENT_VALUE_BOOLEAN UINT32_C(1)
/** @brief Signed 32-bit value. */
#define CNA_CONTENT_VALUE_INT32 UINT32_C(2)
/** @brief 32-bit floating point value. */
#define CNA_CONTENT_VALUE_SINGLE UINT32_C(3)
/** @brief 64-bit floating point value. */
#define CNA_CONTENT_VALUE_DOUBLE UINT32_C(4)
/** @brief UTF-8 string, copied with the text route. */
#define CNA_CONTENT_VALUE_STRING UINT32_C(5)
/** @brief ForeignContentObjectEXT; caller owns its pointee. */
#define CNA_CONTENT_VALUE_FOREIGN_OBJECT UINT32_C(6)
/** @brief Reference-shaped reflective foreign object; snapshot retains the native carrier only. */
#define CNA_CONTENT_VALUE_FOREIGN_REFERENCE UINT32_C(7)
/** @brief Native disposable retained in a snapshot; disposal is separate from snapshot destruction. */
#define CNA_CONTENT_VALUE_DISPOSABLE UINT32_C(8)

/**
 * @brief Assigns a deferred foreign reference after native type validation; no reader token escapes.
 * @param context Registered caller context.
 * @param object Caller-owned destination object, retained until completion/cancellation.
 * @param resource Caller-owned shared pointee, or null for a null resource.
 * @return SUCCESS or a failure that aborts the read; native earlier field mutations remain visible.
 */
typedef CNA_Result (*CNA_ContentSharedAssignCallback)(void* context, void* object, void* resource);
/**
 * @brief Pins or releases the caller object and context for one pending fixup.
 * @param context Caller context; must survive through the matching release invocation.
 * @param object Destination object being pinned or released.
 */
typedef void (*CNA_ContentSharedLifetimeCallback)(void* context, void* object);

/**
 * @brief Produces synchronous borrowed binary bytes for an asset, copied before the calling route returns.
 *
 * Bytes must remain readable until the callback's caller returns (the enclosing load/open route).
 * The context must survive manager destruction, including retained reader owners. Never reenter
 * this manager or destroy it from its callback. There is no loose-file fallback on callback error.
 * Resource mode uses missing/nonbinary/binary kinds 0/1/2; stream mode requires 2. Resource
 * culture is an explicit UTF-8 view (empty means invariant); stream mode passes empty culture.
 * @param context Caller context retained by the manager contract.
 * @param name Logical asset/resource name, borrowed for this invocation.
 * @param culture Resource culture name, borrowed for this invocation.
 * @param out_kind Receives 0 missing, 1 nonbinary, or 2 binary.
 * @param out_data Receives borrowed byte data; null allowed only for zero length.
 * @param out_byte_count Receives readable byte length, limited to INT32_MAX.
 * @return SUCCESS or a failure mapped to content IO; no buffer is adopted on failure.
 */
typedef CNA_Result (*CNA_ContentBytesCallback)(void* context, CNA_StringView name,
    CNA_StringView culture, uint32_t* out_kind, const uint8_t** out_data, uint64_t* out_byte_count);
/**
 * @brief Receives ownership of a disposable snapshot during an uncached read.
 *
 * Release every received handle with cna_content_value_destroy_ext; Dispose is explicit.
 * The manager will not dispose adopted objects. Failure releases this invocation's handle;
 * earlier successful adoptions remain caller-owned. Called on the owner thread.
 * @param context Call-only context.
 * @param disposable Owned snapshot, transferred only on SUCCESS.
 * @return SUCCESS to adopt or a failure to abort reading.
 */
typedef CNA_Result (*CNA_ContentDisposableCallback)(void* context, CNA_ContentValueHandle disposable);

/**
 * @brief Appends a deferred, typed reference field.
 *
 * Native two-pass shared reading permits aliases and cycles. Index zero does not call callbacks.
 * Only reference-shaped reflective resources are accepted. A type mismatch never calls assign.
 * Clearing factories ends registrations, but live readers retain their snapshots.
 * @param builder Builder.
 * @param resource_target_name Expected canonical managed type name, copied.
 * @param assign Required assignment callback.
 * @param retain Required pin callback, called when a nonzero index is scheduled.
 * @param release Required unpin callback, once on completion or reader destruction, including failed reads.
 * @param context Retained by caller until registration and all readers end.
 * @return A CNA result code. Required outputs are checked first; failed new handles are invalid.
 */
CNA_C_API CNA_Result cna_reflective_type_reader_builder_add_shared_field_ext(
    CNA_ReflectiveTypeReaderBuilderHandle builder,
    CNA_StringView resource_target_name,
    CNA_ContentSharedAssignCallback assign,
    CNA_ContentSharedLifetimeCallback retain,
    CNA_ContentSharedLifetimeCallback release,
    void* context);

/**
 * @brief Appends a snapshot of base fields before any derived fields.
 *
 * The caller guarantees valid complete-object storage and base layout, as for ordinary offset fields.
 * No C++ inheritance is inferred. Destroying or editing the base builder cannot change the snapshot.
 * @param builder Derived builder; must have no fields yet.
 * @param base_builder Different base builder with a distinct target name.
 * @param base_offset_in_bytes Byte displacement of base subobject in caller layout.
 * @return A CNA result code. Required outputs are checked first; failed new handles are invalid.
 */
CNA_C_API CNA_Result cna_reflective_type_reader_builder_add_base_ext(
    CNA_ReflectiveTypeReaderBuilderHandle builder,
    CNA_ReflectiveTypeReaderBuilderHandle base_builder,
    uint64_t base_offset_in_bytes);

/**
 * @brief Registers a resolving-only abstract reflective identity.
 *
 * Construction/reading always returns content IO, including existing instances.
 * Registration follows native first-factory-wins semantics and survives builder destruction.
 * @param builder Builder.
 * @return A CNA result code. Required outputs are checked first; failed new handles are invalid.
 */
CNA_C_API CNA_Result cna_reflective_type_reader_builder_register_abstract_ext(
    CNA_ReflectiveTypeReaderBuilderHandle builder);

/**
 * @brief Reads the root and shared resources into an owned value snapshot.
 *
 * Unsupported concrete types return NOT_SUPPORTED after native reading; no value is discarded silently.
 * @param reader Reader positioned at its type table.
 * @param out_value New snapshot.
 * @return A CNA result code. Required outputs are checked first; failed new handles are invalid.
 */
CNA_C_API CNA_Result cna_content_reader_read_asset_value_ext(
    CNA_ContentReaderHandle reader,
    CNA_ContentValueHandle* out_value);

/**
 * @brief Reads a relative reference using the native manager cache and path policy.
 * @param reader Reader positioned at the path string.
 * @param out_value New snapshot, EMPTY for an empty path.
 * @return A CNA result code. Required outputs are checked first; failed new handles are invalid.
 */
CNA_C_API CNA_Result cna_content_reader_read_external_reference_value_ext(
    CNA_ContentReaderHandle reader,
    CNA_ContentValueHandle* out_value);

/**
 * @brief Invokes a reader with an optional existing native value and returns its result.
 *
 * Snapshots own native carriers, not foreign pointees. Native callbacks may partially mutate an
 * existing foreign object before failure; the existing snapshot itself is never replaced.
 * Existing foreign pointee layout must belong to the selected reader: opaque native foreign
 * values share one carrier type and cannot validate unrelated caller layouts.
 * @param type_reader Reader factory instance.
 * @param reader Input reader.
 * @param existing_value Invalid for none; snapshot otherwise.
 * @param out_value New result snapshot.
 * @return A CNA result code. Required outputs are checked first; failed new handles are invalid.
 */
CNA_C_API CNA_Result cna_content_type_reader_read_value_ext(
    CNA_ContentTypeReaderHandle type_reader,
    CNA_ContentReaderHandle reader,
    CNA_ContentValueHandle existing_value,
    CNA_ContentValueHandle* out_value);

/**
 * @brief Associates a reviewed target ID with a managed name or explicit factory key.
 *
 * Uses the native target association registry. Factory removal invalidates native name-to-factory
 * associations. Creating a reader records its declared target name separately from its factory key.
 * @param kind BOOLEAN through FOREIGN_REFERENCE.
 * @param target_name Nonempty copied identity.
 * @return A CNA result code. Required outputs are checked first; failed new handles are invalid.
 */
CNA_C_API CNA_Result cna_content_type_reader_manager_associate_target_ext(
    CNA_ContentValueKind kind,
    CNA_StringView target_name);

/**
 * @brief Constructs the native reader associated with a reviewed target ID.
 * @param kind Reviewed target ID.
 * @param out_type_reader New reader; NOT_SUPPORTED if no association/factory resolves.
 * @return A CNA result code. Required outputs are checked first; failed new handles are invalid.
 */
CNA_C_API CNA_Result cna_content_type_reader_manager_create_target_reader_ext(
    CNA_ContentValueKind kind,
    CNA_ContentTypeReaderHandle* out_type_reader);

/**
 * @brief Reads a reviewed target without consuming a type-reader index.
 *
 * Native instance-identity checks apply: an existing scalar is rejected as replacement.
 * Foreign-reference existing values retain their carrier; foreign pointee mutation may be partial.
 * @param reader Input reader.
 * @param kind Reviewed target ID.
 * @param existing_value Invalid for no existing instance; otherwise must match kind.
 * @param out_value New result snapshot.
 * @return A CNA result code. Required outputs are checked first; failed new handles are invalid.
 */
CNA_C_API CNA_Result cna_content_reader_read_raw_value_ext(
    CNA_ContentReaderHandle reader,
    CNA_ContentValueKind kind,
    CNA_ContentValueHandle existing_value,
    CNA_ContentValueHandle* out_value);

/**
 * @brief Gets the snapshot tag.
 * @param value Snapshot.
 * @param out_kind Tag, unchanged on failure.
 * @return A CNA result code. Required outputs are checked first; failed new handles are invalid.
 */
CNA_C_API CNA_Result cna_content_value_get_kind_ext(
    CNA_ContentValueHandle value,
    CNA_ContentValueKind* out_kind);

/**
 * @brief Copies a fixed scalar or opaque foreign pointer without a partial write.
 *
 * Scalar types are CNA_Bool, int32_t, float, double; foreign tags copy void*.
 * Strings use the text route; EMPTY has zero bytes; DISPOSABLE refuses data copying.
 * @param value Snapshot.
 * @param destination Caller buffer; null only for zero capacity.
 * @param capacity Byte capacity.
 * @param out_byte_count Required bytes, including on short capacity.
 * @return A CNA result code. Required outputs are checked first; failed new handles are invalid.
 */
CNA_C_API CNA_Result cna_content_value_copy_data_ext(
    CNA_ContentValueHandle value,
    void* destination,
    uint64_t capacity,
    uint64_t* out_byte_count);

/**
 * @brief Copies UTF-8 bytes without a terminator from a STRING snapshot.
 * @param value Snapshot.
 * @param destination Caller buffer; null only with zero capacity.
 * @param capacity Byte capacity.
 * @param out_byte_count Required bytes; short capacity writes nothing.
 * @return A CNA result code. Required outputs are checked first; failed new handles are invalid.
 */
CNA_C_API CNA_Result cna_content_value_copy_string_ext(
    CNA_ContentValueHandle value,
    char* destination,
    uint64_t capacity,
    uint64_t* out_byte_count);

/**
 * @brief Disposes a DISPOSABLE snapshot once after success; failed disposal may be retried.
 * @param value Snapshot; other kinds are refused.
 * @return A CNA result code. Required outputs are checked first; failed new handles are invalid.
 */
CNA_C_API CNA_Result cna_content_value_dispose_ext(
    CNA_ContentValueHandle value);

/**
 * @brief Releases the snapshot; never frees a foreign pointee or implicitly disposes an adopted object.
 * @param value Owned snapshot.
 * @return A CNA result code. Required outputs are checked first; failed new handles are invalid.
 */
CNA_C_API CNA_Result cna_content_value_destroy_ext(
    CNA_ContentValueHandle value);

/**
 * @brief Creates a CPU content manager with a copied-byte stream override or resource family.
 *
 * No graphics device is attached; only CPU assets can be read. Native Load cache-before-open,
 * disposal validation, unload and typed/untyped identity apply. Source callbacks persist until
 * the manager and all its retained reader owners are destroyed.
 * @param callback Required source callback.
 * @param context Caller context, valid through final retained-manager release.
 * @param resource_mode True uses native ResourceContentManager; false overrides OpenStream.
 * @param out_content_manager Owned manager; use cna_content_manager_destroy.
 * @return A CNA result code. Required outputs are checked first; failed new handles are invalid.
 */
CNA_C_API CNA_Result cna_content_manager_create_bytes_ext(
    CNA_ContentBytesCallback callback,
    void* context,
    CNA_Bool resource_mode,
    CNA_Handle* out_content_manager);

/**
 * @brief Sets the native strict stream-routing switch for subsequent cache misses.
 * @param content_manager Bytes-created manager.
 * @param enabled Canonical boolean; false restores the manager's native fallback policy.
 * @return A CNA result code. Invalid inputs do not mutate the switch.
 */
CNA_C_API CNA_Result cna_content_manager_set_stream_routing_ext(
    CNA_Handle content_manager, CNA_Bool enabled);

/**
 * @brief Loads a closed-registry type through native cache-before-open semantics.
 * @param content_manager Manager.
 * @param asset_name UTF-8 logical name.
 * @param kind Reviewed target ID.
 * @param out_value New independently owned snapshot.
 * @return A CNA result code. Required outputs are checked first; failed new handles are invalid.
 */
CNA_C_API CNA_Result cna_content_manager_load_value_ext(
    CNA_Handle content_manager,
    CNA_StringView asset_name,
    CNA_ContentValueKind kind,
    CNA_ContentValueHandle* out_value);

/**
 * @brief Reads without caching through the protected native ReadAsset seam.
 * @param content_manager Bytes-created manager.
 * @param asset_name UTF-8 logical name.
 * @param kind Reviewed target ID.
 * @param record_disposable Optional call-only adoption callback; null uses manager disposal recording.
 * @param context Call-only recorder context.
 * @param out_value New snapshot.
 * @return A CNA result code. Required outputs are checked first; failed new handles are invalid.
 */
CNA_C_API CNA_Result cna_content_manager_read_value_ext(
    CNA_Handle content_manager,
    CNA_StringView asset_name,
    CNA_ContentValueKind kind,
    CNA_ContentDisposableCallback record_disposable,
    void* context,
    CNA_ContentValueHandle* out_value);

#ifdef __cplusplus
}
#endif
#endif
