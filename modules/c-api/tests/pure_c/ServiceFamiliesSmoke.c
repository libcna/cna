/* SPDX-License-Identifier: MS-PL */
#include "CnaRuntimeSmoke.h"
#include <math.h>
#define SV(text) ((CNA_StringView){text, sizeof(text)-1})
void cna_test_service_begin(void);
void cna_test_service_end(void);
int cna_test_service_worker(int (*test)(void));
int cna_test_service_name(unsigned kind, const char* text, uint64_t count);
int cna_test_service_config(const CNA_GamerServicesConfiguration* value);
void cna_test_service_initialize(void);
int cna_test_service_endpoint_supported(void);
void cna_test_service_oversized_configuration(void);
void cna_test_service_nul_configuration(void);
CNA_Bool cna_test_service_nan_pair(void);
static CNA_Handle thread_gamer;
static CNA_Handle thread_dictionary;
static int worker(void)
{
    uint64_t bytes = 99;
    CNA_Bool matches = 99;
    CNA_GamerServicesConfiguration value;
    if (cna_gamer_services_configuration_init_ext(&value) != CNA_RESULT_SUCCESS) return 0;
    return cna_gamer_get_type_name_size(thread_gamer, &bytes) == CNA_RESULT_THREAD && bytes == 99 &&
        cna_property_dictionary_contains_pair_ext(thread_dictionary, SV("k"), thread_dictionary, SV("k"), &matches) == CNA_RESULT_THREAD && matches == 99 &&
        cna_gamer_services_configuration_resolve_ext(&value) == CNA_RESULT_THREAD &&
        cna_gamer_services_configuration_set_override_ext(NULL) == CNA_RESULT_THREAD;
}
int main(void)
{
    CnaRuntimeSmoke smoke = {0};
    int status = 1;
    CNA_Handle gamers[5] = {0};
    CNA_Handle source = 0, destination = 0;
    CNA_Bool matches = 99;
    uint64_t bytes = 99;
    char name[96];
    CNA_GamerServicesConfiguration config, resolved, saved;
    RUNTIME_CHECK(SMOKE_API(cna_gamer_services_configuration_init_ext(NULL)) == CNA_RESULT_INVALID_ARGUMENT);
    RUNTIME_CHECK(SMOKE_API(cna_gamer_services_configuration_init_ext(&config)) == CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_VALUE(config.max_avatar_catalog_bytes) == (UINT64_C(64)<<20) && config.avatar_catalog_updates == CNA_TRUE && config.endpoint[0] == 0);
    RUNTIME_CHECK(SMOKE_API(cna_gamer_services_configuration_validate_ext(&config)) == CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_API(cna_gamer_services_configuration_resolve_ext(NULL)) == CNA_RESULT_INVALID_ARGUMENT);
    RUNTIME_CHECK(SMOKE_API(cna_gamer_services_configuration_validate_ext(NULL)) == CNA_RESULT_INVALID_ARGUMENT);
    strcpy(config.game_id, "fixture"); strcpy(config.title_version, "1.2.3"); strcpy(config.ca_bundle, "bundle.pem");
    config.insecure_loopback = CNA_TRUE; config.avatar_catalog_updates = CNA_FALSE; config.max_avatar_catalog_bytes = 12345;
    RUNTIME_CHECK(SMOKE_API(cna_gamer_services_configuration_set_override_ext(&config)) == CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_API(cna_gamer_services_configuration_init_ext(&resolved)) == CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_API(cna_gamer_services_configuration_resolve_ext(&resolved)) == CNA_RESULT_SUCCESS && cna_test_service_config(&resolved));
    saved = resolved;
    strcpy(config.endpoint, "https://example.invalid/cna/v1");
    if (cna_test_service_endpoint_supported()) {
        RUNTIME_CHECK(SMOKE_API(cna_gamer_services_configuration_set_override_ext(&config)) == CNA_RESULT_SUCCESS);
        RUNTIME_CHECK(SMOKE_API(cna_gamer_services_configuration_resolve_ext(&resolved)) == CNA_RESULT_SUCCESS && strcmp(resolved.endpoint, config.endpoint) == 0 && cna_test_service_config(&resolved));
    } else {
        RUNTIME_CHECK(SMOKE_API(cna_gamer_services_configuration_validate_ext(&config)) != CNA_RESULT_SUCCESS);
    }
    RUNTIME_CHECK(SMOKE_API(cna_gamer_services_configuration_set_override_ext(&saved)) == CNA_RESULT_SUCCESS);
    resolved = saved;
    cna_test_service_oversized_configuration();
    RUNTIME_CHECK(SMOKE_API(cna_gamer_services_configuration_resolve_ext(&resolved)) == CNA_RESULT_OVERFLOW && memcmp(&saved, &resolved, sizeof(saved)) == 0);
    RUNTIME_CHECK(SMOKE_API(cna_gamer_services_configuration_set_override_ext(&saved)) == CNA_RESULT_SUCCESS);
    cna_test_service_nul_configuration();
    RUNTIME_CHECK(SMOKE_API(cna_gamer_services_configuration_resolve_ext(&resolved)) == CNA_RESULT_ENCODING && memcmp(&saved, &resolved, sizeof(saved)) == 0);
    RUNTIME_CHECK(SMOKE_API(cna_gamer_services_configuration_set_override_ext(&saved)) == CNA_RESULT_SUCCESS);
    config = saved;
    config.insecure_loopback = 2;
    RUNTIME_CHECK(SMOKE_API(cna_gamer_services_configuration_set_override_ext(&config)) == CNA_RESULT_INVALID_ARGUMENT);
    RUNTIME_CHECK(SMOKE_API(cna_gamer_services_configuration_resolve_ext(&resolved)) == CNA_RESULT_SUCCESS && memcmp(&saved, &resolved, sizeof(saved)) == 0);
    config = saved; config.struct_version = 99;
    RUNTIME_CHECK(SMOKE_API(cna_gamer_services_configuration_validate_ext(&config)) == CNA_RESULT_INVALID_ARGUMENT);
    config = saved; memset(config.endpoint, 'x', sizeof(config.endpoint));
    RUNTIME_CHECK(SMOKE_API(cna_gamer_services_configuration_validate_ext(&config)) == CNA_RESULT_INVALID_ARGUMENT);
    config = saved; config.game_id[0] = (char)0xff;
    RUNTIME_CHECK(SMOKE_API(cna_gamer_services_configuration_validate_ext(&config)) == CNA_RESULT_ENCODING);
    config = saved; strcpy(config.title_version, "bad");
    RUNTIME_CHECK(SMOKE_API(cna_gamer_services_configuration_validate_ext(&config)) != CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_API(cna_gamer_services_configuration_set_override_ext(NULL)) == CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_API(cna_gamer_services_configuration_set_override_ext(&saved)) == CNA_RESULT_SUCCESS);
    cna_test_service_begin();
    cna_test_service_initialize();
    RUNTIME_CHECK(SMOKE_OWN(cna_gamer_get_from_gamertag(SV("Fixture"), &gamers[0]), &gamers[0], cna_gamer_destroy) == CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_OWN(cna_friend_gamer_create_ext(SV("F"), SV("F"), 0, 0, 0, 0, 0, 0, &gamers[1]), &gamers[1], cna_gamer_destroy) == CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_OWN(cna_signed_in_gamer_create_ext(SV("S"), 0, 0, CNA_PLAYER_INDEX_ONE, &gamers[2]), &gamers[2], cna_signed_in_gamer_destroy) == CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_OWN(cna_network_gamer_create(0, SV("N"), &gamers[3]), &gamers[3], cna_network_gamer_destroy) == CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_OWN(cna_local_network_gamer_create_ext(gamers[2], 0, &gamers[4]), &gamers[4], cna_network_gamer_destroy) == CNA_RESULT_SUCCESS);
    for (unsigned i = 0; i < 5; ++i) {
        RUNTIME_CHECK(SMOKE_API(cna_gamer_get_type_name_size(gamers[i], &bytes)) == CNA_RESULT_SUCCESS && SMOKE_VALUE(bytes) > 10);
        memset(name, '!', sizeof(name));
        RUNTIME_CHECK(SMOKE_API(cna_gamer_copy_type_name(gamers[i], name, 1, &bytes)) == CNA_RESULT_BUFFER_TOO_SMALL && name[0] == '!');
        RUNTIME_CHECK(SMOKE_API(cna_gamer_copy_type_name(gamers[i], name, sizeof(name), &bytes)) == CNA_RESULT_SUCCESS && cna_test_service_name(i, name, bytes));
        printf("name[%u]=%.*s\n", i, (int)bytes, name);
    }
    RUNTIME_CHECK(SMOKE_API(cna_gamer_get_type_name_size(0, NULL)) == CNA_RESULT_INVALID_ARGUMENT);
    bytes = 99;
    RUNTIME_CHECK(SMOKE_API(cna_gamer_get_type_name_size(0, &bytes)) == CNA_RESULT_INVALID_HANDLE && bytes == 99);
    RUNTIME_CHECK(SMOKE_OWN(cna_property_dictionary_create_ext(&source), &source, cna_property_dictionary_destroy) == CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_OWN(cna_property_dictionary_create_ext(&destination), &destination, cna_property_dictionary_destroy) == CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_API(cna_gamer_copy_type_name(source, name, sizeof(name), &bytes)) == CNA_RESULT_INVALID_HANDLE);
    /* Each currently writable native boxed type, including NaN equality. */
    for (unsigned kind = 0; kind < 8; ++kind) {
        CNA_Result result;
        switch (kind) {
        case 0: result = cna_property_dictionary_set_int32(source, SV("v"), 42); break;
        case 1: result = cna_property_dictionary_set_int64(source, SV("v"), INT64_MAX); break;
        case 2: result = cna_property_dictionary_set_single(source, SV("v"), -0.0f); break;
        case 3: result = cna_property_dictionary_set_double(source, SV("v"), NAN); break;
        case 4: result = cna_property_dictionary_set_string(source, SV("v"), SV("utf8-\xc3\xa9")); break;
        case 5: result = cna_property_dictionary_set_date_time_ticks(source, SV("v"), 100); break;
        case 6: result = cna_property_dictionary_set_time_span_ticks(source, SV("v"), -100); break;
        default: result = cna_property_dictionary_set_outcome(source, SV("v"), CNA_LEADERBOARD_OUTCOME_WIN); break;
        }
        RUNTIME_CHECK(SMOKE_VALUE(result) == CNA_RESULT_SUCCESS);
        RUNTIME_CHECK(SMOKE_API(cna_property_dictionary_add_pair_ext(destination, SV("k"), source, SV("v"))) == CNA_RESULT_SUCCESS);
        RUNTIME_CHECK(SMOKE_API(cna_property_dictionary_add_pair_ext(destination, SV("k"), source, SV("v"))) == CNA_RESULT_INVALID_ARGUMENT);
        RUNTIME_CHECK(SMOKE_API(cna_property_dictionary_contains_pair_ext(destination, SV("k"), source, SV("v"), &matches)) == CNA_RESULT_SUCCESS && SMOKE_VALUE(matches) == CNA_TRUE);
        if (kind == 3) RUNTIME_CHECK(matches == cna_test_service_nan_pair());
        RUNTIME_CHECK(SMOKE_API(cna_property_dictionary_remove_pair_ext(destination, SV("k"), source, SV("v"), &matches)) == CNA_RESULT_SUCCESS && matches == CNA_TRUE);
        RUNTIME_CHECK(SMOKE_API(cna_property_dictionary_remove_pair_ext(destination, SV("k"), source, SV("v"), &matches)) == CNA_RESULT_SUCCESS && matches == CNA_FALSE);
    }
    RUNTIME_CHECK(SMOKE_API(cna_property_dictionary_set_int32(source, SV("k"), 42)) == CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_API(cna_property_dictionary_set_int64(destination, SV("k"), 42)) == CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_API(cna_property_dictionary_contains_pair_ext(destination, SV("k"), source, SV("k"), &matches)) == CNA_RESULT_SUCCESS && matches == CNA_FALSE);
    RUNTIME_CHECK(SMOKE_API(cna_property_dictionary_remove_pair_ext(destination, SV("k"), source, SV("k"), &matches)) == CNA_RESULT_SUCCESS && matches == CNA_FALSE);
    RUNTIME_CHECK(SMOKE_API(cna_property_dictionary_contains_pair_ext(source, SV("k"), source, SV("k"), &matches)) == CNA_RESULT_SUCCESS && matches == CNA_TRUE);
    matches = 99;
    RUNTIME_CHECK(SMOKE_API(cna_property_dictionary_contains_pair_ext(destination, SV("k"), source, SV("missing"), &matches)) == CNA_RESULT_INVALID_ARGUMENT && matches == 99);
    RUNTIME_CHECK(SMOKE_API(cna_property_dictionary_contains_pair_ext(0, SV("k"), 0, SV("k"), NULL)) == CNA_RESULT_INVALID_ARGUMENT);
    RUNTIME_CHECK(SMOKE_API(cna_property_dictionary_add_pair_ext(destination, SV("\xff"), source, SV("v"))) == CNA_RESULT_ENCODING);
    RUNTIME_CHECK(SMOKE_API(cna_property_dictionary_set_int32(source, SV("same"), 7)) == CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_API(cna_property_dictionary_remove_pair_ext(source, SV("same"), source, SV("same"), &matches)) == CNA_RESULT_SUCCESS && matches == CNA_TRUE);
    matches = 99;
    RUNTIME_CHECK(SMOKE_API(cna_property_dictionary_remove_pair_ext(gamers[2], SV("k"), source, SV("k"), &matches)) == CNA_RESULT_INVALID_HANDLE && matches == 99);
    RUNTIME_CHECK(SMOKE_API(cna_property_dictionary_add_pair_ext(destination, SV("a\0b"), source, SV("k"))) == CNA_RESULT_ENCODING);
    {
        CNA_LeaderboardIdentity identity;
        CNA_Handle reader = 0, entry = 0, columns = 0, chain = 0;
        RUNTIME_CHECK(SMOKE_API(cna_leaderboard_identity_init(CNA_LEADERBOARD_KEY_BEST_SCORE_LIFE_TIME, 0, &identity)) == CNA_RESULT_SUCCESS);
        RUNTIME_CHECK(SMOKE_OWN(cna_leaderboard_reader_read(&identity, 0, 1, &reader), &reader, cna_leaderboard_reader_destroy) == CNA_RESULT_SUCCESS);
        RUNTIME_CHECK(SMOKE_OWN(cna_leaderboard_reader_get_entry_at(reader, 0, &entry), &entry, cna_leaderboard_entry_destroy) == CNA_RESULT_SUCCESS);
        RUNTIME_CHECK(SMOKE_OWN(cna_leaderboard_entry_get_columns(entry, &columns), &columns, cna_property_dictionary_destroy) == CNA_RESULT_SUCCESS);
        RUNTIME_CHECK(SMOKE_API(cna_property_dictionary_add_pair_ext(destination, SV("stream"), columns, SV("stream"))) == CNA_RESULT_SUCCESS);
        RUNTIME_CHECK(SMOKE_API(cna_property_dictionary_contains_pair_ext(destination, SV("stream"), columns, SV("stream"), &matches)) == CNA_RESULT_SUCCESS && matches == CNA_TRUE);
        RUNTIME_CHECK(SMOKE_OWN(cna_property_dictionary_create_ext(&chain), &chain, cna_property_dictionary_destroy) == CNA_RESULT_SUCCESS);
        RUNTIME_CHECK(SMOKE_API(cna_property_dictionary_add_pair_ext(chain, SV("stream"), destination, SV("stream"))) == CNA_RESULT_SUCCESS);
        RUNTIME_CHECK(SMOKE_API(cna_property_dictionary_clear(destination)) == CNA_RESULT_SUCCESS);
        RUNTIME_CHECK(SMOKE_DROP(cna_property_dictionary_destroy(destination), destination) == CNA_RESULT_SUCCESS);
        RUNTIME_CHECK(SMOKE_DROP(cna_property_dictionary_destroy(columns), columns) == CNA_RESULT_SUCCESS);
        RUNTIME_CHECK(SMOKE_DROP(cna_leaderboard_entry_destroy(entry), entry) == CNA_RESULT_SUCCESS);
        RUNTIME_CHECK(SMOKE_DROP(cna_leaderboard_reader_destroy(reader), reader) == CNA_RESULT_SUCCESS);
        RUNTIME_CHECK(SMOKE_API(cna_property_dictionary_get_stream_size_ext(chain, SV("stream"), &matches, &bytes)) == CNA_RESULT_SUCCESS && matches == CNA_TRUE && bytes == 3);
        /* Equal bytes in a separate service read are a different native stream identity. */
        RUNTIME_CHECK(SMOKE_OWN(cna_leaderboard_reader_read(&identity, 0, 1, &reader), &reader, cna_leaderboard_reader_destroy) == CNA_RESULT_SUCCESS);
        RUNTIME_CHECK(SMOKE_OWN(cna_leaderboard_reader_get_entry_at(reader, 0, &entry), &entry, cna_leaderboard_entry_destroy) == CNA_RESULT_SUCCESS);
        RUNTIME_CHECK(SMOKE_OWN(cna_leaderboard_entry_get_columns(entry, &columns), &columns, cna_property_dictionary_destroy) == CNA_RESULT_SUCCESS);
        RUNTIME_CHECK(SMOKE_API(cna_property_dictionary_contains_pair_ext(chain, SV("stream"), columns, SV("stream"), &matches)) == CNA_RESULT_SUCCESS && matches == CNA_FALSE);
        RUNTIME_CHECK(SMOKE_API(cna_property_dictionary_remove_pair_ext(chain, SV("stream"), columns, SV("stream"), &matches)) == CNA_RESULT_SUCCESS && matches == CNA_FALSE);
    }
    thread_gamer = gamers[2]; thread_dictionary = source;
    RUNTIME_CHECK(cna_test_service_worker(worker));
    RUNTIME_CHECK(SMOKE_DROP(cna_network_gamer_destroy(gamers[4]), gamers[4]) == CNA_RESULT_SUCCESS);
    bytes = 99;
    RUNTIME_CHECK(SMOKE_API(cna_gamer_get_type_name_size(gamers[4], &bytes)) == CNA_RESULT_INVALID_HANDLE && bytes == 99);
    RUNTIME_CHECK(SMOKE_API(cna_gamer_services_configuration_set_override_ext(NULL)) == CNA_RESULT_INVALID_STATE);
    RUNTIME_CHECK(SMOKE_API(cna_gamer_services_configuration_resolve_ext(&resolved)) == CNA_RESULT_INVALID_STATE && memcmp(&saved, &resolved, sizeof(saved)) == 0);
    status = 0;
cleanup:
    status |= cna_smoke_cleanup(&smoke);
    cna_test_service_end();
    puts(status ? "ServiceFamiliesSmoke: FAIL" : "ServiceFamiliesSmoke: PASS (names, pairs, config)");
    return status;
}
