// SPDX-License-Identifier: MS-PL

#ifndef CNA_C_NET_H
#define CNA_C_NET_H

#include "CNA/C/math_values.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Fixed-width identity describing why a network session ended. */
typedef uint32_t CNA_NetworkSessionEndReason;

/** @brief The local gamer signed out. */
#define CNA_NETWORK_SESSION_END_REASON_CLIENT_SIGNED_OUT UINT32_C(0)
/** @brief The host ended the session. */
#define CNA_NETWORK_SESSION_END_REASON_HOST_ENDED_SESSION UINT32_C(1)
/** @brief The local gamer was removed by the host. */
#define CNA_NETWORK_SESSION_END_REASON_REMOVED_BY_HOST UINT32_C(2)
/** @brief The connection to the session was lost. */
#define CNA_NETWORK_SESSION_END_REASON_DISCONNECTED UINT32_C(3)

/** @brief Fixed-width identity describing why joining a network session failed. */
typedef uint32_t CNA_NetworkSessionJoinError;

/** @brief The requested session could not be found. */
#define CNA_NETWORK_SESSION_JOIN_ERROR_SESSION_NOT_FOUND UINT32_C(0)
/** @brief The requested session does not allow new gamers to join. */
#define CNA_NETWORK_SESSION_JOIN_ERROR_SESSION_NOT_JOINABLE UINT32_C(1)
/** @brief The requested session has no free player slots. */
#define CNA_NETWORK_SESSION_JOIN_ERROR_SESSION_FULL UINT32_C(2)

/** @brief Fixed-width identity describing the current state of a network session. */
typedef uint32_t CNA_NetworkSessionState;

/** @brief The session is waiting in its pre-game lobby. */
#define CNA_NETWORK_SESSION_STATE_LOBBY UINT32_C(0)
/** @brief The session is actively playing a game. */
#define CNA_NETWORK_SESSION_STATE_PLAYING UINT32_C(1)
/** @brief The session has ended. */
#define CNA_NETWORK_SESSION_STATE_ENDED UINT32_C(2)

/** @brief Fixed-width identity describing the type of a network session. */
typedef uint32_t CNA_NetworkSessionType;

/** @brief A session played entirely on the local machine. */
#define CNA_NETWORK_SESSION_TYPE_LOCAL UINT32_C(0)
/** @brief A session played over a local area network. */
#define CNA_NETWORK_SESSION_TYPE_SYSTEM_LINK UINT32_C(1)
/** @brief A session that matches players with similar skill or preferences. */
#define CNA_NETWORK_SESSION_TYPE_PLAYER_MATCH UINT32_C(2)
/** @brief A competitive, ranked session. */
#define CNA_NETWORK_SESSION_TYPE_RANKED UINT32_C(3)
/** @brief A local session that also reports to leaderboards. */
#define CNA_NETWORK_SESSION_TYPE_LOCAL_WITH_LEADERBOARDS UINT32_C(4)

/**
 * @brief Fixed-width identity describing how a network packet should be delivered.
 *
 * The canonical enumeration is marked as flags but uses plain sequential values, so these are
 * discrete identities and must not be combined bitwise.
 */
typedef uint32_t CNA_SendDataOptions;

/** @brief No delivery guarantees; packets may be dropped or reordered. */
#define CNA_SEND_DATA_OPTIONS_NONE UINT32_C(0)
/** @brief The packet is guaranteed to arrive. */
#define CNA_SEND_DATA_OPTIONS_RELIABLE UINT32_C(1)
/** @brief The packet is guaranteed to arrive in order relative to other in-order packets. */
#define CNA_SEND_DATA_OPTIONS_IN_ORDER UINT32_C(2)
/** @brief The packet is guaranteed to arrive, in order. */
#define CNA_SEND_DATA_OPTIONS_RELIABLE_IN_ORDER UINT32_C(3)
/** @brief The packet contains chat data. */
#define CNA_SEND_DATA_OPTIONS_CHAT UINT32_C(4)

/**
 * @brief Describes measured network quality between the local machine and a remote gamer.
 */
typedef struct CNA_QualityOfService {
    /** @brief Size of this caller-provided structure in bytes. */
    uint32_t struct_size;

    /** @brief Version of this caller-provided structure. */
    uint32_t struct_version;

    /** @brief `CNA_TRUE` when quality-of-service data is available. */
    CNA_Bool is_available;

    /** @brief Reserved bytes; always zero. */
    uint8_t reserved[7];

    /** @brief Average measured round-trip time in 100-nanosecond ticks. */
    int64_t average_roundtrip_ticks;

    /** @brief Minimum measured round-trip time in 100-nanosecond ticks. */
    int64_t minimum_roundtrip_ticks;

    /** @brief Measured downstream bandwidth in bytes per second. */
    int32_t bytes_per_second_downstream;

    /** @brief Measured upstream bandwidth in bytes per second. */
    int32_t bytes_per_second_upstream;
} CNA_QualityOfService;

/**
 * @brief Represents one optional 32-bit session property value.
 */
typedef struct CNA_OptionalInt32 {
    /** @brief `CNA_TRUE` when @ref value carries a property value. */
    CNA_Bool has_value;

    /** @brief Reserved bytes; always zero. */
    uint8_t reserved[3];

    /** @brief The property value; zero when @ref has_value is `CNA_FALSE`. */
    int32_t value;
} CNA_OptionalInt32;

/** @brief Owned handle for a growable list of optional session properties. */
typedef CNA_Handle CNA_NetworkSessionPropertiesHandle;

/** @brief Owned handle for a forward enumerator over session properties. */
typedef CNA_Handle CNA_NetworkSessionPropertyEnumeratorHandle;

/** @brief Owned handle for an in-memory packet write buffer. */
typedef CNA_Handle CNA_PacketWriterHandle;

/** @brief Owned handle for an in-memory packet read buffer. */
typedef CNA_Handle CNA_PacketReaderHandle;

/**
 * @brief Initializes an unmeasured quality-of-service description.
 *
 * @param out_value Caller-provided versioned structure to initialize.
 * @return `CNA_RESULT_SUCCESS` or `CNA_RESULT_INVALID_ARGUMENT` for an invalid structure.
 *
 * Availability is still reported as `CNA_TRUE` with all measurements zero, matching the canonical
 * unmeasured factory exactly.
 */
CNA_C_API CNA_Result cna_quality_of_service_init(CNA_QualityOfService* out_value);

/**
 * @brief Initializes a quality-of-service description from one round-trip sample.
 *
 * @param roundtrip_ticks Measured round-trip time in 100-nanosecond ticks.
 * @param out_value Caller-provided versioned structure to initialize.
 * @return `CNA_RESULT_SUCCESS` or `CNA_RESULT_INVALID_ARGUMENT` for an invalid structure.
 *
 * A single exchange yields one sample, so the average and minimum both take that value and the
 * throughput fields stay zero, matching the canonical measured factory.
 */
CNA_C_API CNA_Result cna_quality_of_service_init_measured(
    int64_t roundtrip_ticks,
    CNA_QualityOfService* out_value);

/**
 * @brief Gets the join error carried by the most recent join failure on this thread.
 *
 * @param out_join_error Receives one of the `CNA_NETWORK_SESSION_JOIN_ERROR_*` identities.
 * @param out_has_join_error Receives `CNA_TRUE` when the last failure on this thread was a join
 * failure that carried an error.
 * @return `CNA_RESULT_SUCCESS` or `CNA_RESULT_INVALID_ARGUMENT` for a null output.
 *
 * The canonical join exception carries this value on the exception object, which never crosses the
 * ABI. The C exception firewall records it per thread instead, alongside the usual result,
 * category and diagnostic text.
 */
CNA_C_API CNA_Result cna_net_get_last_join_error(
    CNA_NetworkSessionJoinError* out_join_error,
    CNA_Bool* out_has_join_error);

/**
 * @brief Creates an owned, empty session-property list.
 *
 * @param out_properties Receives an owned property-list handle on success.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/thread/native failure.
 */
CNA_C_API CNA_Result cna_network_session_properties_create(
    CNA_NetworkSessionPropertiesHandle* out_properties);

/**
 * @brief Gets the number of properties in a list.
 *
 * @param properties Owned property-list handle.
 * @param out_count Receives the element count.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_network_session_properties_get_count(
    CNA_NetworkSessionPropertiesHandle properties,
    int32_t* out_count);

/**
 * @brief Gets whether a property list reports itself as read-only.
 *
 * @param properties Owned property-list handle.
 * @param out_is_read_only Receives the canonical answer, which is always `CNA_TRUE`.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 *
 * The canonical list reports read-only while still accepting add, remove and clear; the C API
 * preserves that rather than hiding it.
 */
CNA_C_API CNA_Result cna_network_session_properties_get_is_read_only(
    CNA_NetworkSessionPropertiesHandle properties,
    CNA_Bool* out_is_read_only);

/**
 * @brief Gets the property at an index without mutating the list.
 *
 * @param properties Owned property-list handle.
 * @param index Zero-based index.
 * @param out_value Receives the optional property value.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for an out-of-range index, or a
 * documented handle/thread failure.
 */
CNA_C_API CNA_Result cna_network_session_properties_get_item(
    CNA_NetworkSessionPropertiesHandle properties,
    int32_t index,
    CNA_OptionalInt32* out_value);

/**
 * @brief Replaces the property at an index.
 *
 * @param properties Owned property-list handle.
 * @param index Zero-based index.
 * @param value The optional property value to store.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 *
 * An index beyond the end appends instead of extending the list, matching the canonical setter. A
 * negative index is refused.
 */
CNA_C_API CNA_Result cna_network_session_properties_set_item(
    CNA_NetworkSessionPropertiesHandle properties,
    int32_t index,
    CNA_OptionalInt32 value);

/**
 * @brief Finds the index of a property value.
 *
 * @param properties Owned property-list handle.
 * @param value The optional value to locate.
 * @param out_index Receives the index, or -1 when the value is absent.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_network_session_properties_index_of(
    CNA_NetworkSessionPropertiesHandle properties,
    CNA_OptionalInt32 value,
    int32_t* out_index);

/**
 * @brief Inserts a property value at an index.
 *
 * @param properties Owned property-list handle.
 * @param index Zero-based insertion index.
 * @param value The optional value to insert.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for an out-of-range index, or a
 * documented handle/thread failure.
 *
 * The canonical list forwards this straight to its backing vector without a bounds check, so the
 * C route decides the range itself rather than passing an invalid position through.
 */
CNA_C_API CNA_Result cna_network_session_properties_insert(
    CNA_NetworkSessionPropertiesHandle properties,
    int32_t index,
    CNA_OptionalInt32 value);

/**
 * @brief Removes the property at an index.
 *
 * @param properties Owned property-list handle.
 * @param index Zero-based index.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for an out-of-range index, or a
 * documented handle/thread failure.
 *
 * The canonical list forwards this straight to its backing vector without a bounds check, so the
 * C route decides the range itself rather than passing an invalid position through.
 */
CNA_C_API CNA_Result cna_network_session_properties_remove_at(
    CNA_NetworkSessionPropertiesHandle properties,
    int32_t index);

/**
 * @brief Appends a property value.
 *
 * @param properties Owned property-list handle.
 * @param value The optional value to append.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_network_session_properties_add(
    CNA_NetworkSessionPropertiesHandle properties,
    CNA_OptionalInt32 value);

/**
 * @brief Removes the first occurrence of a property value.
 *
 * @param properties Owned property-list handle.
 * @param value The optional value to remove.
 * @param out_removed Receives `CNA_TRUE` when a value was removed.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_network_session_properties_remove(
    CNA_NetworkSessionPropertiesHandle properties,
    CNA_OptionalInt32 value,
    CNA_Bool* out_removed);

/**
 * @brief Gets whether a property value is present in the list.
 *
 * @param properties Owned property-list handle.
 * @param value The optional value to locate.
 * @param out_contains Receives `CNA_TRUE` when the value is present.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_network_session_properties_contains(
    CNA_NetworkSessionPropertiesHandle properties,
    CNA_OptionalInt32 value,
    CNA_Bool* out_contains);

/**
 * @brief Removes every property from the list.
 *
 * @param properties Owned property-list handle.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread failure.
 */
CNA_C_API CNA_Result cna_network_session_properties_clear(
    CNA_NetworkSessionPropertiesHandle properties);

/**
 * @brief Copies every property into a caller-owned array.
 *
 * @param properties Owned property-list handle.
 * @param destination Caller-owned destination, or null only when @p capacity is zero.
 * @param capacity Destination capacity in elements.
 * @param index Zero-based starting index within @p destination; must not be negative.
 * @param out_count Receives the number of elements the list holds.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL` when the destination cannot hold
 * @p index plus the element count, `CNA_RESULT_INVALID_ARGUMENT` for a negative index, or a
 * documented handle/thread failure. No partial copy is written.
 */
CNA_C_API CNA_Result cna_network_session_properties_copy_to(
    CNA_NetworkSessionPropertiesHandle properties,
    CNA_OptionalInt32* destination,
    uint64_t capacity,
    int32_t index,
    uint64_t* out_count);

/**
 * @brief Creates an owned forward enumerator positioned before the first property.
 *
 * @param properties Owned property-list handle.
 * @param out_enumerator Receives an owned enumerator handle on success.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 *
 * The enumerator observes the live list, so it must be destroyed before its property list and its
 * results are undefined after the list is mutated, exactly as the canonical enumerator is.
 */
CNA_C_API CNA_Result cna_network_session_properties_create_enumerator(
    CNA_NetworkSessionPropertiesHandle properties,
    CNA_NetworkSessionPropertyEnumeratorHandle* out_enumerator);

/**
 * @brief Advances an enumerator to the next property.
 *
 * @param enumerator Owned enumerator handle.
 * @param out_has_current Receives `CNA_TRUE` when a property is now current.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_network_session_property_enumerator_move_next(
    CNA_NetworkSessionPropertyEnumeratorHandle enumerator,
    CNA_Bool* out_has_current);

/**
 * @brief Gets the property at the enumerator's current position.
 *
 * @param enumerator Owned enumerator handle.
 * @param out_value Receives the optional property value.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_STATE` before the first advance or after the
 * last one, or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_network_session_property_enumerator_get_current(
    CNA_NetworkSessionPropertyEnumeratorHandle enumerator,
    CNA_OptionalInt32* out_value);

/**
 * @brief Resets an enumerator to before the first property.
 *
 * @param enumerator Owned enumerator handle.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread failure.
 */
CNA_C_API CNA_Result cna_network_session_property_enumerator_reset(
    CNA_NetworkSessionPropertyEnumeratorHandle enumerator);

/**
 * @brief Releases an owned enumerator handle.
 *
 * @param enumerator Owned enumerator handle.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread failure.
 */
CNA_C_API CNA_Result cna_network_session_property_enumerator_destroy(
    CNA_NetworkSessionPropertyEnumeratorHandle enumerator);

/**
 * @brief Releases an owned session-property list.
 *
 * @param properties Owned property-list handle.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_STATE` while an enumerator is still open, or a
 * documented handle/thread failure.
 */
CNA_C_API CNA_Result cna_network_session_properties_destroy(
    CNA_NetworkSessionPropertiesHandle properties);

/**
 * @brief Creates an owned packet write buffer.
 *
 * @param capacity Initial capacity hint; must not be negative.
 * @param out_writer Receives an owned packet-writer handle on success.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for a negative capacity, or a
 * documented thread/native failure.
 *
 * A negative capacity is refused exactly as the canonical constructor refuses it; a non-negative
 * capacity is a hint the canonical backing buffer does not act on.
 */
CNA_C_API CNA_Result cna_packet_writer_create(
    int32_t capacity,
    CNA_PacketWriterHandle* out_writer);

/**
 * @brief Gets the length of a packet write buffer.
 *
 * @param writer Owned packet-writer handle.
 * @param out_length Receives the buffer length in bytes.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_packet_writer_get_length(
    CNA_PacketWriterHandle writer,
    int32_t* out_length);

/**
 * @brief Gets the current write position.
 *
 * @param writer Owned packet-writer handle.
 * @param out_position Receives the position in bytes.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_packet_writer_get_position(
    CNA_PacketWriterHandle writer,
    int32_t* out_position);

/**
 * @brief Sets the current write position.
 *
 * @param writer Owned packet-writer handle.
 * @param position The new position in bytes.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_packet_writer_set_position(
    CNA_PacketWriterHandle writer,
    int32_t position);

/**
 * @brief Writes a color as four bytes in R, G, B, A order.
 *
 * @param writer Owned packet-writer handle.
 * @param value The color to write.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread/native failure.
 *
 * This is deliberately not the inverse of `cna_packet_reader_read_color`, which reads four floats;
 * the canonical implementation is asymmetric and the C API preserves that.
 */
CNA_C_API CNA_Result cna_packet_writer_write_color(
    CNA_PacketWriterHandle writer,
    CNA_Color value);

/**
 * @brief Writes a matrix as sixteen floats in row order.
 *
 * @param writer Owned packet-writer handle.
 * @param value The matrix to write.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_packet_writer_write_matrix(
    CNA_PacketWriterHandle writer,
    CNA_Matrix value);

/**
 * @brief Writes a quaternion as four floats in X, Y, Z, W order.
 *
 * @param writer Owned packet-writer handle.
 * @param value The quaternion to write.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_packet_writer_write_quaternion(
    CNA_PacketWriterHandle writer,
    CNA_Quaternion value);

/**
 * @brief Writes a two-component vector.
 *
 * @param writer Owned packet-writer handle.
 * @param value The vector to write.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_packet_writer_write_vector2(
    CNA_PacketWriterHandle writer,
    CNA_Vector2 value);

/**
 * @brief Writes a three-component vector.
 *
 * @param writer Owned packet-writer handle.
 * @param value The vector to write.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_packet_writer_write_vector3(
    CNA_PacketWriterHandle writer,
    CNA_Vector3 value);

/**
 * @brief Writes a four-component vector.
 *
 * @param writer Owned packet-writer handle.
 * @param value The vector to write.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_packet_writer_write_vector4(
    CNA_PacketWriterHandle writer,
    CNA_Vector4 value);

/**
 * @brief Writes a four-byte single-precision float.
 *
 * @param writer Owned packet-writer handle.
 * @param value The value to write.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_packet_writer_write_single(
    CNA_PacketWriterHandle writer,
    float value);

/**
 * @brief Writes an eight-byte double-precision float.
 *
 * @param writer Owned packet-writer handle.
 * @param value The value to write.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_packet_writer_write_double(
    CNA_PacketWriterHandle writer,
    double value);

/**
 * @brief Copies the bytes a packet writer has produced.
 *
 * @param writer Owned packet-writer handle.
 * @param destination Caller-owned destination, or null only when @p capacity is zero.
 * @param capacity Destination capacity in bytes.
 * @param out_bytes Receives the buffer length in bytes.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL`, or a documented
 * argument/handle/thread failure. No partial copy is written.
 *
 * CNAEXT: the canonical API hands a writer straight to a send operation and never exposes its
 * bytes. This route exists so a C consumer can move a packet across a transport the C API does not
 * own, and so packet contents can be observed directly.
 */
CNA_C_API CNA_Result cna_packet_writer_copy_data_ext(
    CNA_PacketWriterHandle writer,
    uint8_t* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

/**
 * @brief Releases an owned packet-writer handle.
 *
 * @param writer Owned packet-writer handle.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread failure.
 */
CNA_C_API CNA_Result cna_packet_writer_destroy(CNA_PacketWriterHandle writer);

/**
 * @brief Creates an owned packet read buffer.
 *
 * @param capacity Initial capacity hint; must not be negative.
 * @param out_reader Receives an owned packet-reader handle on success.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for a negative capacity, or a
 * documented thread/native failure.
 */
CNA_C_API CNA_Result cna_packet_reader_create(
    int32_t capacity,
    CNA_PacketReaderHandle* out_reader);

/**
 * @brief Replaces a packet reader's buffer contents and rewinds it.
 *
 * @param reader Owned packet-reader handle.
 * @param data Caller-owned bytes copied during this call, or null only when @p count is zero.
 * @param count Number of bytes beginning at @p data.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 *
 * CNAEXT: the canonical API fills a reader through a receive operation and never exposes its
 * buffer. This route exists so a C consumer can supply a packet that arrived over a transport the
 * C API does not own.
 */
CNA_C_API CNA_Result cna_packet_reader_set_data_ext(
    CNA_PacketReaderHandle reader,
    const uint8_t* data,
    uint64_t count);

/**
 * @brief Gets the length of a packet read buffer.
 *
 * @param reader Owned packet-reader handle.
 * @param out_length Receives the buffer length in bytes.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_packet_reader_get_length(
    CNA_PacketReaderHandle reader,
    int32_t* out_length);

/**
 * @brief Gets the current read position.
 *
 * @param reader Owned packet-reader handle.
 * @param out_position Receives the position in bytes.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_packet_reader_get_position(
    CNA_PacketReaderHandle reader,
    int32_t* out_position);

/**
 * @brief Sets the current read position.
 *
 * @param reader Owned packet-reader handle.
 * @param position The new position in bytes.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_packet_reader_set_position(
    CNA_PacketReaderHandle reader,
    int32_t position);

/**
 * @brief Reads a color as four floats.
 *
 * @param reader Owned packet-reader handle.
 * @param out_value Receives the color.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_IO` at end of buffer, or a documented
 * argument/handle/thread failure.
 *
 * The canonical reader reads floats here while the writer writes bytes; the asymmetry is preserved
 * rather than corrected, so a color round trip needs the caller to match the two formats.
 */
CNA_C_API CNA_Result cna_packet_reader_read_color(
    CNA_PacketReaderHandle reader,
    CNA_Color* out_value);

/**
 * @brief Reads a matrix as sixteen floats in row order.
 *
 * @param reader Owned packet-reader handle.
 * @param out_value Receives the matrix.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_IO` at end of buffer, or a documented
 * argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_packet_reader_read_matrix(
    CNA_PacketReaderHandle reader,
    CNA_Matrix* out_value);

/**
 * @brief Reads a quaternion as four floats in X, Y, Z, W order.
 *
 * @param reader Owned packet-reader handle.
 * @param out_value Receives the quaternion.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_IO` at end of buffer, or a documented
 * argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_packet_reader_read_quaternion(
    CNA_PacketReaderHandle reader,
    CNA_Quaternion* out_value);

/**
 * @brief Reads a two-component vector.
 *
 * @param reader Owned packet-reader handle.
 * @param out_value Receives the vector.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_IO` at end of buffer, or a documented
 * argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_packet_reader_read_vector2(
    CNA_PacketReaderHandle reader,
    CNA_Vector2* out_value);

/**
 * @brief Reads a three-component vector.
 *
 * @param reader Owned packet-reader handle.
 * @param out_value Receives the vector.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_IO` at end of buffer, or a documented
 * argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_packet_reader_read_vector3(
    CNA_PacketReaderHandle reader,
    CNA_Vector3* out_value);

/**
 * @brief Reads a four-component vector.
 *
 * @param reader Owned packet-reader handle.
 * @param out_value Receives the vector.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_IO` at end of buffer, or a documented
 * argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_packet_reader_read_vector4(
    CNA_PacketReaderHandle reader,
    CNA_Vector4* out_value);

/**
 * @brief Reads a four-byte single-precision float.
 *
 * @param reader Owned packet-reader handle.
 * @param out_value Receives the value.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_IO` at end of buffer, or a documented
 * argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_packet_reader_read_single(
    CNA_PacketReaderHandle reader,
    float* out_value);

/**
 * @brief Reads an eight-byte double-precision float.
 *
 * @param reader Owned packet-reader handle.
 * @param out_value Receives the value.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_IO` at end of buffer, or a documented
 * argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_packet_reader_read_double(
    CNA_PacketReaderHandle reader,
    double* out_value);

/**
 * @brief Releases an owned packet-reader handle.
 *
 * @param reader Owned packet-reader handle.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread failure.
 */
CNA_C_API CNA_Result cna_packet_reader_destroy(CNA_PacketReaderHandle reader);

#ifdef __cplusplus
}
#endif

#endif
