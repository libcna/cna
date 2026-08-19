// SPDX-License-Identifier: MS-PL

#include <CNA/C/cna.h>

#include <string.h>

typedef struct ComponentsSmokeState {
    int validated;
} ComponentsSmokeState;

typedef struct BehaviorState {
    int initialize_calls;
    int update_calls;
    int draw_calls;
    int load_calls;
    int unload_calls;
    int dispose_calls;
    int64_t last_elapsed_ticks;
} BehaviorState;

typedef struct EventState {
    int calls;
} EventState;

typedef struct CollectionState {
    int added;
    int removed;
    CNA_GameComponentHandle last_added;
    CNA_GameComponentHandle last_removed;
} CollectionState;

static void on_initialize(void* const context)
{
    ++((BehaviorState*)context)->initialize_calls;
}

static void on_update_component(const CNA_GameTime* const game_time, void* const context)
{
    BehaviorState* const state = (BehaviorState*)context;
    ++state->update_calls;
    state->last_elapsed_ticks = game_time->elapsed_game_time_ticks;
}

static void on_draw_component(const CNA_GameTime* const game_time, void* const context)
{
    (void)game_time;
    ++((BehaviorState*)context)->draw_calls;
}

static void on_load_content(void* const context)
{
    ++((BehaviorState*)context)->load_calls;
}

static void on_unload_content(void* const context)
{
    ++((BehaviorState*)context)->unload_calls;
}

static void on_dispose_component(void* const context)
{
    ++((BehaviorState*)context)->dispose_calls;
}

static void on_event(void* const context)
{
    ++((EventState*)context)->calls;
}

static void on_component_added(const CNA_GameComponentHandle component, void* const context)
{
    CollectionState* const state = (CollectionState*)context;
    ++state->added;
    state->last_added = component;
}

static void on_component_removed(const CNA_GameComponentHandle component, void* const context)
{
    CollectionState* const state = (CollectionState*)context;
    ++state->removed;
    state->last_removed = component;
}

static void fill_callbacks(CNA_GameComponentCallbacks* const callbacks, BehaviorState* const state)
{
    callbacks->initialize = on_initialize;
    callbacks->update = on_update_component;
    callbacks->draw = on_draw_component;
    callbacks->load_content = on_load_content;
    callbacks->unload_content = on_unload_content;
    callbacks->dispose = on_dispose_component;
    callbacks->context = state;
}

/* A component is the one place a C consumer supplies behavior instead of consuming it, so the
   evidence that matters is that each canonical lifecycle step reaches the C handler. */
static int validate_component_behavior(const CNA_Handle game)
{
    CNA_GameComponentCallbacks callbacks;
    CNA_GameComponentHandle component = CNA_INVALID_HANDLE;
    CNA_GameComponentHandle drawable = CNA_INVALID_HANDLE;
    CNA_GameTime time;
    BehaviorState plain;
    BehaviorState drawn;
    CNA_Handle owner = CNA_INVALID_HANDLE;
    CNA_Handle device = CNA_INVALID_HANDLE;
    CNA_Bool flag = UINT8_C(9);
    uint64_t bytes = UINT64_C(9);
    int32_t order = -99;
    char text[128];

    memset(&plain, 0, sizeof(plain));
    memset(&drawn, 0, sizeof(drawn));
    memset(&time, 0, sizeof(time));
    time.elapsed_game_time_ticks = INT64_C(166667);
    time.total_game_time_ticks = INT64_C(166667);

    memset(&callbacks, 9, sizeof(callbacks));
    if (cna_game_component_callbacks_init(&callbacks) != CNA_RESULT_SUCCESS ||
        callbacks.struct_size != (uint32_t)sizeof(callbacks) ||
        callbacks.struct_version != UINT32_C(1) || callbacks.initialize != 0 ||
        callbacks.context != 0 ||
        cna_game_component_callbacks_init(0) != CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    /* An unversioned callback set is refused before anything is created. */
    {
        CNA_GameComponentCallbacks broken;
        if (cna_game_component_callbacks_init(&broken) != CNA_RESULT_SUCCESS) {
            return 0;
        }
        broken.struct_version = UINT32_C(0);
        if (cna_game_component_create(game, &broken, &component) != CNA_RESULT_INVALID_ARGUMENT ||
            cna_game_component_create(game, 0, &component) != CNA_RESULT_INVALID_ARGUMENT ||
            cna_game_component_create(game, &callbacks, 0) != CNA_RESULT_INVALID_ARGUMENT) {
            return 0;
        }
    }

    fill_callbacks(&callbacks, &plain);
    if (cna_game_component_create(game, &callbacks, &component) != CNA_RESULT_SUCCESS ||
        component == CNA_INVALID_HANDLE ||
        cna_game_component_get_is_drawable(component, &flag) != CNA_RESULT_SUCCESS ||
        flag != CNA_FALSE ||
        cna_game_component_get_game(component, &owner) != CNA_RESULT_SUCCESS || owner != game) {
        return 0;
    }
    memset(text, 0, sizeof(text));
    if (cna_game_component_get_type_name_size(component, &bytes) != CNA_RESULT_SUCCESS ||
        bytes >= (uint64_t)sizeof(text) ||
        cna_game_component_copy_type_name(component, text, (uint64_t)sizeof(text), &bytes) !=
            CNA_RESULT_SUCCESS ||
        strcmp(text, "Microsoft.Xna.Framework.GameComponent") != 0 ||
        cna_game_component_copy_type_name(component, text, UINT64_C(2), &bytes) !=
            CNA_RESULT_BUFFER_TOO_SMALL) {
        return 0;
    }
    /* A component that does not draw refuses every drawing route rather than answering a default. */
    if (cna_drawable_game_component_get_draw_order(component, &order) != CNA_RESULT_INVALID_STATE ||
        cna_drawable_game_component_set_draw_order(component, 1) != CNA_RESULT_INVALID_STATE ||
        cna_drawable_game_component_get_visible(component, &flag) != CNA_RESULT_INVALID_STATE ||
        cna_drawable_game_component_set_visible(component, CNA_TRUE) != CNA_RESULT_INVALID_STATE ||
        cna_drawable_game_component_get_graphics_device(component, &device) !=
            CNA_RESULT_INVALID_STATE ||
        cna_drawable_game_component_draw(component, &time) != CNA_RESULT_INVALID_STATE) {
        return 0;
    }
    /* The canonical defaults: enabled, order zero. */
    if (cna_game_component_get_enabled(component, &flag) != CNA_RESULT_SUCCESS ||
        flag != CNA_TRUE ||
        cna_game_component_get_update_order(component, &order) != CNA_RESULT_SUCCESS ||
        order != 0) {
        return 0;
    }
    /* Initialization and updates reach the C handlers, and the update carries the frame's time. */
    if (cna_game_component_initialize(component) != CNA_RESULT_SUCCESS ||
        plain.initialize_calls != 1 ||
        cna_game_component_update(component, &time) != CNA_RESULT_SUCCESS ||
        plain.update_calls != 1 || plain.last_elapsed_ticks != INT64_C(166667) ||
        cna_game_component_update(component, 0) != CNA_RESULT_INVALID_ARGUMENT ||
        plain.update_calls != 1) {
        return 0;
    }

    fill_callbacks(&callbacks, &drawn);
    if (cna_drawable_game_component_create(game, &callbacks, &drawable) != CNA_RESULT_SUCCESS ||
        cna_game_component_get_is_drawable(drawable, &flag) != CNA_RESULT_SUCCESS ||
        flag != CNA_TRUE) {
        return 0;
    }
    memset(text, 0, sizeof(text));
    if (cna_game_component_copy_type_name(drawable, text, (uint64_t)sizeof(text), &bytes) !=
            CNA_RESULT_SUCCESS ||
        strcmp(text, "Microsoft.Xna.Framework.DrawableGameComponent") != 0) {
        return 0;
    }
    /* Initializing a drawable component loads its content once, which is the canonical behavior
       rather than a separate step a caller has to trigger. */
    if (cna_game_component_initialize(drawable) != CNA_RESULT_SUCCESS ||
        drawn.load_calls != 1 || drawn.initialize_calls != 1 ||
        cna_game_component_initialize(drawable) != CNA_RESULT_SUCCESS ||
        drawn.load_calls != 1) {
        return 0;
    }
    if (cna_drawable_game_component_get_visible(drawable, &flag) != CNA_RESULT_SUCCESS ||
        flag != CNA_TRUE ||
        cna_drawable_game_component_get_draw_order(drawable, &order) != CNA_RESULT_SUCCESS ||
        order != 0 ||
        cna_drawable_game_component_draw(drawable, &time) != CNA_RESULT_SUCCESS ||
        drawn.draw_calls != 1 ||
        cna_drawable_game_component_get_graphics_device(drawable, &device) != CNA_RESULT_SUCCESS ||
        device == CNA_INVALID_HANDLE) {
        return 0;
    }

    /* Every property setter raises its event on a change and stays silent otherwise. */
    {
        EventState enabled_changed = {0};
        EventState order_changed = {0};
        EventState visible_changed = {0};
        EventState disposed = {0};
        CNA_GameComponentEventRegistrationHandle first = CNA_INVALID_HANDLE;
        CNA_GameComponentEventRegistrationHandle second = CNA_INVALID_HANDLE;
        CNA_GameComponentEventRegistrationHandle third = CNA_INVALID_HANDLE;
        CNA_GameComponentEventRegistrationHandle fourth = CNA_INVALID_HANDLE;

        if (cna_game_component_subscribe(
                component,
                CNA_GAME_COMPONENT_EVENT_ENABLED_CHANGED,
                on_event,
                &enabled_changed,
                &first) != CNA_RESULT_SUCCESS ||
            cna_game_component_subscribe(
                component,
                CNA_GAME_COMPONENT_EVENT_UPDATE_ORDER_CHANGED,
                on_event,
                &order_changed,
                &second) != CNA_RESULT_SUCCESS ||
            cna_game_component_subscribe(
                drawable,
                CNA_GAME_COMPONENT_EVENT_VISIBLE_CHANGED,
                on_event,
                &visible_changed,
                &third) != CNA_RESULT_SUCCESS ||
            cna_game_component_subscribe(
                component, CNA_GAME_COMPONENT_EVENT_DISPOSED, on_event, &disposed, &fourth) !=
                CNA_RESULT_SUCCESS) {
            return 0;
        }
        /* A drawing event on a component that does not draw is refused, and an undefined identity
           and a null handler are refused too. A refused subscription clears its output first, so
           these take a handle of their own rather than reusing a live one. */
        CNA_GameComponentEventRegistrationHandle rejected = CNA_INVALID_HANDLE;
        if (cna_game_component_subscribe(
                component,
                CNA_GAME_COMPONENT_EVENT_DRAW_ORDER_CHANGED,
                on_event,
                &order_changed,
                &rejected) != CNA_RESULT_INVALID_STATE ||
            rejected != CNA_INVALID_HANDLE ||
            cna_game_component_subscribe(
                component,
                CNA_GAME_COMPONENT_EVENT_MAXIMUM + UINT32_C(1),
                on_event,
                &order_changed,
                &rejected) != CNA_RESULT_INVALID_ARGUMENT ||
            cna_game_component_subscribe(
                component, CNA_GAME_COMPONENT_EVENT_ENABLED_CHANGED, 0, &order_changed, &rejected) !=
                CNA_RESULT_INVALID_ARGUMENT) {
            return 0;
        }
        if (cna_game_component_set_enabled(component, CNA_FALSE) != CNA_RESULT_SUCCESS ||
            enabled_changed.calls != 1 ||
            cna_game_component_set_enabled(component, CNA_FALSE) != CNA_RESULT_SUCCESS ||
            enabled_changed.calls != 1 ||
            cna_game_component_get_enabled(component, &flag) != CNA_RESULT_SUCCESS ||
            flag != CNA_FALSE) {
            return 0;
        }
        if (cna_game_component_set_update_order(component, 7) != CNA_RESULT_SUCCESS ||
            order_changed.calls != 1 ||
            cna_game_component_set_update_order(component, 7) != CNA_RESULT_SUCCESS ||
            order_changed.calls != 1 ||
            cna_game_component_get_update_order(component, &order) != CNA_RESULT_SUCCESS ||
            order != 7) {
            return 0;
        }
        if (cna_drawable_game_component_set_visible(drawable, CNA_FALSE) != CNA_RESULT_SUCCESS ||
            visible_changed.calls != 1 ||
            cna_drawable_game_component_set_draw_order(drawable, 3) != CNA_RESULT_SUCCESS ||
            cna_drawable_game_component_get_draw_order(drawable, &order) != CNA_RESULT_SUCCESS ||
            order != 3) {
            return 0;
        }
        /* The canonical comparison subtracts this component's order from the other's, so the one
           that updates earlier compares greater. That inversion is reported, not corrected. */
        if (cna_game_component_compare_to(component, drawable, &order) != CNA_RESULT_SUCCESS ||
            order != -7 ||
            cna_game_component_compare_to(drawable, component, &order) != CNA_RESULT_SUCCESS ||
            order != 7 ||
            cna_game_component_compare_to(component, component, &order) != CNA_RESULT_SUCCESS ||
            order != 0) {
            return 0;
        }
        /* Disposal reaches the C handler and raises the event once; a second disposal does neither,
           which is this canonical type's own idempotence. */
        if (cna_game_component_dispose(component) != CNA_RESULT_SUCCESS ||
            plain.dispose_calls != 1 || disposed.calls != 1 ||
            cna_game_component_dispose(component) != CNA_RESULT_SUCCESS ||
            plain.dispose_calls != 1 || disposed.calls != 1) {
            return 0;
        }
        /* Disposing a drawable component unloads its content. */
        if (cna_game_component_dispose(drawable) != CNA_RESULT_SUCCESS || drawn.unload_calls != 1 ||
            drawn.dispose_calls != 1) {
            return 0;
        }
        /* Detaching a registration stops delivery; a released one is refused. */
        if (cna_game_component_unsubscribe(first) != CNA_RESULT_SUCCESS ||
            cna_game_component_unsubscribe(first) != CNA_RESULT_INVALID_HANDLE ||
            cna_game_component_unsubscribe(second) != CNA_RESULT_SUCCESS ||
            cna_game_component_unsubscribe(third) != CNA_RESULT_SUCCESS ||
            cna_game_component_unsubscribe(fourth) != CNA_RESULT_SUCCESS) {
            return 0;
        }
    }

    return cna_game_component_destroy(component) == CNA_RESULT_SUCCESS &&
        cna_game_component_destroy(component) == CNA_RESULT_INVALID_HANDLE &&
        cna_game_component_destroy(drawable) == CNA_RESULT_SUCCESS &&
        cna_game_component_get_enabled(component, &flag) == CNA_RESULT_INVALID_HANDLE;
}

static int validate_collection(const CNA_Handle game)
{
    CNA_GameComponentCallbacks callbacks;
    CNA_GameComponentHandle first = CNA_INVALID_HANDLE;
    CNA_GameComponentHandle second = CNA_INVALID_HANDLE;
    CNA_GameComponentHandle found = CNA_INVALID_HANDLE;
    CNA_GameComponentEventRegistrationHandle added = CNA_INVALID_HANDLE;
    CNA_GameComponentEventRegistrationHandle removed = CNA_INVALID_HANDLE;
    /* A refused subscription clears its output first, so the refusal checks take a handle of their
       own rather than overwriting a live one. */
    CNA_GameComponentEventRegistrationHandle rejected = CNA_INVALID_HANDLE;
    CollectionState collection;
    BehaviorState behavior;
    CNA_Bool flag = UINT8_C(9);
    uint64_t count = UINT64_C(99);
    int32_t index = -99;

    memset(&collection, 0, sizeof(collection));
    memset(&behavior, 0, sizeof(behavior));
    if (cna_game_component_callbacks_init(&callbacks) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    fill_callbacks(&callbacks, &behavior);
    if (cna_game_component_create(game, &callbacks, &first) != CNA_RESULT_SUCCESS ||
        cna_drawable_game_component_create(game, &callbacks, &second) != CNA_RESULT_SUCCESS ||
        cna_game_components_get_count(game, &count) != CNA_RESULT_SUCCESS || count != UINT64_C(0) ||
        cna_game_components_get_count(game, 0) != CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    if (cna_game_components_subscribe_added(game, on_component_added, &collection, &added) !=
            CNA_RESULT_SUCCESS ||
        cna_game_components_subscribe_removed(game, on_component_removed, &collection, &removed) !=
            CNA_RESULT_SUCCESS ||
        cna_game_components_subscribe_added(game, 0, &collection, &rejected) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        rejected != CNA_INVALID_HANDLE) {
        return 0;
    }
    /* Adding reports the component that was added, as a handle rather than an address. */
    if (cna_game_components_add(game, first) != CNA_RESULT_SUCCESS ||
        collection.added != 1 || collection.last_added != first ||
        cna_game_components_get_count(game, &count) != CNA_RESULT_SUCCESS || count != UINT64_C(1) ||
        cna_game_components_contains(game, first, &flag) != CNA_RESULT_SUCCESS ||
        flag != CNA_TRUE ||
        cna_game_components_index_of(game, first, &index) != CNA_RESULT_SUCCESS || index != 0 ||
        cna_game_components_get_at(game, UINT64_C(0), &found) != CNA_RESULT_SUCCESS ||
        found != first) {
        return 0;
    }
    /* A component that is not in the collection reports the canonical -1 rather than a flag. */
    if (cna_game_components_index_of(game, second, &index) != CNA_RESULT_SUCCESS || index != -1 ||
        cna_game_components_contains(game, second, &flag) != CNA_RESULT_SUCCESS ||
        flag != CNA_FALSE) {
        return 0;
    }
    if (cna_game_components_insert(game, UINT64_C(0), second) != CNA_RESULT_SUCCESS ||
        collection.added != 2 || collection.last_added != second ||
        cna_game_components_get_at(game, UINT64_C(0), &found) != CNA_RESULT_SUCCESS ||
        found != second ||
        cna_game_components_index_of(game, first, &index) != CNA_RESULT_SUCCESS || index != 1 ||
        cna_game_components_insert(game, UINT64_C(9), first) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_game_components_get_at(game, UINT64_C(9), &found) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_game_components_remove_at(game, UINT64_C(9)) != CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    /* Removing does not dispose or release: the same component can be added again. */
    if (cna_game_components_remove(game, second, &flag) != CNA_RESULT_SUCCESS || flag != CNA_TRUE ||
        collection.removed != 1 || collection.last_removed != second ||
        cna_game_components_remove(game, second, &flag) != CNA_RESULT_SUCCESS ||
        flag != CNA_FALSE || collection.removed != 1 ||
        cna_game_components_add(game, second) != CNA_RESULT_SUCCESS ||
        cna_game_components_get_count(game, &count) != CNA_RESULT_SUCCESS || count != UINT64_C(2)) {
        return 0;
    }
    if (cna_game_components_remove_at(game, UINT64_C(0)) != CNA_RESULT_SUCCESS ||
        collection.removed != 2 ||
        cna_game_components_get_count(game, &count) != CNA_RESULT_SUCCESS || count != UINT64_C(1)) {
        return 0;
    }
    /* Clearing raises the removed event once per component. */
    if (cna_game_components_clear(game) != CNA_RESULT_SUCCESS || collection.removed != 3 ||
        cna_game_components_get_count(game, &count) != CNA_RESULT_SUCCESS || count != UINT64_C(0)) {
        return 0;
    }
    {
        uint64_t bytes = UINT64_C(9);
        char text[128];
        memset(text, 0, sizeof(text));
        if (cna_game_components_get_type_name_size(game, &bytes) != CNA_RESULT_SUCCESS ||
            bytes >= (uint64_t)sizeof(text) ||
            cna_game_components_copy_type_name(game, text, (uint64_t)sizeof(text), &bytes) !=
                CNA_RESULT_SUCCESS ||
            strcmp(text, "Microsoft.Xna.Framework.GameComponentCollection") != 0) {
            return 0;
        }
    }
    /* Releasing a component that is still in the collection takes it out first, which the canonical
       destructor does not do: a handle-based ABI must not leave the runtime holding a released
       pointer. */
    if (cna_game_components_add(game, first) != CNA_RESULT_SUCCESS ||
        cna_game_component_destroy(first) != CNA_RESULT_SUCCESS ||
        cna_game_components_get_count(game, &count) != CNA_RESULT_SUCCESS || count != UINT64_C(0)) {
        return 0;
    }
    return cna_game_component_destroy(second) == CNA_RESULT_SUCCESS &&
        cna_game_component_unsubscribe(added) == CNA_RESULT_SUCCESS &&
        cna_game_component_unsubscribe(removed) == CNA_RESULT_SUCCESS;
}

/* The canonical service container is keyed by C++ type identity, which has no C expression, so what
   C gets is a lookup by named identity over the services the runtime itself registers. */
static int validate_services(const CNA_Handle game)
{
    CNA_Bool present = UINT8_C(9);

    if (cna_game_services_contains_ext(
            game, CNA_GAME_SERVICE_TYPE_GRAPHICS_DEVICE_MANAGER, &present) != CNA_RESULT_SUCCESS ||
        (present != CNA_FALSE && present != CNA_TRUE) ||
        cna_game_services_contains_ext(
            game, CNA_GAME_SERVICE_TYPE_GRAPHICS_DEVICE_SERVICE, &present) != CNA_RESULT_SUCCESS ||
        (present != CNA_FALSE && present != CNA_TRUE) ||
        cna_game_services_contains_ext(
            game, CNA_GAME_SERVICE_TYPE_MAXIMUM + UINT32_C(1), &present) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_game_services_contains_ext(
            game, CNA_GAME_SERVICE_TYPE_GRAPHICS_DEVICE_MANAGER, 0) !=
            CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    /* Removing is permitted because the canonical operation permits it, removing what is not there
       is an ordinary success, and nothing in C can put it back. This runs last for that reason. */
    if (cna_game_services_remove_ext(game, CNA_GAME_SERVICE_TYPE_GRAPHICS_DEVICE_MANAGER) !=
            CNA_RESULT_SUCCESS ||
        cna_game_services_remove_ext(game, CNA_GAME_SERVICE_TYPE_GRAPHICS_DEVICE_MANAGER) !=
            CNA_RESULT_SUCCESS ||
        cna_game_services_contains_ext(
            game, CNA_GAME_SERVICE_TYPE_GRAPHICS_DEVICE_MANAGER, &present) != CNA_RESULT_SUCCESS ||
        present != CNA_FALSE ||
        cna_game_services_remove_ext(game, CNA_GAME_SERVICE_TYPE_MAXIMUM + UINT32_C(1)) !=
            CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    return 1;
}

static int validate_identities(void)
{
    return CNA_GAME_COMPONENT_EVENT_ENABLED_CHANGED == UINT32_C(0) &&
        CNA_GAME_COMPONENT_EVENT_UPDATE_ORDER_CHANGED == UINT32_C(1) &&
        CNA_GAME_COMPONENT_EVENT_DRAW_ORDER_CHANGED == UINT32_C(2) &&
        CNA_GAME_COMPONENT_EVENT_VISIBLE_CHANGED == UINT32_C(3) &&
        CNA_GAME_COMPONENT_EVENT_DISPOSED == UINT32_C(4) &&
        CNA_GAME_COMPONENT_EVENT_MAXIMUM == CNA_GAME_COMPONENT_EVENT_DISPOSED &&
        CNA_GAME_SERVICE_TYPE_GRAPHICS_DEVICE_MANAGER == UINT32_C(0) &&
        CNA_GAME_SERVICE_TYPE_GRAPHICS_DEVICE_SERVICE == UINT32_C(1) &&
        CNA_GAME_SERVICE_TYPE_MAXIMUM == CNA_GAME_SERVICE_TYPE_GRAPHICS_DEVICE_SERVICE;
}

static CNA_Result on_update(
    const CNA_Handle game,
    const CNA_GameTime* const game_time,
    void* const context,
    CNA_CallbackError* const out_error)
{
    (void)out_error;
    ComponentsSmokeState* const state = (ComponentsSmokeState*)context;
    if (game_time == 0) {
        return CNA_RESULT_INVALID_STATE;
    }
    /* CBIND-068: this suite runs more than one frame now, and the validators below are a
       one-shot -- they remove services and destroy components, so a second pass would fail on the
       state the first one deliberately left behind. */
    if (state->validated != 0) {
        return CNA_RESULT_SUCCESS;
    }
    if (!validate_identities() || !validate_component_behavior(game) ||
        !validate_collection(game) || !validate_services(game)) {
        return CNA_RESULT_INVALID_STATE;
    }
    state->validated = 1;
    return CNA_RESULT_SUCCESS;
}

int main(void)
{
    ComponentsSmokeState smoke_state = {0};
    CNA_GameCallbacks callbacks = {
        sizeof(CNA_GameCallbacks), UINT32_C(1), 0, on_update, 0, 0, 0, &smoke_state
    };
    CNA_GameCreateInfo create_info = {
        sizeof(CNA_GameCreateInfo),
        UINT32_C(1),
        CNA_TRUE,
        {0U, 0U, 0U, 0U, 0U, 0U, 0U},
        INT64_C(166667),
        {"C API game component smoke", UINT64_C(26)},
        &callbacks
    };
    CNA_Handle game = CNA_INVALID_HANDLE;
    if (cna_game_create(&create_info, &game) != CNA_RESULT_SUCCESS ||
        cna_game_run_one_frame(game) != CNA_RESULT_SUCCESS ||
        smoke_state.validated != 1) {
        return 1;
    }

    /* CBIND-068: a component in the game's collection actually ticks, once per frame.
     *
     * Every assertion above this one drives a component **directly** -- cna_game_component_update
     * with a time the test supplies -- which proves the callback table is wired and says nothing
     * about whether the game ever calls it. It did not: CGame::Update and CGame::Draw were the
     * only overrides in that class that never chained to the base, and Game::Update is what walks
     * the updateable components and then runs FrameworkDispatcher::Update(). A component added
     * through cna_game_components_add was constructed, initialized by the add path, and then never
     * ticked again.
     *
     * This has to run from outside a lifecycle callback, because the base pass happens *after* the
     * consumer's update handler returns -- an assertion made inside that handler would read the
     * counts of the frame before it. Reported from the C#/.NET binding, which saw a component
     * report initialized=true and updated 0 times across six frames.
     */
    {
        BehaviorState ticker = {0, 0, 0, 0, 0, 0, 0};
        CNA_GameComponentCallbacks ticker_callbacks;
        CNA_GameComponentHandle component = CNA_INVALID_HANDLE;
        int frame = 0;
        int updates_after_first = 0;
        int draws_after_first = 0;

        if (cna_game_component_callbacks_init(&ticker_callbacks) != CNA_RESULT_SUCCESS) {
            return 3;
        }
        fill_callbacks(&ticker_callbacks, &ticker);
        if (cna_drawable_game_component_create(game, &ticker_callbacks, &component) !=
                CNA_RESULT_SUCCESS ||
            cna_game_components_add(game, component) != CNA_RESULT_SUCCESS) {
            return 3;
        }
        /* Adding initializes it, which is the canonical add path and not the frame loop. */
        if (ticker.initialize_calls != 1 || ticker.update_calls != 0) {
            return 4;
        }
        if (cna_game_run_one_frame(game) != CNA_RESULT_SUCCESS) {
            return 5;
        }
        updates_after_first = ticker.update_calls;
        draws_after_first = ticker.draw_calls;
        if (updates_after_first < 1) {
            return 6;
        }
        for (frame = 0; frame < 3; ++frame) {
            if (cna_game_run_one_frame(game) != CNA_RESULT_SUCCESS) {
                return 7;
            }
        }
        /* One update per frame, exactly -- a fixed-timestep frame runs Update once. */
        if (ticker.update_calls != updates_after_first + 3) {
            return 8;
        }
        /* And drawing reaches it too, which is Game::Draw's own pass over visible components. A
           frame may legitimately suppress its draw, so this asserts growth rather than a count. */
        if (ticker.draw_calls <= draws_after_first - 1 || ticker.draw_calls < 1) {
            return 9;
        }
        /* Disabling it stops the ticks without removing it, which is what Enabled is for. */
        if (cna_game_component_set_enabled(component, CNA_FALSE) != CNA_RESULT_SUCCESS) {
            return 10;
        }
        {
            const int before = ticker.update_calls;
            if (cna_game_run_one_frame(game) != CNA_RESULT_SUCCESS ||
                ticker.update_calls != before) {
                return 11;
            }
        }
        if (cna_game_component_destroy(component) != CNA_RESULT_SUCCESS) {
            return 12;
        }
    }

    return cna_game_destroy(game) == CNA_RESULT_SUCCESS ? 0 : 2;
}
