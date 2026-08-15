// SPDX-License-Identifier: MS-PL

#ifndef CNA_C_NET_SESSIONS_H
#define CNA_C_NET_SESSIONS_H

#include "CNA/C/gamer_services.h"
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

/** @brief Owned handle for a network session. */
typedef CNA_Handle CNA_NetworkSessionHandle;

/** @brief Largest number of gamers a session supports. */
#define CNA_NETWORK_SESSION_MAX_SUPPORTED_GAMERS INT32_C(31)

/** @brief Largest number of previous gamers a session remembers. */
#define CNA_NETWORK_SESSION_MAX_PREVIOUS_GAMERS INT32_C(100)

/** @brief Fixed-width identity naming what a queued session event describes. */
typedef uint32_t CNA_NetworkEventType;

/** @brief A packet was sent to a gamer. */
#define CNA_NETWORK_EVENT_TYPE_PACKET_SEND UINT32_C(0)
/** @brief A gamer joined the session. */
#define CNA_NETWORK_EVENT_TYPE_GAMER_JOIN UINT32_C(1)
/** @brief A gamer left the session. */
#define CNA_NETWORK_EVENT_TYPE_GAMER_LEAVE UINT32_C(2)
/** @brief The session host changed. */
#define CNA_NETWORK_EVENT_TYPE_HOST_CHANGE UINT32_C(3)
/** @brief The session state changed. */
#define CNA_NETWORK_EVENT_TYPE_STATE_CHANGE UINT32_C(4)

/**
 * @brief Describes one event queued on a session.
 */
typedef struct CNA_NetworkEventInfo {
    /** @brief Size of this caller-provided structure in bytes. */
    uint32_t struct_size;

    /** @brief Version of this caller-provided structure. */
    uint32_t struct_version;

    /** @brief One of the `CNA_NETWORK_EVENT_TYPE_*` identities. */
    CNA_NetworkEventType type;

    /** @brief One of the `CNA_SEND_DATA_OPTIONS_*` identities, for a packet event. */
    CNA_SendDataOptions reliable;

    /** @brief One of the `CNA_NETWORK_SESSION_STATE_*` identities, for a state-change event. */
    CNA_NetworkSessionState state;

    /** @brief One of the `CNA_NETWORK_SESSION_END_REASON_*` identities, for a session end. */
    CNA_NetworkSessionEndReason reason;

    /** @brief The gamer the event is addressed to, or `CNA_INVALID_HANDLE`. */
    CNA_NetworkGamerHandle gamer;

    /** @brief The gamer that sent a packet event's payload, or `CNA_INVALID_HANDLE`. */
    CNA_NetworkGamerHandle sender;

    /** @brief Packet payload copied during the call, or null when @ref packet_byte_count is zero. */
    const uint8_t* packet;

    /** @brief Number of payload bytes beginning at @ref packet. */
    uint64_t packet_byte_count;
} CNA_NetworkEventInfo;

/**
 * @brief Creates an owned network session with a maximum local-gamer count.
 *
 * @param session_type One of the `CNA_NETWORK_SESSION_TYPE_*` identities.
 * @param max_local_gamers Largest number of local gamers, between one and four.
 * @param max_gamers Largest number of gamers in the session.
 * @param out_session Receives an owned session handle on success.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for an unknown identity or an
 * out-of-range count, `CNA_RESULT_INVALID_STATE` when a session already exists, or a documented
 * thread/native failure.
 *
 * The canonical creation is a fake-async pair the canonical synchronous overload already drives to
 * completion, so this is one synchronous call. Only one session exists at a time, matching the
 * canonical process-wide restriction. This overload takes its local gamers from the process-wide
 * signed-in collection, so that collection must already hold at least one gamer; the canonical
 * constructor otherwise fails while selecting its host.
 */
CNA_C_API CNA_Result cna_network_session_create(
    CNA_NetworkSessionType session_type,
    int32_t max_local_gamers,
    int32_t max_gamers,
    CNA_NetworkSessionHandle* out_session);

/**
 * @brief Creates an owned network session with private slots and session properties.
 *
 * @param session_type One of the `CNA_NETWORK_SESSION_TYPE_*` identities.
 * @param max_local_gamers Largest number of local gamers, between one and four.
 * @param max_gamers Largest number of gamers in the session.
 * @param private_gamer_slots Number of reserved private slots.
 * @param session_properties Properties copied during creation, or `CNA_INVALID_HANDLE`.
 * @param out_session Receives an owned session handle on success.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/state/handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_network_session_create_with_properties(
    CNA_NetworkSessionType session_type,
    int32_t max_local_gamers,
    int32_t max_gamers,
    int32_t private_gamer_slots,
    CNA_NetworkSessionPropertiesHandle session_properties,
    CNA_NetworkSessionHandle* out_session);

/**
 * @brief Creates an owned network session from an explicit local-gamer list.
 *
 * @param session_type One of the `CNA_NETWORK_SESSION_TYPE_*` identities.
 * @param local_gamers Caller-owned array of signed-in gamer handles, or null when @p count is zero.
 * @param count Number of handles beginning at @p local_gamers.
 * @param max_gamers Largest number of gamers in the session.
 * @param private_gamer_slots Number of reserved private slots.
 * @param session_properties Properties copied during creation, or `CNA_INVALID_HANDLE`.
 * @param out_session Receives an owned session handle on success.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/state/handle/thread/native failure.
 *
 * This is the overload a C consumer normally uses: the canonical session constructor requires at
 * least one signed-in gamer, and the other two overloads take theirs from the process-wide
 * signed-in collection, which must therefore be published first.
 */
CNA_C_API CNA_Result cna_network_session_create_with_local_gamers(
    CNA_NetworkSessionType session_type,
    const CNA_Handle* local_gamers,
    uint64_t count,
    int32_t max_gamers,
    int32_t private_gamer_slots,
    CNA_NetworkSessionPropertiesHandle session_properties,
    CNA_NetworkSessionHandle* out_session);

/**
 * @brief Gets whether a session has been disposed.
 *
 * @param session Owned session handle.
 * @param out_is_disposed Receives `CNA_TRUE` when the session has been disposed.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_network_session_get_is_disposed(
    CNA_NetworkSessionHandle session,
    CNA_Bool* out_is_disposed);

/**
 * @brief Gets the number of gamers in one of a session's rosters.
 *
 * @param session Owned session handle.
 * @param roster One of the `CNA_NETWORK_SESSION_ROSTER_*` identities.
 * @param out_count Receives the gamer count.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for an unknown roster, or a
 * documented handle/thread failure.
 */
CNA_C_API CNA_Result cna_network_session_get_gamer_count(
    CNA_NetworkSessionHandle session,
    uint32_t roster,
    int32_t* out_count);

/** @brief Every gamer in the session, local and remote. */
#define CNA_NETWORK_SESSION_ROSTER_ALL UINT32_C(0)
/** @brief The local gamers in the session. */
#define CNA_NETWORK_SESSION_ROSTER_LOCAL UINT32_C(1)
/** @brief The remote gamers in the session. */
#define CNA_NETWORK_SESSION_ROSTER_REMOTE UINT32_C(2)
/** @brief The gamers that have left the session. */
#define CNA_NETWORK_SESSION_ROSTER_PREVIOUS UINT32_C(3)

/**
 * @brief Gets one gamer from one of a session's rosters.
 *
 * @param session Owned session handle.
 * @param roster One of the `CNA_NETWORK_SESSION_ROSTER_*` identities.
 * @param index Zero-based index within that roster.
 * @param out_gamer Receives a borrowed-view gamer handle on success.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for an unknown roster or an
 * out-of-range index, or a documented handle/thread failure.
 *
 * The returned handle observes a gamer the session owns, so it must be released before the session.
 */
CNA_C_API CNA_Result cna_network_session_get_gamer(
    CNA_NetworkSessionHandle session,
    uint32_t roster,
    int32_t index,
    CNA_NetworkGamerHandle* out_gamer);

/**
 * @brief Gets whether a session allows host migration.
 *
 * @param session Owned session handle.
 * @param out_value Receives `CNA_TRUE` when host migration is allowed.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_network_session_get_allow_host_migration(
    CNA_NetworkSessionHandle session,
    CNA_Bool* out_value);

/**
 * @brief Sets whether a session allows host migration.
 *
 * @param session Owned session handle.
 * @param value The new value.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_network_session_set_allow_host_migration(
    CNA_NetworkSessionHandle session,
    CNA_Bool value);

/**
 * @brief Gets whether a session allows joining in progress.
 *
 * @param session Owned session handle.
 * @param out_value Receives `CNA_TRUE` when joining in progress is allowed.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_network_session_get_allow_join_in_progress(
    CNA_NetworkSessionHandle session,
    CNA_Bool* out_value);

/**
 * @brief Sets whether a session allows joining in progress.
 *
 * @param session Owned session handle.
 * @param value The new value.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_network_session_set_allow_join_in_progress(
    CNA_NetworkSessionHandle session,
    CNA_Bool value);

/**
 * @brief Gets the measured inbound throughput.
 *
 * @param session Owned session handle.
 * @param out_value Receives the bytes received per second.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_network_session_get_bytes_per_second_received(
    CNA_NetworkSessionHandle session,
    int32_t* out_value);

/**
 * @brief Gets the measured outbound throughput.
 *
 * @param session Owned session handle.
 * @param out_value Receives the bytes sent per second.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_network_session_get_bytes_per_second_sent(
    CNA_NetworkSessionHandle session,
    int32_t* out_value);

/**
 * @brief Gets the session host.
 *
 * @param session Owned session handle.
 * @param out_gamer Receives a borrowed-view gamer handle, or `CNA_INVALID_HANDLE` when the session
 * has no host.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_network_session_get_host(
    CNA_NetworkSessionHandle session,
    CNA_NetworkGamerHandle* out_gamer);

/**
 * @brief Gets whether every gamer in a session is ready.
 *
 * @param session Owned session handle.
 * @param out_value Receives `CNA_TRUE` when everyone is ready.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_network_session_get_is_everyone_ready(
    CNA_NetworkSessionHandle session,
    CNA_Bool* out_value);

/**
 * @brief Gets whether the local machine hosts a session.
 *
 * @param session Owned session handle.
 * @param out_value Receives `CNA_TRUE` when the local machine is the host.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_network_session_get_is_host(
    CNA_NetworkSessionHandle session,
    CNA_Bool* out_value);

/**
 * @brief Gets the largest number of gamers a session accepts.
 *
 * @param session Owned session handle.
 * @param out_value Receives the maximum.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_network_session_get_max_gamers(
    CNA_NetworkSessionHandle session,
    int32_t* out_value);

/**
 * @brief Sets the largest number of gamers a session accepts.
 *
 * @param session Owned session handle.
 * @param value The new maximum.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_network_session_set_max_gamers(
    CNA_NetworkSessionHandle session,
    int32_t value);

/**
 * @brief Gets the number of reserved private slots.
 *
 * @param session Owned session handle.
 * @param out_value Receives the private slot count.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_network_session_get_private_gamer_slots(
    CNA_NetworkSessionHandle session,
    int32_t* out_value);

/**
 * @brief Sets the number of reserved private slots.
 *
 * @param session Owned session handle.
 * @param value The new private slot count.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_network_session_set_private_gamer_slots(
    CNA_NetworkSessionHandle session,
    int32_t value);

/**
 * @brief Copies a session's properties into a new owned list.
 *
 * @param session Owned session handle.
 * @param out_properties Receives an owned property-list handle on success.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 *
 * The canonical getter returns a reference into the session; the C route hands back an independent
 * copy so a caller's list cannot alias session state.
 */
CNA_C_API CNA_Result cna_network_session_copy_session_properties(
    CNA_NetworkSessionHandle session,
    CNA_NetworkSessionPropertiesHandle* out_properties);

/**
 * @brief Gets a session's current state.
 *
 * @param session Owned session handle.
 * @param out_value Receives one of the `CNA_NETWORK_SESSION_STATE_*` identities.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_network_session_get_session_state(
    CNA_NetworkSessionHandle session,
    CNA_NetworkSessionState* out_value);

/**
 * @brief Gets a session's type.
 *
 * @param session Owned session handle.
 * @param out_value Receives one of the `CNA_NETWORK_SESSION_TYPE_*` identities.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_network_session_get_session_type(
    CNA_NetworkSessionHandle session,
    CNA_NetworkSessionType* out_value);

/**
 * @brief Gets the simulated latency applied to a session.
 *
 * @param session Owned session handle.
 * @param out_ticks Receives the latency in 100-nanosecond ticks.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_network_session_get_simulated_latency_ticks(
    CNA_NetworkSessionHandle session,
    int64_t* out_ticks);

/**
 * @brief Sets the simulated latency applied to a session.
 *
 * @param session Owned session handle.
 * @param ticks The latency in 100-nanosecond ticks.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_network_session_set_simulated_latency_ticks(
    CNA_NetworkSessionHandle session,
    int64_t ticks);

/**
 * @brief Gets the simulated packet loss applied to a session.
 *
 * @param session Owned session handle.
 * @param out_value Receives the loss fraction.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_network_session_get_simulated_packet_loss(
    CNA_NetworkSessionHandle session,
    float* out_value);

/**
 * @brief Sets the simulated packet loss applied to a session.
 *
 * @param session Owned session handle.
 * @param value The loss fraction.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_network_session_set_simulated_packet_loss(
    CNA_NetworkSessionHandle session,
    float value);

/**
 * @brief Gets the UTF-8 byte count of a session's fully qualified .NET type name.
 *
 * @param session Owned session handle.
 * @param out_bytes Receives the byte count without a terminator.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_network_session_get_type_name_size(
    CNA_NetworkSessionHandle session,
    uint64_t* out_bytes);

/**
 * @brief Copies a session's fully qualified .NET type name without a terminator.
 *
 * @param session Owned session handle.
 * @param destination Caller-owned destination, or null only when @p capacity is zero.
 * @param capacity Destination capacity in bytes.
 * @param out_bytes Receives the required byte count without a terminator.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL`, or a documented
 * argument/handle/thread failure. No partial name is written.
 */
CNA_C_API CNA_Result cna_network_session_copy_type_name(
    CNA_NetworkSessionHandle session,
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

/**
 * @brief Pumps a session's queued events and transport.
 *
 * @param session Owned session handle.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_STATE` after disposal, or a documented
 * handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_network_session_update(CNA_NetworkSessionHandle session);

/**
 * @brief Adds a local gamer to a session.
 *
 * @param session Owned session handle.
 * @param signed_in_gamer Signed-in gamer handle, or `CNA_INVALID_HANDLE` for none.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_STATE` when the local-gamer limit is reached,
 * or a documented handle/thread/native failure.
 *
 * Signed-in gamers are a gamer-services type with no C representation yet, so only
 * `CNA_INVALID_HANDLE` is accepted today; the parameter shape is already final.
 */
CNA_C_API CNA_Result cna_network_session_add_local_gamer(
    CNA_NetworkSessionHandle session,
    CNA_Handle signed_in_gamer);

/**
 * @brief Finds a gamer in a session by its session-local identifier.
 *
 * @param session Owned session handle.
 * @param gamer_id The identifier to look for.
 * @param out_gamer Receives a borrowed-view gamer handle, or `CNA_INVALID_HANDLE` when no gamer
 * carries that identifier.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_network_session_find_gamer_by_id(
    CNA_NetworkSessionHandle session,
    uint8_t gamer_id,
    CNA_NetworkGamerHandle* out_gamer);

/**
 * @brief Clears the ready flag on every gamer in a session.
 *
 * @param session Owned session handle.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_network_session_reset_ready(CNA_NetworkSessionHandle session);

/**
 * @brief Moves a session into its playing state.
 *
 * @param session Owned session handle.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_STATE` when the session is not in its lobby,
 * or a documented handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_network_session_start_game(CNA_NetworkSessionHandle session);

/**
 * @brief Moves a session back into its lobby.
 *
 * @param session Owned session handle.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_STATE` when the session is not playing, or a
 * documented handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_network_session_end_game(CNA_NetworkSessionHandle session);

/**
 * @brief Queues an event on a session.
 *
 * @param session Owned session handle.
 * @param event_info Versioned event description; its payload is copied during this call.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 *
 * This maps a CNA extension used to deliver a transport event into a session.
 */
CNA_C_API CNA_Result cna_network_session_send_network_event_ext(
    CNA_NetworkSessionHandle session,
    const CNA_NetworkEventInfo* event_info);

/**
 * @brief Adds a remote gamer to a session.
 *
 * @param session Owned session handle.
 * @param gamer Owned gamer handle.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_STATE` when the session is full, or a
 * documented handle/thread/native failure.
 *
 * This maps a CNA extension. The canonical call deliberately does not take ownership, so the C
 * route retains the gamer handle's resource for the session's lifetime; the caller may release its
 * own handle immediately without invalidating the session's roster.
 */
CNA_C_API CNA_Result cna_network_session_add_remote_gamer_ext(
    CNA_NetworkSessionHandle session,
    CNA_NetworkGamerHandle gamer);

/**
 * @brief Removes a gamer from a session.
 *
 * @param session Owned session handle.
 * @param gamer Owned or borrowed gamer handle.
 * @param reason One of the `CNA_NETWORK_SESSION_END_REASON_*` identities.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for an unknown reason, or a
 * documented handle/thread/native failure.
 *
 * This maps a CNA extension. Any retention the session took for a remote gamer is released.
 */
CNA_C_API CNA_Result cna_network_session_remove_gamer_ext(
    CNA_NetworkSessionHandle session,
    CNA_NetworkGamerHandle gamer,
    CNA_NetworkSessionEndReason reason);

/**
 * @brief Gets how many gamers a session owns outright.
 *
 * @param session Owned session handle.
 * @param out_count Receives the owned-gamer count.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 *
 * This maps a CNA diagnostic accessor and reports the local gamers a session allocated itself,
 * not the remote gamers it merely references.
 */
CNA_C_API CNA_Result cna_network_session_get_owned_gamer_count_ext(
    CNA_NetworkSessionHandle session,
    uint64_t* out_count);

/**
 * @brief Gets how many session objects currently exist in the process.
 *
 * @param out_count Receives the instance count.
 * @return `CNA_RESULT_SUCCESS` or `CNA_RESULT_INVALID_ARGUMENT` for a null output.
 *
 * This maps a CNA diagnostic accessor.
 */
CNA_C_API CNA_Result cna_network_session_get_instance_count_ext(int32_t* out_count);

/**
 * @brief Gets how many pending creation actions currently exist in the process.
 *
 * @param out_count Receives the action count.
 * @return `CNA_RESULT_SUCCESS` or `CNA_RESULT_INVALID_ARGUMENT` for a null output.
 *
 * This maps a CNA diagnostic accessor.
 */
CNA_C_API CNA_Result cna_network_session_get_active_action_count_ext(int32_t* out_count);

/**
 * @brief Disposes a session without releasing its handle.
 *
 * @param session Owned session handle.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread/native failure. Disposal is
 * idempotent.
 */
CNA_C_API CNA_Result cna_network_session_dispose(CNA_NetworkSessionHandle session);

/**
 * @brief Disposes and releases an owned session handle.
 *
 * @param session Owned session handle.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_STATE` while a borrowed gamer view is still
 * open, or a documented handle/thread failure.
 */
CNA_C_API CNA_Result cna_network_session_destroy(CNA_NetworkSessionHandle session);

/**
 * @brief Receives the completion of a session operation.
 *
 * The canonical delegate receives an operation object, which never crosses the ABI, so the C
 * callback receives only the caller's own context.
 *
 * @param context Caller-owned context supplied at the call site.
 */
typedef void (*CNA_NetworkSessionAsyncCallback)(void* context);

/**
 * @brief Creates an owned session through the canonical asynchronous pair.
 *
 * @param session_type One of the `CNA_NETWORK_SESSION_TYPE_*` identities.
 * @param max_local_gamers Largest number of local gamers, between one and four.
 * @param max_gamers Largest number of gamers; the canonical asynchronous path ignores it.
 * @param callback Optional completion callback invoked before this call returns.
 * @param context Caller-owned callback context, which may be null.
 * @param out_session Receives an owned session handle on success.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/state/thread/native failure.
 *
 * CNA completes the canonical `Begin`/`End` pair before `Begin` returns, so this is one
 * synchronous call that still invokes the completion callback. It is not identical to
 * `cna_network_session_create`: the canonical end step substitutes its own gamer limit rather than
 * forwarding @p max_gamers, and that behavior is preserved.
 */
CNA_C_API CNA_Result cna_network_session_create_async(
    CNA_NetworkSessionType session_type,
    int32_t max_local_gamers,
    int32_t max_gamers,
    CNA_NetworkSessionAsyncCallback callback,
    void* context,
    CNA_NetworkSessionHandle* out_session);

/**
 * @brief Creates an owned session with private slots and properties through the asynchronous pair.
 *
 * @param session_type One of the `CNA_NETWORK_SESSION_TYPE_*` identities.
 * @param max_local_gamers Largest number of local gamers, between one and four.
 * @param max_gamers Largest number of gamers; the canonical asynchronous path ignores it.
 * @param private_gamer_slots Number of reserved private slots.
 * @param session_properties Properties copied during creation, or `CNA_INVALID_HANDLE`.
 * @param callback Optional completion callback invoked before this call returns.
 * @param context Caller-owned callback context, which may be null.
 * @param out_session Receives an owned session handle on success.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/state/handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_network_session_create_with_properties_async(
    CNA_NetworkSessionType session_type,
    int32_t max_local_gamers,
    int32_t max_gamers,
    int32_t private_gamer_slots,
    CNA_NetworkSessionPropertiesHandle session_properties,
    CNA_NetworkSessionAsyncCallback callback,
    void* context,
    CNA_NetworkSessionHandle* out_session);

/**
 * @brief Creates an owned session from an explicit local-gamer list through the asynchronous pair.
 *
 * @param session_type One of the `CNA_NETWORK_SESSION_TYPE_*` identities.
 * @param local_gamers Caller-owned array of signed-in gamer handles, or null when @p count is zero.
 * @param count Number of handles beginning at @p local_gamers.
 * @param max_gamers Largest number of gamers; the canonical asynchronous path ignores it.
 * @param private_gamer_slots Number of reserved private slots.
 * @param session_properties Properties copied during creation, or `CNA_INVALID_HANDLE`.
 * @param callback Optional completion callback invoked before this call returns.
 * @param context Caller-owned callback context, which may be null.
 * @param out_session Receives an owned session handle on success.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/state/handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_network_session_create_with_local_gamers_async(
    CNA_NetworkSessionType session_type,
    const CNA_Handle* local_gamers,
    uint64_t count,
    int32_t max_gamers,
    int32_t private_gamer_slots,
    CNA_NetworkSessionPropertiesHandle session_properties,
    CNA_NetworkSessionAsyncCallback callback,
    void* context,
    CNA_NetworkSessionHandle* out_session);

/**
 * @brief Searches for joinable sessions.
 *
 * @param session_type One of the `CNA_NETWORK_SESSION_TYPE_*` identities.
 * @param max_local_gamers Largest number of local gamers, between one and four.
 * @param search_properties Properties copied during the search, or `CNA_INVALID_HANDLE`.
 * @param out_collection Receives an owned collection handle on success.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/state/handle/thread/native failure.
 *
 * Only a `SystemLink` search reaches real discovery; every other session type returns an empty
 * collection, matching the canonical implementation rather than pretending to search.
 */
CNA_C_API CNA_Result cna_network_session_find(
    CNA_NetworkSessionType session_type,
    int32_t max_local_gamers,
    CNA_NetworkSessionPropertiesHandle search_properties,
    CNA_AvailableNetworkSessionCollectionHandle* out_collection);

/**
 * @brief Searches for joinable sessions on behalf of an explicit local-gamer list.
 *
 * @param session_type One of the `CNA_NETWORK_SESSION_TYPE_*` identities.
 * @param local_gamers Caller-owned array of signed-in gamer handles, or null when @p count is zero.
 * @param count Number of handles beginning at @p local_gamers.
 * @param search_properties Properties copied during the search, or `CNA_INVALID_HANDLE`.
 * @param out_collection Receives an owned collection handle on success.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/state/handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_network_session_find_with_local_gamers(
    CNA_NetworkSessionType session_type,
    const CNA_Handle* local_gamers,
    uint64_t count,
    CNA_NetworkSessionPropertiesHandle search_properties,
    CNA_AvailableNetworkSessionCollectionHandle* out_collection);

/**
 * @brief Searches for joinable sessions through the canonical asynchronous pair.
 *
 * @param session_type One of the `CNA_NETWORK_SESSION_TYPE_*` identities.
 * @param max_local_gamers Largest number of local gamers, between one and four.
 * @param search_properties Properties copied during the search, or `CNA_INVALID_HANDLE`.
 * @param callback Optional completion callback invoked before this call returns.
 * @param context Caller-owned callback context, which may be null.
 * @param out_collection Receives an owned collection handle on success.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/state/handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_network_session_find_async(
    CNA_NetworkSessionType session_type,
    int32_t max_local_gamers,
    CNA_NetworkSessionPropertiesHandle search_properties,
    CNA_NetworkSessionAsyncCallback callback,
    void* context,
    CNA_AvailableNetworkSessionCollectionHandle* out_collection);

/**
 * @brief Searches on behalf of an explicit local-gamer list through the asynchronous pair.
 *
 * @param session_type One of the `CNA_NETWORK_SESSION_TYPE_*` identities.
 * @param local_gamers Caller-owned array of signed-in gamer handles, or null when @p count is zero.
 * @param count Number of handles beginning at @p local_gamers.
 * @param search_properties Properties copied during the search, or `CNA_INVALID_HANDLE`.
 * @param callback Optional completion callback invoked before this call returns.
 * @param context Caller-owned callback context, which may be null.
 * @param out_collection Receives an owned collection handle on success.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/state/handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_network_session_find_with_local_gamers_async(
    CNA_NetworkSessionType session_type,
    const CNA_Handle* local_gamers,
    uint64_t count,
    CNA_NetworkSessionPropertiesHandle search_properties,
    CNA_NetworkSessionAsyncCallback callback,
    void* context,
    CNA_AvailableNetworkSessionCollectionHandle* out_collection);

/**
 * @brief Joins a discovered session.
 *
 * @param available_session Owned discovered-session handle.
 * @param out_session Receives an owned session handle on success.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_STATE` when a session already exists, or a
 * documented argument/handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_network_session_join(
    CNA_AvailableNetworkSessionHandle available_session,
    CNA_NetworkSessionHandle* out_session);

/**
 * @brief Joins a discovered session through the canonical asynchronous pair.
 *
 * @param available_session Owned discovered-session handle.
 * @param callback Optional completion callback invoked before this call returns.
 * @param context Caller-owned callback context, which may be null.
 * @param out_session Receives an owned session handle on success.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_STATE` when a session already exists, or a
 * documented argument/handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_network_session_join_async(
    CNA_AvailableNetworkSessionHandle available_session,
    CNA_NetworkSessionAsyncCallback callback,
    void* context,
    CNA_NetworkSessionHandle* out_session);

/**
 * @brief Joins the session an accepted invite names.
 *
 * @param max_local_gamers Largest number of local gamers, between one and four.
 * @param out_session Receives an owned session handle on success.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_STATE` when a session already exists, or a
 * documented argument/thread/native failure.
 *
 * The canonical implementation builds a session from the accepted invite's own fixed values rather
 * than from any live invite state.
 */
CNA_C_API CNA_Result cna_network_session_join_invited(
    int32_t max_local_gamers,
    CNA_NetworkSessionHandle* out_session);

/**
 * @brief Joins the session an accepted invite names on behalf of an explicit local-gamer list.
 *
 * @param local_gamers Caller-owned array of signed-in gamer handles, or null when @p count is zero.
 * @param count Number of handles beginning at @p local_gamers.
 * @param out_session Receives an owned session handle on success.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_STATE` when a session already exists, or a
 * documented argument/handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_network_session_join_invited_with_local_gamers(
    const CNA_Handle* local_gamers,
    uint64_t count,
    CNA_NetworkSessionHandle* out_session);

/**
 * @brief Joins the invited session through the canonical asynchronous pair.
 *
 * @param max_local_gamers Largest number of local gamers, between one and four.
 * @param callback Optional completion callback invoked before this call returns.
 * @param context Caller-owned callback context, which may be null.
 * @param out_session Receives an owned session handle on success.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_STATE` when a session already exists, or a
 * documented argument/thread/native failure.
 */
CNA_C_API CNA_Result cna_network_session_join_invited_async(
    int32_t max_local_gamers,
    CNA_NetworkSessionAsyncCallback callback,
    void* context,
    CNA_NetworkSessionHandle* out_session);

/**
 * @brief Joins the invited session for an explicit local-gamer list through the asynchronous pair.
 *
 * @param local_gamers Caller-owned array of signed-in gamer handles, or null when @p count is zero.
 * @param count Number of handles beginning at @p local_gamers.
 * @param callback Optional completion callback invoked before this call returns.
 * @param context Caller-owned callback context, which may be null.
 * @param out_session Receives an owned session handle on success.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_STATE` when a session already exists, or a
 * documented argument/handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_network_session_join_invited_with_local_gamers_async(
    const CNA_Handle* local_gamers,
    uint64_t count,
    CNA_NetworkSessionAsyncCallback callback,
    void* context,
    CNA_NetworkSessionHandle* out_session);

/** @brief Owned handle for one session event subscription. */
typedef CNA_Handle CNA_NetworkSessionEventRegistrationHandle;

/**
 * @brief Receives a game-started event.
 *
 * @param session The session that raised the event.
 * @param info The event description, valid only for the duration of this call.
 * @param context Caller-owned context supplied at subscription.
 */
typedef void (*CNA_GameStartedCallback)(
    CNA_NetworkSessionHandle session,
    const CNA_GameStartedEventInfo* info,
    void* context);

/**
 * @brief Receives a game-ended event.
 *
 * @param session The session that raised the event.
 * @param info The event description, valid only for the duration of this call.
 * @param context Caller-owned context supplied at subscription.
 */
typedef void (*CNA_GameEndedCallback)(
    CNA_NetworkSessionHandle session,
    const CNA_GameEndedEventInfo* info,
    void* context);

/**
 * @brief Receives a gamer-joined event.
 *
 * @param session The session that raised the event.
 * @param info The event description; its gamer handle is valid only for the duration of this call.
 * @param context Caller-owned context supplied at subscription.
 */
typedef void (*CNA_GamerJoinedCallback)(
    CNA_NetworkSessionHandle session,
    const CNA_GamerJoinedEventInfo* info,
    void* context);

/**
 * @brief Receives a gamer-left event.
 *
 * @param session The session that raised the event.
 * @param info The event description; its gamer handle is valid only for the duration of this call.
 * @param context Caller-owned context supplied at subscription.
 */
typedef void (*CNA_GamerLeftCallback)(
    CNA_NetworkSessionHandle session,
    const CNA_GamerLeftEventInfo* info,
    void* context);

/**
 * @brief Receives a host-changed event.
 *
 * @param session The session that raised the event.
 * @param info The event description; its gamer handles are valid only for this call.
 * @param context Caller-owned context supplied at subscription.
 */
typedef void (*CNA_HostChangedCallback)(
    CNA_NetworkSessionHandle session,
    const CNA_HostChangedEventInfo* info,
    void* context);

/**
 * @brief Receives a session-ended event.
 *
 * @param session The session that raised the event.
 * @param info The event description, valid only for the duration of this call.
 * @param context Caller-owned context supplied at subscription.
 */
typedef void (*CNA_NetworkSessionEndedCallback)(
    CNA_NetworkSessionHandle session,
    const CNA_NetworkSessionEndedEventInfo* info,
    void* context);

/**
 * @brief Receives a leaderboard-write event.
 *
 * @param session The session that raised the event.
 * @param info The event description; its gamer handle is valid only for the duration of this call.
 * @param context Caller-owned context supplied at subscription.
 */
typedef void (*CNA_WriteLeaderboardsCallback)(
    CNA_NetworkSessionHandle session,
    const CNA_WriteLeaderboardsEventInfo* info,
    void* context);

/**
 * @brief Receives an accepted-invite event.
 *
 * @param info The event description; its gamer handle is valid only for the duration of this call.
 * @param context Caller-owned context supplied at subscription.
 */
typedef void (*CNA_InviteAcceptedCallback)(
    const CNA_InviteAcceptedEventInfo* info,
    void* context);

/**
 * @brief Subscribes to a session's game-started event.
 *
 * @param session Owned session handle.
 * @param callback Non-null callback invoked synchronously when the event is raised.
 * @param context Caller-owned callback context, which may be null.
 * @param out_registration Receives an owned registration handle on success.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_network_session_subscribe_game_started(
    CNA_NetworkSessionHandle session,
    CNA_GameStartedCallback callback,
    void* context,
    CNA_NetworkSessionEventRegistrationHandle* out_registration);

/**
 * @brief Subscribes to a session's game-ended event.
 *
 * @param session Owned session handle.
 * @param callback Non-null callback invoked synchronously when the event is raised.
 * @param context Caller-owned callback context, which may be null.
 * @param out_registration Receives an owned registration handle on success.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_network_session_subscribe_game_ended(
    CNA_NetworkSessionHandle session,
    CNA_GameEndedCallback callback,
    void* context,
    CNA_NetworkSessionEventRegistrationHandle* out_registration);

/**
 * @brief Subscribes to a session's gamer-joined event.
 *
 * @param session Owned session handle.
 * @param callback Non-null callback invoked synchronously when the event is raised.
 * @param context Caller-owned callback context, which may be null.
 * @param out_registration Receives an owned registration handle on success.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 *
 * The canonical event replays itself for every gamer already in the session the moment a handler
 * subscribes, so the callback fires before this call returns when the session is not empty.
 */
CNA_C_API CNA_Result cna_network_session_subscribe_gamer_joined(
    CNA_NetworkSessionHandle session,
    CNA_GamerJoinedCallback callback,
    void* context,
    CNA_NetworkSessionEventRegistrationHandle* out_registration);

/**
 * @brief Subscribes to a session's gamer-left event.
 *
 * @param session Owned session handle.
 * @param callback Non-null callback invoked synchronously when the event is raised.
 * @param context Caller-owned callback context, which may be null.
 * @param out_registration Receives an owned registration handle on success.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_network_session_subscribe_gamer_left(
    CNA_NetworkSessionHandle session,
    CNA_GamerLeftCallback callback,
    void* context,
    CNA_NetworkSessionEventRegistrationHandle* out_registration);

/**
 * @brief Subscribes to a session's host-changed event.
 *
 * @param session Owned session handle.
 * @param callback Non-null callback invoked synchronously when the event is raised.
 * @param context Caller-owned callback context, which may be null.
 * @param out_registration Receives an owned registration handle on success.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_network_session_subscribe_host_changed(
    CNA_NetworkSessionHandle session,
    CNA_HostChangedCallback callback,
    void* context,
    CNA_NetworkSessionEventRegistrationHandle* out_registration);

/**
 * @brief Subscribes to a session's session-ended event.
 *
 * @param session Owned session handle.
 * @param callback Non-null callback invoked synchronously when the event is raised.
 * @param context Caller-owned callback context, which may be null.
 * @param out_registration Receives an owned registration handle on success.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_network_session_subscribe_session_ended(
    CNA_NetworkSessionHandle session,
    CNA_NetworkSessionEndedCallback callback,
    void* context,
    CNA_NetworkSessionEventRegistrationHandle* out_registration);

/**
 * @brief Subscribes to a session's arbitrated-leaderboard write event.
 *
 * @param session Owned session handle.
 * @param callback Non-null callback invoked synchronously when the event is raised.
 * @param context Caller-owned callback context, which may be null.
 * @param out_registration Receives an owned registration handle on success.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_network_session_subscribe_write_arbitrated_leaderboard(
    CNA_NetworkSessionHandle session,
    CNA_WriteLeaderboardsCallback callback,
    void* context,
    CNA_NetworkSessionEventRegistrationHandle* out_registration);

/**
 * @brief Subscribes to a session's unarbitrated-leaderboard write event.
 *
 * @param session Owned session handle.
 * @param callback Non-null callback invoked synchronously when the event is raised.
 * @param context Caller-owned callback context, which may be null.
 * @param out_registration Receives an owned registration handle on success.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_network_session_subscribe_write_unarbitrated_leaderboard(
    CNA_NetworkSessionHandle session,
    CNA_WriteLeaderboardsCallback callback,
    void* context,
    CNA_NetworkSessionEventRegistrationHandle* out_registration);

/**
 * @brief Subscribes to a session's true-skill write event.
 *
 * @param session Owned session handle.
 * @param callback Non-null callback invoked synchronously when the event is raised.
 * @param context Caller-owned callback context, which may be null.
 * @param out_registration Receives an owned registration handle on success.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_network_session_subscribe_write_true_skill(
    CNA_NetworkSessionHandle session,
    CNA_WriteLeaderboardsCallback callback,
    void* context,
    CNA_NetworkSessionEventRegistrationHandle* out_registration);

/**
 * @brief Subscribes to the process-wide accepted-invite event.
 *
 * @param callback Non-null callback invoked synchronously when the event is raised.
 * @param context Caller-owned callback context, which may be null.
 * @param out_registration Receives an owned registration handle on success.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/thread/native failure.
 *
 * The canonical event is a static member, so the subscription belongs to the process rather than
 * to any one session handle.
 */
CNA_C_API CNA_Result cna_network_session_subscribe_invite_accepted(
    CNA_InviteAcceptedCallback callback,
    void* context,
    CNA_NetworkSessionEventRegistrationHandle* out_registration);

/**
 * @brief Unsubscribes and releases a session event registration.
 *
 * @param registration Owned registration handle.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread failure. A second release returns
 * `CNA_RESULT_INVALID_HANDLE`.
 *
 * Releasing a registration whose session is already gone is a no-op rather than a failure.
 */
CNA_C_API CNA_Result cna_network_session_unsubscribe(
    CNA_NetworkSessionEventRegistrationHandle registration);

#ifdef __cplusplus
}
#endif

#endif
