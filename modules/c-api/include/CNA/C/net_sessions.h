// SPDX-License-Identifier: MS-PL

#ifndef CNA_C_NET_SESSIONS_H
#define CNA_C_NET_SESSIONS_H

#include "CNA/C/net_gamers.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Owned handle for one session found by discovery. */
typedef CNA_Handle CNA_AvailableNetworkSessionHandle;

/** @brief Owned handle for a read-only collection of discovered sessions. */
typedef CNA_Handle CNA_AvailableNetworkSessionCollectionHandle;

/**
 * @brief Configures creation of a discovered session description.
 */
typedef struct CNA_AvailableNetworkSessionCreateInfo {
    /** @brief Size of this caller-provided structure in bytes. */
    uint32_t struct_size;

    /** @brief Version of this caller-provided structure. */
    uint32_t struct_version;

    /** @brief Number of gamers already in the session. */
    int32_t current_gamer_count;

    /** @brief Number of unoccupied private slots. */
    int32_t open_private_gamer_slots;

    /** @brief Number of unoccupied public slots. */
    int32_t open_public_gamer_slots;

    /** @brief One of the `CNA_NETWORK_SESSION_TYPE_*` identities. */
    CNA_NetworkSessionType session_type;

    /** @brief Port the host accepts connections on. */
    uint16_t host_port;

    /** @brief Reserved bytes; callers must initialize these to zero. */
    uint8_t reserved[6];

    /** @brief UTF-8 gamertag of the session host, copied during creation. */
    CNA_StringView host_gamertag;

    /** @brief UTF-8 address the host accepts connections on, copied during creation. */
    CNA_StringView host_address;

    /**
     * @brief Session properties copied during creation, or `CNA_INVALID_HANDLE` for an empty set.
     */
    CNA_NetworkSessionPropertiesHandle session_properties;
} CNA_AvailableNetworkSessionCreateInfo;

/**
 * @brief Creates an owned description of a discovered session.
 *
 * @param create_info Versioned creation configuration.
 * @param quality_of_service Measured quality of service, or null for the unmeasured description.
 * @param out_session Receives an owned discovered-session handle on success.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_ENCODING` for invalid UTF-8, or a documented
 * argument/handle/thread/native failure.
 *
 * CNAEXT: the canonical factory exists so a discovery backend can publish what it found. A C
 * consumer needs it for the same reason, and because discovery results are otherwise unreachable
 * until the session slice lands.
 */
CNA_C_API CNA_Result cna_available_network_session_create_ext(
    const CNA_AvailableNetworkSessionCreateInfo* create_info,
    const CNA_QualityOfService* quality_of_service,
    CNA_AvailableNetworkSessionHandle* out_session);

/**
 * @brief Gets how many gamers are already in a discovered session.
 *
 * @param session Owned discovered-session handle.
 * @param out_value Receives the gamer count.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_available_network_session_get_current_gamer_count(
    CNA_AvailableNetworkSessionHandle session,
    int32_t* out_value);

/**
 * @brief Gets the UTF-8 byte count of a discovered session's host gamertag.
 *
 * @param session Owned discovered-session handle.
 * @param out_bytes Receives the byte count without a terminator.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_available_network_session_get_host_gamertag_size(
    CNA_AvailableNetworkSessionHandle session,
    uint64_t* out_bytes);

/**
 * @brief Copies a discovered session's host gamertag without a terminator.
 *
 * @param session Owned discovered-session handle.
 * @param destination Caller-owned destination, or null only when @p capacity is zero.
 * @param capacity Destination capacity in bytes.
 * @param out_bytes Receives the required byte count without a terminator.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL`, or a documented
 * argument/handle/thread failure. No partial value is written.
 */
CNA_C_API CNA_Result cna_available_network_session_copy_host_gamertag(
    CNA_AvailableNetworkSessionHandle session,
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

/**
 * @brief Gets how many private slots a discovered session still has open.
 *
 * @param session Owned discovered-session handle.
 * @param out_value Receives the open private slot count.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_available_network_session_get_open_private_gamer_slots(
    CNA_AvailableNetworkSessionHandle session,
    int32_t* out_value);

/**
 * @brief Gets how many public slots a discovered session still has open.
 *
 * @param session Owned discovered-session handle.
 * @param out_value Receives the open public slot count.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_available_network_session_get_open_public_gamer_slots(
    CNA_AvailableNetworkSessionHandle session,
    int32_t* out_value);

/**
 * @brief Gets the measured quality of service for a discovered session.
 *
 * @param session Owned discovered-session handle.
 * @param out_value Caller-provided versioned structure to receive the measurement.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_available_network_session_get_quality_of_service(
    CNA_AvailableNetworkSessionHandle session,
    CNA_QualityOfService* out_value);

/**
 * @brief Copies a discovered session's properties into a new owned list.
 *
 * @param session Owned discovered-session handle.
 * @param out_properties Receives an owned property-list handle on success.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 *
 * The canonical getter returns a reference into the discovered session, which was itself built
 * from a copied property list. The C route hands back an independent copy so the caller's list
 * cannot outlive or alias the description it came from.
 */
CNA_C_API CNA_Result cna_available_network_session_copy_session_properties(
    CNA_AvailableNetworkSessionHandle session,
    CNA_NetworkSessionPropertiesHandle* out_properties);

/**
 * @brief Compares two discovered sessions for equality.
 *
 * @param left Owned discovered-session handle.
 * @param right Owned discovered-session handle.
 * @param out_equal Receives `CNA_TRUE` when the descriptions are equal.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_available_network_session_equals(
    CNA_AvailableNetworkSessionHandle left,
    CNA_AvailableNetworkSessionHandle right,
    CNA_Bool* out_equal);

/**
 * @brief Compares two discovered sessions for inequality.
 *
 * @param left Owned discovered-session handle.
 * @param right Owned discovered-session handle.
 * @param out_not_equal Receives `CNA_TRUE` when the descriptions differ.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_available_network_session_not_equals(
    CNA_AvailableNetworkSessionHandle left,
    CNA_AvailableNetworkSessionHandle right,
    CNA_Bool* out_not_equal);

/**
 * @brief Gets the UTF-8 byte count of a discovered session's connect address.
 *
 * @param session Owned discovered-session handle.
 * @param out_bytes Receives the byte count without a terminator.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_available_network_session_get_connect_address_size_ext(
    CNA_AvailableNetworkSessionHandle session,
    uint64_t* out_bytes);

/**
 * @brief Copies a discovered session's connect address without a terminator.
 *
 * @param session Owned discovered-session handle.
 * @param destination Caller-owned destination, or null only when @p capacity is zero.
 * @param capacity Destination capacity in bytes.
 * @param out_bytes Receives the required byte count without a terminator.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL`, or a documented
 * argument/handle/thread failure. No partial value is written.
 */
CNA_C_API CNA_Result cna_available_network_session_copy_connect_address_ext(
    CNA_AvailableNetworkSessionHandle session,
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

/**
 * @brief Gets the port a discovered session's host accepts connections on.
 *
 * @param session Owned discovered-session handle.
 * @param out_value Receives the port.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_available_network_session_get_connect_port_ext(
    CNA_AvailableNetworkSessionHandle session,
    uint16_t* out_value);

/**
 * @brief Gets the session type a discovered session advertises.
 *
 * @param session Owned discovered-session handle.
 * @param out_value Receives one of the `CNA_NETWORK_SESSION_TYPE_*` identities.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_available_network_session_get_session_type_ext(
    CNA_AvailableNetworkSessionHandle session,
    CNA_NetworkSessionType* out_value);

/**
 * @brief Releases an owned discovered-session handle.
 *
 * @param session Owned discovered-session handle.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread failure.
 */
CNA_C_API CNA_Result cna_available_network_session_destroy(
    CNA_AvailableNetworkSessionHandle session);

/**
 * @brief Creates an owned, read-only collection of discovered sessions.
 *
 * @param sessions Caller-owned array of discovered-session handles, or null when @p count is zero.
 * @param count Number of handles beginning at @p sessions.
 * @param out_collection Receives an owned collection handle on success.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 *
 * CNAEXT: the canonical factory exists so a discovery backend can publish its results. Each
 * description is copied into the collection, so the source handles stay independently owned and
 * may be released immediately.
 */
CNA_C_API CNA_Result cna_available_network_session_collection_create_ext(
    const CNA_AvailableNetworkSessionHandle* sessions,
    uint64_t count,
    CNA_AvailableNetworkSessionCollectionHandle* out_collection);

/**
 * @brief Gets the number of discovered sessions in a collection.
 *
 * @param collection Owned collection handle.
 * @param out_count Receives the element count.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_available_network_session_collection_get_count(
    CNA_AvailableNetworkSessionCollectionHandle collection,
    int32_t* out_count);

/**
 * @brief Copies one discovered session out of a collection into a new owned handle.
 *
 * @param collection Owned collection handle.
 * @param index Zero-based index.
 * @param out_session Receives an owned discovered-session handle on success.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for an out-of-range index, or a
 * documented handle/thread/native failure.
 *
 * The returned handle owns an independent copy, so it stays valid after the collection is disposed
 * or released.
 */
CNA_C_API CNA_Result cna_available_network_session_collection_copy_session(
    CNA_AvailableNetworkSessionCollectionHandle collection,
    int32_t index,
    CNA_AvailableNetworkSessionHandle* out_session);

/**
 * @brief Gets whether a collection has been disposed.
 *
 * @param collection Owned collection handle.
 * @param out_is_disposed Receives `CNA_TRUE` when the collection has been disposed.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_available_network_session_collection_get_is_disposed(
    CNA_AvailableNetworkSessionCollectionHandle collection,
    CNA_Bool* out_is_disposed);

/**
 * @brief Disposes a collection without releasing its handle.
 *
 * @param collection Owned collection handle.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread/native failure. Disposal is
 * idempotent.
 */
CNA_C_API CNA_Result cna_available_network_session_collection_dispose(
    CNA_AvailableNetworkSessionCollectionHandle collection);

/**
 * @brief Releases an owned collection handle, disposing it if necessary.
 *
 * @param collection Owned collection handle.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread failure.
 */
CNA_C_API CNA_Result cna_available_network_session_collection_destroy(
    CNA_AvailableNetworkSessionCollectionHandle collection);

#ifdef __cplusplus
}
#endif

#endif
