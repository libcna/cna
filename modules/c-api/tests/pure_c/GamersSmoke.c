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

static int sign_ins;

static void on_signed_in(void* const context, const CNA_SignedInGamerEventInfo* const info)
{
    if (info != 0 && info->gamer != CNA_INVALID_HANDLE) {
        *(int*)context += 1;
    }
}

/* Every cna_gamer_* route reaches the canonical base, so the same routes have to work through a
   signed-in gamer handle and through a friend handle. */
static int validate_base_surface(const CNA_GamerHandle gamer, const char* const gamertag)
{
    uint64_t size = UINT64_C(0);
    char text[64];
    uint64_t tag = UINT64_C(99);
    CNA_Bool disposed = UINT8_C(9);

    if (cna_gamer_get_gamertag_size(gamer, &size) != CNA_RESULT_SUCCESS || size > sizeof(text) ||
        !text_is(cna_gamer_copy_gamertag(gamer, text, sizeof(text), &size), size, text, gamertag)) {
        return 0;
    }
    if (cna_gamer_set_display_name(gamer, view("Renamed")) != CNA_RESULT_SUCCESS ||
        cna_gamer_get_display_name_size(gamer, &size) != CNA_RESULT_SUCCESS ||
        !text_is(cna_gamer_copy_display_name(gamer, text, sizeof(text), &size), size, text,
                 "Renamed")) {
        return 0;
    }
    /* A gamer's text form is its display name, not its gamertag. */
    if (cna_gamer_get_text_size(gamer, &size) != CNA_RESULT_SUCCESS ||
        !text_is(cna_gamer_copy_text(gamer, text, sizeof(text), &size), size, text, "Renamed")) {
        return 0;
    }
    if (cna_gamer_copy_gamertag(gamer, text, UINT64_C(1), &size) != CNA_RESULT_BUFFER_TOO_SMALL) {
        return 0;
    }
    if (cna_gamer_get_is_disposed(gamer, &disposed) != CNA_RESULT_SUCCESS ||
        disposed != CNA_FALSE) {
        return 0;
    }
    /* A tag this ABI never wrote reads back as zero rather than as a reinterpreted box. */
    if (cna_gamer_get_tag(gamer, &tag) != CNA_RESULT_SUCCESS || tag != UINT64_C(0) ||
        cna_gamer_set_tag(gamer, UINT64_C(0x1234567890ABCDEF)) != CNA_RESULT_SUCCESS ||
        cna_gamer_get_tag(gamer, &tag) != CNA_RESULT_SUCCESS ||
        tag != UINT64_C(0x1234567890ABCDEF)) {
        return 0;
    }
    return cna_gamer_get_gamertag_size(gamer, 0) == CNA_RESULT_INVALID_ARGUMENT;
}

static int validate_profile(const CNA_GamerHandle gamer)
{
    CNA_GamerProfileHandle profile = CNA_INVALID_HANDLE;
    CNA_GamerProfileHandle async_profile = CNA_INVALID_HANDLE;
    CNA_GamerProfileInfo info = {sizeof(CNA_GamerProfileInfo), UINT32_C(1), 0, UINT32_C(0), 0, 0,
                                 0.0F, UINT8_C(0), {0U, 0U, 0U}};
    CNA_GamerProfileInfo broken = {sizeof(CNA_GamerProfileInfo), UINT32_C(9999), 0, UINT32_C(0), 0,
                                   0, 0.0F, UINT8_C(0), {0U, 0U, 0U}};
    CNA_Bool has_picture = UINT8_C(9);
    uint64_t size = UINT64_C(0);
    char text[64];
    int ok;

    if (cna_gamer_get_profile(gamer, &profile) != CNA_RESULT_SUCCESS ||
        profile == CNA_INVALID_HANDLE) {
        return 0;
    }
    ok = cna_gamer_profile_get_info(profile, &info) == CNA_RESULT_SUCCESS &&
        info.is_disposed == CNA_FALSE && info.gamer_zone <= CNA_GAMER_ZONE_MAXIMUM &&
        cna_gamer_profile_get_info(profile, &broken) == CNA_RESULT_INVALID_ARGUMENT;

    ok = ok && cna_gamer_profile_get_motto_size(profile, &size) == CNA_RESULT_SUCCESS &&
        size <= sizeof(text) &&
        cna_gamer_profile_copy_motto(profile, text, sizeof(text), &size) == CNA_RESULT_SUCCESS;
    ok = ok && cna_gamer_profile_get_region_name_size(profile, &size) == CNA_RESULT_SUCCESS &&
        size <= sizeof(text) &&
        cna_gamer_profile_copy_region_name(profile, text, sizeof(text), &size) ==
            CNA_RESULT_SUCCESS;

    /* No runtime here carries a gamer picture, so the flag is clear and that is a success. */
    ok = ok && cna_gamer_profile_get_picture_size(profile, &has_picture, &size) ==
                   CNA_RESULT_SUCCESS &&
        has_picture == CNA_FALSE && size == UINT64_C(0);

    /* The asynchronous read is one synchronous call that still invokes the callback. */
    completions = 0;
    ok = ok && cna_gamer_begin_get_profile(gamer, &on_complete, &completions, &async_profile) ==
                   CNA_RESULT_SUCCESS &&
        completions == 1 && async_profile != CNA_INVALID_HANDLE &&
        async_profile != profile;
    /* A null callback is accepted: the caller may only want the answer. */
    ok = ok && cna_gamer_profile_destroy(async_profile) == CNA_RESULT_SUCCESS &&
        cna_gamer_begin_get_profile(gamer, 0, 0, &async_profile) == CNA_RESULT_SUCCESS &&
        cna_gamer_profile_destroy(async_profile) == CNA_RESULT_SUCCESS;

    ok = (cna_gamer_profile_destroy(profile) == CNA_RESULT_SUCCESS) && ok;
    return ok && cna_gamer_profile_get_motto_size(profile, &size) == CNA_RESULT_INVALID_HANDLE;
}

/* Neither lookup can succeed on any runtime this ABI builds on, and the refusal is the answer. */
static int validate_unsupported_lookups(void)
{
    CNA_GamerHandle gamer = CNA_INVALID_HANDLE;
    uint64_t size = UINT64_C(0);
    char text[64];

    completions = 0;
    if (cna_gamer_get_from_gamertag(view("someone"), &gamer) != CNA_RESULT_NOT_SUPPORTED ||
        gamer != CNA_INVALID_HANDLE ||
        cna_gamer_begin_get_from_gamertag(view("someone"), &on_complete, &completions, &gamer) !=
            CNA_RESULT_NOT_SUPPORTED ||
        completions != 0) {
        return 0;
    }
    if (cna_gamer_get_partner_token_size(view("https://example"), &size) !=
            CNA_RESULT_NOT_SUPPORTED ||
        cna_gamer_copy_partner_token(view("https://example"), text, sizeof(text), &size) !=
            CNA_RESULT_NOT_SUPPORTED ||
        cna_gamer_begin_get_partner_token(view("https://example"), &on_complete, &completions, text,
                                          sizeof(text), &size) != CNA_RESULT_NOT_SUPPORTED ||
        completions != 0) {
        return 0;
    }
    return cna_gamer_get_from_gamertag(view("someone"), 0) == CNA_RESULT_INVALID_ARGUMENT &&
        cna_gamer_get_partner_token_size(view("https://example"), 0) ==
            CNA_RESULT_INVALID_ARGUMENT;
}

static int validate_signed_in_surface(const CNA_SignedInGamerHandle gamer)
{
    CNA_GamerPresence presence;
    CNA_GamerPrivileges privileges = {sizeof(CNA_GamerPrivileges), UINT32_C(1), UINT32_C(0),
                                      UINT32_C(0), UINT32_C(0), UINT8_C(0), UINT8_C(0), UINT8_C(0),
                                      UINT8_C(0), {0U, 0U, 0U, 0U}};
    CNA_Bool flag = UINT8_C(9);
    CNA_PlayerIndex player_index = UINT32_C(99);
    int32_t party_size = -1;
    int ok;

    if (cna_gamer_presence_init(&presence) != CNA_RESULT_SUCCESS ||
        cna_gamer_presence_init(0) != CNA_RESULT_INVALID_ARGUMENT ||
        presence.presence_mode != CNA_GAMER_PRESENCE_MODE_NONE) {
        return 0;
    }
    ok = cna_signed_in_gamer_get_is_guest(gamer, &flag) == CNA_RESULT_SUCCESS &&
        cna_signed_in_gamer_get_is_signed_in_to_live(gamer, &flag) == CNA_RESULT_SUCCESS &&
        cna_signed_in_gamer_get_player_index(gamer, &player_index) == CNA_RESULT_SUCCESS &&
        player_index <= CNA_PLAYER_INDEX_FOUR;

    ok = ok && cna_signed_in_gamer_get_party_size(gamer, &party_size) == CNA_RESULT_SUCCESS &&
        cna_signed_in_gamer_set_party_size(gamer, 4) == CNA_RESULT_SUCCESS &&
        cna_signed_in_gamer_get_party_size(gamer, &party_size) == CNA_RESULT_SUCCESS &&
        party_size == 4;

    /* Presence round-trips through the whole value, because the canonical API hands out a mutable
       object rather than taking a new one. */
    presence.presence_mode = CNA_GAMER_PRESENCE_MODE_CORNFLOWER_BLUE;
    presence.presence_value = 7;
    ok = ok && cna_signed_in_gamer_set_presence(gamer, &presence) == CNA_RESULT_SUCCESS &&
        cna_signed_in_gamer_get_presence(gamer, &presence) == CNA_RESULT_SUCCESS &&
        presence.presence_mode == CNA_GAMER_PRESENCE_MODE_CORNFLOWER_BLUE &&
        presence.presence_value == 7;
    presence.presence_mode = UINT32_C(9999);
    ok = ok && cna_signed_in_gamer_set_presence(gamer, &presence) == CNA_RESULT_INVALID_ARGUMENT &&
        cna_signed_in_gamer_set_presence(gamer, 0) == CNA_RESULT_INVALID_ARGUMENT;
    /* The free-text presence extension accepts the text and stores nothing. */
    ok = ok && cna_signed_in_gamer_set_presence_mode_string_ext(gamer, view("Anything")) ==
                   CNA_RESULT_SUCCESS &&
        cna_signed_in_gamer_get_presence(gamer, &presence) == CNA_RESULT_SUCCESS &&
        presence.presence_mode == CNA_GAMER_PRESENCE_MODE_CORNFLOWER_BLUE;

    ok = ok && cna_signed_in_gamer_get_privileges(gamer, &privileges) == CNA_RESULT_SUCCESS &&
        privileges.allow_communication <= CNA_GAMER_PRIVILEGE_SETTING_MAXIMUM &&
        privileges.allow_profile_viewing <= CNA_GAMER_PRIVILEGE_SETTING_MAXIMUM &&
        privileges.allow_user_created_content <= CNA_GAMER_PRIVILEGE_SETTING_MAXIMUM;

    /* Whether a capture device exists is the machine's business, so both answers are correct: a
       flag when one is enumerated, an argument refusal when the index is past the end. */
    {
        const CNA_Result headset = cna_signed_in_gamer_is_headset(gamer, UINT64_C(0), &flag);
        ok = ok && (headset == CNA_RESULT_SUCCESS || headset == CNA_RESULT_INVALID_ARGUMENT);
        ok = ok && cna_signed_in_gamer_is_headset(gamer, UINT64_C(9999), &flag) ==
                       CNA_RESULT_INVALID_ARGUMENT;
    }

    /* Awarding persists locally and reports success; the asynchronous form also runs the callback. */
    completions = 0;
    ok = ok && cna_signed_in_gamer_award_achievement(gamer, view("CnaCApiFirstSteps")) ==
                   CNA_RESULT_SUCCESS &&
        cna_signed_in_gamer_begin_award_achievement(gamer, view("CnaCApiFirstSteps"), &on_complete,
                                                    &completions) == CNA_RESULT_SUCCESS &&
        completions == 1;
    return ok;
}

static int validate_friends(const CNA_SignedInGamerHandle signed_in)
{
    CNA_GamerCollectionHandle empty = CNA_INVALID_HANDLE;
    CNA_GamerCollectionHandle collection = CNA_INVALID_HANDLE;
    CNA_GamerEnumeratorHandle cursor = CNA_INVALID_HANDLE;
    CNA_GamerHandle first = CNA_INVALID_HANDLE;
    CNA_GamerHandle second = CNA_INVALID_HANDLE;
    CNA_GamerHandle borrowed = CNA_INVALID_HANDLE;
    CNA_GamerHandle copied[4];
    CNA_FriendGamerInfo info = {sizeof(CNA_FriendGamerInfo), UINT32_C(1), UINT8_C(0), UINT8_C(0),
                               UINT8_C(0), UINT8_C(0), UINT8_C(0), UINT8_C(0), UINT8_C(0),
                               UINT8_C(0), UINT8_C(0), UINT8_C(0), UINT8_C(0), UINT8_C(0),
                               {0U, 0U, 0U, 0U}};
    CNA_Bool flag = UINT8_C(9);
    int32_t count = -1;
    int32_t index = -1;
    uint64_t size = UINT64_C(0);
    char text[64];
    int ok;

    /* There is no friend service, so the canonical answer is an empty collection -- a success. */
    if (cna_signed_in_gamer_get_friends(signed_in, &empty) != CNA_RESULT_SUCCESS ||
        empty == CNA_INVALID_HANDLE ||
        cna_gamer_collection_get_count(empty, &count) != CNA_RESULT_SUCCESS || count != 0 ||
        cna_gamer_collection_destroy(empty) != CNA_RESULT_SUCCESS) {
        return 0;
    }

    if (cna_friend_gamer_create_ext(view("FriendOne"), view("Friend One"), CNA_TRUE, CNA_TRUE,
                                    CNA_FALSE, CNA_FALSE, CNA_FALSE, CNA_TRUE,
                                    &first) != CNA_RESULT_SUCCESS ||
        cna_friend_gamer_create_ext(view("FriendTwo"), view("Friend Two"), CNA_FALSE, CNA_FALSE,
                                    CNA_TRUE, CNA_TRUE, CNA_TRUE, CNA_FALSE,
                                    &second) != CNA_RESULT_SUCCESS) {
        return 0;
    }

    ok = cna_friend_gamer_get_info(first, &info) == CNA_RESULT_SUCCESS &&
        info.is_online == CNA_TRUE && info.is_playing == CNA_TRUE && info.is_away == CNA_FALSE &&
        info.friend_request_received_from == CNA_TRUE && info.friend_request_sent_to == CNA_FALSE;
    ok = ok && cna_friend_gamer_get_info(second, &info) == CNA_RESULT_SUCCESS &&
        info.is_online == CNA_FALSE && info.is_busy == CNA_TRUE &&
        info.friend_request_sent_to == CNA_TRUE;

    /* A friend's presence is free text, while a signed-in gamer's is a mode and a value. */
    ok = ok && cna_friend_gamer_get_presence_size(first, &size) == CNA_RESULT_SUCCESS &&
        size <= sizeof(text) &&
        cna_friend_gamer_copy_presence(first, text, sizeof(text), &size) == CNA_RESULT_SUCCESS;
    /* The friend-only routes refuse a gamer that is not a friend. */
    ok = ok && cna_friend_gamer_get_info(signed_in, &info) == CNA_RESULT_INVALID_HANDLE;

    ok = ok && validate_base_surface(first, "FriendOne");

    {
        const CNA_GamerHandle both[2] = {first, second};
        ok = ok && cna_friend_collection_create_ext(both, UINT64_C(2), &collection) ==
                       CNA_RESULT_SUCCESS &&
            collection != CNA_INVALID_HANDLE;
    }
    ok = ok && cna_gamer_collection_get_count(collection, &count) == CNA_RESULT_SUCCESS &&
        count == 2;
    ok = ok && cna_gamer_collection_get_at(collection, 0, &borrowed) == CNA_RESULT_SUCCESS &&
        borrowed == first &&
        cna_gamer_collection_get_at(collection, 1, &borrowed) == CNA_RESULT_SUCCESS &&
        borrowed == second;
    /* The canonical indexer validates the range, so an index past the end is refused there. */
    ok = ok && cna_gamer_collection_get_at(collection, 2, &borrowed) ==
                   CNA_RESULT_INVALID_ARGUMENT &&
        cna_gamer_collection_get_at(collection, -1, &borrowed) == CNA_RESULT_INVALID_ARGUMENT;

    ok = ok && cna_gamer_collection_index_of(collection, second, &index) == CNA_RESULT_SUCCESS &&
        index == 1 &&
        cna_gamer_collection_contains(collection, first, &flag) == CNA_RESULT_SUCCESS &&
        flag == CNA_TRUE;

    ok = ok && cna_gamer_collection_copy_to(collection, copied, UINT64_C(4), 1, &size) ==
                   CNA_RESULT_SUCCESS &&
        size == UINT64_C(2) && copied[1] == first && copied[2] == second;
    ok = ok && cna_gamer_collection_copy_to(collection, copied, UINT64_C(1), 0, &size) ==
                   CNA_RESULT_BUFFER_TOO_SMALL &&
        size == UINT64_C(2);

    /* A cursor starts before the first element and walks the collection in order. */
    ok = ok && cna_gamer_collection_create_enumerator(collection, &cursor) == CNA_RESULT_SUCCESS &&
        cna_gamer_enumerator_get_current(cursor, &borrowed) == CNA_RESULT_INVALID_STATE &&
        cna_gamer_enumerator_move_next(cursor, &flag) == CNA_RESULT_SUCCESS && flag == CNA_TRUE &&
        cna_gamer_enumerator_get_current(cursor, &borrowed) == CNA_RESULT_SUCCESS &&
        borrowed == first &&
        cna_gamer_enumerator_move_next(cursor, &flag) == CNA_RESULT_SUCCESS && flag == CNA_TRUE &&
        cna_gamer_enumerator_get_current(cursor, &borrowed) == CNA_RESULT_SUCCESS &&
        borrowed == second &&
        cna_gamer_enumerator_move_next(cursor, &flag) == CNA_RESULT_SUCCESS && flag == CNA_FALSE;
    ok = ok && cna_gamer_enumerator_reset(cursor) == CNA_RESULT_SUCCESS &&
        cna_gamer_enumerator_move_next(cursor, &flag) == CNA_RESULT_SUCCESS && flag == CNA_TRUE &&
        cna_gamer_enumerator_get_current(cursor, &borrowed) == CNA_RESULT_SUCCESS &&
        borrowed == first;
    ok = (cna_gamer_enumerator_destroy(cursor) == CNA_RESULT_SUCCESS) && ok;

    /* Removing a gamer the collection does not hold is a no-op that reports success. */
    ok = ok && cna_gamer_collection_remove(collection, first) == CNA_RESULT_SUCCESS &&
        cna_gamer_collection_get_count(collection, &count) == CNA_RESULT_SUCCESS && count == 1 &&
        cna_gamer_collection_remove(collection, first) == CNA_RESULT_SUCCESS &&
        cna_gamer_collection_get_count(collection, &count) == CNA_RESULT_SUCCESS && count == 1;
    ok = ok && cna_gamer_collection_add(collection, first) == CNA_RESULT_SUCCESS &&
        cna_gamer_collection_get_count(collection, &count) == CNA_RESULT_SUCCESS && count == 2;
    ok = ok && cna_gamer_collection_clear(collection) == CNA_RESULT_SUCCESS &&
        cna_gamer_collection_get_count(collection, &count) == CNA_RESULT_SUCCESS && count == 0;

    ok = ok && cna_friend_collection_get_is_disposed(collection, &flag) == CNA_RESULT_SUCCESS &&
        flag == CNA_FALSE;
    ok = (cna_gamer_collection_destroy(collection) == CNA_RESULT_SUCCESS) && ok;
    ok = ok && cna_gamer_collection_get_count(collection, &count) == CNA_RESULT_INVALID_HANDLE;

    ok = (cna_gamer_destroy(second) == CNA_RESULT_SUCCESS) && ok;
    ok = (cna_gamer_destroy(first) == CNA_RESULT_SUCCESS) && ok;
    return ok && cna_gamer_destroy(first) == CNA_RESULT_INVALID_HANDLE;
}

int main(void)
{
    CNA_SignedInGamerHandle gamer = CNA_INVALID_HANDLE;
    CNA_SignedInGamerHandle published[1];
    CNA_SignedInGamerHandle borrowed = CNA_INVALID_HANDLE;
    CNA_Handle registration = CNA_INVALID_HANDLE;
    CNA_Handle rejected = CNA_INVALID_HANDLE;
    CNA_Bool has_gamer = UINT8_C(9);
    CNA_Bool is_friend = UINT8_C(9);
    int32_t count = -1;

    if (cna_signed_in_gamer_create_ext(view("CnaCApiGamer"), CNA_TRUE, CNA_FALSE,
                                       CNA_PLAYER_INDEX_TWO, &gamer) != CNA_RESULT_SUCCESS ||
        gamer == CNA_INVALID_HANDLE) {
        return CNA_TEST_FAIL(1);
    }
    if (!validate_base_surface(gamer, "CnaCApiGamer")) {
        return CNA_TEST_FAIL(2);
    }
    if (!validate_profile(gamer)) {
        return CNA_TEST_FAIL(3);
    }
    if (!validate_unsupported_lookups()) {
        return CNA_TEST_FAIL(4);
    }
    if (!validate_signed_in_surface(gamer)) {
        return CNA_TEST_FAIL(5);
    }
    if (!validate_friends(gamer)) {
        return CNA_TEST_FAIL(6);
    }

    /* No friend list exists to consult, so the answer is always negative rather than a refusal. */
    if (cna_signed_in_gamer_is_friend(gamer, gamer, &is_friend) != CNA_RESULT_SUCCESS ||
        is_friend != CNA_FALSE) {
        return CNA_TEST_FAIL(7);
    }

    /* Availability is separate from the answer: no gamer at an index is an ordinary success. */
    if (cna_gamer_get_signed_in_gamer_at_player_index(CNA_PLAYER_INDEX_ONE, &has_gamer,
                                                      &borrowed) != CNA_RESULT_SUCCESS ||
        has_gamer != CNA_FALSE || borrowed != CNA_INVALID_HANDLE ||
        cna_gamer_get_signed_in_gamer_at_player_index(UINT32_C(99), &has_gamer, &borrowed) !=
            CNA_RESULT_INVALID_ARGUMENT) {
        return CNA_TEST_FAIL(8);
    }

    published[0] = gamer;
    if (cna_gamer_set_signed_in_gamers_ext(published, UINT64_C(1)) != CNA_RESULT_SUCCESS ||
        cna_gamer_get_signed_in_gamer_count(&count) != CNA_RESULT_SUCCESS || count != 1) {
        return CNA_TEST_FAIL(9);
    }
    /* The canonical indexer is **positional**: it reads the collection at the player index rather
       than searching for the gamer whose own player index matches. One published gamer therefore
       answers at index one, not at the index it was created with. */
    if (cna_gamer_get_signed_in_gamer_at_player_index(CNA_PLAYER_INDEX_ONE, &has_gamer,
                                                      &borrowed) != CNA_RESULT_SUCCESS ||
        has_gamer != CNA_TRUE || borrowed == CNA_INVALID_HANDLE) {
        return CNA_TEST_FAIL(10);
    }
    /* A borrowed view is a handle of its own, and releasing it does not touch the gamer. */
    if (cna_signed_in_gamer_destroy(borrowed) != CNA_RESULT_SUCCESS ||
        cna_gamer_get_signed_in_gamer_count(&count) != CNA_RESULT_SUCCESS || count != 1) {
        return CNA_TEST_FAIL(11);
    }
    /* A player index past the published gamers is still an ordinary success with the flag clear. */
    if (cna_gamer_get_signed_in_gamer_at_player_index(CNA_PLAYER_INDEX_TWO, &has_gamer,
                                                      &borrowed) != CNA_RESULT_SUCCESS ||
        has_gamer != CNA_FALSE ||
        cna_gamer_get_signed_in_gamer_at_player_index(CNA_PLAYER_INDEX_FOUR, &has_gamer,
                                                      &borrowed) != CNA_RESULT_SUCCESS ||
        has_gamer != CNA_FALSE) {
        return CNA_TEST_FAIL(12);
    }

    /* CBIND-044C: the collection's remaining operations. The canonical property returns a
       collection object, which has no C form; what a caller does with it -- count, position,
       membership -- is named instead. Position is not the player index: the one published gamer is
       at position zero whatever player index it carries. */
    {
        CNA_SignedInGamerHandle positional = CNA_INVALID_HANDLE;
        int32_t position = INT32_C(-99);
        CNA_Bool contains = CNA_FALSE;
        if (cna_gamer_get_signed_in_gamer_at(0, &positional) != CNA_RESULT_SUCCESS ||
            positional == CNA_INVALID_HANDLE) {
            return CNA_TEST_FAIL(13);
        }
        if (cna_gamer_signed_in_index_of(positional, &position) != CNA_RESULT_SUCCESS ||
            position != 0 ||
            cna_gamer_signed_in_contains(positional, &contains) != CNA_RESULT_SUCCESS ||
            contains != CNA_TRUE) {
            return CNA_TEST_FAIL(14);
        }
        /* Not being in the collection is an answer, not a failure. */
        CNA_SignedInGamerHandle outsider = CNA_INVALID_HANDLE;
        if (cna_signed_in_gamer_create_ext(view("CnaCApiOutsider"), CNA_FALSE, CNA_FALSE,
                                           CNA_PLAYER_INDEX_THREE, &outsider) !=
                CNA_RESULT_SUCCESS ||
            cna_gamer_signed_in_index_of(outsider, &position) != CNA_RESULT_SUCCESS ||
            position != -1 ||
            cna_gamer_signed_in_contains(outsider, &contains) != CNA_RESULT_SUCCESS ||
            contains != CNA_FALSE ||
            cna_signed_in_gamer_destroy(outsider) != CNA_RESULT_SUCCESS) {
            return CNA_TEST_FAIL(15);
        }
        /* A refused lookup clears its output first, so the refusal probes take a handle of their
           own rather than the live one -- reusing it would destroy the handle under test. */
        CNA_SignedInGamerHandle refused = CNA_INVALID_HANDLE;
        if (cna_gamer_get_signed_in_gamer_at(1, &refused) != CNA_RESULT_INVALID_ARGUMENT ||
            refused != CNA_INVALID_HANDLE ||
            cna_gamer_get_signed_in_gamer_at(-1, &refused) != CNA_RESULT_INVALID_ARGUMENT ||
            cna_gamer_get_signed_in_gamer_at(0, 0) != CNA_RESULT_INVALID_ARGUMENT ||
            cna_gamer_signed_in_index_of(positional, 0) != CNA_RESULT_INVALID_ARGUMENT ||
            cna_gamer_signed_in_contains(positional, 0) != CNA_RESULT_INVALID_ARGUMENT) {
            return CNA_TEST_FAIL(16);
        }
        /* A handle of the wrong family is refused rather than searched for. */
        if (cna_gamer_signed_in_index_of(CNA_INVALID_HANDLE, &position) !=
            CNA_RESULT_INVALID_HANDLE) {
            return CNA_TEST_FAIL(17);
        }
        if (cna_signed_in_gamer_destroy(positional) != CNA_RESULT_SUCCESS) {
            return CNA_TEST_FAIL(18);
        }
    }

    sign_ins = 0;
    if (cna_signed_in_gamer_subscribe_signed_in_ext(&on_signed_in, &sign_ins, &registration) !=
            CNA_RESULT_SUCCESS ||
        registration == CNA_INVALID_HANDLE ||
        cna_signed_in_gamer_subscribe_signed_in_ext(0, &sign_ins, &rejected) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        rejected != CNA_INVALID_HANDLE) {
        return CNA_TEST_FAIL(13);
    }
    if (cna_gamer_unsubscribe_ext(registration) != CNA_RESULT_SUCCESS ||
        cna_gamer_unsubscribe_ext(registration) != CNA_RESULT_INVALID_HANDLE) {
        return CNA_TEST_FAIL(14);
    }
    if (cna_signed_in_gamer_subscribe_signed_out_ext(&on_signed_in, &sign_ins, &registration) !=
            CNA_RESULT_SUCCESS ||
        cna_gamer_unsubscribe_ext(registration) != CNA_RESULT_SUCCESS) {
        return CNA_TEST_FAIL(15);
    }

    if (cna_gamer_set_signed_in_gamers_ext(0, UINT64_C(0)) != CNA_RESULT_SUCCESS) {
        return CNA_TEST_FAIL(16);
    }
    return cna_signed_in_gamer_destroy(gamer) == CNA_RESULT_SUCCESS ? 0 : 17;
}
