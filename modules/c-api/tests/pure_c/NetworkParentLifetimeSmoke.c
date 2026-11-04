// SPDX-License-Identifier: MS-PL

#include <CNA/C/cna.h>

#include <stdio.h>
#include <string.h>

#define CHECK(expression) do { if (!(expression)) { \
    fprintf(stderr, "line %d: %s\n", __LINE__, #expression); return 0; \
} } while (0)

static CNA_StringView view(const char* text)
{
    CNA_StringView result = {text, (uint64_t)strlen(text)};
    return result;
}

static int signed_in_parent_survives_its_input_handle(void)
{
    CNA_SignedInGamerHandle input = CNA_INVALID_HANDLE;
    CNA_SignedInGamerHandle replacement = CNA_INVALID_HANDLE;
    CNA_SignedInGamerHandle alias = CNA_INVALID_HANDLE;
    CNA_NetworkGamerHandle local = CNA_INVALID_HANDLE;
    CNA_NetworkGamerHandle second = CNA_INVALID_HANDLE;
    CNA_SignedInGamerHandle second_alias = CNA_INVALID_HANDLE;
    char tag[128];
    uint64_t bytes = 0;
    int32_t party_size = 0;
    const char* original = "Original standalone local gamer parent";

    CHECK(cna_signed_in_gamer_create_ext(view(original), CNA_FALSE, CNA_FALSE,
        CNA_PLAYER_INDEX_ONE, &input) == CNA_RESULT_SUCCESS);
    CHECK(cna_local_network_gamer_create_ext(input, CNA_INVALID_HANDLE, &local) == CNA_RESULT_SUCCESS);
    /* Releasing an unpublished input handle is legal. Its dependent must retain the object. */
    CHECK(cna_signed_in_gamer_destroy(input) == CNA_RESULT_SUCCESS);
    CHECK(cna_signed_in_gamer_create_ext(view("Replacement standalone local gamer parent"),
        CNA_FALSE, CNA_FALSE, CNA_PLAYER_INDEX_TWO, &replacement) == CNA_RESULT_SUCCESS);
    CHECK(cna_local_network_gamer_get_signed_in_gamer(local, &alias) == CNA_RESULT_SUCCESS);
    CHECK(cna_local_network_gamer_get_signed_in_gamer(local, &second_alias) == CNA_RESULT_SUCCESS);
    CHECK(cna_signed_in_gamer_set_party_size(alias, 3) == CNA_RESULT_SUCCESS);
    CHECK(cna_signed_in_gamer_get_party_size(second_alias, &party_size) == CNA_RESULT_SUCCESS);
    CHECK(party_size == 3);
    CHECK(cna_signed_in_gamer_destroy(second_alias) == CNA_RESULT_SUCCESS);
    CHECK(cna_signed_in_gamer_copy_gamertag(alias, tag, sizeof(tag), &bytes) == CNA_RESULT_SUCCESS);
    CHECK(bytes == (uint64_t)strlen(original) && memcmp(tag, original, (size_t)bytes) == 0);
    CHECK(cna_network_gamer_destroy(local) == CNA_RESULT_SUCCESS);
    /* The returned signed-in view also retains its native backing after the local handle ends. */
    CHECK(cna_signed_in_gamer_copy_gamertag(alias, tag, sizeof(tag), &bytes) == CNA_RESULT_SUCCESS);
    CHECK(bytes == (uint64_t)strlen(original) && memcmp(tag, original, (size_t)bytes) == 0);
    CHECK(cna_local_network_gamer_create_ext(alias, CNA_INVALID_HANDLE, &second) == CNA_RESULT_SUCCESS);
    CHECK(cna_signed_in_gamer_destroy(alias) == CNA_RESULT_SUCCESS);
    CHECK(cna_local_network_gamer_get_signed_in_gamer(second, &second_alias) == CNA_RESULT_SUCCESS);
    CHECK(cna_network_gamer_destroy(second) == CNA_RESULT_SUCCESS);
    CHECK(cna_signed_in_gamer_copy_gamertag(second_alias, tag, sizeof(tag), &bytes) == CNA_RESULT_SUCCESS);
    CHECK(bytes == (uint64_t)strlen(original) && memcmp(tag, original, (size_t)bytes) == 0);
    CHECK(cna_signed_in_gamer_destroy(second_alias) == CNA_RESULT_SUCCESS);
    CHECK(cna_signed_in_gamer_destroy(replacement) == CNA_RESULT_SUCCESS);
    return 1;
}

static int missing_signed_in_parent_is_not_an_owned_object(void)
{
    CNA_NetworkGamerHandle local = CNA_INVALID_HANDLE;
    CNA_NetworkGamerHandle plain = CNA_INVALID_HANDLE;
    CNA_SignedInGamerHandle alias = UINT64_C(0xdead);
    CHECK(cna_local_network_gamer_create_ext(CNA_INVALID_HANDLE, CNA_INVALID_HANDLE, &local) == CNA_RESULT_SUCCESS);
    CHECK(cna_local_network_gamer_get_signed_in_gamer(local, &alias) == CNA_RESULT_SUCCESS);
    CHECK(alias == CNA_INVALID_HANDLE);
    CHECK(cna_local_network_gamer_get_signed_in_gamer(local, NULL) == CNA_RESULT_INVALID_ARGUMENT);
    CHECK(cna_network_gamer_create(CNA_INVALID_HANDLE, view("plain"), &plain) == CNA_RESULT_SUCCESS);
    alias = UINT64_C(0xdead);
    CHECK(cna_local_network_gamer_get_signed_in_gamer(plain, &alias) == CNA_RESULT_INVALID_HANDLE);
    CHECK(alias == CNA_INVALID_HANDLE);
    CHECK(cna_network_gamer_destroy(plain) == CNA_RESULT_SUCCESS);
    CHECK(cna_network_gamer_destroy(local) == CNA_RESULT_SUCCESS);
    return 1;
}

static int session_outlives_standalone_gamer_handles(void)
{
    CNA_SignedInGamerHandle signed_in = CNA_INVALID_HANDLE;
    CNA_NetworkSessionHandle session = CNA_INVALID_HANDLE;
    CNA_NetworkGamerHandle remote = CNA_INVALID_HANDLE;
    CNA_NetworkGamerHandle local = CNA_INVALID_HANDLE;
    CNA_NetworkMachineHandle empty_machine = CNA_INVALID_HANDLE;
    CNA_Handle owner = CNA_INVALID_HANDLE;
    int32_t instances = -1;
    CNA_Bool disposed = CNA_TRUE;
    CHECK(cna_signed_in_gamer_create_ext(view("Session parent"), CNA_FALSE, CNA_FALSE,
        CNA_PLAYER_INDEX_ONE, &signed_in) == CNA_RESULT_SUCCESS);
    CHECK(cna_gamer_set_signed_in_gamers_ext(&signed_in, 1) == CNA_RESULT_SUCCESS);
    CHECK(cna_network_session_create(CNA_NETWORK_SESSION_TYPE_LOCAL, 1, 4, &session) == CNA_RESULT_SUCCESS);
    CHECK(cna_network_gamer_create(session, view("Standalone remote"), &remote) == CNA_RESULT_SUCCESS);
    CHECK(cna_local_network_gamer_create_ext(signed_in, session, &local) == CNA_RESULT_SUCCESS);
    CHECK(cna_network_session_destroy(session) == CNA_RESULT_INVALID_STATE);
    CHECK(cna_network_session_dispose(session) == CNA_RESULT_INVALID_STATE);
    CHECK(cna_network_session_get_is_disposed(session, &disposed) == CNA_RESULT_SUCCESS && disposed == CNA_FALSE);
    CHECK(cna_network_gamer_get_session(remote, &owner) == CNA_RESULT_SUCCESS && owner == session);
    CHECK(cna_network_gamer_get_session(local, &owner) == CNA_RESULT_SUCCESS && owner == session);
    CHECK(cna_network_gamer_copy_machine(remote, &empty_machine) == CNA_RESULT_SUCCESS);
    CHECK(cna_network_gamer_destroy(remote) == CNA_RESULT_SUCCESS);
    CHECK(cna_network_session_destroy(session) == CNA_RESULT_INVALID_STATE);
    CHECK(cna_network_gamer_destroy(local) == CNA_RESULT_SUCCESS);
    CHECK(cna_network_session_dispose(session) == CNA_RESULT_SUCCESS);
    CHECK(cna_network_session_dispose(session) == CNA_RESULT_SUCCESS);
    CHECK(cna_network_session_destroy(session) == CNA_RESULT_SUCCESS);
    CHECK(cna_network_machine_destroy(empty_machine) == CNA_RESULT_SUCCESS);
    CHECK(cna_network_session_get_instance_count_ext(&instances) == CNA_RESULT_SUCCESS && instances == 0);
    CHECK(cna_gamer_set_signed_in_gamers_ext(NULL, 0) == CNA_RESULT_SUCCESS);
    CHECK(cna_signed_in_gamer_destroy(signed_in) == CNA_RESULT_SUCCESS);
    return 1;
}

static int copied_machine_retains_its_roster_parent(void)
{
    CNA_SignedInGamerHandle signed_in = CNA_INVALID_HANDLE;
    CNA_NetworkSessionHandle session = CNA_INVALID_HANDLE;
    CNA_NetworkGamerHandle local = CNA_INVALID_HANDLE;
    CNA_NetworkGamerHandle machine_gamer = CNA_INVALID_HANDLE;
    CNA_NetworkMachineHandle machine = CNA_INVALID_HANDLE;
    CNA_NetworkMachineHandle second = CNA_INVALID_HANDLE;
    CNA_Handle owner = CNA_INVALID_HANDLE;
    int32_t count = 0;
    CNA_Bool is_local = CNA_FALSE;
    CHECK(cna_signed_in_gamer_create_ext(view("Machine parent"), CNA_FALSE, CNA_FALSE,
        CNA_PLAYER_INDEX_ONE, &signed_in) == CNA_RESULT_SUCCESS);
    CHECK(cna_gamer_set_signed_in_gamers_ext(&signed_in, 1) == CNA_RESULT_SUCCESS);
    CHECK(cna_network_session_create(CNA_NETWORK_SESSION_TYPE_LOCAL, 1, 4, &session) == CNA_RESULT_SUCCESS);
    CHECK(cna_network_session_get_gamer(session, CNA_NETWORK_SESSION_ROSTER_LOCAL, 0, &local) == CNA_RESULT_SUCCESS);
    CHECK(cna_network_gamer_copy_machine(local, &machine) == CNA_RESULT_SUCCESS);
    CHECK(cna_network_gamer_destroy(local) == CNA_RESULT_SUCCESS);
    CHECK(cna_network_session_destroy(session) == CNA_RESULT_INVALID_STATE);
    CHECK(cna_network_session_dispose(session) == CNA_RESULT_INVALID_STATE);
    CHECK(cna_network_machine_get_gamer_count(machine, &count) == CNA_RESULT_SUCCESS && count == 1);
    CHECK(cna_network_machine_get_gamer(machine, 0, &machine_gamer) == CNA_RESULT_SUCCESS);
    CHECK(cna_network_gamer_get_is_local(machine_gamer, &is_local) == CNA_RESULT_SUCCESS && is_local == CNA_TRUE);
    CHECK(cna_network_gamer_get_session(machine_gamer, &owner) == CNA_RESULT_SUCCESS && owner == session);
    CHECK(cna_network_gamer_copy_machine(machine_gamer, &second) == CNA_RESULT_SUCCESS);
    CHECK(cna_network_gamer_destroy(machine_gamer) == CNA_RESULT_SUCCESS);
    CHECK(cna_network_machine_destroy(machine) == CNA_RESULT_INVALID_STATE);
    CHECK(cna_network_session_destroy(session) == CNA_RESULT_INVALID_STATE);
    CHECK(cna_network_machine_destroy(second) == CNA_RESULT_SUCCESS);
    CHECK(cna_network_machine_destroy(machine) == CNA_RESULT_SUCCESS);
    CHECK(cna_network_session_destroy(session) == CNA_RESULT_SUCCESS);
    CHECK(cna_gamer_set_signed_in_gamers_ext(NULL, 0) == CNA_RESULT_SUCCESS);
    CHECK(cna_signed_in_gamer_destroy(signed_in) == CNA_RESULT_SUCCESS);
    return 1;
}

static int roster_retention_does_not_retain_its_session_recursively(void)
{
    CNA_SignedInGamerHandle signed_in = CNA_INVALID_HANDLE;
    CNA_NetworkSessionHandle session = CNA_INVALID_HANDLE;
    CNA_NetworkGamerHandle remote = CNA_INVALID_HANDLE;
    CNA_NetworkGamerHandle local = CNA_INVALID_HANDLE;
    int32_t instances = -1;
    CHECK(cna_signed_in_gamer_create_ext(view("No cycle"), CNA_FALSE, CNA_FALSE,
        CNA_PLAYER_INDEX_ONE, &signed_in) == CNA_RESULT_SUCCESS);
    CHECK(cna_gamer_set_signed_in_gamers_ext(&signed_in, 1) == CNA_RESULT_SUCCESS);
    CHECK(cna_network_session_create(CNA_NETWORK_SESSION_TYPE_LOCAL, 1, 4, &session) == CNA_RESULT_SUCCESS);
    CHECK(cna_network_gamer_create(session, view("Retained remote"), &remote) == CNA_RESULT_SUCCESS);
    CHECK(cna_network_session_add_remote_gamer_ext(session, remote) == CNA_RESULT_SUCCESS);
    CHECK(cna_network_gamer_destroy(remote) == CNA_RESULT_SUCCESS);
    CHECK(cna_network_session_get_gamer(session, CNA_NETWORK_SESSION_ROSTER_LOCAL, 0, &local) == CNA_RESULT_SUCCESS);
    /* Reusing a same-session view must not make the session retain its own handle record. */
    CHECK(cna_network_session_add_remote_gamer_ext(session, local) == CNA_RESULT_SUCCESS);
    CHECK(cna_network_gamer_destroy(local) == CNA_RESULT_SUCCESS);
    CHECK(cna_network_session_destroy(session) == CNA_RESULT_SUCCESS);
    CHECK(cna_network_session_get_instance_count_ext(&instances) == CNA_RESULT_SUCCESS && instances == 0);
    CHECK(cna_gamer_set_signed_in_gamers_ext(NULL, 0) == CNA_RESULT_SUCCESS);
    CHECK(cna_signed_in_gamer_destroy(signed_in) == CNA_RESULT_SUCCESS);
    return 1;
}

static int extension_links_stay_in_their_session(void)
{
    CNA_SignedInGamerHandle signed_in = CNA_INVALID_HANDLE;
    CNA_NetworkSessionHandle session = CNA_INVALID_HANDLE;
    CNA_NetworkGamerHandle local = CNA_INVALID_HANDLE;
    CNA_NetworkGamerHandle detached = CNA_INVALID_HANDLE;
    CNA_NetworkGamerHandle bound = CNA_INVALID_HANDLE;
    CNA_NetworkMachineHandle machine = CNA_INVALID_HANDLE;
    CNA_NetworkMachineHandle assigned_copy = CNA_INVALID_HANDLE;
    CNA_NetworkMachineHandle empty_copy = CNA_INVALID_HANDLE;
    int32_t count = -1;
    CHECK(cna_signed_in_gamer_create_ext(view("Graph parent"), CNA_FALSE, CNA_FALSE,
        CNA_PLAYER_INDEX_ONE, &signed_in) == CNA_RESULT_SUCCESS);
    CHECK(cna_gamer_set_signed_in_gamers_ext(&signed_in, 1) == CNA_RESULT_SUCCESS);
    CHECK(cna_network_session_create(CNA_NETWORK_SESSION_TYPE_LOCAL, 1, 4, &session) == CNA_RESULT_SUCCESS);
    CHECK(cna_network_gamer_create(CNA_INVALID_HANDLE, view("Detached"), &detached) == CNA_RESULT_SUCCESS);
    CHECK(cna_network_session_add_remote_gamer_ext(session, detached) == CNA_RESULT_INVALID_ARGUMENT);
    CHECK(cna_network_session_get_gamer_count(session, CNA_NETWORK_SESSION_ROSTER_ALL, &count) == CNA_RESULT_SUCCESS && count == 1);
    CHECK(cna_network_session_get_gamer(session, CNA_NETWORK_SESSION_ROSTER_LOCAL, 0, &local) == CNA_RESULT_SUCCESS);
    CHECK(cna_network_gamer_copy_machine(local, &machine) == CNA_RESULT_SUCCESS);
    CHECK(cna_network_gamer_set_machine(detached, machine) == CNA_RESULT_INVALID_ARGUMENT);
    CHECK(cna_network_gamer_copy_machine(detached, &empty_copy) == CNA_RESULT_SUCCESS);
    CHECK(cna_network_machine_get_gamer_count(empty_copy, &count) == CNA_RESULT_SUCCESS && count == 0);
    CHECK(cna_network_machine_destroy(empty_copy) == CNA_RESULT_SUCCESS);
    CHECK(cna_network_gamer_destroy(detached) == CNA_RESULT_SUCCESS);
    CHECK(cna_network_gamer_create(session, view("Bound"), &bound) == CNA_RESULT_SUCCESS);
    CHECK(cna_network_gamer_set_machine(bound, machine) == CNA_RESULT_SUCCESS);
    CHECK(cna_network_gamer_copy_machine(bound, &assigned_copy) == CNA_RESULT_SUCCESS);
    CHECK(cna_network_gamer_destroy(bound) == CNA_RESULT_SUCCESS);
    CHECK(cna_network_machine_destroy(machine) == CNA_RESULT_SUCCESS);
    CHECK(cna_network_gamer_destroy(local) == CNA_RESULT_SUCCESS);
    CHECK(cna_network_session_destroy(session) == CNA_RESULT_INVALID_STATE);
    CHECK(cna_network_machine_destroy(assigned_copy) == CNA_RESULT_SUCCESS);
    CHECK(cna_network_session_destroy(session) == CNA_RESULT_SUCCESS);
    CHECK(cna_gamer_set_signed_in_gamers_ext(NULL, 0) == CNA_RESULT_SUCCESS);
    CHECK(cna_signed_in_gamer_destroy(signed_in) == CNA_RESULT_SUCCESS);
    return 1;
}

static int removed_gamer_remains_in_the_previous_roster(void)
{
    CNA_SignedInGamerHandle signed_in = CNA_INVALID_HANDLE;
    CNA_NetworkSessionHandle session = CNA_INVALID_HANDLE;
    CNA_NetworkGamerHandle remote = CNA_INVALID_HANDLE;
    CNA_NetworkGamerHandle previous = CNA_INVALID_HANDLE;
    CNA_NetworkGamerHandle replacement = CNA_INVALID_HANDLE;
    uint8_t id = 0;
    CHECK(cna_signed_in_gamer_create_ext(view("History parent"), CNA_FALSE, CNA_FALSE,
        CNA_PLAYER_INDEX_ONE, &signed_in) == CNA_RESULT_SUCCESS);
    CHECK(cna_gamer_set_signed_in_gamers_ext(&signed_in, 1) == CNA_RESULT_SUCCESS);
    CHECK(cna_network_session_create(CNA_NETWORK_SESSION_TYPE_LOCAL, 1, 4, &session) == CNA_RESULT_SUCCESS);
    CHECK(cna_network_gamer_create(session, view("History original"), &remote) == CNA_RESULT_SUCCESS);
    CHECK(cna_network_gamer_set_id_ext(remote, 77) == CNA_RESULT_SUCCESS);
    CHECK(cna_network_session_add_remote_gamer_ext(session, remote) == CNA_RESULT_SUCCESS);
    CHECK(cna_network_session_remove_gamer_ext(session, remote, CNA_NETWORK_SESSION_END_REASON_DISCONNECTED) == CNA_RESULT_SUCCESS);
    CHECK(cna_network_gamer_destroy(remote) == CNA_RESULT_SUCCESS);
    CHECK(cna_network_gamer_create(session, view("History replacement"), &replacement) == CNA_RESULT_SUCCESS);
    CHECK(cna_network_gamer_set_id_ext(replacement, 3) == CNA_RESULT_SUCCESS);
    CHECK(cna_network_session_get_gamer(session, CNA_NETWORK_SESSION_ROSTER_PREVIOUS, 0, &previous) == CNA_RESULT_SUCCESS);
    CHECK(cna_network_gamer_get_id(previous, &id) == CNA_RESULT_SUCCESS && id == 77);
    CHECK(cna_network_gamer_destroy(previous) == CNA_RESULT_SUCCESS);
    CHECK(cna_network_gamer_destroy(replacement) == CNA_RESULT_SUCCESS);
    CHECK(cna_network_session_destroy(session) == CNA_RESULT_SUCCESS);
    CHECK(cna_gamer_set_signed_in_gamers_ext(NULL, 0) == CNA_RESULT_SUCCESS);
    CHECK(cna_signed_in_gamer_destroy(signed_in) == CNA_RESULT_SUCCESS);
    return 1;
}

static int session_creation_retains_signed_in_source(int mode)
{
    CNA_SignedInGamerHandle input = CNA_INVALID_HANDLE;
    CNA_SignedInGamerHandle replacement = CNA_INVALID_HANDLE;
    CNA_SignedInGamerHandle alias = CNA_INVALID_HANDLE;
    CNA_SignedInGamerHandle second_alias = CNA_INVALID_HANDLE;
    CNA_NetworkSessionHandle session = CNA_INVALID_HANDLE;
    CNA_NetworkGamerHandle local = CNA_INVALID_HANDLE;
    CNA_NetworkMachineHandle machine = CNA_INVALID_HANDLE;
    CNA_Result result;
    char tag[128];
    uint64_t bytes = 0;
    int32_t party_size = 0;
    int32_t instances = -1;
    const char* original = "Original session signed-in source";
    CHECK(cna_signed_in_gamer_create_ext(view(original), CNA_FALSE, CNA_FALSE,
        CNA_PLAYER_INDEX_ONE, &input) == CNA_RESULT_SUCCESS);
    /* Explicit-list variants deliberately use an unpublished source. */
    if (mode % 3 != 2)
        CHECK(cna_gamer_set_signed_in_gamers_ext(&input, 1) == CNA_RESULT_SUCCESS);
    switch (mode) {
    case 0:
        result = cna_network_session_create(CNA_NETWORK_SESSION_TYPE_LOCAL, 1, 4, &session);
        break;
    case 1:
        result = cna_network_session_create_with_properties(CNA_NETWORK_SESSION_TYPE_LOCAL,
            1, 4, 0, CNA_INVALID_HANDLE, &session);
        break;
    case 2:
        result = cna_network_session_create_with_local_gamers(CNA_NETWORK_SESSION_TYPE_LOCAL,
            &input, 1, 4, 0, CNA_INVALID_HANDLE, &session);
        break;
    case 3:
        result = cna_network_session_create_async(CNA_NETWORK_SESSION_TYPE_LOCAL,
            1, 4, NULL, NULL, &session);
        break;
    case 4:
        result = cna_network_session_create_with_properties_async(CNA_NETWORK_SESSION_TYPE_LOCAL,
            1, 4, 0, CNA_INVALID_HANDLE, NULL, NULL, &session);
        break;
    default:
        result = cna_network_session_create_with_local_gamers_async(CNA_NETWORK_SESSION_TYPE_LOCAL,
            &input, 1, 4, 0, CNA_INVALID_HANDLE, NULL, NULL, &session);
        break;
    }
    CHECK(result == CNA_RESULT_SUCCESS);
    CHECK(cna_gamer_set_signed_in_gamers_ext(NULL, 0) == CNA_RESULT_SUCCESS);
    CHECK(cna_signed_in_gamer_destroy(input) == CNA_RESULT_SUCCESS);
    CHECK(cna_signed_in_gamer_create_ext(view("Replacement session signed-in source"),
        CNA_FALSE, CNA_FALSE, CNA_PLAYER_INDEX_TWO, &replacement) == CNA_RESULT_SUCCESS);
    CHECK(cna_network_session_get_gamer(session, CNA_NETWORK_SESSION_ROSTER_LOCAL, 0, &local) == CNA_RESULT_SUCCESS);
    CHECK(cna_local_network_gamer_get_signed_in_gamer(local, &alias) == CNA_RESULT_SUCCESS);
    CHECK(cna_signed_in_gamer_copy_gamertag(alias, tag, sizeof(tag), &bytes) == CNA_RESULT_SUCCESS);
    CHECK(bytes == (uint64_t)strlen(original) && memcmp(tag, original, (size_t)bytes) == 0);
    CHECK(cna_network_gamer_copy_machine(local, &machine) == CNA_RESULT_SUCCESS);
    CHECK(cna_network_gamer_destroy(local) == CNA_RESULT_SUCCESS);
    CHECK(cna_network_machine_get_gamer(machine, 0, &local) == CNA_RESULT_SUCCESS);
    CHECK(cna_local_network_gamer_get_signed_in_gamer(local, &second_alias) == CNA_RESULT_SUCCESS);
    CHECK(cna_signed_in_gamer_set_party_size(alias, 3) == CNA_RESULT_SUCCESS);
    CHECK(cna_signed_in_gamer_get_party_size(second_alias, &party_size) == CNA_RESULT_SUCCESS && party_size == 3);
    CHECK(cna_network_gamer_destroy(local) == CNA_RESULT_SUCCESS);
    CHECK(cna_network_machine_destroy(machine) == CNA_RESULT_SUCCESS);
    CHECK(cna_network_session_destroy(session) == CNA_RESULT_SUCCESS);
    CHECK(cna_network_session_get_instance_count_ext(&instances) == CNA_RESULT_SUCCESS && instances == 0);
    /* Signed-in views retain the native identity, without keeping the session itself alive. */
    CHECK(cna_signed_in_gamer_copy_gamertag(alias, tag, sizeof(tag), &bytes) == CNA_RESULT_SUCCESS);
    CHECK(bytes == (uint64_t)strlen(original) && memcmp(tag, original, (size_t)bytes) == 0);
    CHECK(cna_signed_in_gamer_get_party_size(second_alias, &party_size) == CNA_RESULT_SUCCESS && party_size == 3);
    CHECK(cna_signed_in_gamer_destroy(alias) == CNA_RESULT_SUCCESS);
    CHECK(cna_signed_in_gamer_destroy(second_alias) == CNA_RESULT_SUCCESS);
    CHECK(cna_signed_in_gamer_destroy(replacement) == CNA_RESULT_SUCCESS);
    return 1;
}

static int added_local_retains_signed_in_source(int factory)
{
    CNA_SignedInGamerHandle host = CNA_INVALID_HANDLE;
    CNA_SignedInGamerHandle input = CNA_INVALID_HANDLE;
    CNA_SignedInGamerHandle replacement = CNA_INVALID_HANDLE;
    CNA_SignedInGamerHandle alias = CNA_INVALID_HANDLE;
    CNA_NetworkSessionHandle session = CNA_INVALID_HANDLE;
    CNA_NetworkGamerHandle local = CNA_INVALID_HANDLE;
    char tag[128];
    uint64_t bytes = 0;
    int32_t instances = -1;
    const char* original = "Added local signed-in source";
    CHECK(cna_signed_in_gamer_create_ext(view("Added local host"), CNA_FALSE, CNA_FALSE,
        CNA_PLAYER_INDEX_ONE, &host) == CNA_RESULT_SUCCESS);
    CHECK(cna_signed_in_gamer_create_ext(view(original), CNA_FALSE, CNA_FALSE,
        CNA_PLAYER_INDEX_TWO, &input) == CNA_RESULT_SUCCESS);
    CHECK(cna_gamer_set_signed_in_gamers_ext(&host, 1) == CNA_RESULT_SUCCESS);
    CHECK(cna_network_session_create(CNA_NETWORK_SESSION_TYPE_LOCAL, 2, 4, &session) == CNA_RESULT_SUCCESS);
    if (factory) {
        CHECK(cna_local_network_gamer_create_ext(input, session, &local) == CNA_RESULT_SUCCESS);
        CHECK(cna_network_session_add_remote_gamer_ext(session, local) == CNA_RESULT_SUCCESS);
        CHECK(cna_network_gamer_destroy(local) == CNA_RESULT_SUCCESS);
    } else {
        CHECK(cna_network_session_add_local_gamer(session, input) == CNA_RESULT_SUCCESS);
    }
    CHECK(cna_signed_in_gamer_destroy(input) == CNA_RESULT_SUCCESS);
    /* An added gamer joins at the next update (XNA AddLocalGamer, TQ-039); the retained input
       backs it even though its handle is already released. */
    CHECK(cna_network_session_update(session) == CNA_RESULT_SUCCESS);
    CHECK(cna_signed_in_gamer_create_ext(view("Added local replacement"), CNA_FALSE, CNA_FALSE,
        CNA_PLAYER_INDEX_THREE, &replacement) == CNA_RESULT_SUCCESS);
    CHECK(cna_network_session_get_gamer(session, factory ? CNA_NETWORK_SESSION_ROSTER_REMOTE :
        CNA_NETWORK_SESSION_ROSTER_LOCAL, factory ? 0 : 1, &local) == CNA_RESULT_SUCCESS);
    CHECK(cna_local_network_gamer_get_signed_in_gamer(local, &alias) == CNA_RESULT_SUCCESS);
    CHECK(cna_signed_in_gamer_copy_gamertag(alias, tag, sizeof(tag), &bytes) == CNA_RESULT_SUCCESS);
    CHECK(bytes == (uint64_t)strlen(original) && memcmp(tag, original, (size_t)bytes) == 0);
    CHECK(cna_network_gamer_destroy(local) == CNA_RESULT_SUCCESS);
    CHECK(cna_network_session_destroy(session) == CNA_RESULT_SUCCESS);
    CHECK(cna_network_session_get_instance_count_ext(&instances) == CNA_RESULT_SUCCESS && instances == 0);
    CHECK(cna_signed_in_gamer_copy_gamertag(alias, tag, sizeof(tag), &bytes) == CNA_RESULT_SUCCESS);
    CHECK(bytes == (uint64_t)strlen(original) && memcmp(tag, original, (size_t)bytes) == 0);
    CHECK(cna_signed_in_gamer_destroy(alias) == CNA_RESULT_SUCCESS);
    CHECK(cna_signed_in_gamer_destroy(replacement) == CNA_RESULT_SUCCESS);
    CHECK(cna_gamer_set_signed_in_gamers_ext(NULL, 0) == CNA_RESULT_SUCCESS);
    CHECK(cna_signed_in_gamer_destroy(host) == CNA_RESULT_SUCCESS);
    return 1;
}

struct CallbackSource {
    CNA_SignedInGamerHandle input;
    CNA_SignedInGamerHandle replacement;
    CNA_Result result;
    int calls;
    int replace_published;
};

static void release_source_before_end(void* context)
{
    struct CallbackSource* source = (struct CallbackSource*)context;
    ++source->calls;
    source->result = cna_gamer_set_signed_in_gamers_ext(NULL, 0);
    if (source->result != CNA_RESULT_SUCCESS) return;
    source->result = cna_signed_in_gamer_destroy(source->input);
    if (source->result != CNA_RESULT_SUCCESS) return;
    source->result = cna_signed_in_gamer_create_ext(view("After completion callback"),
        CNA_FALSE, CNA_FALSE, CNA_PLAYER_INDEX_TWO, &source->replacement);
    if (source->result != CNA_RESULT_SUCCESS) return;
    if (source->replace_published)
        source->result = cna_gamer_set_signed_in_gamers_ext(&source->replacement, 1);
}

static int callback_release_preserves_native_selection(int mode)
{
    struct CallbackSource source = {CNA_INVALID_HANDLE, CNA_INVALID_HANDLE, CNA_RESULT_INTERNAL, 0, mode != 2};
    CNA_SignedInGamerHandle alias = CNA_INVALID_HANDLE;
    CNA_SignedInGamerHandle churn = CNA_INVALID_HANDLE;
    CNA_NetworkSessionHandle session = CNA_INVALID_HANDLE;
    CNA_NetworkGamerHandle local = CNA_INVALID_HANDLE;
    CNA_Result result;
    char tag[128];
    uint64_t bytes = 0;
    const char* original = "Before completion callback";
    const char* selected = mode == 2 ? original : "After completion callback";
    CHECK(cna_signed_in_gamer_create_ext(view(original), CNA_FALSE, CNA_FALSE,
        CNA_PLAYER_INDEX_ONE, &source.input) == CNA_RESULT_SUCCESS);
    if (mode != 2)
        CHECK(cna_gamer_set_signed_in_gamers_ext(&source.input, 1) == CNA_RESULT_SUCCESS);
    if (mode == 0)
        result = cna_network_session_create_async(CNA_NETWORK_SESSION_TYPE_LOCAL, 1, 4,
            release_source_before_end, &source, &session);
    else if (mode == 1)
        result = cna_network_session_create_with_properties_async(CNA_NETWORK_SESSION_TYPE_LOCAL,
            1, 4, 0, CNA_INVALID_HANDLE, release_source_before_end, &source, &session);
    else
        result = cna_network_session_create_with_local_gamers_async(CNA_NETWORK_SESSION_TYPE_LOCAL,
            &source.input, 1, 4, 0, CNA_INVALID_HANDLE, release_source_before_end, &source, &session);
    CHECK(source.calls == 1 && source.result == CNA_RESULT_SUCCESS);
    CHECK(result == CNA_RESULT_SUCCESS);
    CHECK(cna_gamer_set_signed_in_gamers_ext(NULL, 0) == CNA_RESULT_SUCCESS);
    CHECK(cna_signed_in_gamer_destroy(source.replacement) == CNA_RESULT_SUCCESS);
    CHECK(cna_signed_in_gamer_create_ext(view("Allocation after completion"), CNA_FALSE, CNA_FALSE,
        CNA_PLAYER_INDEX_THREE, &churn) == CNA_RESULT_SUCCESS);
    CHECK(cna_network_session_get_gamer(session, CNA_NETWORK_SESSION_ROSTER_LOCAL, 0, &local) == CNA_RESULT_SUCCESS);
    CHECK(cna_local_network_gamer_get_signed_in_gamer(local, &alias) == CNA_RESULT_SUCCESS);
    CHECK(cna_signed_in_gamer_copy_gamertag(alias, tag, sizeof(tag), &bytes) == CNA_RESULT_SUCCESS);
    CHECK(bytes == (uint64_t)strlen(selected) && memcmp(tag, selected, (size_t)bytes) == 0);
    CHECK(cna_network_gamer_destroy(local) == CNA_RESULT_SUCCESS);
    CHECK(cna_network_session_destroy(session) == CNA_RESULT_SUCCESS);
    CHECK(cna_signed_in_gamer_copy_gamertag(alias, tag, sizeof(tag), &bytes) == CNA_RESULT_SUCCESS);
    CHECK(bytes == (uint64_t)strlen(selected) && memcmp(tag, selected, (size_t)bytes) == 0);
    CHECK(cna_signed_in_gamer_destroy(alias) == CNA_RESULT_SUCCESS);
    CHECK(cna_signed_in_gamer_destroy(churn) == CNA_RESULT_SUCCESS);
    return 1;
}

static int shared_machine_aliases(int alias_first)
{
    CNA_SignedInGamerHandle host = 0, added = 0;
    CNA_NetworkSessionHandle session = 0;
    CNA_NetworkGamerHandle first = 0, second = 0, cached = 0, detached = 0, right_cached = 0;
    CNA_NetworkMachineHandle left = 0, right = 0, empty = 0, replacement = 0, unbound = 0;
    CNA_NetworkMachineHandle refused = 99;
    int32_t count = -1;
    uint8_t id = 0;
    CHECK(cna_signed_in_gamer_create_ext(view("Shared host"), CNA_FALSE, CNA_FALSE,
        CNA_PLAYER_INDEX_ONE, &host) == CNA_RESULT_SUCCESS);
    CHECK(cna_signed_in_gamer_create_ext(view("Shared added"), CNA_FALSE, CNA_FALSE,
        CNA_PLAYER_INDEX_TWO, &added) == CNA_RESULT_SUCCESS);
    CHECK(cna_gamer_set_signed_in_gamers_ext(&host, 1) == CNA_RESULT_SUCCESS);
    CHECK(cna_network_session_create(CNA_NETWORK_SESSION_TYPE_LOCAL, 2, 4, &session) == CNA_RESULT_SUCCESS);
    CHECK(cna_network_session_get_gamer(session, CNA_NETWORK_SESSION_ROSTER_LOCAL, 0, &first) == CNA_RESULT_SUCCESS);
    CHECK(cna_network_gamer_get_shared_machine_ext(first, &left) == CNA_RESULT_SUCCESS);
    CHECK(cna_network_gamer_get_shared_machine_ext(first, NULL) == CNA_RESULT_INVALID_ARGUMENT);
    CHECK(cna_network_gamer_get_shared_machine_ext(0, &refused) == CNA_RESULT_INVALID_HANDLE && refused == 0);
    CHECK(cna_network_machine_get_gamer_count(left, &count) == CNA_RESULT_SUCCESS && count == 1);
    CHECK(cna_network_machine_get_gamer(left, 0, &cached) == CNA_RESULT_SUCCESS);
    CHECK(cna_network_session_add_local_gamer(session, added) == CNA_RESULT_SUCCESS);
    CHECK(cna_network_session_update(session) == CNA_RESULT_SUCCESS); /* it joins here (TQ-039) */
    CHECK(cna_network_session_get_gamer(session, CNA_NETWORK_SESSION_ROSTER_LOCAL, 1, &second) == CNA_RESULT_SUCCESS);
    CHECK(cna_network_gamer_get_shared_machine_ext(second, &right) == CNA_RESULT_SUCCESS);
    /* Oracle: CLOSURE_060 native shared identity, independent aliases and per-alias view guards. */
    CHECK(left != right);
    CHECK(cna_network_gamer_set_id_ext(second, 42) == CNA_RESULT_SUCCESS);
    CHECK(cna_network_machine_get_gamer(right, 1, &right_cached) == CNA_RESULT_SUCCESS);
    CHECK(cna_network_machine_destroy(right) == CNA_RESULT_INVALID_STATE);
    CHECK(cna_network_gamer_get_id(right_cached, &id) == CNA_RESULT_SUCCESS && id == 42);
    CHECK(cna_network_gamer_set_shared_machine_ext(first, left) == CNA_RESULT_SUCCESS);
    CHECK(cna_network_gamer_set_shared_machine_ext(first, left) == CNA_RESULT_SUCCESS);
    CHECK(cna_network_machine_get_gamer_count(left, &count) == CNA_RESULT_SUCCESS && count == 2);
    refused = 99;
    CHECK(cna_network_machine_get_gamer(right, 2, &refused) == CNA_RESULT_INVALID_ARGUMENT && refused == 0);
    /* Mutation through a gamer reached from one alias is visible through the other. */
    CHECK(cna_network_gamer_set_id_ext(cached, 73) == CNA_RESULT_SUCCESS);
    CHECK(cna_network_gamer_get_id(first, &id) == CNA_RESULT_SUCCESS && id == 73);
    CHECK(cna_network_machine_get_gamer_count(right, &count) == CNA_RESULT_SUCCESS && count == 2);
    CHECK(cna_network_machine_get_gamer_count(left, &count) == CNA_RESULT_SUCCESS && count == 2);
    CHECK(cna_network_gamer_set_shared_machine_ext(second, left) == CNA_RESULT_SUCCESS);
    CHECK(cna_network_gamer_set_shared_machine_ext(second, 0) == CNA_RESULT_INVALID_HANDLE);
    CHECK(cna_network_gamer_set_shared_machine_ext(second, host) == CNA_RESULT_INVALID_HANDLE);
    CHECK(cna_network_gamer_create(0, view("Other session boundary"), &detached) == CNA_RESULT_SUCCESS);
    CHECK(cna_network_gamer_set_shared_machine_ext(detached, left) == CNA_RESULT_INVALID_ARGUMENT);
    /* Even an empty identity already associated with no session cannot cross into this session. */
    CHECK(cna_network_gamer_get_shared_machine_ext(detached, &unbound) == CNA_RESULT_SUCCESS);
    CHECK(cna_network_gamer_set_shared_machine_ext(second, unbound) == CNA_RESULT_INVALID_ARGUMENT);
    CHECK(cna_network_machine_destroy(unbound) == CNA_RESULT_SUCCESS);
    CHECK(cna_network_gamer_destroy(detached) == CNA_RESULT_SUCCESS);
    CHECK(cna_network_machine_create(&empty) == CNA_RESULT_SUCCESS);
    CHECK(cna_network_gamer_set_shared_machine_ext(second, empty) == CNA_RESULT_SUCCESS);
    /* Reattach to an old identity without changing either roster, then detach again. */
    CHECK(cna_network_gamer_set_shared_machine_ext(second, right) == CNA_RESULT_SUCCESS);
    CHECK(cna_network_machine_get_gamer_count(right, &count) == CNA_RESULT_SUCCESS && count == 2);
    CHECK(cna_network_machine_get_gamer_count(empty, &count) == CNA_RESULT_SUCCESS && count == 0);
    CHECK(cna_network_gamer_set_shared_machine_ext(second, empty) == CNA_RESULT_SUCCESS);
    CHECK(cna_network_gamer_get_shared_machine_ext(second, &replacement) == CNA_RESULT_SUCCESS);
    CHECK(cna_network_machine_destroy(empty) == CNA_RESULT_SUCCESS);
    CHECK(cna_network_machine_get_gamer_count(replacement, &count) == CNA_RESULT_SUCCESS && count == 0);
    CHECK(cna_network_machine_get_gamer_count(left, &count) == CNA_RESULT_SUCCESS && count == 2);
    CHECK(cna_network_machine_get_gamer_count(right, &count) == CNA_RESULT_SUCCESS && count == 2);
    CHECK(cna_network_gamer_get_id(cached, &id) == CNA_RESULT_SUCCESS && id == 73);
    /* Removing the gamer reached through an alias edits the native machine roster itself. */
    CHECK(cna_network_session_remove_gamer_ext(session, cached,
        CNA_NETWORK_SESSION_END_REASON_DISCONNECTED) == CNA_RESULT_SUCCESS);
    CHECK(cna_network_machine_get_gamer_count(left, &count) == CNA_RESULT_SUCCESS && count == 1);
    CHECK(cna_network_machine_get_gamer_count(right, &count) == CNA_RESULT_SUCCESS && count == 1);
    CHECK(cna_network_machine_destroy(left) == CNA_RESULT_INVALID_STATE);
    CHECK(cna_network_session_dispose(session) == CNA_RESULT_INVALID_STATE);
    if (!alias_first) {
        CHECK(cna_network_gamer_destroy(first) == CNA_RESULT_SUCCESS);
        CHECK(cna_network_gamer_destroy(second) == CNA_RESULT_SUCCESS);
    }
    CHECK(cna_network_gamer_get_id(cached, &id) == CNA_RESULT_SUCCESS && id == 73);
    CHECK(cna_network_gamer_destroy(cached) == CNA_RESULT_SUCCESS);
    CHECK(cna_network_machine_destroy(right) == CNA_RESULT_INVALID_STATE);
    CHECK(cna_network_gamer_get_id(right_cached, &id) == CNA_RESULT_SUCCESS && id == 42);
    CHECK(cna_network_gamer_destroy(right_cached) == CNA_RESULT_SUCCESS);
    if (alias_first) {
        CHECK(cna_network_machine_destroy(left) == CNA_RESULT_SUCCESS);
        CHECK(cna_network_machine_get_gamer_count(right, &count) == CNA_RESULT_SUCCESS && count == 1);
        CHECK(cna_network_gamer_destroy(first) == CNA_RESULT_SUCCESS);
        CHECK(cna_network_gamer_destroy(second) == CNA_RESULT_SUCCESS);
        CHECK(cna_network_machine_destroy(right) == CNA_RESULT_SUCCESS);
    } else {
        CHECK(cna_network_machine_destroy(right) == CNA_RESULT_SUCCESS);
        CHECK(cna_network_machine_get_gamer_count(left, &count) == CNA_RESULT_SUCCESS && count == 1);
        CHECK(cna_network_machine_destroy(left) == CNA_RESULT_SUCCESS);
    }
    count = -77;
    CHECK(cna_network_machine_get_gamer_count(left, &count) == CNA_RESULT_INVALID_HANDLE && count == -77);
    refused = 99;
    CHECK(cna_network_machine_get_gamer(left, 0, &refused) == CNA_RESULT_INVALID_HANDLE && refused == 0);
    /* An empty shared alias also guards its associated session. */
    CHECK(cna_network_session_destroy(session) == CNA_RESULT_INVALID_STATE);
    CHECK(cna_network_machine_destroy(replacement) == CNA_RESULT_SUCCESS);
    CHECK(cna_network_machine_destroy(replacement) == CNA_RESULT_INVALID_HANDLE);
    CHECK(cna_network_session_destroy(session) == CNA_RESULT_SUCCESS);
    CHECK(cna_gamer_set_signed_in_gamers_ext(NULL, 0) == CNA_RESULT_SUCCESS);
    CHECK(cna_signed_in_gamer_destroy(added) == CNA_RESULT_SUCCESS);
    CHECK(cna_signed_in_gamer_destroy(host) == CNA_RESULT_SUCCESS);
    return 1;
}

int main(int argc, char** argv)
{
    int mode;
    if (argc == 2 && strcmp(argv[1], "shared-machine") == 0)
        return shared_machine_aliases(0) && shared_machine_aliases(1) ? 0 : 1;
    if (argc == 2 && strcmp(argv[1], "signed-in-session") == 0)
        return session_creation_retains_signed_in_source(0) ? 0 : 1;
    if (argc == 2 && strcmp(argv[1], "signed-in-add-local") == 0)
        return added_local_retains_signed_in_source(0) ? 0 : 1;
    if (argc == 2 && strcmp(argv[1], "signed-in-callback") == 0)
        return callback_release_preserves_native_selection(2) ? 0 : 1;
    if (argc == 2 && strcmp(argv[1], "session") == 0)
        return session_outlives_standalone_gamer_handles() ? 0 : 1;
    if (argc == 2 && strcmp(argv[1], "machine") == 0)
        return copied_machine_retains_its_roster_parent() ? 0 : 1;
    if (argc == 2 && strcmp(argv[1], "extension-graphs") == 0)
        return extension_links_stay_in_their_session() ? 0 : 1;
    if (argc == 2 && strcmp(argv[1], "history") == 0)
        return removed_gamer_remains_in_the_previous_roster() ? 0 : 1;
    for (mode = 0; mode != 6; ++mode)
        if (!session_creation_retains_signed_in_source(mode)) return 1;
    for (mode = 0; mode != 3; ++mode)
        if (!callback_release_preserves_native_selection(mode)) return 1;
    if (!added_local_retains_signed_in_source(0) || !added_local_retains_signed_in_source(1)) return 1;
    return shared_machine_aliases(0) && shared_machine_aliases(1) &&
        signed_in_parent_survives_its_input_handle() &&
        missing_signed_in_parent_is_not_an_owned_object() &&
        session_outlives_standalone_gamer_handles() &&
        copied_machine_retains_its_roster_parent() &&
        roster_retention_does_not_retain_its_session_recursively() &&
        extension_links_stay_in_their_session() &&
        removed_gamer_remains_in_the_previous_roster() ? 0 : 1;
}
