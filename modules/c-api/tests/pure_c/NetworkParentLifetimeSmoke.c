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

int main(void)
{
    return signed_in_parent_survives_its_input_handle() &&
        missing_signed_in_parent_is_not_an_owned_object() ? 0 : 1;
}
