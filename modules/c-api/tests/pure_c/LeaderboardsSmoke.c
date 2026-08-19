// SPDX-License-Identifier: MS-PL

#include <CNA/C/cna.h>

#include <string.h>

static CNA_StringView view(const char* const text)
{
    CNA_StringView result;
    result.data = text;
    result.byte_length = (uint64_t)strlen(text);
    return result;
}

static int completions;

static void on_complete(void* const context)
{
    *(int*)context += 1;
}

static int rating_changes;

static void on_rating_changed(void* const context)
{
    *(int*)context += 1;
}

static int validate_identity(void)
{
    CNA_LeaderboardIdentity identity;
    CNA_LeaderboardIdentity broken;

    if (cna_leaderboard_identity_init(CNA_LEADERBOARD_KEY_BEST_SCORE_LIFE_TIME, 0, &identity) !=
            CNA_RESULT_SUCCESS ||
        cna_leaderboard_identity_init(CNA_LEADERBOARD_KEY_BEST_SCORE_LIFE_TIME, 0, 0) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_leaderboard_identity_init(UINT32_C(9999), 0, &identity) !=
            CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    if (identity.game_mode != 0 || strcmp(identity.key, "BestScoreLifeTime") != 0) {
        return 0;
    }
    /* The game mode is part of the identity, and the same key with another mode is another
       leaderboard. */
    if (cna_leaderboard_identity_init(CNA_LEADERBOARD_KEY_BEST_TIME_RECENT, 7, &identity) !=
            CNA_RESULT_SUCCESS ||
        identity.game_mode != 7 || strcmp(identity.key, "BestTimeRecent") != 0) {
        return 0;
    }
    /* An identity whose inline key has no terminator is refused rather than read past its end. */
    broken = identity;
    memset(broken.key, 'x', sizeof(broken.key));
    {
        CNA_LeaderboardReaderHandle reader = CNA_INVALID_HANDLE;
        if (cna_leaderboard_reader_read(&broken, 0, 4, &reader) != CNA_RESULT_INVALID_ARGUMENT) {
            return 0;
        }
    }
    broken = identity;
    broken.struct_version = UINT32_C(9999);
    {
        CNA_LeaderboardReaderHandle reader = CNA_INVALID_HANDLE;
        return cna_leaderboard_reader_read(&broken, 0, 4, &reader) == CNA_RESULT_INVALID_ARGUMENT &&
            cna_leaderboard_reader_read(0, 0, 4, &reader) == CNA_RESULT_INVALID_ARGUMENT;
    }
}

static int validate_entry(const CNA_SignedInGamerHandle gamer)
{
    CNA_LeaderboardEntryHandle entry = CNA_INVALID_HANDLE;
    CNA_LeaderboardEntryHandle same = CNA_INVALID_HANDLE;
    CNA_LeaderboardEntryHandle other = CNA_INVALID_HANDLE;
    CNA_LeaderboardEntryInfo info = {sizeof(CNA_LeaderboardEntryInfo), UINT32_C(1), 0, UINT8_C(0),
                                     {0U, 0U, 0U}, INT64_C(0)};
    CNA_PropertyDictionaryHandle columns = CNA_INVALID_HANDLE;
    CNA_GamerHandle borrowed = CNA_INVALID_HANDLE;
    CNA_Bool flag = UINT8_C(9);
    int32_t count = -1;
    int ok;

    if (cna_leaderboard_entry_create_ext(gamer, INT64_C(100), 1, &entry) != CNA_RESULT_SUCCESS ||
        entry == CNA_INVALID_HANDLE ||
        cna_leaderboard_entry_create_ext(gamer, INT64_C(100), 1, &same) != CNA_RESULT_SUCCESS ||
        cna_leaderboard_entry_create_ext(CNA_INVALID_HANDLE, INT64_C(50), 2, &other) !=
            CNA_RESULT_SUCCESS) {
        return 0;
    }

    ok = cna_leaderboard_entry_get_info(entry, &info) == CNA_RESULT_SUCCESS &&
        info.rating == INT64_C(100) && info.ranking == 1 && info.has_gamer == CNA_TRUE;
    /* An entry with no gamer is an ordinary success with the flag clear. */
    ok = ok && cna_leaderboard_entry_get_info(other, &info) == CNA_RESULT_SUCCESS &&
        info.has_gamer == CNA_FALSE &&
        cna_leaderboard_entry_get_gamer(other, &flag, &borrowed) == CNA_RESULT_SUCCESS &&
        flag == CNA_FALSE;
    ok = ok && cna_leaderboard_entry_get_gamer(entry, &flag, &borrowed) == CNA_RESULT_SUCCESS &&
        flag == CNA_TRUE && borrowed != CNA_INVALID_HANDLE;
    if (borrowed != CNA_INVALID_HANDLE) {
        /* The borrowed view is a handle of its own; releasing it does not touch the gamer. */
        ok = (cna_gamer_destroy(borrowed) == CNA_RESULT_SUCCESS) && ok;
    }

    /* Entries compare by gamer, rating and ranking -- not by their columns. */
    ok = ok && cna_leaderboard_entry_equals(entry, same, &flag) == CNA_RESULT_SUCCESS &&
        flag == CNA_TRUE &&
        cna_leaderboard_entry_equals(entry, other, &flag) == CNA_RESULT_SUCCESS &&
        flag == CNA_FALSE;

    /* The hook is one per entry and runs after each write. */
    rating_changes = 0;
    ok = ok && cna_leaderboard_entry_set_rating_changed_hook_ext(entry, &on_rating_changed,
                                                                 &rating_changes) ==
                   CNA_RESULT_SUCCESS &&
        cna_leaderboard_entry_set_rating(entry, INT64_C(250)) == CNA_RESULT_SUCCESS &&
        rating_changes == 1 &&
        cna_leaderboard_entry_get_info(entry, &info) == CNA_RESULT_SUCCESS &&
        info.rating == INT64_C(250);
    ok = ok && cna_leaderboard_entry_set_rating_changed_hook_ext(entry, 0, 0) ==
                   CNA_RESULT_SUCCESS &&
        cna_leaderboard_entry_set_rating(entry, INT64_C(300)) == CNA_RESULT_SUCCESS &&
        rating_changes == 1;
    /* Writing changed the entry, and equality now says so. */
    ok = ok && cna_leaderboard_entry_equals(entry, same, &flag) == CNA_RESULT_SUCCESS &&
        flag == CNA_FALSE;

    /* The columns are the entry's own, so writing through the dictionary changes the entry. */
    ok = ok && cna_leaderboard_entry_get_columns(entry, &columns) == CNA_RESULT_SUCCESS &&
        columns != CNA_INVALID_HANDLE &&
        cna_property_dictionary_set_int32(columns, view("laps"), 4) == CNA_RESULT_SUCCESS;
    if (columns != CNA_INVALID_HANDLE) {
        ok = (cna_property_dictionary_destroy(columns) == CNA_RESULT_SUCCESS) && ok;
    }
    columns = CNA_INVALID_HANDLE;
    ok = ok && cna_leaderboard_entry_get_columns(entry, &columns) == CNA_RESULT_SUCCESS &&
        cna_property_dictionary_get_count(columns, &count) == CNA_RESULT_SUCCESS && count == 1;
    if (columns != CNA_INVALID_HANDLE) {
        ok = (cna_property_dictionary_destroy(columns) == CNA_RESULT_SUCCESS) && ok;
    }

    ok = (cna_leaderboard_entry_destroy(other) == CNA_RESULT_SUCCESS) && ok;
    ok = (cna_leaderboard_entry_destroy(same) == CNA_RESULT_SUCCESS) && ok;
    ok = (cna_leaderboard_entry_destroy(entry) == CNA_RESULT_SUCCESS) && ok;
    return ok && cna_leaderboard_entry_destroy(entry) == CNA_RESULT_INVALID_HANDLE;
}

static int validate_reader(const CNA_SignedInGamerHandle gamer)
{
    CNA_LeaderboardIdentity identity;
    CNA_LeaderboardIdentity read_back;
    CNA_LeaderboardReaderHandle reader = CNA_INVALID_HANDLE;
    CNA_LeaderboardReaderHandle from_pivot = CNA_INVALID_HANDLE;
    CNA_LeaderboardReaderHandle from_gamers = CNA_INVALID_HANDLE;
    CNA_LeaderboardEntryHandle entry = CNA_INVALID_HANDLE;
    CNA_LeaderboardReaderInfo info = {sizeof(CNA_LeaderboardReaderInfo), UINT32_C(1), 0, 0, 0,
                                      UINT8_C(0), UINT8_C(0), UINT8_C(0), UINT8_C(0)};
    const CNA_GamerHandle gamers[1] = {gamer};
    int ok;

    if (cna_leaderboard_identity_init(CNA_LEADERBOARD_KEY_BEST_SCORE_LIFE_TIME, 0, &identity) !=
        CNA_RESULT_SUCCESS) {
        return 0;
    }
    /* The read is complete when it returns: nothing here defers. */
    if (cna_leaderboard_reader_read(&identity, 0, 4, &reader) != CNA_RESULT_SUCCESS ||
        reader == CNA_INVALID_HANDLE) {
        return 0;
    }

    ok = cna_leaderboard_reader_get_info(reader, &info) == CNA_RESULT_SUCCESS &&
        info.is_disposed == CNA_FALSE && info.page_start == 0 && info.entry_count >= 0 &&
        info.total_leaderboard_size >= info.entry_count;
    read_back = identity;
    ok = ok && cna_leaderboard_reader_get_identity(reader, &read_back) == CNA_RESULT_SUCCESS &&
        strcmp(read_back.key, identity.key) == 0 && read_back.game_mode == identity.game_mode;

    /* An entry index outside the page is refused; a leaderboard nobody has written is empty. */
    ok = ok && cna_leaderboard_reader_get_entry_at(reader, info.entry_count, &entry) ==
                   CNA_RESULT_INVALID_ARGUMENT &&
        cna_leaderboard_reader_get_entry_at(reader, -1, &entry) == CNA_RESULT_INVALID_ARGUMENT;

    /* Paging is refused when there is no page to move to, which is a state failure. */
    if (ok && info.can_page_down == CNA_FALSE) {
        ok = cna_leaderboard_reader_page_down(reader) == CNA_RESULT_INVALID_STATE;
        /* CBIND-065: the callback form of the same refusal. Its page-up twin was asserted below
           and this one never was, while the matrix credited both to this file. A refused begin
           must not run the callback -- an asynchronous route that reports failure *and* completes
           would have a caller handling the same operation twice. */
        completions = 0;
        ok = ok && cna_leaderboard_reader_begin_page_down(reader, &on_complete, &completions) ==
                       CNA_RESULT_INVALID_STATE &&
            completions == 0;
    }
    if (ok && info.can_page_up == CNA_FALSE) {
        ok = cna_leaderboard_reader_page_up(reader) == CNA_RESULT_INVALID_STATE;
        completions = 0;
        ok = ok && cna_leaderboard_reader_begin_page_up(reader, &on_complete, &completions) ==
                       CNA_RESULT_INVALID_STATE &&
            completions == 0;
    }

    /* All three read shapes answer a reader, and the callback form runs its callback once. */
    completions = 0;
    ok = ok && cna_leaderboard_reader_read_from_pivot(&identity, gamer, 4, &from_pivot) ==
                   CNA_RESULT_SUCCESS &&
        from_pivot != CNA_INVALID_HANDLE;
    ok = ok && cna_leaderboard_reader_read_from_gamers(&identity, gamers, UINT64_C(1), gamer, 4,
                                                       &from_gamers) == CNA_RESULT_SUCCESS &&
        from_gamers != CNA_INVALID_HANDLE;
    if (from_gamers != CNA_INVALID_HANDLE) {
        ok = (cna_leaderboard_reader_destroy(from_gamers) == CNA_RESULT_SUCCESS) && ok;
        from_gamers = CNA_INVALID_HANDLE;
    }
    ok = ok && cna_leaderboard_reader_begin_read(&identity, 0, 4, &on_complete, &completions,
                                                 &from_gamers) == CNA_RESULT_SUCCESS &&
        completions == 1;
    if (from_gamers != CNA_INVALID_HANDLE) {
        ok = (cna_leaderboard_reader_destroy(from_gamers) == CNA_RESULT_SUCCESS) && ok;
        from_gamers = CNA_INVALID_HANDLE;
    }
    ok = ok && cna_leaderboard_reader_begin_read_from_pivot(&identity, gamer, 4, &on_complete,
                                                            &completions, &from_gamers) ==
                   CNA_RESULT_SUCCESS &&
        completions == 2;
    if (from_gamers != CNA_INVALID_HANDLE) {
        ok = (cna_leaderboard_reader_destroy(from_gamers) == CNA_RESULT_SUCCESS) && ok;
        from_gamers = CNA_INVALID_HANDLE;
    }
    ok = ok && cna_leaderboard_reader_begin_read_from_gamers(&identity, gamers, UINT64_C(1), gamer,
                                                             4, 0, 0, &from_gamers) ==
                   CNA_RESULT_SUCCESS;
    if (from_gamers != CNA_INVALID_HANDLE) {
        ok = (cna_leaderboard_reader_destroy(from_gamers) == CNA_RESULT_SUCCESS) && ok;
    }
    ok = ok && cna_leaderboard_reader_read_from_gamers(&identity, 0, UINT64_C(1), gamer, 4,
                                                       &from_gamers) == CNA_RESULT_INVALID_ARGUMENT;

    if (from_pivot != CNA_INVALID_HANDLE) {
        ok = (cna_leaderboard_reader_destroy(from_pivot) == CNA_RESULT_SUCCESS) && ok;
    }
    ok = (cna_leaderboard_reader_destroy(reader) == CNA_RESULT_SUCCESS) && ok;
    return ok && cna_leaderboard_reader_get_info(reader, &info) == CNA_RESULT_INVALID_HANDLE;
}

int main(void)
{
    CNA_SignedInGamerHandle gamer = CNA_INVALID_HANDLE;
    int status = 0;

    if (!validate_identity()) {
        return 1;
    }
    if (cna_signed_in_gamer_create_ext(view("CnaCApiRacer"), CNA_FALSE, CNA_FALSE,
                                       CNA_PLAYER_INDEX_ONE, &gamer) != CNA_RESULT_SUCCESS) {
        return 2;
    }
    if (!validate_entry(gamer)) {
        status = 3;
    }
    if (status == 0 && !validate_reader(gamer)) {
        status = 4;
    }
    if (status == 0 && cna_signed_in_gamer_destroy(gamer) != CNA_RESULT_SUCCESS) {
        status = 5;
    }
    return status;
}
