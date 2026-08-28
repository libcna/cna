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

static int text_is(
    const CNA_Result size_result,
    const uint64_t size,
    const char* const actual,
    const char* const expected)
{
    return size_result == CNA_RESULT_SUCCESS && size == (uint64_t)strlen(expected) &&
        memcmp(actual, expected, (size_t)size) == 0;
}

static int completions;

static void on_complete(void* const context)
{
    *(int*)context += 1;
}

static int validate_achievement(void)
{
    CNA_AchievementHandle first = CNA_INVALID_HANDLE;
    CNA_AchievementHandle same = CNA_INVALID_HANDLE;
    CNA_AchievementHandle other = CNA_INVALID_HANDLE;
    CNA_AchievementInfo info = {sizeof(CNA_AchievementInfo), UINT32_C(1), 0, UINT8_C(0), UINT8_C(0),
                                UINT8_C(0), UINT8_C(0), INT64_C(0)};
    CNA_AchievementInfo broken = {sizeof(CNA_AchievementInfo), UINT32_C(9999), 0, UINT8_C(0),
                                  UINT8_C(0), UINT8_C(0), UINT8_C(0), INT64_C(0)};
    CNA_Bool equals = UINT8_C(9);
    uint64_t size = UINT64_C(0);
    char text[64];
    int ok;

    if (cna_achievement_create_ext(view("FirstSteps"), view("First Steps"), view("Begin the game"),
                                   CNA_TRUE, CNA_TRUE, INT64_C(637000000000000000),
                                   &first) != CNA_RESULT_SUCCESS ||
        first == CNA_INVALID_HANDLE ||
        cna_achievement_create_ext(view("FirstSteps"), view("First Steps"), view("Begin the game"),
                                   CNA_TRUE, CNA_TRUE, INT64_C(637000000000000000),
                                   &same) != CNA_RESULT_SUCCESS ||
        cna_achievement_create_ext(view("LastSteps"), view("Last Steps"), view("Finish the game"),
                                   CNA_FALSE, CNA_FALSE, INT64_C(0),
                                   &other) != CNA_RESULT_SUCCESS) {
        return 0;
    }

    ok = cna_achievement_get_info(first, &info) == CNA_RESULT_SUCCESS &&
        info.is_earned == CNA_TRUE && info.display_before_earned == CNA_TRUE &&
        info.earned_date_time_ticks == INT64_C(637000000000000000) &&
        cna_achievement_get_info(first, &broken) == CNA_RESULT_INVALID_ARGUMENT &&
        cna_achievement_get_info(first, 0) == CNA_RESULT_INVALID_ARGUMENT;
    ok = ok && cna_achievement_get_info(other, &info) == CNA_RESULT_SUCCESS &&
        info.is_earned == CNA_FALSE && info.display_before_earned == CNA_FALSE;

    ok = ok && cna_achievement_get_key_size(first, &size) == CNA_RESULT_SUCCESS &&
        text_is(cna_achievement_copy_key(first, text, sizeof(text), &size), size, text,
                "FirstSteps");
    ok = ok && cna_achievement_get_name_size(first, &size) == CNA_RESULT_SUCCESS &&
        text_is(cna_achievement_copy_name(first, text, sizeof(text), &size), size, text,
                "First Steps");
    ok = ok && cna_achievement_get_description_size(first, &size) == CNA_RESULT_SUCCESS &&
        text_is(cna_achievement_copy_description(first, text, sizeof(text), &size), size, text,
                "Begin the game");
    /* Nothing supplies how-to-earn text here, so it is empty rather than absent. */
    ok = ok && cna_achievement_get_how_to_earn_size(first, &size) == CNA_RESULT_SUCCESS &&
        cna_achievement_copy_how_to_earn(first, text, sizeof(text), &size) == CNA_RESULT_SUCCESS;
    ok = ok && cna_achievement_copy_key(first, text, UINT64_C(1), &size) ==
                   CNA_RESULT_BUFFER_TOO_SMALL;

    /* The canonical picture accessor is not implemented and says so. */
    ok = ok && cna_achievement_get_picture_size(first, &size) == CNA_RESULT_NOT_SUPPORTED;

    /* Equality is by value across every field, not by identity. */
    ok = ok && cna_achievement_equals(first, same, &equals) == CNA_RESULT_SUCCESS &&
        equals == CNA_TRUE &&
        cna_achievement_equals(first, other, &equals) == CNA_RESULT_SUCCESS &&
        equals == CNA_FALSE;

    ok = (cna_achievement_destroy(other) == CNA_RESULT_SUCCESS) && ok;
    ok = (cna_achievement_destroy(same) == CNA_RESULT_SUCCESS) && ok;
    ok = (cna_achievement_destroy(first) == CNA_RESULT_SUCCESS) && ok;
    return ok && cna_achievement_destroy(first) == CNA_RESULT_INVALID_HANDLE;
}

static int validate_collection(void)
{
    CNA_AchievementHandle first = CNA_INVALID_HANDLE;
    CNA_AchievementHandle second = CNA_INVALID_HANDLE;
    CNA_AchievementHandle third = CNA_INVALID_HANDLE;
    CNA_AchievementHandle borrowed = CNA_INVALID_HANDLE;
    CNA_AchievementHandle copied[4];
    CNA_AchievementCollectionHandle collection = CNA_INVALID_HANDLE;
    CNA_Bool flag = UINT8_C(9);
    int32_t count = -1;
    int32_t index = -1;
    uint64_t size = UINT64_C(0);
    char text[64];
    int ok;

    if (cna_achievement_create_ext(view("A"), view("Alpha"), view(""), CNA_TRUE, CNA_TRUE,
                                   INT64_C(1), &first) != CNA_RESULT_SUCCESS ||
        cna_achievement_create_ext(view("B"), view("Beta"), view(""), CNA_TRUE, CNA_FALSE,
                                   INT64_C(2), &second) != CNA_RESULT_SUCCESS ||
        cna_achievement_create_ext(view("C"), view("Gamma"), view(""), CNA_FALSE, CNA_FALSE,
                                   INT64_C(3), &third) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    {
        const CNA_AchievementHandle both[2] = {first, second};
        if (cna_achievement_collection_create_ext(both, UINT64_C(2), &collection) !=
                CNA_RESULT_SUCCESS ||
            collection == CNA_INVALID_HANDLE) {
            return 0;
        }
    }

    ok = cna_achievement_collection_get_count(collection, &count) == CNA_RESULT_SUCCESS &&
        count == 2 &&
        cna_achievement_collection_get_is_disposed(collection, &flag) == CNA_RESULT_SUCCESS &&
        flag == CNA_FALSE &&
        cna_achievement_collection_get_is_read_only(collection, &flag) == CNA_RESULT_SUCCESS;

    /* The collection copied the achievements in, so releasing the caller's handle changes nothing. */
    ok = (cna_achievement_destroy(first) == CNA_RESULT_SUCCESS) && ok;
    ok = ok && cna_achievement_collection_get_count(collection, &count) == CNA_RESULT_SUCCESS &&
        count == 2;

    /* A handle answered by the collection is a copy, and it still compares equal to what is held. */
    ok = ok && cna_achievement_collection_get_at(collection, 0, &borrowed) == CNA_RESULT_SUCCESS &&
        borrowed != CNA_INVALID_HANDLE;
    /* The copy has to be sequenced before the comparison: C does not order a call against the
       output it wrote when both are arguments of the same expression. */
    if (ok) {
        const CNA_Result copied_key = cna_achievement_copy_key(borrowed, text, sizeof(text), &size);
        ok = text_is(copied_key, size, text, "A");
    }
    ok = ok && cna_achievement_collection_contains(collection, borrowed, &flag) ==
                   CNA_RESULT_SUCCESS &&
        flag == CNA_TRUE &&
        cna_achievement_collection_index_of(collection, borrowed, &index) == CNA_RESULT_SUCCESS &&
        index == 0;
    ok = ok && cna_achievement_collection_get_at(collection, 2, &borrowed) ==
                   CNA_RESULT_INVALID_ARGUMENT &&
        cna_achievement_collection_get_at(collection, -1, &borrowed) ==
            CNA_RESULT_INVALID_ARGUMENT;

    /* The key lookup finds the same achievement the index does, and refuses an unknown key. */
    {
        CNA_AchievementHandle by_key = CNA_INVALID_HANDLE;
        ok = ok && cna_achievement_collection_get_by_key(collection, view("B"), &by_key) ==
                       CNA_RESULT_SUCCESS;
        if (ok) {
            const CNA_Result copied_key = cna_achievement_copy_key(by_key, text, sizeof(text), &size);
            ok = text_is(copied_key, size, text, "B");
        }
        if (by_key != CNA_INVALID_HANDLE) {
            ok = (cna_achievement_destroy(by_key) == CNA_RESULT_SUCCESS) && ok;
        }
        ok = ok && cna_achievement_collection_get_by_key(collection, view("Nope"), &by_key) !=
                       CNA_RESULT_SUCCESS;
    }

    ok = ok && cna_achievement_collection_add(collection, third) == CNA_RESULT_SUCCESS &&
        cna_achievement_collection_get_count(collection, &count) == CNA_RESULT_SUCCESS &&
        count == 3;
    ok = ok && cna_achievement_collection_insert(collection, 0, third) == CNA_RESULT_SUCCESS &&
        cna_achievement_collection_get_count(collection, &count) == CNA_RESULT_SUCCESS &&
        count == 4 &&
        cna_achievement_collection_insert(collection, 99, third) == CNA_RESULT_INVALID_ARGUMENT;

    /* Removal answers whether anything was found, unlike the gamer collection's. */
    ok = ok && cna_achievement_collection_remove(collection, third, &flag) == CNA_RESULT_SUCCESS &&
        flag == CNA_TRUE &&
        cna_achievement_collection_get_count(collection, &count) == CNA_RESULT_SUCCESS &&
        count == 3;
    ok = ok && cna_achievement_collection_remove_at(collection, 0) == CNA_RESULT_SUCCESS &&
        cna_achievement_collection_get_count(collection, &count) == CNA_RESULT_SUCCESS &&
        count == 2 &&
        cna_achievement_collection_remove_at(collection, 99) == CNA_RESULT_INVALID_ARGUMENT;

    ok = ok && cna_achievement_collection_copy_to(collection, copied, UINT64_C(4), 1, &size) ==
                   CNA_RESULT_SUCCESS &&
        size == UINT64_C(2) && copied[1] != CNA_INVALID_HANDLE && copied[2] != CNA_INVALID_HANDLE;
    if (size == UINT64_C(2)) {
        ok = (cna_achievement_destroy(copied[1]) == CNA_RESULT_SUCCESS) && ok;
        ok = (cna_achievement_destroy(copied[2]) == CNA_RESULT_SUCCESS) && ok;
    }
    ok = ok && cna_achievement_collection_copy_to(collection, copied, UINT64_C(1), 0, &size) ==
                   CNA_RESULT_BUFFER_TOO_SMALL &&
        size == UINT64_C(2);

    ok = ok && cna_achievement_collection_clear(collection) == CNA_RESULT_SUCCESS &&
        cna_achievement_collection_get_count(collection, &count) == CNA_RESULT_SUCCESS &&
        count == 0;

    if (borrowed != CNA_INVALID_HANDLE) {
        ok = (cna_achievement_destroy(borrowed) == CNA_RESULT_SUCCESS) && ok;
    }
    ok = (cna_achievement_destroy(third) == CNA_RESULT_SUCCESS) && ok;
    ok = (cna_achievement_destroy(second) == CNA_RESULT_SUCCESS) && ok;
    ok = (cna_achievement_collection_destroy(collection) == CNA_RESULT_SUCCESS) && ok;
    return ok && cna_achievement_collection_get_count(collection, &count) ==
                     CNA_RESULT_INVALID_HANDLE;
}

/* What a gamer's achievement read answers is what awarding one persisted. */
static int validate_gamer_achievements(const CNA_SignedInGamerHandle gamer)
{
    CNA_AchievementCollectionHandle earned = CNA_INVALID_HANDLE;
    CNA_AchievementCollectionHandle again = CNA_INVALID_HANDLE;
    CNA_AchievementHandle entry = CNA_INVALID_HANDLE;
    CNA_AchievementInfo info = {sizeof(CNA_AchievementInfo), UINT32_C(1), 0, UINT8_C(0), UINT8_C(0),
                                UINT8_C(0), UINT8_C(0), INT64_C(0)};
    int32_t count = -1;
    uint64_t size = UINT64_C(0);
    char text[64];
    int ok;

    if (cna_signed_in_gamer_award_achievement(gamer, view("CnaCApiEarned")) !=
        CNA_RESULT_SUCCESS) {
        return 0;
    }
    if (cna_signed_in_gamer_get_achievements(gamer, &earned) != CNA_RESULT_SUCCESS ||
        earned == CNA_INVALID_HANDLE) {
        return 0;
    }
    ok = cna_achievement_collection_get_count(earned, &count) == CNA_RESULT_SUCCESS && count >= 1;
    ok = ok && cna_achievement_collection_get_by_key(earned, view("CnaCApiEarned"), &entry) ==
                   CNA_RESULT_SUCCESS;
    /* An earned achievement carries only a key, an earned flag and a timestamp: no catalog exists
       here to supply a name, a description or a score. */
    ok = ok && cna_achievement_get_info(entry, &info) == CNA_RESULT_SUCCESS &&
        info.is_earned == CNA_TRUE && info.gamer_score == 0 &&
        info.earned_date_time_ticks > INT64_C(0);
    ok = ok && cna_achievement_get_name_size(entry, &size) == CNA_RESULT_SUCCESS &&
        size == UINT64_C(0);
    if (ok) {
        const CNA_Result copied_key = cna_achievement_copy_key(entry, text, sizeof(text), &size);
        ok = text_is(copied_key, size, text, "CnaCApiEarned");
    }

    /* The asynchronous read is one synchronous call that still invokes the callback. */
    completions = 0;
    ok = ok && cna_signed_in_gamer_begin_get_achievements(gamer, &on_complete, &completions,
                                                          &again) == CNA_RESULT_SUCCESS &&
        completions == 1 && again != CNA_INVALID_HANDLE &&
        cna_achievement_collection_get_count(again, &count) == CNA_RESULT_SUCCESS && count >= 1;

    if (entry != CNA_INVALID_HANDLE) {
        ok = (cna_achievement_destroy(entry) == CNA_RESULT_SUCCESS) && ok;
    }
    if (again != CNA_INVALID_HANDLE) {
        ok = (cna_achievement_collection_destroy(again) == CNA_RESULT_SUCCESS) && ok;
    }
    ok = (cna_achievement_collection_destroy(earned) == CNA_RESULT_SUCCESS) && ok;
    return ok;
}

int main(void)
{
    CNA_SignedInGamerHandle gamer = CNA_INVALID_HANDLE;
    int status = CNA_TEST_FAIL(0);

    if (!validate_achievement()) {
        return CNA_TEST_FAIL(1);
    }
    if (!validate_collection()) {
        return CNA_TEST_FAIL(2);
    }
    if (cna_signed_in_gamer_create_ext(view("CnaCApiAchiever"), CNA_FALSE, CNA_FALSE,
                                       CNA_PLAYER_INDEX_ONE, &gamer) != CNA_RESULT_SUCCESS) {
        return CNA_TEST_FAIL(3);
    }
    if (!validate_gamer_achievements(gamer)) {
        status = CNA_TEST_FAIL(4);
    }
    if (status == 0 && cna_signed_in_gamer_destroy(gamer) != CNA_RESULT_SUCCESS) {
        status = CNA_TEST_FAIL(5);
    }
    return status;
}
