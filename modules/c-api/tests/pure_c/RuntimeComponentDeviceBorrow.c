// SPDX-License-Identifier: MS-PL

/*
 * CBIND-128: a drawable component may borrow its graphics device from the callbacks the game
 * itself drives.
 *
 * CGame opened the callback scope only around the consumer's own lifecycle callback. Game::Update,
 * Game::Draw and Game::Initialize walk the components *after* that callback returns, so a
 * component's update, draw and load-content callbacks ran with the scope already closed and
 * cna_drawable_game_component_get_graphics_device answered CNA_RESULT_INVALID_STATE -- exactly
 * where XNA code reads DrawableGameComponent.GraphicsDevice. The same gap let a component callback
 * re-enter cna_game_run_one_frame, which every lifecycle callback refuses.
 *
 * The existing smoke (RuntimeComponentsSmoke.c) only borrowed from inside the game's own update
 * callback, so it could not see this. Reported from the C#/.NET binding (cna-cs-samples
 * CNA-REPORT-004).
 */

#include <CNA/C/cna.h>

#include "CnaTestReport.h"

typedef struct DeviceProbe {
    CNA_Handle game;
    CNA_GameComponentHandle self;
    int load_calls;
    int load_borrowed;
    int update_calls;
    int update_borrowed;
    int draw_calls;
    int draw_borrowed;
    int reentry_tried;
    CNA_Result reentry_result;
    CNA_Handle last_device;
} DeviceProbe;

/* Borrowing succeeds and the handle is a usable device, not merely a non-null value. */
static int borrow_usable_device(DeviceProbe* const probe)
{
    CNA_Handle device = CNA_INVALID_HANDLE;
    CNA_Viewport viewport;
    if (cna_drawable_game_component_get_graphics_device(probe->self, &device) !=
            CNA_RESULT_SUCCESS ||
        device == CNA_INVALID_HANDLE ||
        cna_graphics_device_get_viewport(device, &viewport) != CNA_RESULT_SUCCESS ||
        viewport.width <= 0 || viewport.height <= 0) {
        return 0;
    }
    probe->last_device = device;
    return 1;
}

static void on_load_content(void* const context)
{
    DeviceProbe* const probe = (DeviceProbe*)context;
    ++probe->load_calls;
    probe->load_borrowed += borrow_usable_device(probe);
}

static void on_update(const CNA_GameTime* const game_time, void* const context)
{
    DeviceProbe* const probe = (DeviceProbe*)context;
    (void)game_time;
    ++probe->update_calls;
    probe->update_borrowed += borrow_usable_device(probe);
    /* A component callback is a lifecycle callback: driving the game from it is refused. Tried
       once, because before the fix the nested frame ran and would reach this callback again. */
    if (!probe->reentry_tried) {
        probe->reentry_tried = 1;
        probe->reentry_result = cna_game_run_one_frame(probe->game);
    }
}

static void on_draw(const CNA_GameTime* const game_time, void* const context)
{
    DeviceProbe* const probe = (DeviceProbe*)context;
    (void)game_time;
    ++probe->draw_calls;
    probe->draw_borrowed += borrow_usable_device(probe);
}

int main(void)
{
    CNA_GameCallbacks callbacks = {
        sizeof(CNA_GameCallbacks), UINT32_C(1), 0, 0, 0, 0, 0, 0
    };
    CNA_GameCreateInfo create_info = {
        sizeof(CNA_GameCreateInfo),
        UINT32_C(1),
        CNA_TRUE,
        {0U, 0U, 0U, 0U, 0U, 0U, 0U},
        INT64_C(166667),
        {"C API component device borrow", UINT64_C(29)},
        &callbacks
    };
    DeviceProbe probe = {0};
    CNA_GameComponentCallbacks component_callbacks;
    CNA_Handle device = CNA_INVALID_HANDLE;
    CNA_Viewport viewport;
    CNA_Bool removed = CNA_FALSE;
    int frame = 0;

    if (cna_game_create(&create_info, &probe.game) != CNA_RESULT_SUCCESS ||
        cna_game_component_callbacks_init(&component_callbacks) != CNA_RESULT_SUCCESS) {
        return CNA_TEST_FAIL(1);
    }
    component_callbacks.load_content = on_load_content;
    component_callbacks.update = on_update;
    component_callbacks.draw = on_draw;
    component_callbacks.context = &probe;

    /* Added before the first frame, the way an XNA game adds components in its constructor, so it
       is Game::Initialize that initializes it and loads its content. */
    if (cna_drawable_game_component_create(probe.game, &component_callbacks, &probe.self) !=
            CNA_RESULT_SUCCESS ||
        cna_game_components_add(probe.game, probe.self) != CNA_RESULT_SUCCESS ||
        probe.load_calls != 0) {
        return CNA_TEST_FAIL(2);
    }

    for (frame = 0; frame < 3; ++frame) {
        if (cna_game_run_one_frame(probe.game) != CNA_RESULT_SUCCESS) {
            return CNA_TEST_FAIL(3);
        }
    }

    (void)fprintf(
        stderr,
        "load %d/%d, update %d/%d, draw %d/%d borrowed; re-entry %d\n",
        probe.load_borrowed, probe.load_calls,
        probe.update_borrowed, probe.update_calls,
        probe.draw_borrowed, probe.draw_calls,
        (int)probe.reentry_result);
    if (probe.load_calls != 1 || probe.load_borrowed != 1) {
        return CNA_TEST_FAIL(4);
    }
    if (probe.update_calls < 1 || probe.update_borrowed != probe.update_calls) {
        return CNA_TEST_FAIL(5);
    }
    if (probe.draw_calls < 1 || probe.draw_borrowed != probe.draw_calls) {
        return CNA_TEST_FAIL(6);
    }
    if (!probe.reentry_tried || probe.reentry_result != CNA_RESULT_INVALID_STATE) {
        return CNA_TEST_FAIL(7);
    }

    /* Back in the caller the scope is closed: the borrowed handle is gone and a fresh borrow is
       refused, which is the contract a callback-scoped handle has always had. */
    if (cna_graphics_device_get_viewport(probe.last_device, &viewport) !=
            CNA_RESULT_INVALID_HANDLE ||
        cna_drawable_game_component_get_graphics_device(probe.self, &device) !=
            CNA_RESULT_INVALID_STATE ||
        device != CNA_INVALID_HANDLE) {
        return CNA_TEST_FAIL(8);
    }

    /* A component added between frames is initialized by the add itself. Its load-content
       callback is still a callback CNA makes on the game's behalf, so it may borrow too. */
    {
        DeviceProbe late = {0};
        CNA_GameComponentHandle late_component = CNA_INVALID_HANDLE;
        late.game = probe.game;
        late.reentry_tried = 1;
        component_callbacks.context = &late;
        if (cna_drawable_game_component_create(probe.game, &component_callbacks, &late_component) !=
                CNA_RESULT_SUCCESS) {
            return CNA_TEST_FAIL(9);
        }
        late.self = late_component;
        if (cna_game_components_add(probe.game, late_component) != CNA_RESULT_SUCCESS ||
            late.load_calls != 1 || late.load_borrowed != 1 ||
            cna_graphics_device_get_viewport(late.last_device, &viewport) !=
                CNA_RESULT_INVALID_HANDLE) {
            return CNA_TEST_FAIL(10);
        }
        if (cna_game_components_remove(probe.game, late_component, &removed) != CNA_RESULT_SUCCESS ||
            removed != CNA_TRUE ||
            cna_game_component_destroy(late_component) != CNA_RESULT_SUCCESS) {
            return CNA_TEST_FAIL(11);
        }
    }

    if (cna_game_components_remove(probe.game, probe.self, &removed) != CNA_RESULT_SUCCESS ||
        removed != CNA_TRUE ||
        cna_game_component_destroy(probe.self) != CNA_RESULT_SUCCESS) {
        return CNA_TEST_FAIL(12);
    }
    return cna_game_destroy(probe.game) == CNA_RESULT_SUCCESS ? 0 : CNA_TEST_FAIL(13);
}
