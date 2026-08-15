// SPDX-License-Identifier: MS-PL

#ifndef CNA_C_GAMER_SERVICES_H
#define CNA_C_GAMER_SERVICES_H

#include "CNA/C/input.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Owned handle for a signed-in gamer. */
typedef CNA_Handle CNA_SignedInGamerHandle;

/**
 * @brief Creates an owned signed-in gamer.
 *
 * @param gamertag UTF-8 gamertag copied during this call.
 * @param is_signed_in_to_live `CNA_TRUE` when the gamer is signed in to the online service.
 * @param is_guest `CNA_TRUE` when the gamer is a guest.
 * @param player_index One of the `CNA_PLAYER_INDEX_*` identities.
 * @param out_gamer Receives an owned signed-in gamer handle on success.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_ENCODING` for invalid UTF-8, or a documented
 * argument/thread/native failure.
 *
 * CNAEXT: the canonical factory exists so a platform layer can publish a signed-in gamer. This is
 * the minimum gamer-services surface a network session needs, because the canonical session
 * constructor requires at least one signed-in gamer; the rest of the gamer-services API is a later
 * coverage task.
 */
CNA_C_API CNA_Result cna_signed_in_gamer_create_ext(
    CNA_StringView gamertag,
    CNA_Bool is_signed_in_to_live,
    CNA_Bool is_guest,
    CNA_PlayerIndex player_index,
    CNA_SignedInGamerHandle* out_gamer);

/**
 * @brief Gets the UTF-8 byte count of a signed-in gamer's gamertag.
 *
 * @param gamer Owned signed-in gamer handle.
 * @param out_bytes Receives the byte count without a terminator.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_signed_in_gamer_get_gamertag_size(
    CNA_SignedInGamerHandle gamer,
    uint64_t* out_bytes);

/**
 * @brief Copies a signed-in gamer's gamertag without a terminator.
 *
 * @param gamer Owned signed-in gamer handle.
 * @param destination Caller-owned destination, or null only when @p capacity is zero.
 * @param capacity Destination capacity in bytes.
 * @param out_bytes Receives the required byte count without a terminator.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL`, or a documented
 * argument/handle/thread failure. No partial value is written.
 */
CNA_C_API CNA_Result cna_signed_in_gamer_copy_gamertag(
    CNA_SignedInGamerHandle gamer,
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

/**
 * @brief Releases an owned signed-in gamer handle.
 *
 * @param gamer Owned signed-in gamer handle.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_STATE` while the process-wide signed-in
 * collection still references it, or a documented handle/thread failure.
 */
CNA_C_API CNA_Result cna_signed_in_gamer_destroy(CNA_SignedInGamerHandle gamer);

/**
 * @brief Replaces the process-wide collection of signed-in gamers.
 *
 * @param gamers Caller-owned array of signed-in gamer handles, or null when @p count is zero.
 * @param count Number of handles beginning at @p gamers.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 *
 * CNAEXT: the canonical collection holds non-owning pointers, so the C layer retains each handle's
 * resource for as long as the collection references it and releases the previous retention here.
 */
CNA_C_API CNA_Result cna_gamer_set_signed_in_gamers_ext(
    const CNA_SignedInGamerHandle* gamers,
    uint64_t count);

/**
 * @brief Gets how many gamers are in the process-wide signed-in collection.
 *
 * @param out_count Receives the gamer count.
 * @return `CNA_RESULT_SUCCESS` or `CNA_RESULT_INVALID_ARGUMENT` for a null output.
 *
 * Element access is through the handles the caller created and published; a complete collection
 * view arrives with the gamer-services coverage task.
 */
CNA_C_API CNA_Result cna_gamer_get_signed_in_gamer_count(int32_t* out_count);

#ifdef __cplusplus
}
#endif

#endif
