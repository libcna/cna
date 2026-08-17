// SPDX-License-Identifier: MS-PL

#ifndef CNA_C_NET_GAMERS_H
#define CNA_C_NET_GAMERS_H

#include "CNA/C/net.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Owned handle for a gamer participating in a network session. */
typedef CNA_Handle CNA_NetworkGamerHandle;

/** @brief Owned handle for a machine hosting one or more network gamers. */
typedef CNA_Handle CNA_NetworkMachineHandle;

/**
 * @brief Creates an owned network gamer.
 *
 * @param session Owning session handle, or `CNA_INVALID_HANDLE` for a gamer with no session.
 * @param gamertag UTF-8 gamertag copied during this call; an empty view selects the canonical
 * default.
 * @param out_gamer Receives an owned gamer handle on success.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_ENCODING` for invalid UTF-8, or a documented
 * argument/handle/thread/native failure.
 *
 * Session handles arrive with the session slice; until then only `CNA_INVALID_HANDLE` is accepted
 * and any other value is refused as an invalid handle.
 */
CNA_C_API CNA_Result cna_network_gamer_create(
    CNA_Handle session,
    CNA_StringView gamertag,
    CNA_NetworkGamerHandle* out_gamer);

/**
 * @brief Gets whether a gamer has left the session.
 *
 * @param gamer Owned gamer handle.
 * @param out_value Receives `CNA_TRUE` when the gamer has left.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_network_gamer_get_has_left_session(
    CNA_NetworkGamerHandle gamer,
    CNA_Bool* out_value);

/**
 * @brief Marks whether a gamer has left the session.
 *
 * @param gamer Owned gamer handle.
 * @param value The new value.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread failure.
 *
 * This maps a CNA extension: the canonical setter is private and never called, so the canonical
 * property is permanently false without it.
 */
CNA_C_API CNA_Result cna_network_gamer_set_has_left_session_ext(
    CNA_NetworkGamerHandle gamer,
    CNA_Bool value);

/**
 * @brief Gets whether a gamer has a voice device available.
 *
 * @param gamer Owned gamer handle.
 * @param out_value Receives `CNA_TRUE` when voice is available.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_network_gamer_get_has_voice(
    CNA_NetworkGamerHandle gamer,
    CNA_Bool* out_value);

/**
 * @brief Gets a gamer's session-local identifier.
 *
 * @param gamer Owned gamer handle.
 * @param out_value Receives the identifier.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_network_gamer_get_id(
    CNA_NetworkGamerHandle gamer,
    uint8_t* out_value);

/**
 * @brief Sets a gamer's session-local identifier.
 *
 * @param gamer Owned gamer handle.
 * @param value The new identifier.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread failure.
 *
 * This maps a CNA extension: the canonical getter is hardcoded to zero without it.
 */
CNA_C_API CNA_Result cna_network_gamer_set_id_ext(
    CNA_NetworkGamerHandle gamer,
    uint8_t value);

/**
 * @brief Gets whether a gamer is a guest.
 *
 * @param gamer Owned gamer handle.
 * @param out_value Receives `CNA_TRUE` for a guest.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_network_gamer_get_is_guest(
    CNA_NetworkGamerHandle gamer,
    CNA_Bool* out_value);

/**
 * @brief Gets whether a gamer is the session host.
 *
 * @param gamer Owned gamer handle.
 * @param out_value Receives `CNA_TRUE` for the host.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_network_gamer_get_is_host(
    CNA_NetworkGamerHandle gamer,
    CNA_Bool* out_value);

/**
 * @brief Sets whether a gamer is the session host.
 *
 * @param gamer Owned gamer handle.
 * @param value The new host state.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread failure.
 *
 * This maps a CNA extension: the canonical getter is hardcoded to true without it.
 */
CNA_C_API CNA_Result cna_network_gamer_set_is_host_ext(
    CNA_NetworkGamerHandle gamer,
    CNA_Bool value);

/**
 * @brief Gets whether a gamer is a local gamer.
 *
 * @param gamer Owned gamer handle.
 * @param out_value Receives `CNA_TRUE` for a local gamer.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_network_gamer_get_is_local(
    CNA_NetworkGamerHandle gamer,
    CNA_Bool* out_value);

/**
 * @brief Gets whether a gamer is muted by the local user.
 *
 * @param gamer Owned gamer handle.
 * @param out_value Receives `CNA_TRUE` when muted.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_network_gamer_get_is_muted_by_local_user(
    CNA_NetworkGamerHandle gamer,
    CNA_Bool* out_value);

/**
 * @brief Gets whether a gamer occupies a private slot.
 *
 * @param gamer Owned gamer handle.
 * @param out_value Receives `CNA_TRUE` for a private slot.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_network_gamer_get_is_private_slot(
    CNA_NetworkGamerHandle gamer,
    CNA_Bool* out_value);

/**
 * @brief Gets whether a gamer is ready.
 *
 * @param gamer Owned gamer handle.
 * @param out_value Receives `CNA_TRUE` when ready.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_network_gamer_get_is_ready(
    CNA_NetworkGamerHandle gamer,
    CNA_Bool* out_value);

/**
 * @brief Sets whether a gamer is ready.
 *
 * @param gamer Owned gamer handle.
 * @param value The new ready state.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread failure.
 */
CNA_C_API CNA_Result cna_network_gamer_set_is_ready(
    CNA_NetworkGamerHandle gamer,
    CNA_Bool value);

/**
 * @brief Gets whether a gamer is currently talking.
 *
 * @param gamer Owned gamer handle.
 * @param out_value Receives `CNA_TRUE` when talking.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_network_gamer_get_is_talking(
    CNA_NetworkGamerHandle gamer,
    CNA_Bool* out_value);

/**
 * @brief Copies the machine hosting a gamer into a new owned handle.
 *
 * @param gamer Owned gamer handle.
 * @param out_machine Receives an owned machine handle on success.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 *
 * The canonical setter takes its machine by value, so the gamer holds its own copy. The C route
 * hands back an independent copy of that state for the same reason; the canonical machine exposes
 * no mutator, so a copy is observationally identical to the reference the canonical getter returns.
 */
CNA_C_API CNA_Result cna_network_gamer_copy_machine(
    CNA_NetworkGamerHandle gamer,
    CNA_NetworkMachineHandle* out_machine);

/**
 * @brief Sets the machine hosting a gamer.
 *
 * @param gamer Owned gamer handle.
 * @param machine Owned machine handle whose state is copied during this call.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_network_gamer_set_machine(
    CNA_NetworkGamerHandle gamer,
    CNA_NetworkMachineHandle machine);

/**
 * @brief Gets the measured round-trip time to a gamer.
 *
 * @param gamer Owned gamer handle.
 * @param out_ticks Receives the round-trip time in 100-nanosecond ticks.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_network_gamer_get_roundtrip_ticks(
    CNA_NetworkGamerHandle gamer,
    int64_t* out_ticks);

/**
 * @brief Sets the measured round-trip time to a gamer.
 *
 * @param gamer Owned gamer handle.
 * @param ticks The measured round-trip time in 100-nanosecond ticks.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread failure.
 *
 * This maps a CNA extension used to publish a transport-measured round-trip time; the canonical
 * property has no setter.
 */
CNA_C_API CNA_Result cna_network_gamer_set_roundtrip_ticks_ext(
    CNA_NetworkGamerHandle gamer,
    int64_t ticks);

/**
 * @brief Gets the session a gamer belongs to.
 *
 * @param gamer Owned gamer handle.
 * @param out_session Receives the session handle supplied at creation, or `CNA_INVALID_HANDLE`.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_network_gamer_get_session(
    CNA_NetworkGamerHandle gamer,
    CNA_Handle* out_session);

/**
 * @brief Releases an owned gamer handle.
 *
 * @param gamer Owned gamer handle.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread failure.
 */
CNA_C_API CNA_Result cna_network_gamer_destroy(CNA_NetworkGamerHandle gamer);

/**
 * @brief Creates an owned network machine.
 *
 * @param out_machine Receives an owned machine handle on success.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/thread/native failure.
 */
CNA_C_API CNA_Result cna_network_machine_create(CNA_NetworkMachineHandle* out_machine);

/**
 * @brief Gets the number of gamers associated with a machine.
 *
 * @param machine Owned machine handle.
 * @param out_count Receives the gamer count.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_network_machine_get_gamer_count(
    CNA_NetworkMachineHandle machine,
    int32_t* out_count);

/**
 * @brief Gets one gamer associated with a machine.
 *
 * @param machine Owned machine handle.
 * @param index Zero-based gamer index.
 * @param out_gamer Receives a borrowed-view gamer handle on success.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for an out-of-range index, or a
 * documented handle/thread failure.
 *
 * The returned handle observes a gamer the machine's collection owns, so it must be released
 * before the machine. Only a session populates this collection, so a machine created through
 * `cna_network_machine_create` reports zero gamers.
 */
CNA_C_API CNA_Result cna_network_machine_get_gamer(
    CNA_NetworkMachineHandle machine,
    int32_t index,
    CNA_NetworkGamerHandle* out_gamer);

/**
 * @brief Removes a machine's gamers from the network session.
 *
 * @param machine Owned machine handle.
 * @return `CNA_RESULT_NOT_SUPPORTED`, or a documented handle/thread failure.
 *
 * The canonical operation is a declared placeholder that always throws, and the C route reports
 * that faithfully rather than pretending it succeeded.
 */
CNA_C_API CNA_Result cna_network_machine_remove_from_session(CNA_NetworkMachineHandle machine);

/**
 * @brief Releases an owned machine handle.
 *
 * @param machine Owned machine handle.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_STATE` while a borrowed gamer view is still
 * open, or a documented handle/thread failure.
 */
CNA_C_API CNA_Result cna_network_machine_destroy(CNA_NetworkMachineHandle machine);

/**
 * @brief Describes a game-ended event.
 */
typedef struct CNA_GameEndedEventInfo {
    /** @brief Size of this caller-provided structure in bytes. */
    uint32_t struct_size;

    /** @brief Version of this caller-provided structure. */
    uint32_t struct_version;
} CNA_GameEndedEventInfo;

/**
 * @brief Describes a game-started event.
 */
typedef struct CNA_GameStartedEventInfo {
    /** @brief Size of this caller-provided structure in bytes. */
    uint32_t struct_size;

    /** @brief Version of this caller-provided structure. */
    uint32_t struct_version;
} CNA_GameStartedEventInfo;

/**
 * @brief Describes a gamer-joined event.
 */
typedef struct CNA_GamerJoinedEventInfo {
    /** @brief Size of this caller-provided structure in bytes. */
    uint32_t struct_size;

    /** @brief Version of this caller-provided structure. */
    uint32_t struct_version;

    /** @brief The gamer that joined, or `CNA_INVALID_HANDLE` when none was reported. */
    CNA_NetworkGamerHandle gamer;
} CNA_GamerJoinedEventInfo;

/**
 * @brief Describes a gamer-left event.
 */
typedef struct CNA_GamerLeftEventInfo {
    /** @brief Size of this caller-provided structure in bytes. */
    uint32_t struct_size;

    /** @brief Version of this caller-provided structure. */
    uint32_t struct_version;

    /** @brief The gamer that left, or `CNA_INVALID_HANDLE` when none was reported. */
    CNA_NetworkGamerHandle gamer;
} CNA_GamerLeftEventInfo;

/**
 * @brief Describes a host-changed event.
 */
typedef struct CNA_HostChangedEventInfo {
    /** @brief Size of this caller-provided structure in bytes. */
    uint32_t struct_size;

    /** @brief Version of this caller-provided structure. */
    uint32_t struct_version;

    /** @brief The previous host, or `CNA_INVALID_HANDLE` when none was reported. */
    CNA_NetworkGamerHandle old_host;

    /** @brief The new host, or `CNA_INVALID_HANDLE` when none was reported. */
    CNA_NetworkGamerHandle new_host;
} CNA_HostChangedEventInfo;

/**
 * @brief Describes a session-ended event.
 */
typedef struct CNA_NetworkSessionEndedEventInfo {
    /** @brief Size of this caller-provided structure in bytes. */
    uint32_t struct_size;

    /** @brief Version of this caller-provided structure. */
    uint32_t struct_version;

    /** @brief One of the `CNA_NETWORK_SESSION_END_REASON_*` identities. */
    CNA_NetworkSessionEndReason end_reason;

    /** @brief Reserved bytes; always zero. */
    uint8_t reserved[4];
} CNA_NetworkSessionEndedEventInfo;

/**
 * @brief Describes a leaderboard-write event.
 */
typedef struct CNA_WriteLeaderboardsEventInfo {
    /** @brief Size of this caller-provided structure in bytes. */
    uint32_t struct_size;

    /** @brief Version of this caller-provided structure. */
    uint32_t struct_version;

    /** @brief The gamer whose leaderboards are written, or `CNA_INVALID_HANDLE`. */
    CNA_NetworkGamerHandle gamer;

    /** @brief `CNA_TRUE` when the gamer is leaving the session. */
    CNA_Bool is_leaving;

    /** @brief Reserved bytes; always zero. */
    uint8_t reserved[7];
} CNA_WriteLeaderboardsEventInfo;

/**
 * @brief Initializes a game-ended event description.
 *
 * @param out_info Caller-provided versioned structure to initialize.
 * @return `CNA_RESULT_SUCCESS` or `CNA_RESULT_INVALID_ARGUMENT` for an invalid structure.
 */
CNA_C_API CNA_Result cna_game_ended_event_info_init(CNA_GameEndedEventInfo* out_info);

/**
 * @brief Initializes a game-started event description.
 *
 * @param out_info Caller-provided versioned structure to initialize.
 * @return `CNA_RESULT_SUCCESS` or `CNA_RESULT_INVALID_ARGUMENT` for an invalid structure.
 */
CNA_C_API CNA_Result cna_game_started_event_info_init(CNA_GameStartedEventInfo* out_info);

/**
 * @brief Initializes a gamer-joined event description.
 *
 * @param gamer The gamer that joined, or `CNA_INVALID_HANDLE`.
 * @param out_info Caller-provided versioned structure to initialize.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_HANDLE` for a handle that is not a live gamer,
 * or `CNA_RESULT_INVALID_ARGUMENT` for an invalid structure.
 */
CNA_C_API CNA_Result cna_gamer_joined_event_info_init(
    CNA_NetworkGamerHandle gamer,
    CNA_GamerJoinedEventInfo* out_info);

/**
 * @brief Initializes a gamer-left event description.
 *
 * @param gamer The gamer that left, or `CNA_INVALID_HANDLE`.
 * @param out_info Caller-provided versioned structure to initialize.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_HANDLE` for a handle that is not a live gamer,
 * or `CNA_RESULT_INVALID_ARGUMENT` for an invalid structure.
 */
CNA_C_API CNA_Result cna_gamer_left_event_info_init(
    CNA_NetworkGamerHandle gamer,
    CNA_GamerLeftEventInfo* out_info);

/**
 * @brief Initializes a host-changed event description.
 *
 * @param old_host The previous host, or `CNA_INVALID_HANDLE`.
 * @param new_host The new host, or `CNA_INVALID_HANDLE`.
 * @param out_info Caller-provided versioned structure to initialize.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_HANDLE` for a handle that is not a live gamer,
 * or `CNA_RESULT_INVALID_ARGUMENT` for an invalid structure.
 */
CNA_C_API CNA_Result cna_host_changed_event_info_init(
    CNA_NetworkGamerHandle old_host,
    CNA_NetworkGamerHandle new_host,
    CNA_HostChangedEventInfo* out_info);

/**
 * @brief Initializes a session-ended event description.
 *
 * @param end_reason One of the `CNA_NETWORK_SESSION_END_REASON_*` identities.
 * @param out_info Caller-provided versioned structure to initialize.
 * @return `CNA_RESULT_SUCCESS` or `CNA_RESULT_INVALID_ARGUMENT` for an unknown reason or an
 * invalid structure.
 */
CNA_C_API CNA_Result cna_network_session_ended_event_info_init(
    CNA_NetworkSessionEndReason end_reason,
    CNA_NetworkSessionEndedEventInfo* out_info);

/**
 * @brief Initializes a leaderboard-write event description.
 *
 * @param gamer The gamer whose leaderboards are written, or `CNA_INVALID_HANDLE`.
 * @param is_leaving `CNA_TRUE` when the gamer is leaving the session.
 * @param out_info Caller-provided versioned structure to initialize.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_HANDLE` for a handle that is not a live gamer,
 * or `CNA_RESULT_INVALID_ARGUMENT` for an invalid structure.
 */
CNA_C_API CNA_Result cna_write_leaderboards_event_info_init(
    CNA_NetworkGamerHandle gamer,
    CNA_Bool is_leaving,
    CNA_WriteLeaderboardsEventInfo* out_info);

#ifdef __cplusplus
}
#endif

#endif
