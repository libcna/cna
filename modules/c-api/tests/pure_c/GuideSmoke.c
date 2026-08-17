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

static int validate_settings(void)
{
    CNA_Bool flag = UINT8_C(9);
    CNA_NotificationPosition position = UINT32_C(99);

    if (cna_guide_get_is_screen_saver_enabled(&flag) != CNA_RESULT_SUCCESS ||
        cna_guide_set_is_screen_saver_enabled(CNA_TRUE) != CNA_RESULT_SUCCESS ||
        cna_guide_get_is_screen_saver_enabled(&flag) != CNA_RESULT_SUCCESS ||
        cna_guide_set_is_screen_saver_enabled(UINT8_C(9)) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_guide_get_is_screen_saver_enabled(0) != CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    if (cna_guide_set_is_trial_mode(CNA_TRUE) != CNA_RESULT_SUCCESS ||
        cna_guide_get_is_trial_mode(&flag) != CNA_RESULT_SUCCESS || flag != CNA_TRUE ||
        cna_guide_set_is_trial_mode(CNA_FALSE) != CNA_RESULT_SUCCESS ||
        cna_guide_get_is_trial_mode(&flag) != CNA_RESULT_SUCCESS || flag != CNA_FALSE) {
        return 0;
    }
    if (cna_guide_set_is_visible(CNA_TRUE) != CNA_RESULT_SUCCESS ||
        cna_guide_get_is_visible(&flag) != CNA_RESULT_SUCCESS ||
        cna_guide_set_is_visible(CNA_FALSE) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    if (cna_guide_set_notification_position(CNA_NOTIFICATION_POSITION_BOTTOM_RIGHT) !=
            CNA_RESULT_SUCCESS ||
        cna_guide_get_notification_position(&position) != CNA_RESULT_SUCCESS ||
        position != CNA_NOTIFICATION_POSITION_BOTTOM_RIGHT ||
        cna_guide_set_notification_position(UINT32_C(9999)) != CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    return cna_guide_set_simulate_trial_mode(CNA_TRUE) == CNA_RESULT_SUCCESS &&
        cna_guide_get_simulate_trial_mode(&flag) == CNA_RESULT_SUCCESS && flag == CNA_TRUE &&
        cna_guide_set_simulate_trial_mode(CNA_FALSE) == CNA_RESULT_SUCCESS;
}

/* The keyboard input is the one operation in this ABI that genuinely stays pending. */
static int validate_keyboard_input(void)
{
    CNA_Bool flag = UINT8_C(9);
    uint64_t size = UINT64_C(0);
    char text[64];

    /* Nothing has been started, so every read refuses rather than answering an empty string. */
    if (cna_guide_get_has_pending_keyboard_input_ext(&flag) != CNA_RESULT_SUCCESS ||
        flag != CNA_FALSE ||
        cna_guide_get_pending_keyboard_input_title_size_ext(&size) != CNA_RESULT_INVALID_STATE ||
        cna_guide_end_show_keyboard_input_size(&size) != CNA_RESULT_INVALID_STATE ||
        cna_guide_simulate_keyboard_input_cancel_ext() != CNA_RESULT_INVALID_STATE) {
        return 0;
    }
    /* Discarding nothing is not a failure. */
    if (cna_guide_reset_pending_keyboard_input_ext() != CNA_RESULT_SUCCESS) {
        return 0;
    }

    completions = 0;
    if (cna_guide_begin_show_keyboard_input(CNA_PLAYER_INDEX_ONE, view("Name"), view("Type it"),
                                            view("Ada"), CNA_FALSE, &on_complete,
                                            &completions) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    /* The callback has not run: the input is waiting for the user, not already finished. */
    if (completions != 0 ||
        cna_guide_get_has_pending_keyboard_input_ext(&flag) != CNA_RESULT_SUCCESS ||
        flag != CNA_TRUE) {
        return 0;
    }
    /* Only one input may be pending, and the second attempt does not disturb the first. */
    if (cna_guide_begin_show_keyboard_input(CNA_PLAYER_INDEX_ONE, view("Other"), view("No"),
                                            view(""), CNA_FALSE, 0, 0) !=
            CNA_RESULT_INVALID_STATE ||
        cna_guide_get_has_pending_keyboard_input_ext(&flag) != CNA_RESULT_SUCCESS ||
        flag != CNA_TRUE) {
        return 0;
    }
    if (cna_guide_get_pending_keyboard_input_title_size_ext(&size) != CNA_RESULT_SUCCESS ||
        size != UINT64_C(4) ||
        cna_guide_copy_pending_keyboard_input_title_ext(text, sizeof(text), &size) !=
            CNA_RESULT_SUCCESS ||
        memcmp(text, "Name", (size_t)size) != 0) {
        return 0;
    }
    if (cna_guide_get_pending_keyboard_input_description_size_ext(&size) != CNA_RESULT_SUCCESS ||
        cna_guide_copy_pending_keyboard_input_description_ext(text, sizeof(text), &size) !=
            CNA_RESULT_SUCCESS ||
        memcmp(text, "Type it", (size_t)size) != 0) {
        return 0;
    }
    /* Not in password mode, so what is displayed is what was typed. */
    if (cna_guide_get_pending_keyboard_input_display_text_size_ext(&size) != CNA_RESULT_SUCCESS ||
        cna_guide_copy_pending_keyboard_input_display_text_ext(text, sizeof(text), &size) !=
            CNA_RESULT_SUCCESS ||
        size < UINT64_C(3) || memcmp(text, "Ada", (size_t)3) != 0) {
        return 0;
    }
    /* An answer cannot be read before the user has given one. */
    if (cna_guide_end_show_keyboard_input_size(&size) != CNA_RESULT_INVALID_STATE) {
        return 0;
    }

    if (cna_guide_simulate_keyboard_input_cancel_ext() != CNA_RESULT_SUCCESS || completions != 1) {
        return 0;
    }
    /* Cancelling completes the input: nothing is pending, and the answer is now readable. */
    if (cna_guide_get_has_pending_keyboard_input_ext(&flag) != CNA_RESULT_SUCCESS ||
        flag != CNA_FALSE ||
        cna_guide_was_keyboard_input_canceled_ext(&flag) != CNA_RESULT_SUCCESS ||
        flag != CNA_TRUE) {
        return 0;
    }
    /* A cancelled input carries **no** text: the canonical implementation clears what was typed, so
       the cancellation flag is the only thing that separates this from a confirmed empty string. */
    if (cna_guide_end_show_keyboard_input_size(&size) != CNA_RESULT_SUCCESS ||
        size != UINT64_C(0) ||
        cna_guide_end_show_keyboard_input(text, sizeof(text), &size) != CNA_RESULT_SUCCESS ||
        size != UINT64_C(0)) {
        return 0;
    }

    /* Password mode masks what is displayed without changing what was typed. */
    if (cna_guide_begin_show_keyboard_input(CNA_PLAYER_INDEX_TWO, view("Secret"), view("Hidden"),
                                            view("hunter2"), CNA_TRUE, 0, 0) !=
        CNA_RESULT_SUCCESS) {
        return 0;
    }
    if (cna_guide_copy_pending_keyboard_input_display_text_ext(text, sizeof(text), &size) !=
            CNA_RESULT_SUCCESS ||
        (size >= UINT64_C(7) && memcmp(text, "hunter2", (size_t)7) == 0)) {
        return 0;
    }
    /* Discarding never runs the callback: the input did not complete, it was thrown away. */
    completions = 0;
    if (cna_guide_reset_pending_keyboard_input_ext() != CNA_RESULT_SUCCESS || completions != 0 ||
        cna_guide_get_has_pending_keyboard_input_ext(&flag) != CNA_RESULT_SUCCESS ||
        flag != CNA_FALSE) {
        return 0;
    }
    return cna_guide_begin_show_keyboard_input(UINT32_C(99), view(""), view(""), view(""),
                                               CNA_FALSE, 0, 0) == CNA_RESULT_INVALID_ARGUMENT;
}

static int validate_message_box(void)
{
    const CNA_StringView buttons[2] = {{"Yes", UINT64_C(3)}, {"No", UINT64_C(2)}};
    CNA_Bool flag = UINT8_C(9);
    CNA_Bool has_choice = UINT8_C(9);
    int32_t button = -1;

    if (cna_guide_get_has_pending_message_box_ext(&flag) != CNA_RESULT_SUCCESS ||
        flag != CNA_FALSE ||
        cna_guide_end_show_message_box(&has_choice, &button) != CNA_RESULT_INVALID_STATE ||
        cna_guide_get_pending_message_box_focus_button_ext(&button) != CNA_RESULT_INVALID_STATE ||
        cna_guide_simulate_message_box_click_ext(0) != CNA_RESULT_INVALID_STATE) {
        return 0;
    }
    /* An empty button list has no button to ever choose, so it is refused. */
    if (cna_guide_begin_show_message_box(CNA_PLAYER_INDEX_ONE, view("T"), view("B"), buttons,
                                         UINT64_C(0), 0, CNA_MESSAGE_BOX_ICON_NONE, 0,
                                         0) != CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    if (cna_guide_begin_show_message_box(CNA_PLAYER_INDEX_ONE, view("T"), view("B"), buttons,
                                         UINT64_C(2), 0, UINT32_C(9999), 0,
                                         0) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_guide_begin_show_message_box(UINT32_C(99), view("T"), view("B"), buttons, UINT64_C(2),
                                         0, CNA_MESSAGE_BOX_ICON_NONE, 0,
                                         0) != CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }

    completions = 0;
    if (cna_guide_begin_show_message_box(CNA_PLAYER_INDEX_ONE, view("Quit?"), view("Are you sure?"),
                                         buttons, UINT64_C(2), 1, CNA_MESSAGE_BOX_ICON_WARNING,
                                         &on_complete, &completions) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    if (completions != 0 ||
        cna_guide_get_has_pending_message_box_ext(&flag) != CNA_RESULT_SUCCESS ||
        flag != CNA_TRUE ||
        cna_guide_get_pending_message_box_focus_button_ext(&button) != CNA_RESULT_SUCCESS ||
        button != 1) {
        return 0;
    }
    /* Only one box may be pending, and the answer cannot be read before there is one. */
    if (cna_guide_begin_show_message_box(CNA_PLAYER_INDEX_ONE, view("T"), view("B"), buttons,
                                         UINT64_C(2), 0, CNA_MESSAGE_BOX_ICON_NONE, 0, 0) !=
            CNA_RESULT_INVALID_STATE ||
        cna_guide_end_show_message_box(&has_choice, &button) != CNA_RESULT_INVALID_STATE) {
        return 0;
    }
    if (cna_guide_simulate_message_box_click_ext(2) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_guide_simulate_message_box_click_ext(-1) != CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    if (cna_guide_simulate_message_box_click_ext(1) != CNA_RESULT_SUCCESS || completions != 1) {
        return 0;
    }
    if (cna_guide_end_show_message_box(&has_choice, &button) != CNA_RESULT_SUCCESS ||
        has_choice != CNA_TRUE || button != 1 ||
        cna_guide_get_has_pending_message_box_ext(&flag) != CNA_RESULT_SUCCESS ||
        flag != CNA_FALSE) {
        return 0;
    }

    /* Discarding never runs the callback either. */
    completions = 0;
    if (cna_guide_begin_show_message_box(CNA_PLAYER_INDEX_ONE, view("T"), view("B"), buttons,
                                         UINT64_C(2), 0, CNA_MESSAGE_BOX_ICON_ERROR, &on_complete,
                                         &completions) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    return cna_guide_reset_pending_message_box_ext() == CNA_RESULT_SUCCESS && completions == 0 &&
        cna_guide_get_has_pending_message_box_ext(&flag) == CNA_RESULT_SUCCESS &&
        flag == CNA_FALSE &&
        cna_guide_end_show_message_box(0, &button) == CNA_RESULT_INVALID_ARGUMENT;
}

/* Every guide screen is a no-op here, so what a caller can observe is that it is accepted and that
   a bad identity is still refused at the boundary. */
static int validate_screens(const CNA_SignedInGamerHandle gamer)
{
    const CNA_GamerHandle recipients[1] = {gamer};

    if (cna_guide_delay_notifications(INT64_C(1000000)) != CNA_RESULT_SUCCESS ||
        cna_guide_show_compose_message(CNA_PLAYER_INDEX_ONE, view("Hi"), recipients,
                                       UINT64_C(1)) != CNA_RESULT_SUCCESS ||
        cna_guide_show_compose_message(CNA_PLAYER_INDEX_ONE, view("Hi"), 0, UINT64_C(0)) !=
            CNA_RESULT_SUCCESS ||
        cna_guide_show_compose_message(CNA_PLAYER_INDEX_ONE, view("Hi"), 0, UINT64_C(1)) !=
            CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    if (cna_guide_show_friend_request(CNA_PLAYER_INDEX_ONE, gamer) != CNA_RESULT_SUCCESS ||
        cna_guide_show_friends(CNA_PLAYER_INDEX_ONE) != CNA_RESULT_SUCCESS ||
        cna_guide_show_game_invite(CNA_PLAYER_INDEX_ONE, recipients, UINT64_C(1)) !=
            CNA_RESULT_SUCCESS ||
        cna_guide_show_game_invite_for_session(view("session")) != CNA_RESULT_SUCCESS ||
        cna_guide_show_gamer_card(CNA_PLAYER_INDEX_ONE, gamer) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    if (cna_guide_show_marketplace(CNA_PLAYER_INDEX_ONE) != CNA_RESULT_SUCCESS ||
        cna_guide_show_messages(CNA_PLAYER_INDEX_ONE) != CNA_RESULT_SUCCESS ||
        cna_guide_show_party(CNA_PLAYER_INDEX_ONE) != CNA_RESULT_SUCCESS ||
        cna_guide_show_party_sessions(CNA_PLAYER_INDEX_ONE) != CNA_RESULT_SUCCESS ||
        cna_guide_show_player_review(CNA_PLAYER_INDEX_ONE, gamer) != CNA_RESULT_SUCCESS ||
        cna_guide_show_players(CNA_PLAYER_INDEX_ONE) != CNA_RESULT_SUCCESS ||
        cna_guide_show_sign_in(2, CNA_FALSE) != CNA_RESULT_SUCCESS ||
        cna_guide_show_achievements_ext(CNA_PLAYER_INDEX_ONE) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    /* An undefined player index is refused even though nothing would have used it. */
    return cna_guide_show_friends(UINT32_C(99)) == CNA_RESULT_INVALID_ARGUMENT &&
        cna_guide_show_marketplace(UINT32_C(99)) == CNA_RESULT_INVALID_ARGUMENT &&
        cna_guide_show_gamer_card(CNA_PLAYER_INDEX_ONE, CNA_INVALID_HANDLE) ==
            CNA_RESULT_INVALID_HANDLE &&
        cna_guide_show_sign_in(2, UINT8_C(9)) == CNA_RESULT_INVALID_ARGUMENT;
}

static int validate_dispatcher(const CNA_Handle game)
{
    CNA_Bool flag = UINT8_C(9);
    uint64_t window_handle = UINT64_C(0);
    uint64_t freed = UINT64_C(0);
    CNA_Handle registration = CNA_INVALID_HANDLE;
    CNA_Handle rejected = CNA_INVALID_HANDLE;

    if (cna_gamer_services_dispatcher_get_is_initialized(&flag) != CNA_RESULT_SUCCESS ||
        cna_gamer_services_dispatcher_get_is_initialized(0) != CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    if (cna_gamer_services_dispatcher_set_window_handle(UINT64_C(0xABCD)) != CNA_RESULT_SUCCESS ||
        cna_gamer_services_dispatcher_get_window_handle(&window_handle) != CNA_RESULT_SUCCESS ||
        window_handle != UINT64_C(0xABCD)) {
        return 0;
    }
    if (cna_gamer_services_dispatcher_initialize(game) != CNA_RESULT_SUCCESS ||
        cna_gamer_services_dispatcher_get_is_initialized(&flag) != CNA_RESULT_SUCCESS ||
        flag != CNA_TRUE ||
        cna_gamer_services_dispatcher_initialize(CNA_INVALID_HANDLE) != CNA_RESULT_INVALID_HANDLE) {
        return 0;
    }
    if (cna_gamer_services_dispatcher_update() != CNA_RESULT_SUCCESS ||
        cna_gamer_services_dispatcher_update_async(&flag) != CNA_RESULT_SUCCESS ||
        cna_gamer_services_dispatcher_update_async(0) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_gamer_services_dispatcher_get_freed_gamer_count_ext(&freed) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    if (cna_gamer_services_dispatcher_subscribe_installing_title_update_ext(
            &on_complete, &completions, &registration) != CNA_RESULT_SUCCESS ||
        registration == CNA_INVALID_HANDLE ||
        cna_gamer_services_dispatcher_subscribe_installing_title_update_ext(
            0, &completions, &rejected) != CNA_RESULT_INVALID_ARGUMENT ||
        rejected != CNA_INVALID_HANDLE) {
        return 0;
    }
    return cna_gamer_unsubscribe_ext(registration) == CNA_RESULT_SUCCESS &&
        cna_gamer_unsubscribe_ext(registration) == CNA_RESULT_INVALID_HANDLE;
}

int main(void)
{
    CNA_GameCreateInfo game_info = {
        sizeof(CNA_GameCreateInfo),
        UINT32_C(1),
        CNA_TRUE,
        {0U, 0U, 0U, 0U, 0U, 0U, 0U},
        INT64_C(166667),
        {"C API guide smoke", UINT64_C(17)},
        0
    };
    CNA_Handle game = CNA_INVALID_HANDLE;
    CNA_SignedInGamerHandle gamer = CNA_INVALID_HANDLE;
    CNA_Handle component = CNA_INVALID_HANDLE;
    int status = 0;

    if (!validate_settings()) {
        return 1;
    }
    if (!validate_keyboard_input()) {
        return 2;
    }
    if (!validate_message_box()) {
        return 3;
    }
    if (cna_game_create(&game_info, &game) != CNA_RESULT_SUCCESS) {
        return 4;
    }
    if (cna_signed_in_gamer_create_ext(view("CnaCApiGuideGamer"), CNA_FALSE, CNA_FALSE,
                                       CNA_PLAYER_INDEX_ONE, &gamer) != CNA_RESULT_SUCCESS) {
        status = 5;
    }
    if (status == 0 && !validate_screens(gamer)) {
        status = 6;
    }
    /* Both renderers refuse a handle that is not the surface they need, which is the boundary this
       container can exercise: drawing itself needs a font and a backbuffer a headless tree has not
       got. */
    if (status == 0 &&
        (cna_guide_render_pending_keyboard_input_ext(CNA_INVALID_HANDLE, CNA_INVALID_HANDLE,
                                                     CNA_INVALID_HANDLE, CNA_INVALID_HANDLE) !=
             CNA_RESULT_INVALID_HANDLE ||
         cna_guide_render_pending_message_box_ext(CNA_INVALID_HANDLE, CNA_INVALID_HANDLE,
                                                  CNA_INVALID_HANDLE, CNA_INVALID_HANDLE) !=
             CNA_RESULT_INVALID_HANDLE)) {
        status = 7;
    }
    if (status == 0 && !validate_dispatcher(game)) {
        status = 8;
    }
    if (status == 0 &&
        (cna_gamer_services_component_create(game, &component) != CNA_RESULT_SUCCESS ||
         component == CNA_INVALID_HANDLE)) {
        status = 9;
    }
    /* The handle is an ordinary component handle, so the component routes accept it. */
    if (status == 0) {
        CNA_Bool enabled = UINT8_C(9);
        if (cna_game_component_get_enabled(component, &enabled) != CNA_RESULT_SUCCESS ||
            cna_game_component_destroy(component) != CNA_RESULT_SUCCESS) {
            status = 10;
        }
    }
    if (status == 0 && cna_signed_in_gamer_destroy(gamer) != CNA_RESULT_SUCCESS) {
        status = 11;
    }
    if (status == 0 && cna_game_destroy(game) != CNA_RESULT_SUCCESS) {
        status = 12;
    }
    return status;
}
