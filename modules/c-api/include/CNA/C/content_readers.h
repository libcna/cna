// SPDX-License-Identifier: MS-PL

#ifndef CNA_C_CONTENT_READERS_H
#define CNA_C_CONTENT_READERS_H

#include "CNA/C/math_values.h"
#include "CNA/C/storage.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Owned handle for an object-graph reader over a compiled asset stream. */
typedef CNA_Handle CNA_ContentReaderHandle;

/** @brief Owned handle for one `.xnb` type reader instance. */
typedef CNA_Handle CNA_ContentTypeReaderHandle;

/** @brief Fixed-width identity naming why a recognized reader is deliberately unsupported. */
typedef uint32_t CNA_UnsupportedContentReaderReason;

/** @brief The general effect reader, which would require compiled platform shader bytecode. */
#define CNA_UNSUPPORTED_CONTENT_READER_REASON_COMPILED_PLATFORM_SHADER_BYTECODE UINT32_C(0)

/**
 * @brief Configures creation of an owned content reader.
 */
typedef struct CNA_ContentReaderCreateInfo {
    /** @brief Size of this caller-provided structure in bytes. */
    uint32_t struct_size;

    /** @brief Version of this caller-provided structure. */
    uint32_t struct_version;

    /**
     * @brief Owned content-manager handle, or `CNA_INVALID_HANDLE` for a standalone reader.
     *
     * The canonical reader accepts a null manager; only external references and the
     * manager-backed disposal fallback need one.
     */
    CNA_Handle content_manager;

    /**
     * @brief Owned readable storage stream positioned after the container header.
     *
     * The reader borrows the stream for its whole lifetime, so the stream cannot be closed until
     * the reader is destroyed; destroying the reader then closes it.
     */
    CNA_StorageStreamHandle stream;

    /** @brief UTF-8 logical asset name used for diagnostics; copied during creation. */
    CNA_StringView asset_name;

    /** @brief Container version from the compiled asset header. */
    int32_t version;

    /** @brief Platform identifier byte from the compiled asset header. */
    uint8_t platform;

    /** @brief Reserved bytes; callers must initialize these to zero. */
    uint8_t reserved[3];
} CNA_ContentReaderCreateInfo;

/**
 * @brief Creates an owned content reader over a storage stream.
 *
 * @param create_info Versioned creation configuration.
 * @param out_reader Receives an owned content-reader handle on success.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` when the stream is not readable, or a
 * documented argument/encoding/handle/thread/native failure.
 *
 * The canonical disposal-recording callback and read limits keep their defaults; neither is a C
 * type. No `System::IO::Stream` or `ContentManager` pointer crosses the boundary.
 *
 * The canonical reader closes the stream when it is destroyed, matching the binary-reader contract
 * it derives from. The stream handle itself stays valid and must still be released with
 * `cna_storage_stream_close`, which is idempotent.
 */
CNA_C_API CNA_Result cna_content_reader_create(
    const CNA_ContentReaderCreateInfo* create_info,
    CNA_ContentReaderHandle* out_reader);

/**
 * @brief Gets the content manager a reader was created with.
 *
 * @param reader Owned content-reader handle.
 * @param out_content_manager Receives the manager handle, or `CNA_INVALID_HANDLE` when the reader
 * was created standalone.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_content_reader_get_content_manager(
    CNA_ContentReaderHandle reader,
    CNA_Handle* out_content_manager);

/**
 * @brief Gets the UTF-8 byte count of a reader's logical asset name.
 *
 * @param reader Owned content-reader handle.
 * @param out_bytes Receives the byte count without a terminator.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_content_reader_get_asset_name_size(
    CNA_ContentReaderHandle reader,
    uint64_t* out_bytes);

/**
 * @brief Copies a reader's logical asset name as UTF-8 bytes without a terminator.
 *
 * @param reader Owned content-reader handle.
 * @param destination Caller-owned destination, or null only when @p capacity is zero.
 * @param capacity Destination capacity in bytes.
 * @param out_bytes Receives the required byte count without a terminator.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL`, or a documented
 * argument/handle/thread failure. No partial name is written.
 */
CNA_C_API CNA_Result cna_content_reader_copy_asset_name(
    CNA_ContentReaderHandle reader,
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

/**
 * @brief Gets the container version a reader was created with.
 *
 * @param reader Owned content-reader handle.
 * @param out_version Receives the container version.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_content_reader_get_version(
    CNA_ContentReaderHandle reader,
    int32_t* out_version);

/**
 * @brief Gets the platform identifier a reader was created with.
 *
 * @param reader Owned content-reader handle.
 * @param out_platform Receives the platform identifier byte.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_content_reader_get_platform(
    CNA_ContentReaderHandle reader,
    uint8_t* out_platform);

/**
 * @brief Reads a matrix in canonical row order and advances the stream.
 *
 * @param reader Owned content-reader handle.
 * @param out_value Receives the sixteen read values.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_IO` at end of stream, or a documented
 * argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_content_reader_read_matrix(
    CNA_ContentReaderHandle reader,
    CNA_Matrix* out_value);

/**
 * @brief Reads a quaternion as X, Y, Z, W and advances the stream.
 *
 * @param reader Owned content-reader handle.
 * @param out_value Receives the read value.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_IO` at end of stream, or a documented
 * argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_content_reader_read_quaternion(
    CNA_ContentReaderHandle reader,
    CNA_Quaternion* out_value);

/**
 * @brief Reads a two-component vector and advances the stream.
 *
 * @param reader Owned content-reader handle.
 * @param out_value Receives the read value.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_IO` at end of stream, or a documented
 * argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_content_reader_read_vector2(
    CNA_ContentReaderHandle reader,
    CNA_Vector2* out_value);

/**
 * @brief Reads a three-component vector and advances the stream.
 *
 * @param reader Owned content-reader handle.
 * @param out_value Receives the read value.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_IO` at end of stream, or a documented
 * argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_content_reader_read_vector3(
    CNA_ContentReaderHandle reader,
    CNA_Vector3* out_value);

/**
 * @brief Reads a four-component vector and advances the stream.
 *
 * @param reader Owned content-reader handle.
 * @param out_value Receives the read value.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_IO` at end of stream, or a documented
 * argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_content_reader_read_vector4(
    CNA_ContentReaderHandle reader,
    CNA_Vector4* out_value);

/**
 * @brief Reads four bytes as an RGBA color and advances the stream.
 *
 * @param reader Owned content-reader handle.
 * @param out_value Receives the read value.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_IO` at end of stream, or a documented
 * argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_content_reader_read_color(
    CNA_ContentReaderHandle reader,
    CNA_Color* out_value);

/**
 * @brief Reads a bounding sphere as a center vector followed by a radius.
 *
 * @param reader Owned content-reader handle.
 * @param out_value Receives the read value.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_IO` at end of stream, or a documented
 * argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_content_reader_read_bounding_sphere(
    CNA_ContentReaderHandle reader,
    CNA_BoundingSphere* out_value);

/**
 * @brief Reads the next object through the type-reader dispatch protocol and discards it.
 *
 * @param reader Owned content-reader handle.
 * @param out_has_value Receives `CNA_TRUE` when the reference was not null.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_IO` for a malformed graph, or a documented
 * argument/handle/thread failure.
 *
 * The canonical operation returns a type-erased object, which has no C representation, so this
 * route reports only presence. It exists so a C consumer can advance past an untyped field, such
 * as a model's tag, and observe whether one was present.
 */
CNA_C_API CNA_Result cna_content_reader_read_object_tag(
    CNA_ContentReaderHandle reader,
    CNA_Bool* out_has_value);

/**
 * @brief Reads and instantiates the compiled type-reader table, then the shared-resource count.
 *
 * @param reader Owned content-reader handle.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_IO` when an entry names an unregistered or
 * version-incompatible reader, or a documented handle/thread failure.
 *
 * The stream must be positioned at the start of the table.
 */
CNA_C_API CNA_Result cna_content_reader_initialize_type_readers(CNA_ContentReaderHandle reader);

/**
 * @brief Reads every shared resource and then runs the queued fixups.
 *
 * @param reader Owned content-reader handle.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_IO` for a malformed graph, or a documented
 * handle/thread failure.
 */
CNA_C_API CNA_Result cna_content_reader_read_shared_resources(CNA_ContentReaderHandle reader);

/**
 * @brief Validates a declared collection element count against the reader's configured limit.
 *
 * @param reader Owned content-reader handle.
 * @param count Declared element count as read from the file.
 * @param reader_name UTF-8 canonical reader name used in the diagnostic.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_IO` when the count is negative or over the limit, or a
 * documented argument/encoding/handle/thread failure.
 */
CNA_C_API CNA_Result cna_content_reader_check_collection_element_count(
    CNA_ContentReaderHandle reader,
    int64_t count,
    CNA_StringView reader_name);

/**
 * @brief Validates a decoded buffer's byte size against the reader's configured limit.
 *
 * @param reader Owned content-reader handle.
 * @param byte_size Required decoded size in bytes.
 * @param reader_name UTF-8 canonical reader name used in the diagnostic.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_IO` when the size is negative or over the limit, or a
 * documented argument/encoding/handle/thread failure.
 */
CNA_C_API CNA_Result cna_content_reader_check_decoded_byte_size(
    CNA_ContentReaderHandle reader,
    int64_t byte_size,
    CNA_StringView reader_name);

/**
 * @brief Reads exactly the requested number of bytes or fails.
 *
 * @param reader Owned content-reader handle.
 * @param count Number of bytes required; must not be negative.
 * @param reader_name UTF-8 canonical reader name used in the diagnostic.
 * @param destination Caller-owned destination, or null only when @p capacity is zero.
 * @param capacity Destination capacity in bytes.
 * @param out_bytes Receives the required byte count.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL`, `CNA_RESULT_IO` for a negative
 * count or a truncated stream, or a documented argument/encoding/handle/thread failure.
 *
 * The capacity is checked before the stream is read, so a destination that is too small leaves the
 * stream position unchanged.
 */
CNA_C_API CNA_Result cna_content_reader_read_bytes_exact(
    CNA_ContentReaderHandle reader,
    int32_t count,
    CNA_StringView reader_name,
    uint8_t* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

/**
 * @brief Releases an owned content reader and its borrow on the underlying stream.
 *
 * @param reader Owned content-reader handle.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread failure. A second release returns
 * `CNA_RESULT_INVALID_HANDLE`.
 *
 * This also closes the borrowed stream, so a further reader over the same file needs a freshly
 * opened stream. The stream handle is still released separately.
 */
CNA_C_API CNA_Result cna_content_reader_destroy(CNA_ContentReaderHandle reader);

/**
 * @brief Removes every registered type-reader factory from the process-wide registry.
 *
 * @return `CNA_RESULT_SUCCESS` or a documented native failure.
 *
 * The registry is process-wide and is the only way compiled assets resolve a reader name, so
 * clearing it makes every subsequent compiled-asset load fail until factories are registered
 * again. Follow it with `cna_content_register_known_unsupported_xnb_readers` and a fresh
 * `cna_content_manager_register_builtin_loaders` when the registry must be restored.
 */
CNA_C_API CNA_Result cna_content_type_reader_manager_clear_type_creators(void);

/**
 * @brief Gets whether a factory is registered for a canonical reader name.
 *
 * @param canonical_name UTF-8 canonical reader name.
 * @param out_is_registered Receives `CNA_TRUE` when a factory is registered.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/encoding/native failure.
 */
CNA_C_API CNA_Result cna_content_type_reader_manager_get_is_registered(
    CNA_StringView canonical_name,
    CNA_Bool* out_is_registered);

/**
 * @brief Creates one fresh reader instance for a canonical reader name.
 *
 * @param canonical_name UTF-8 canonical reader name.
 * @param out_type_reader Receives an owned type-reader handle on success.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` when no factory is registered under
 * that name, or a documented argument/encoding/thread/native failure.
 */
CNA_C_API CNA_Result cna_content_type_reader_manager_create_reader(
    CNA_StringView canonical_name,
    CNA_ContentTypeReaderHandle* out_type_reader);

/**
 * @brief Constructs one instance of a caller-supplied content type reader.
 *
 * @param context The context supplied at registration.
 * @param out_reader_context Receives a per-instance context this ABI passes back to every other
 *        callback in the table, and finally to @ref CNA_ContentTypeReaderDestroyCallback. May be
 *        left as the registration context when the reader is stateless.
 * @return `CNA_RESULT_SUCCESS`, or any documented result code to fail the load that asked for it.
 *
 * Called once per compiled asset file that names this reader, so each file gets a fresh,
 * unshared instance -- the same rule the built-in readers follow.
 */
typedef CNA_Result (*CNA_ContentTypeReaderCreateCallback)(
    void* context,
    void** out_reader_context);

/**
 * @brief Deserializes one object with a caller-supplied content type reader.
 *
 * @param reader_context The per-instance context from @ref CNA_ContentTypeReaderCreateCallback.
 * @param input **Callback-scoped borrowed** ContentReader handle, positioned at this object's
 *        serialized data. It is invalidated before this callback returns and has no destroy
 *        operation; caching it and using it later fails with `CNA_RESULT_INVALID_HANDLE`.
 * @param existing_object The object to deserialize into, or null for a fresh one. Non-null only
 *        when the table set `can_deserialize_into_existing_object`.
 * @param out_object Receives the caller's opaque object. This ABI never dereferences, copies or
 *        frees it; it is returned to whoever asked for the asset, and its lifetime is the
 *        caller's own business.
 * @return `CNA_RESULT_SUCCESS`, or any documented result code. **A failure fails the load** and
 *         is reported to the caller of `cna_content_manager_load_foreign_ext` -- which is why
 *         this callback returns a result where `CNA_GameComponentCallbacks` returns `void`: a
 *         component that fails has a next frame to recover in, and a half-read asset does not.
 */
typedef CNA_Result (*CNA_ContentTypeReaderReadCallback)(
    void* reader_context,
    CNA_ContentReaderHandle input,
    void* existing_object,
    void** out_object);

/**
 * @brief Releases one reader instance created by @ref CNA_ContentTypeReaderCreateCallback.
 *
 * @param reader_context The per-instance context to release.
 *
 * Called when the instance is finished with, whether its read succeeded or failed. It returns
 * `void` deliberately: nothing can act on a failure to clean up. Objects the reader *produced*
 * are not released here -- they belong to whoever received them.
 */
typedef void (*CNA_ContentTypeReaderDestroyCallback)(void* reader_context);

/** @brief The behavior of one caller-supplied content type reader. */
typedef struct CNA_ContentTypeReaderCallbacks {
    /** @brief Size of this caller-provided structure in bytes. */
    uint32_t struct_size;
    /** @brief Version of this caller-provided structure; currently one. */
    uint32_t struct_version;
    /** @brief Canonical target type name this reader produces; copied during registration. */
    CNA_StringView target_type_name;
    /** @brief The reader's `TypeVersion`, matched against the version each file declares. */
    int32_t type_version;
    /** @brief Whether @ref read accepts a non-null `existing_object`. */
    CNA_Bool can_deserialize_into_existing_object;
    /** @brief Reserved; must be zero. */
    uint8_t reserved[3];
    /** @brief Required per-file instance factory. */
    CNA_ContentTypeReaderCreateCallback create;
    /** @brief Required deserialization callback. */
    CNA_ContentTypeReaderReadCallback read;
    /** @brief Optional per-instance cleanup; may be null. */
    CNA_ContentTypeReaderDestroyCallback destroy;
    /** @brief Caller-owned context passed to @ref create; may be null. */
    void* context;
} CNA_ContentTypeReaderCallbacks;

/**
 * @brief Registers a caller-supplied reader factory under a canonical reader name.
 *
 * @param canonical_name UTF-8 canonical reader name exactly as compiled assets spell it, copied
 *        during the call.
 * @param callbacks Versioned behavior table; every pointer in it is copied, and @ref
 *        CNA_ContentTypeReaderCallbacks::context stays caller-owned and must outlive the
 *        registration.
 * @param out_registration Receives an **owned** registration handle, released with
 *        `cna_content_type_reader_manager_unregister`. Releasing it withdraws the factory.
 * @return `CNA_RESULT_SUCCESS`; `CNA_RESULT_INVALID_ARGUMENT` for a null output, a malformed
 *         table, a missing `create` or `read`, or a name that is empty or not valid UTF-8;
 *         `CNA_RESULT_INVALID_STATE` when a factory is already registered under that name; or a
 *         documented thread/memory failure.
 *
 * This is the registry's extension point for a type CNA does not know. Without it the only
 * readers a compiled asset can name are the ones this library was built with, which makes the
 * canonical content pipeline extensible in name only.
 *
 * **Refusing a duplicate is a deliberate deviation.** The canonical `AddTypeCreator` silently
 * ignores a repeat registration of the same key, which is right for built-in readers registering
 * themselves twice through two static-initialization paths and wrong here: a caller who registers
 * a name someone else already owns would otherwise receive a live handle whose factory is never
 * called, and would learn about it only from assets that deserialize into the wrong type.
 *
 * The registry is process-wide, so a registration outlives any one game and is not tied to one.
 * Every callback in the table runs on the thread performing the load.
 */
CNA_C_API CNA_Result cna_content_type_reader_manager_register(
    CNA_StringView canonical_name,
    const CNA_ContentTypeReaderCallbacks* callbacks,
    CNA_Handle* out_registration);

/**
 * @brief Withdraws a caller-supplied reader factory and destroys its registration handle.
 *
 * @param registration Owned registration handle from `cna_content_type_reader_manager_register`.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread failure. A second unregistration
 *         returns `CNA_RESULT_INVALID_HANDLE`.
 *
 * Reader instances already constructed for an in-flight load are unaffected; what this changes is
 * what a **later** load finds. After it returns, the name is free for another registration, and
 * `cna_content_type_reader_manager_get_is_registered` answers false for it.
 */
CNA_C_API CNA_Result cna_content_type_reader_manager_unregister(CNA_Handle registration);

/**
 * @brief Registers the placeholder readers for recognized but unsupported compiled asset types.
 *
 * @return `CNA_RESULT_SUCCESS` or a documented native failure. The registration is idempotent.
 *
 * This set is currently empty: its one entry was the general `EffectReader`, and compiled Effect
 * Framework bytecode is now a supported format with a reader that really decodes it. The route
 * remains the registry's published extension point rather than being withdrawn, so a future
 * recognized-but-unsupported format registers here without an ABI change. To obtain such a reader
 * directly, use `cna_known_unsupported_content_type_reader_create`.
 */
CNA_C_API CNA_Result cna_content_register_known_unsupported_xnb_readers(void);

/**
 * @brief Creates a placeholder reader that always refuses to read.
 *
 * @param target_type_name UTF-8 canonical target type name.
 * @param reason One of the `CNA_UNSUPPORTED_CONTENT_READER_REASON_*` identities.
 * @param out_type_reader Receives an owned type-reader handle on success.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for an unknown reason, or a
 * documented encoding/thread/native failure.
 */
CNA_C_API CNA_Result cna_known_unsupported_content_type_reader_create(
    CNA_StringView target_type_name,
    CNA_UnsupportedContentReaderReason reason,
    CNA_ContentTypeReaderHandle* out_type_reader);

/**
 * @brief Gets whether a type reader can deserialize into an existing object.
 *
 * @param type_reader Owned type-reader handle.
 * @param out_value Receives `CNA_TRUE` when in-place deserialization is supported.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_content_type_reader_get_can_deserialize_into_existing_object(
    CNA_ContentTypeReaderHandle type_reader,
    CNA_Bool* out_value);

/**
 * @brief Gets the UTF-8 byte count of a type reader's canonical target type name.
 *
 * @param type_reader Owned type-reader handle.
 * @param out_bytes Receives the byte count without a terminator.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_content_type_reader_get_target_type_name_size(
    CNA_ContentTypeReaderHandle type_reader,
    uint64_t* out_bytes);

/**
 * @brief Copies a type reader's canonical target type name without a terminator.
 *
 * @param type_reader Owned type-reader handle.
 * @param destination Caller-owned destination, or null only when @p capacity is zero.
 * @param capacity Destination capacity in bytes.
 * @param out_bytes Receives the required byte count without a terminator.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL`, or a documented
 * argument/handle/thread failure. No partial name is written.
 */
CNA_C_API CNA_Result cna_content_type_reader_copy_target_type_name(
    CNA_ContentTypeReaderHandle type_reader,
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

/**
 * @brief Gets a type reader's own serialization version.
 *
 * @param type_reader Owned type-reader handle.
 * @param out_version Receives the type version.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_content_type_reader_get_type_version(
    CNA_ContentTypeReaderHandle type_reader,
    int32_t* out_version);

/**
 * @brief Gets whether a type reader accepts data serialized at a given version.
 *
 * @param type_reader Owned type-reader handle.
 * @param serialized_version Version read from a compiled asset's type-reader table entry.
 * @param out_value Receives `CNA_TRUE` when that version is accepted.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_content_type_reader_supports_version(
    CNA_ContentTypeReaderHandle type_reader,
    int32_t serialized_version,
    CNA_Bool* out_value);

/**
 * @brief Runs a type reader's post-table initialization step.
 *
 * @param type_reader Owned type-reader handle.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread/native failure.
 *
 * The canonical parameter is a reader-manager instance whose whole registry is static, so the
 * adapter supplies one rather than exposing a manager object that carries no state.
 */
CNA_C_API CNA_Result cna_content_type_reader_initialize(CNA_ContentTypeReaderHandle type_reader);

/**
 * @brief Runs a type reader against a content reader and discards the produced object.
 *
 * @param type_reader Owned type-reader handle.
 * @param reader Owned content-reader handle positioned at the object's data.
 * @param out_has_value Receives `CNA_TRUE` when the reader produced an object.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_IO` when the reader refuses the data, or a documented
 * argument/handle/thread failure.
 *
 * The canonical operation returns a type-erased object and accepts one to deserialize into.
 * Neither has a C representation, so this route always passes "no existing instance" and reports
 * only whether an object was produced. A placeholder reader for an unsupported type therefore
 * surfaces as a failure with its canonical diagnostic rather than as a value.
 */
CNA_C_API CNA_Result cna_content_type_reader_read_untyped(
    CNA_ContentTypeReaderHandle type_reader,
    CNA_ContentReaderHandle reader,
    CNA_Bool* out_has_value);

/**
 * @brief Releases an owned type-reader handle.
 *
 * @param type_reader Owned type-reader handle.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread failure. A second release returns
 * `CNA_RESULT_INVALID_HANDLE`.
 */
CNA_C_API CNA_Result cna_content_type_reader_destroy(CNA_ContentTypeReaderHandle type_reader);

/* --- CBIND-105: the reflective content readers ------------------------------------------------- */

/**
 * @brief Which value one declared field of a reflectively-serialized type holds.
 *
 * These are the types XNA writes **inline**, as a value type. A .NET *reference* type -- a string,
 * a nested object, a list -- is written with its own type-reader index in front and read back as an
 * object, which a C caller cannot receive into a byte offset; declare one with
 * @ref cna_reflective_type_reader_builder_add_custom and read it yourself.
 */
typedef uint32_t CNA_ContentFieldKind;

/** @brief A `bool`, one byte. */
#define CNA_CONTENT_FIELD_BOOLEAN UINT32_C(0)
/** @brief A 32-bit float. */
#define CNA_CONTENT_FIELD_SINGLE UINT32_C(1)
/** @brief A 64-bit float. */
#define CNA_CONTENT_FIELD_DOUBLE UINT32_C(2)
/** @brief A signed 32-bit integer. */
#define CNA_CONTENT_FIELD_INT32 UINT32_C(3)
/** @brief An unsigned 32-bit integer. */
#define CNA_CONTENT_FIELD_UINT32 UINT32_C(4)
/** @brief A signed 64-bit integer. */
#define CNA_CONTENT_FIELD_INT64 UINT32_C(5)
/** @brief An unsigned byte. */
#define CNA_CONTENT_FIELD_BYTE UINT32_C(6)
/** @brief A `CNA_Vector2`. */
#define CNA_CONTENT_FIELD_VECTOR2 UINT32_C(7)
/** @brief A `CNA_Vector3`. */
#define CNA_CONTENT_FIELD_VECTOR3 UINT32_C(8)
/** @brief A `CNA_Vector4`. */
#define CNA_CONTENT_FIELD_VECTOR4 UINT32_C(9)
/** @brief A `CNA_Matrix`. */
#define CNA_CONTENT_FIELD_MATRIX UINT32_C(10)
/** @brief A `CNA_Quaternion`. */
#define CNA_CONTENT_FIELD_QUATERNION UINT32_C(11)
/** @brief A `CNA_Color`. */
#define CNA_CONTENT_FIELD_COLOR UINT32_C(12)
/** @brief A .NET `TimeSpan`, written as its `Int64` tick count and stored as `int64_t` ticks. */
#define CNA_CONTENT_FIELD_TIMESPAN UINT32_C(13)
/** @brief Highest field kind this ABI names. */
#define CNA_CONTENT_FIELD_MAXIMUM CNA_CONTENT_FIELD_TIMESPAN

/**
 * @brief Declares a reflectively-serialized type's fields and registers the readers it needs.
 *
 * The canonical builder is a fluent C++ template whose `Field(&T::member)` captures a
 * pointer-to-member. C has no member pointer, so a field is declared by **kind and byte offset**
 * instead -- which is the same information, expressed the way C can carry it.
 */
typedef CNA_Handle CNA_ReflectiveTypeReaderBuilderHandle;

/**
 * @brief Creates the object a reflective read is about to fill.
 *
 * The canonical reader default-constructs its `T`; C has no such type, so the caller makes one.
 * That also settles ownership without an allocator crossing the ABI: whatever this returns is the
 * caller's, allocated and freed by the caller, and CNA only writes the declared fields into it.
 *
 * @param context The context supplied at builder creation.
 * @param out_object Receives the object. It must be at least as large as the highest declared field
 *        offset plus that field's size.
 * @return `CNA_RESULT_SUCCESS`, or any documented result code to fail the load.
 */
typedef CNA_Result (*CNA_ReflectiveObjectCreateCallback)(
    void* context,
    void** out_object);

/**
 * @brief Reads one member the wire format does not map onto a plain offset.
 *
 * @param context The context supplied with this field.
 * @param object The object being filled, from @ref CNA_ReflectiveObjectCreateCallback.
 * @param input **Callback-scoped borrowed** ContentReader handle, positioned at this member's
 *        value. It is invalidated before this callback returns and has no destroy operation.
 * @return `CNA_RESULT_SUCCESS`, or any documented result code to fail the load.
 *
 * The payload is positional, so what matters is where this sits in the declaration chain: read
 * exactly one value's worth and store whatever it means.
 */
typedef CNA_Result (*CNA_ReflectiveFieldCallback)(
    void* context,
    void* object,
    CNA_ContentReaderHandle input);

/**
 * @brief Begins describing a reflectively-serialized type.
 *
 * @param target_type_name The .NET name of the type, as the `.xnb` spells it. Must not be empty.
 * @param create_callback Non-null; makes the object each read fills.
 * @param context Caller-owned context passed back to @p create_callback; it must outlive the
 *        registration.
 * @param out_builder Receives the new builder handle.
 * @return A CNA result code.
 *
 * **Declare the fields in wire order, which is not the type's field order.** The content pipeline
 * writes the serialized *properties* first and then the public fields, each group in declaration
 * order. Check the order against a decoded file rather than against the source type.
 */
CNA_C_API CNA_Result cna_reflective_type_reader_builder_create(
    CNA_StringView target_type_name,
    CNA_ReflectiveObjectCreateCallback create_callback,
    void* context,
    CNA_ReflectiveTypeReaderBuilderHandle* out_builder);

/**
 * @brief Releases a builder.
 *
 * A builder describes a registration; releasing it does not withdraw one already made.
 *
 * @param builder The builder to release.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_reflective_type_reader_builder_destroy(
    CNA_ReflectiveTypeReaderBuilderHandle builder);

/**
 * @brief Declares the next field, read inline at a byte offset in the object.
 *
 * @param builder The builder.
 * @param kind Which value the field holds.
 * @param offset_in_bytes Where to store it in the object.
 * @return A CNA result code; a kind above `CNA_CONTENT_FIELD_MAXIMUM` is
 *         `CNA_RESULT_INVALID_ARGUMENT`.
 */
CNA_C_API CNA_Result cna_reflective_type_reader_builder_add_field(
    CNA_ReflectiveTypeReaderBuilderHandle builder,
    CNA_ContentFieldKind kind,
    uint64_t offset_in_bytes);

/**
 * @brief Declares the next field, which is an enum stored as an `int32_t`.
 *
 * @param builder The builder.
 * @param offset_in_bytes Where to store the value in the object.
 * @param enum_type_name The .NET name of the enum, as the `.xnb` spells it. Must not be empty.
 * @return A CNA result code.
 *
 * An enum needs its own route because its .NET name cannot be recovered from the value: registering
 * the reflective reader also registers an enum reader under that name, which a game would otherwise
 * have to do by hand. **An `.xnb`'s type-reader table must resolve in full before any object is
 * read**, even for readers this one never dispatches to -- the reflective payload writes enums
 * inline and never reaches the enum reader at all.
 */
CNA_C_API CNA_Result cna_reflective_type_reader_builder_add_enum_field(
    CNA_ReflectiveTypeReaderBuilderHandle builder,
    uint64_t offset_in_bytes,
    CNA_StringView enum_type_name);

/**
 * @brief Declares the next member, read by a caller-supplied callback.
 *
 * This is how a reference-type member is read -- a string, a nested object, a list -- since those
 * are written with their own reader index and cannot land in a byte offset.
 *
 * @param builder The builder.
 * @param callback Non-null; reads the value and stores what it means.
 * @param context Caller-owned context passed back to @p callback; it must outlive the registration.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_reflective_type_reader_builder_add_custom(
    CNA_ReflectiveTypeReaderBuilderHandle builder,
    CNA_ReflectiveFieldCallback callback,
    void* context);

/**
 * @brief Registers the reflective reader and every enum reader the type needs.
 *
 * @param builder The builder.
 * @return A CNA result code.
 *
 * **No registration handle, because the canonical `Register()` has no undo either.** Registering
 * the same canonical name again replaces the entry, which is what both sides rely on, and a
 * withdrawal route here would be surface the canonical layer does not have. Safe to call more than
 * once.
 */
CNA_C_API CNA_Result cna_reflective_type_reader_builder_register(
    CNA_ReflectiveTypeReaderBuilderHandle builder);

/**
 * @brief Reports the byte length of the canonical name a reflectively-read type is written under.
 *
 * @param target_type_name The .NET name of the serialized type.
 * @param out_byte_count Receives the length, without a terminator.
 * @return A CNA result code.
 *
 * CNA normalizes an `.xnb`'s assembly-qualified name down to this, so it is the key the reader
 * table is looking for -- which is what makes it worth publishing rather than keeping private.
 */
CNA_C_API CNA_Result cna_reflective_type_reader_get_canonical_name_size(
    CNA_StringView target_type_name,
    uint64_t* out_byte_count);

/**
 * @brief Copies the canonical name a reflectively-read type is written under.
 *
 * @param target_type_name The .NET name of the serialized type.
 * @param destination Destination bytes, or null only for zero capacity. Not terminated.
 * @param capacity Destination capacity in bytes.
 * @param out_byte_count Receives the required byte count; always written on a valid output.
 * @return A CNA result code; insufficient capacity performs no partial write.
 */
CNA_C_API CNA_Result cna_reflective_type_reader_copy_canonical_name(
    CNA_StringView target_type_name,
    char* destination,
    uint64_t capacity,
    uint64_t* out_byte_count);

/**
 * @brief Reports the byte length of the canonical name an enum is written under.
 *
 * @param target_type_name The .NET name of the enum.
 * @param out_byte_count Receives the length, without a terminator.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_enum_type_reader_get_canonical_name_size(
    CNA_StringView target_type_name,
    uint64_t* out_byte_count);

/**
 * @brief Copies the canonical name an enum is written under.
 *
 * @param target_type_name The .NET name of the enum.
 * @param destination Destination bytes, or null only for zero capacity. Not terminated.
 * @param capacity Destination capacity in bytes.
 * @param out_byte_count Receives the required byte count; always written on a valid output.
 * @return A CNA result code; insufficient capacity performs no partial write.
 */
CNA_C_API CNA_Result cna_enum_type_reader_copy_canonical_name(
    CNA_StringView target_type_name,
    char* destination,
    uint64_t capacity,
    uint64_t* out_byte_count);

#ifdef __cplusplus
}
#endif

#endif
