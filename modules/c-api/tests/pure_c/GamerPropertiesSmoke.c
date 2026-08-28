// SPDX-License-Identifier: MS-PL

#include <CNA/C/cna.h>

#include "CnaTestReport.h"

#include <string.h>

static CNA_StringView view(const char* const text)
{
    CNA_StringView result;
    result.data = text;
    result.byte_length = (uint64_t)strlen(text);
    return result;
}

static int kind_of(const CNA_PropertyDictionaryHandle dictionary, const char* const key)
{
    CNA_Bool found = UINT8_C(9);
    CNA_PropertyValueKind kind = UINT32_C(9999);

    if (cna_property_dictionary_try_get_value_kind_ext(dictionary, view(key), &found, &kind) !=
        CNA_RESULT_SUCCESS) {
        return -1;
    }
    return found == CNA_TRUE ? (int)kind : -2;
}

static int validate_game_defaults(const CNA_SignedInGamerHandle gamer)
{
    CNA_GameDefaults defaults;
    CNA_GameDefaults from_gamer;
    CNA_GameDefaults broken;

    if (cna_game_defaults_init(&defaults) != CNA_RESULT_SUCCESS ||
        cna_game_defaults_init(0) != CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    if (defaults.game_difficulty > CNA_GAME_DIFFICULTY_MAXIMUM ||
        defaults.controller_sensitivity > CNA_CONTROLLER_SENSITIVITY_MAXIMUM ||
        defaults.racing_camera_angle > CNA_RACING_CAMERA_ANGLE_MAXIMUM) {
        return 0;
    }
    /* The two colors are optional, and nothing here has chosen one. */
    if (defaults.has_primary_color != CNA_FALSE || defaults.has_secondary_color != CNA_FALSE) {
        return 0;
    }
    from_gamer = defaults;
    if (cna_signed_in_gamer_get_game_defaults(gamer, &from_gamer) != CNA_RESULT_SUCCESS ||
        from_gamer.game_difficulty != defaults.game_difficulty ||
        from_gamer.has_primary_color != CNA_FALSE) {
        return 0;
    }
    broken = defaults;
    broken.struct_version = UINT32_C(9999);
    return cna_signed_in_gamer_get_game_defaults(gamer, &broken) == CNA_RESULT_INVALID_ARGUMENT &&
        cna_signed_in_gamer_get_game_defaults(gamer, 0) == CNA_RESULT_INVALID_ARGUMENT;
}

static int validate_typed_values(const CNA_PropertyDictionaryHandle dictionary)
{
    int64_t ticks = INT64_C(-1);
    double doubled = -1.0;
    int32_t small = -1;
    int64_t large = INT64_C(-1);
    CNA_LeaderboardOutcome outcome = UINT32_C(9999);
    float single = -1.0F;
    uint64_t size = UINT64_C(0);
    char text[64];
    int ok;

    ok = cna_property_dictionary_set_date_time_ticks(dictionary, view("when"),
                                                     INT64_C(637000000000000000)) ==
            CNA_RESULT_SUCCESS &&
        cna_property_dictionary_set_double(dictionary, view("ratio"), 0.5) == CNA_RESULT_SUCCESS &&
        cna_property_dictionary_set_int32(dictionary, view("lives"), 3) == CNA_RESULT_SUCCESS &&
        cna_property_dictionary_set_int64(dictionary, view("score"), INT64_C(9000000000)) ==
            CNA_RESULT_SUCCESS &&
        cna_property_dictionary_set_outcome(dictionary, view("result"),
                                            CNA_LEADERBOARD_OUTCOME_WIN) == CNA_RESULT_SUCCESS &&
        cna_property_dictionary_set_single(dictionary, view("speed"), 1.25F) ==
            CNA_RESULT_SUCCESS &&
        cna_property_dictionary_set_string(dictionary, view("mode"), view("arcade")) ==
            CNA_RESULT_SUCCESS &&
        cna_property_dictionary_set_time_span_ticks(dictionary, view("elapsed"),
                                                    INT64_C(1000000)) == CNA_RESULT_SUCCESS;

    ok = ok && kind_of(dictionary, "when") == (int)CNA_PROPERTY_VALUE_KIND_DATE_TIME &&
        kind_of(dictionary, "ratio") == (int)CNA_PROPERTY_VALUE_KIND_DOUBLE &&
        kind_of(dictionary, "lives") == (int)CNA_PROPERTY_VALUE_KIND_INT32 &&
        kind_of(dictionary, "score") == (int)CNA_PROPERTY_VALUE_KIND_INT64 &&
        kind_of(dictionary, "result") == (int)CNA_PROPERTY_VALUE_KIND_OUTCOME &&
        kind_of(dictionary, "speed") == (int)CNA_PROPERTY_VALUE_KIND_SINGLE &&
        kind_of(dictionary, "mode") == (int)CNA_PROPERTY_VALUE_KIND_STRING &&
        kind_of(dictionary, "elapsed") == (int)CNA_PROPERTY_VALUE_KIND_TIME_SPAN;
    /* A key that is not there is an ordinary success with the flag clear. */
    ok = ok && kind_of(dictionary, "absent") == -2;

    ok = ok && cna_property_dictionary_get_date_time_ticks(dictionary, view("when"), &ticks) ==
                   CNA_RESULT_SUCCESS &&
        ticks == INT64_C(637000000000000000);
    ok = ok && cna_property_dictionary_get_double(dictionary, view("ratio"), &doubled) ==
                   CNA_RESULT_SUCCESS &&
        doubled == 0.5;
    ok = ok && cna_property_dictionary_get_int32(dictionary, view("lives"), &small) ==
                   CNA_RESULT_SUCCESS &&
        small == 3;
    ok = ok && cna_property_dictionary_get_int64(dictionary, view("score"), &large) ==
                   CNA_RESULT_SUCCESS &&
        large == INT64_C(9000000000);
    ok = ok && cna_property_dictionary_get_outcome(dictionary, view("result"), &outcome) ==
                   CNA_RESULT_SUCCESS &&
        outcome == CNA_LEADERBOARD_OUTCOME_WIN;
    ok = ok && cna_property_dictionary_get_single(dictionary, view("speed"), &single) ==
                   CNA_RESULT_SUCCESS &&
        single == 1.25F;
    ok = ok && cna_property_dictionary_get_time_span_ticks(dictionary, view("elapsed"), &ticks) ==
                   CNA_RESULT_SUCCESS &&
        ticks == INT64_C(1000000);

    ok = ok && cna_property_dictionary_get_string_size(dictionary, view("mode"), &size) ==
                   CNA_RESULT_SUCCESS &&
        size == UINT64_C(6);
    if (ok) {
        const CNA_Result copied =
            cna_property_dictionary_copy_string(dictionary, view("mode"), text, sizeof(text), &size);
        ok = copied == CNA_RESULT_SUCCESS && size == UINT64_C(6) &&
            memcmp(text, "arcade", (size_t)6) == 0;
    }
    ok = ok && cna_property_dictionary_copy_string(dictionary, view("mode"), text, UINT64_C(1),
                                                   &size) == CNA_RESULT_BUFFER_TOO_SMALL;

    /* Reading a slot with the wrong getter is a state failure, not a generic internal one. */
    ok = ok && cna_property_dictionary_get_int32(dictionary, view("mode"), &small) ==
                   CNA_RESULT_INVALID_STATE &&
        cna_property_dictionary_get_string_size(dictionary, view("lives"), &size) ==
            CNA_RESULT_INVALID_STATE;
    /* An unknown key is an argument failure, which is a different answer. */
    ok = ok && cna_property_dictionary_get_int32(dictionary, view("absent"), &small) ==
                   CNA_RESULT_INVALID_ARGUMENT;

    /* Nothing here stores a stream, so the stream getter finds a slot of the wrong kind. */
    {
        CNA_Bool has_stream = UINT8_C(9);
        ok = ok && cna_property_dictionary_get_stream_size_ext(dictionary, view("mode"),
                                                               &has_stream, &size) ==
                       CNA_RESULT_INVALID_STATE;
    }

    /* Writing replaces whatever the slot held, including a value of another kind. */
    ok = ok && cna_property_dictionary_set_string(dictionary, view("lives"), view("three")) ==
                   CNA_RESULT_SUCCESS &&
        kind_of(dictionary, "lives") == (int)CNA_PROPERTY_VALUE_KIND_STRING;
    ok = ok && cna_property_dictionary_set_int32(dictionary, view("lives"), 3) ==
                   CNA_RESULT_SUCCESS &&
        kind_of(dictionary, "lives") == (int)CNA_PROPERTY_VALUE_KIND_INT32;

    return ok && cna_property_dictionary_set_outcome(dictionary, view("result"),
                                                     UINT32_C(9999)) == CNA_RESULT_INVALID_ARGUMENT;
}

static int validate_enumeration(const CNA_PropertyDictionaryHandle dictionary)
{
    int32_t count = -1;
    uint64_t size = UINT64_C(0);
    char text[64];
    CNA_Bool flag = UINT8_C(9);
    int32_t index;
    int ok;

    ok = cna_property_dictionary_get_count(dictionary, &count) == CNA_RESULT_SUCCESS && count == 8;
    /* The canonical dictionary reports itself read-only and is nonetheless writable. */
    ok = ok && cna_property_dictionary_get_is_read_only(dictionary, &flag) == CNA_RESULT_SUCCESS &&
        flag == CNA_TRUE;
    ok = ok && cna_property_dictionary_contains_key(dictionary, view("mode"), &flag) ==
                   CNA_RESULT_SUCCESS &&
        flag == CNA_TRUE &&
        cna_property_dictionary_contains_key(dictionary, view("absent"), &flag) ==
            CNA_RESULT_SUCCESS &&
        flag == CNA_FALSE;

    /* Keys come back in ascending order, so the first one is "elapsed". */
    ok = ok && cna_property_dictionary_get_key_size_at(dictionary, 0, &size) == CNA_RESULT_SUCCESS;
    if (ok) {
        const CNA_Result copied =
            cna_property_dictionary_copy_key_at(dictionary, 0, text, sizeof(text), &size);
        ok = copied == CNA_RESULT_SUCCESS && size == UINT64_C(7) &&
            memcmp(text, "elapsed", (size_t)7) == 0;
    }
    for (index = 0; ok && index < count; ++index) {
        ok = cna_property_dictionary_get_key_size_at(dictionary, index, &size) ==
                 CNA_RESULT_SUCCESS &&
            size != UINT64_C(0) && size <= sizeof(text) &&
            cna_property_dictionary_copy_key_at(dictionary, index, text, sizeof(text), &size) ==
                CNA_RESULT_SUCCESS;
    }
    ok = ok && cna_property_dictionary_get_key_size_at(dictionary, count, &size) ==
                   CNA_RESULT_INVALID_ARGUMENT &&
        cna_property_dictionary_get_key_size_at(dictionary, -1, &size) ==
            CNA_RESULT_INVALID_ARGUMENT;

    ok = ok && cna_property_dictionary_remove(dictionary, view("mode"), &flag) ==
                   CNA_RESULT_SUCCESS &&
        flag == CNA_TRUE &&
        cna_property_dictionary_remove(dictionary, view("mode"), &flag) == CNA_RESULT_SUCCESS &&
        flag == CNA_FALSE &&
        cna_property_dictionary_get_count(dictionary, &count) == CNA_RESULT_SUCCESS && count == 7;

    return ok && cna_property_dictionary_clear(dictionary) == CNA_RESULT_SUCCESS &&
        cna_property_dictionary_get_count(dictionary, &count) == CNA_RESULT_SUCCESS && count == 0;
}

int main(void)
{
    CNA_SignedInGamerHandle gamer = CNA_INVALID_HANDLE;
    CNA_PropertyDictionaryHandle dictionary = CNA_INVALID_HANDLE;
    int status = CNA_TEST_FAIL(0);

    if (cna_signed_in_gamer_create_ext(view("CnaCApiProperties"), CNA_FALSE, CNA_FALSE,
                                       CNA_PLAYER_INDEX_ONE, &gamer) != CNA_RESULT_SUCCESS) {
        return CNA_TEST_FAIL(1);
    }
    if (!validate_game_defaults(gamer)) {
        status = CNA_TEST_FAIL(2);
    }
    if (status == 0 &&
        (cna_property_dictionary_create_ext(&dictionary) != CNA_RESULT_SUCCESS ||
         dictionary == CNA_INVALID_HANDLE ||
         cna_property_dictionary_create_ext(0) != CNA_RESULT_INVALID_ARGUMENT)) {
        status = CNA_TEST_FAIL(3);
    }
    if (status == 0 && !validate_typed_values(dictionary)) {
        status = CNA_TEST_FAIL(4);
    }
    if (status == 0 && !validate_enumeration(dictionary)) {
        status = CNA_TEST_FAIL(5);
    }
    if (status == 0 &&
        (cna_property_dictionary_destroy(dictionary) != CNA_RESULT_SUCCESS ||
         cna_property_dictionary_get_count(dictionary, 0) != CNA_RESULT_INVALID_ARGUMENT)) {
        status = CNA_TEST_FAIL(6);
    }
    if (status == 0 && cna_signed_in_gamer_destroy(gamer) != CNA_RESULT_SUCCESS) {
        status = CNA_TEST_FAIL(7);
    }
    return status;
}
