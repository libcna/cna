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

int main(int argc, char** argv)
{
    if (argc == 2 && strcmp(argv[1], "session") == 0)
        return session_outlives_standalone_gamer_handles() ? 0 : 1;
    if (argc == 2 && strcmp(argv[1], "machine") == 0)
        return copied_machine_retains_its_roster_parent() ? 0 : 1;
    if (argc == 2 && strcmp(argv[1], "extension-graphs") == 0)
        return extension_links_stay_in_their_session() ? 0 : 1;
    if (argc == 2 && strcmp(argv[1], "history") == 0)
        return removed_gamer_remains_in_the_previous_roster() ? 0 : 1;
    return signed_in_parent_survives_its_input_handle() &&
        missing_signed_in_parent_is_not_an_owned_object() &&
        session_outlives_standalone_gamer_handles() &&
        copied_machine_retains_its_roster_parent() &&
        roster_retention_does_not_retain_its_session_recursively() &&
        extension_links_stay_in_their_session() &&
        removed_gamer_remains_in_the_previous_roster() ? 0 : 1;
}
