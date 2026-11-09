// SPDX-License-Identifier: MS-PL

#include "CnaTestRunFrame.h"
#include "CnaLifetimeSequence.h"
#include <string.h>

#define GRAPH_COUNT 20U

enum GraphKind { TEXTURE, TARGET, BUFFER, EFFECT_VIEW, COMPONENT, CONTENT_TEXTURE };
typedef struct GraphSlot {
    CNA_Handle handle;
    CNA_Handle stale;
    CNA_Handle game;
    enum GraphKind kind;
    enum GraphKind stale_kind;
    CNA_Bool disposed;
    CNA_Bool member;
    unsigned dispose_calls;
    CNA_Result reentry_result;
} GraphSlot;

typedef struct GraphState {
    CNA_Handle game;
    CNA_Handle device;
    CNA_Handle second_device;
    CNA_Handle second_batch;
    CNA_Handle sentinel;
    CNA_Handle manager;
    CNA_Handle callback_device;
    int callback_checked;
} GraphState;

static const CNA_Texture2DCreateInfo texture_info = {
    sizeof(CNA_Texture2DCreateInfo), 1U, 4U, 4U, CNA_FALSE, {0U, 0U, 0U}, CNA_SURFACE_FORMAT_COLOR};
static const CNA_RenderTarget2DCreateInfo target_info = {
    sizeof(CNA_RenderTarget2DCreateInfo), 1U, 4U, 4U, CNA_FALSE, {0U, 0U, 0U},
    CNA_SURFACE_FORMAT_COLOR, CNA_DEPTH_FORMAT_NONE, 0, CNA_RENDER_TARGET_USAGE_PRESERVE_CONTENTS, 0U};
static const CNA_StringView asset_name = {"xna40/texture/probe_4x4.png", sizeof("xna40/texture/probe_4x4.png") - 1U};

static void on_component_dispose(void* context)
{
    GraphSlot* slot = (GraphSlot*)context;
    ++slot->dispose_calls;
    slot->reentry_result = cna_game_destroy(slot->game);
}

static int create_slot(GraphState* graph, GraphSlot* slot, unsigned kind)
{
    slot->kind = (enum GraphKind)kind;
    slot->game = graph->game;
    slot->disposed = CNA_FALSE;
    slot->member = CNA_FALSE;
    slot->dispose_calls = 0U;
    slot->reentry_result = CNA_RESULT_SUCCESS;
    switch (slot->kind) {
    case TEXTURE:
        EXPECT_RESULT(cna_texture2d_create(graph->device, &texture_info, &slot->handle), CNA_RESULT_SUCCESS);
        break;
    case TARGET:
        EXPECT_RESULT(cna_render_target2d_create(graph->device, &target_info, &slot->handle), CNA_RESULT_SUCCESS);
        break;
    case BUFFER: {
        CNA_Handle declaration = CNA_INVALID_HANDLE;
        const CNA_VertexElement element = {0, CNA_VERTEX_ELEMENT_FORMAT_VECTOR3, CNA_VERTEX_ELEMENT_USAGE_POSITION, 0};
        EXPECT_RESULT(cna_vertex_declaration_create(&element, 1U, &declaration), CNA_RESULT_SUCCESS);
        const CNA_VertexBufferCreateInfo info = {sizeof(CNA_VertexBufferCreateInfo), 1U,
            declaration, 3, CNA_BUFFER_USAGE_NONE, CNA_FALSE, {0U, 0U, 0U, 0U, 0U, 0U, 0U}};
        EXPECT_RESULT(cna_vertex_buffer_create(graph->device, &info, &slot->handle), CNA_RESULT_SUCCESS);
        EXPECT_RESULT(cna_vertex_declaration_destroy(declaration), CNA_RESULT_SUCCESS);
        break;
    }
    case EFFECT_VIEW: {
        CNA_Handle effect = CNA_INVALID_HANDLE;
        EXPECT_RESULT(cna_effect_create_empty(graph->device, &effect), CNA_RESULT_SUCCESS);
        EXPECT_RESULT(cna_effect_get_parameters(effect, &slot->handle), CNA_RESULT_SUCCESS);
        EXPECT_RESULT(cna_effect_destroy(effect), CNA_RESULT_SUCCESS);
        EXPECT_RESULT(cna_effect_destroy(effect), CNA_RESULT_INVALID_HANDLE);
        break;
    }
    case COMPONENT: {
        CNA_GameComponentCallbacks callbacks;
        EXPECT_RESULT(cna_game_component_callbacks_init(&callbacks), CNA_RESULT_SUCCESS);
        callbacks.dispose = on_component_dispose;
        callbacks.context = slot;
        EXPECT_RESULT(cna_game_component_create(graph->game, &callbacks, &slot->handle), CNA_RESULT_SUCCESS);
        break;
    }
    default:
        /* Repeated loads can alias one cached native texture. Start a fresh cache entry so each
           slot models an independent native object as well as an independent owned C token. */
        EXPECT_RESULT(cna_content_manager_unload(graph->manager), CNA_RESULT_SUCCESS);
        EXPECT_RESULT(cna_content_manager_load_texture2d(graph->manager, asset_name, &slot->handle), CNA_RESULT_SUCCESS);
        break;
    }
    REQUIRE(slot->handle != CNA_INVALID_HANDLE && slot->handle != slot->stale);
    return 1;
}

static CNA_Result release_graph_handle(enum GraphKind kind, CNA_Handle handle)
{
    switch (kind) {
    case TEXTURE: case CONTENT_TEXTURE: return cna_texture2d_destroy(handle);
    case TARGET: return cna_render_target_destroy(handle);
    case BUFFER: return cna_vertex_buffer_destroy(handle);
    case EFFECT_VIEW: return cna_effect_parameter_collection_destroy(handle);
    default: return cna_game_component_destroy(handle);
    }
}

static int check_slot(const GraphSlot* slot)
{
    if (slot->kind == EFFECT_VIEW) {
        uint64_t count = UINT64_MAX;
        EXPECT_RESULT(cna_effect_parameter_collection_get_count(slot->handle, &count), CNA_RESULT_SUCCESS);
        REQUIRE(count == 0U);
    } else if (slot->kind == COMPONENT) {
        CNA_Handle parent = CNA_INVALID_HANDLE;
        EXPECT_RESULT(cna_game_component_get_game(slot->handle, &parent), CNA_RESULT_SUCCESS);
        REQUIRE(parent == slot->game);
    } else {
        CNA_Bool disposed = CNA_TRUE;
        EXPECT_RESULT(cna_graphics_resource_get_is_disposed(slot->handle, &disposed), CNA_RESULT_SUCCESS);
        REQUIRE(disposed == slot->disposed);
    }
    return 1;
}

static int check_members(GraphState* graph, const GraphSlot* slots)
{
    uint64_t expected = 0U;
    uint64_t count = UINT64_MAX;
    for (unsigned i = 0U; i < GRAPH_COUNT; ++i) {
        if (slots[i].kind == COMPONENT) {
            CNA_Bool contains = CNA_FALSE;
            EXPECT_RESULT(cna_game_components_contains(graph->game, slots[i].handle, &contains), CNA_RESULT_SUCCESS);
            REQUIRE(contains == slots[i].member);
            if (slots[i].member == CNA_TRUE) ++expected;
        }
    }
    EXPECT_RESULT(cna_game_components_get_count(graph->game, &count), CNA_RESULT_SUCCESS);
    REQUIRE(count == expected);
    for (uint64_t i = 0U; i < count; ++i) {
        CNA_Handle borrowed = CNA_INVALID_HANDLE;
        unsigned found = 0U;
        EXPECT_RESULT(cna_game_components_get_at(graph->game, i, &borrowed), CNA_RESULT_SUCCESS);
        for (unsigned j = 0U; j < GRAPH_COUNT; ++j)
            if (slots[j].handle == borrowed && slots[j].member == CNA_TRUE) ++found;
        REQUIRE(found == 1U);
    }
    return 1;
}

static int run_sequence(GraphState* graph, unsigned index)
{
    /* Native callbacks may still run during exit cleanup after a failed assertion. */
    static GraphSlot slots[GRAPH_COUNT];
    const CNA_ContentManagerCreateInfo manager_info = {sizeof(CNA_ContentManagerCreateInfo), 1U,
        {CNA_C_API_TEST_ASSET_DIR, sizeof(CNA_C_API_TEST_ASSET_DIR) - 1U}, 0U};
    begin_sequence("graphics-lifetime", index);
    memset(slots, 0, sizeof(slots));
    EXPECT_RESULT(cna_content_manager_create(graph->device, &manager_info, &graph->manager), CNA_RESULT_SUCCESS);
    for (unsigned i = 0U; i < GRAPH_COUNT; ++i) REQUIRE(create_slot(graph, &slots[i], i % 6U));
    for (sequence_step = 0U; sequence_step < OPERATION_COUNT; ++sequence_step) {
        const unsigned which = next_random() % GRAPH_COUNT;
        const unsigned operation = next_random() % 9U;
        GraphSlot* slot = &slots[which];
        sequence_trace[sequence_step] = operation * 32U + which;
        switch (operation) {
        case 0: case 1:
            EXPECT_RESULT(release_graph_handle(slot->kind, slot->handle), CNA_RESULT_SUCCESS);
            slot->stale = slot->handle;
            slot->stale_kind = slot->kind;
            EXPECT_RESULT(release_graph_handle(slot->stale_kind, slot->stale), CNA_RESULT_INVALID_HANDLE);
            REQUIRE(create_slot(graph, slot, next_random() % 6U));
            break;
        case 2:
            if (slot->kind == COMPONENT) {
                const unsigned before = slot->dispose_calls;
                const CNA_Handle expired = graph->device;
                CNA_Bool disposed = CNA_FALSE;
                EXPECT_RESULT(cna_game_component_dispose(slot->handle), CNA_RESULT_SUCCESS);
                EXPECT_RESULT(cna_game_component_dispose(slot->handle), CNA_RESULT_SUCCESS);
                slot->member = CNA_FALSE;
                REQUIRE(slot->dispose_calls == before + 2U && slot->reentry_result == CNA_RESULT_INVALID_STATE);
                /* Direct component callbacks close the outermost borrowing scope, even pre-run. */
                EXPECT_RESULT(cna_graphics_device_get_is_disposed(expired, &disposed), CNA_RESULT_INVALID_HANDLE);
                EXPECT_RESULT(cna_game_get_graphics_device(graph->game, &graph->device), CNA_RESULT_SUCCESS);
                REQUIRE(graph->device != expired);
            } else if (slot->kind != EFFECT_VIEW && slot->kind != CONTENT_TEXTURE) {
                EXPECT_RESULT(cna_graphics_resource_dispose(slot->handle), CNA_RESULT_SUCCESS);
                EXPECT_RESULT(cna_graphics_resource_dispose(slot->handle), CNA_RESULT_SUCCESS);
                slot->disposed = CNA_TRUE;
            }
            break;
        case 3:
            EXPECT_RESULT(cna_game_destroy(graph->game), CNA_RESULT_INVALID_STATE);
            break;
        case 4:
            if (slot->kind == TARGET && slot->disposed == CNA_FALSE) {
                EXPECT_RESULT(cna_graphics_device_set_render_target2d(graph->device, slot->handle), CNA_RESULT_SUCCESS);
                EXPECT_RESULT(cna_render_target_destroy(slot->handle), CNA_RESULT_INVALID_STATE);
                EXPECT_RESULT(cna_graphics_resource_dispose(slot->handle), CNA_RESULT_INVALID_STATE);
                EXPECT_RESULT(cna_graphics_device_set_render_target2d(graph->device, CNA_INVALID_HANDLE), CNA_RESULT_SUCCESS);
            }
            break;
        case 5:
            if ((slot->kind == TEXTURE || slot->kind == CONTENT_TEXTURE) && slot->disposed == CNA_FALSE) {
                const CNA_SpriteBatchBeginInfo begin = {sizeof(CNA_SpriteBatchBeginInfo), 1U, CNA_SPRITE_SORT_MODE_DEFERRED, 0U};
                const CNA_SpriteCommand command = {sizeof(CNA_SpriteCommand), 1U, slot->handle,
                    {0, 0, 4, 4}, {0U, 0U, 0U, 0U}, {255U, 255U, 255U, 255U},
                    0.0F, {0.0F, 0.0F}, CNA_SPRITE_EFFECT_NONE, 0.0F};
                EXPECT_RESULT(cna_sprite_batch_begin(graph->second_batch, &begin), CNA_RESULT_SUCCESS);
                EXPECT_RESULT(cna_sprite_batch_submit_many(graph->second_batch, &command, 1U), CNA_RESULT_INVALID_HANDLE);
                EXPECT_RESULT(cna_sprite_batch_end(graph->second_batch), CNA_RESULT_SUCCESS);
            }
            break;
        case 6:
            if (slot->kind == COMPONENT) {
                if (slot->member == CNA_FALSE) {
                    EXPECT_RESULT(cna_game_components_add(graph->game, slot->handle), CNA_RESULT_SUCCESS);
                    slot->member = CNA_TRUE;
                } else {
                    CNA_Bool removed = CNA_FALSE;
                    EXPECT_RESULT(cna_game_components_remove(graph->game, slot->handle, &removed), CNA_RESULT_SUCCESS);
                    REQUIRE(removed == CNA_TRUE);
                    slot->member = CNA_FALSE;
                }
            }
            break;
        case 7: {
            CNA_Texture2DCreateInfo invalid = texture_info;
            CNA_Handle rejected = graph->sentinel;
            invalid.width = 0U;
            EXPECT_RESULT(cna_texture2d_create(graph->device, &invalid, &rejected), CNA_RESULT_INVALID_ARGUMENT);
            REQUIRE(rejected == CNA_INVALID_HANDLE);
            EXPECT_RESULT(cna_content_manager_unload(graph->manager), CNA_RESULT_SUCCESS);
            EXPECT_RESULT(cna_content_manager_unload(graph->manager), CNA_RESULT_SUCCESS);
            break;
        }
        default:
            EXPECT_RESULT(cna_game_components_clear(graph->game), CNA_RESULT_SUCCESS);
            for (unsigned i = 0U; i < GRAPH_COUNT; ++i) slots[i].member = CNA_FALSE;
            break;
        }
        REQUIRE(check_slot(slot));
        REQUIRE(check_members(graph, slots));
        if (slot->stale != CNA_INVALID_HANDLE)
            EXPECT_RESULT(release_graph_handle(slot->stale_kind, slot->stale), CNA_RESULT_INVALID_HANDLE);
        {
            CNA_Bool disposed = CNA_TRUE;
            EXPECT_RESULT(cna_graphics_resource_get_is_disposed(graph->sentinel, &disposed), CNA_RESULT_SUCCESS);
            REQUIRE(disposed == CNA_FALSE);
        }
    }
    /* Content textures keep independently transferred ownership after manager destruction. */
    EXPECT_RESULT(cna_content_manager_destroy(graph->manager), CNA_RESULT_SUCCESS);
    EXPECT_RESULT(cna_content_manager_unload(graph->manager), CNA_RESULT_INVALID_HANDLE);
    for (unsigned i = 0U; i < GRAPH_COUNT; ++i) {
        REQUIRE(check_slot(&slots[i]));
        if (slots[i].kind == CONTENT_TEXTURE) {
            CNA_Texture2DInfo info = {sizeof(CNA_Texture2DInfo), 1U, 0U, 0U, 0U, 0U};
            EXPECT_RESULT(cna_texture2d_get_info(slots[i].handle, &info), CNA_RESULT_SUCCESS);
            REQUIRE(info.width == 4U && info.height == 4U);
            CNA_Color pixels[16];
            uint64_t count = 0U;
            EXPECT_RESULT(cna_texture2d_get_data_rgba8(slots[i].handle, pixels, 16U, &count), CNA_RESULT_SUCCESS);
            REQUIRE(count == 16U);
        }
        EXPECT_RESULT(release_graph_handle(slots[i].kind, slots[i].handle), CNA_RESULT_SUCCESS);
    }
    {
        uint64_t count = UINT64_MAX;
        EXPECT_RESULT(cna_game_components_get_count(graph->game, &count), CNA_RESULT_SUCCESS);
        REQUIRE(count == 0U);
    }
    return 1;
}

static CNA_Result on_load(CNA_Handle game, const CNA_GameTime* time, void* context, CNA_CallbackError* error)
{
    GraphState* graph = (GraphState*)context;
    CNA_Handle repeated = CNA_INVALID_HANDLE;
    (void)error;
    if (time != NULL || cna_game_destroy(game) != CNA_RESULT_INVALID_STATE ||
        cna_game_run_one_frame(game) != CNA_RESULT_INVALID_STATE ||
        cna_game_get_graphics_device(game, &graph->callback_device) != CNA_RESULT_SUCCESS ||
        cna_game_get_graphics_device(game, &repeated) != CNA_RESULT_SUCCESS ||
        graph->callback_device != repeated)
        return CNA_RESULT_INVALID_STATE;
    graph->callback_checked = 1;
    return CNA_RESULT_SUCCESS;
}

static int run_campaign(unsigned first, unsigned end)
{
    static GraphState graph;
    CNA_Handle retained_effect = CNA_INVALID_HANDLE;
    CNA_Handle retained_view = CNA_INVALID_HANDLE;
    const CNA_GameCallbacks callbacks = {sizeof(CNA_GameCallbacks), 1U, on_load, NULL, NULL, NULL, NULL, &graph};
    const CNA_GameCreateInfo game_info = {sizeof(CNA_GameCreateInfo), 1U, CNA_TRUE,
        {0U, 0U, 0U, 0U, 0U, 0U, 0U}, INT64_C(166667), {NULL, 0U}, &callbacks};
    CNA_PresentationParameters parameters;
    begin_sequence("graphics-lifetime", first);
    EXPECT_RESULT(cna_game_create(&game_info, &graph.game), CNA_RESULT_SUCCESS);
    EXPECT_RESULT(cna_game_get_graphics_device(graph.game, &graph.device), CNA_RESULT_SUCCESS);
    EXPECT_RESULT(cna_presentation_parameters_init(&parameters), CNA_RESULT_SUCCESS);
    parameters.back_buffer_width = 32;
    parameters.back_buffer_height = 32;
    parameters.is_full_screen = CNA_FALSE;
    EXPECT_RESULT(cna_graphics_device_create(0U, CNA_GRAPHICS_PROFILE_REACH, &parameters, &graph.second_device), CNA_RESULT_SUCCESS);
    EXPECT_RESULT(cna_sprite_batch_create(graph.second_device, &graph.second_batch), CNA_RESULT_SUCCESS);
    EXPECT_RESULT(cna_texture2d_create(graph.device, &texture_info, &graph.sentinel), CNA_RESULT_SUCCESS);
    for (unsigned i = first; i < end; ++i) REQUIRE(run_sequence(&graph, i));
    EXPECT_RESULT(cna_effect_create_empty(graph.device, &retained_effect), CNA_RESULT_SUCCESS);
    EXPECT_RESULT(cna_effect_get_parameters(retained_effect, &retained_view), CNA_RESULT_SUCCESS);
    EXPECT_RESULT(cna_effect_destroy(retained_effect), CNA_RESULT_SUCCESS);
    EXPECT_RESULT(cna_test_run_initialized_frame(graph.game), CNA_RESULT_SUCCESS);
    REQUIRE(graph.callback_checked == 1);
    {
        CNA_Bool disposed = CNA_TRUE;
        EXPECT_RESULT(cna_graphics_device_get_is_disposed(graph.device, &disposed), CNA_RESULT_INVALID_HANDLE);
        EXPECT_RESULT(cna_graphics_device_get_is_disposed(graph.callback_device, &disposed), CNA_RESULT_INVALID_HANDLE);
        EXPECT_RESULT(cna_game_destroy(graph.game), CNA_RESULT_INVALID_STATE);
        EXPECT_RESULT(cna_graphics_resource_get_is_disposed(graph.sentinel, &disposed), CNA_RESULT_SUCCESS);
        REQUIRE(disposed == CNA_FALSE);
    }
    EXPECT_RESULT(cna_texture2d_destroy(graph.sentinel), CNA_RESULT_SUCCESS);
    /* The retained view is now the game's only owned child. */
    EXPECT_RESULT(cna_game_destroy(graph.game), CNA_RESULT_INVALID_STATE);
    EXPECT_RESULT(cna_effect_parameter_collection_destroy(retained_view), CNA_RESULT_SUCCESS);
    EXPECT_RESULT(cna_game_destroy(graph.game), CNA_RESULT_SUCCESS);
    EXPECT_RESULT(cna_game_destroy(graph.game), CNA_RESULT_INVALID_HANDLE);
    /* An independent device and its child remain valid after unrelated game shutdown. */
    {
        CNA_Bool disposed = CNA_TRUE;
        EXPECT_RESULT(cna_graphics_device_get_is_disposed(graph.second_device, &disposed), CNA_RESULT_SUCCESS);
        REQUIRE(disposed == CNA_FALSE);
    }
    EXPECT_RESULT(cna_sprite_batch_destroy(graph.second_batch), CNA_RESULT_SUCCESS);
    EXPECT_RESULT(cna_graphics_device_destroy(graph.second_device), CNA_RESULT_SUCCESS);
    EXPECT_RESULT(cna_graphics_device_destroy(graph.second_device), CNA_RESULT_INVALID_HANDLE);
    return 1;
}

typedef struct MembershipProbe {
    CNA_Handle game;
    CNA_Handle component;
    unsigned removed;
    unsigned disposed;
    int invalid;
} MembershipProbe;

static void on_member_removed(CNA_Handle component, void* context)
{
    MembershipProbe* probe = (MembershipProbe*)context;
    CNA_Bool contains = CNA_TRUE;
    ++probe->removed;
    if (component != probe->component ||
        cna_game_components_contains(probe->game, component, &contains) != CNA_RESULT_SUCCESS || contains != CNA_FALSE)
        probe->invalid = 1;
}

static void on_member_disposed(void* context)
{
    MembershipProbe* probe = (MembershipProbe*)context;
    CNA_Bool contains = CNA_TRUE;
    ++probe->disposed;
    if (probe->removed != 1U || cna_game_components_contains(probe->game, probe->component, &contains) !=
        CNA_RESULT_SUCCESS || contains != CNA_FALSE) probe->invalid = 1;
}

static int membership_regression(void)
{
    static MembershipProbe probe;
    CNA_GameComponentCallbacks callbacks;
    CNA_Handle removed_registration = CNA_INVALID_HANDLE;
    CNA_Handle disposed_registration = CNA_INVALID_HANDLE;
    CNA_Bool flag = CNA_FALSE;
    uint64_t count = UINT64_MAX;
    const CNA_GameCreateInfo info = {sizeof(CNA_GameCreateInfo), 1U, CNA_TRUE,
        {0U, 0U, 0U, 0U, 0U, 0U, 0U}, INT64_C(166667), {NULL, 0U}, NULL};
    begin_sequence("membership-regression", 7U);
    EXPECT_RESULT(cna_game_create(&info, &probe.game), CNA_RESULT_SUCCESS);
    EXPECT_RESULT(cna_game_component_callbacks_init(&callbacks), CNA_RESULT_SUCCESS);
    EXPECT_RESULT(cna_game_component_create(probe.game, &callbacks, &probe.component), CNA_RESULT_SUCCESS);
    EXPECT_RESULT(cna_game_components_subscribe_removed(probe.game, on_member_removed, &probe,
        &removed_registration), CNA_RESULT_SUCCESS);
    EXPECT_RESULT(cna_game_component_subscribe(probe.component, CNA_GAME_COMPONENT_EVENT_DISPOSED,
        on_member_disposed, &probe, &disposed_registration), CNA_RESULT_SUCCESS);
    EXPECT_RESULT(cna_game_components_add(probe.game, probe.component), CNA_RESULT_SUCCESS);
    EXPECT_RESULT(cna_game_components_contains(probe.game, probe.component, &flag), CNA_RESULT_SUCCESS);
    REQUIRE(flag == CNA_TRUE);
    EXPECT_RESULT(cna_game_component_dispose(probe.component), CNA_RESULT_SUCCESS);
    EXPECT_RESULT(cna_game_component_dispose(probe.component), CNA_RESULT_SUCCESS);
    EXPECT_RESULT(cna_game_components_contains(probe.game, probe.component, &flag), CNA_RESULT_SUCCESS);
    REQUIRE(flag == CNA_FALSE && probe.removed == 1U && probe.disposed == 2U && probe.invalid == 0);
    EXPECT_RESULT(cna_game_components_get_count(probe.game, &count), CNA_RESULT_SUCCESS);
    REQUIRE(count == 0U);
    /* Disposal leaves a valid handle, and Remove still differs from Dispose and typed destroy. */
    EXPECT_RESULT(cna_game_components_add(probe.game, probe.component), CNA_RESULT_SUCCESS);
    EXPECT_RESULT(cna_game_components_remove(probe.game, probe.component, &flag), CNA_RESULT_SUCCESS);
    REQUIRE(flag == CNA_TRUE && probe.removed == 2U && probe.disposed == 2U);
    EXPECT_RESULT(cna_game_components_add(probe.game, probe.component), CNA_RESULT_SUCCESS);
    EXPECT_RESULT(cna_game_component_destroy(probe.component), CNA_RESULT_SUCCESS);
    EXPECT_RESULT(cna_game_components_get_count(probe.game, &count), CNA_RESULT_SUCCESS);
    REQUIRE(count == 0U && probe.removed == 3U && probe.disposed == 2U && probe.invalid == 0);
    EXPECT_RESULT(cna_game_component_unsubscribe(disposed_registration), CNA_RESULT_SUCCESS);
    EXPECT_RESULT(cna_game_component_unsubscribe(removed_registration), CNA_RESULT_SUCCESS);
    EXPECT_RESULT(cna_game_destroy(probe.game), CNA_RESULT_SUCCESS);
    puts("component membership: Dispose removes before Disposed, repeat Dispose repeats only Disposed; PASS");
    return 1;
}

static unsigned exit_disposal_count;
static unsigned exit_texture_disposal_count;

static void count_exit_disposal(CNA_Handle device, void* context)
{
    (void)device;
    (void)context;
    ++exit_disposal_count;
}

static void count_exit_texture_disposal(CNA_Handle resource, void* context)
{
    (void)resource;
    (void)context;
    ++exit_texture_disposal_count;
}

static void require_early_exit_cleanup(void)
{
    /* HND-TQ-47 R2: the device's disposal releases the texture's native object without disposing
     * it (XNA ReleaseAllDeviceResources), so the device's own Disposing is the cleanup witness and
     * the texture raises nothing. */
    if (exit_disposal_count != 1U || exit_texture_disposal_count != 0U) {
        fputs("exit cleanup did not run once before the older finalizer\n", stderr);
        _Exit(1);
    }
    fputs("native disposal preceded older finalizer exactly once: PASS\n", stderr);
}

static int standalone_exit_regression(int recreate)
{
    CNA_PresentationParameters parameters;
    CNA_Handle device = CNA_INVALID_HANDLE;
    CNA_Handle texture = CNA_INVALID_HANDLE;
    CNA_Handle registration = CNA_INVALID_HANDLE;
    CNA_Handle device_registration = CNA_INVALID_HANDLE;
    begin_sequence(recreate ? "recreate-exit-regression" : "standalone-exit-regression", 0U);
    /* Represents a library finalizer installed before the latest GPU initialization. */
    REQUIRE(atexit(require_early_exit_cleanup) == 0);
    EXPECT_RESULT(cna_presentation_parameters_init(&parameters), CNA_RESULT_SUCCESS);
    parameters.back_buffer_width = 32;
    parameters.back_buffer_height = 32;
    parameters.is_full_screen = CNA_FALSE;
    EXPECT_RESULT(cna_graphics_device_create(0U, CNA_GRAPHICS_PROFILE_REACH, &parameters, &device), CNA_RESULT_SUCCESS);
    if (recreate) {
        /* The recreated renderer must refresh cleanup after this later finalizer, too. */
        REQUIRE(atexit(require_early_exit_cleanup) == 0);
        EXPECT_RESULT(cna_graphics_device_recreate_renderer_for_multi_sample_count_ext(device, 0), CNA_RESULT_SUCCESS);
    }
    EXPECT_RESULT(cna_texture2d_create(device, &texture_info, &texture), CNA_RESULT_SUCCESS);
    EXPECT_RESULT(cna_graphics_resource_subscribe_disposing(texture, count_exit_texture_disposal, NULL,
        &registration), CNA_RESULT_SUCCESS);
    EXPECT_RESULT(cna_graphics_device_subscribe_event(device, CNA_GRAPHICS_DEVICE_EVENT_DISPOSING,
        count_exit_disposal, NULL, &device_registration), CNA_RESULT_SUCCESS);
    puts("leaving standalone device/texture/subscription for ordered exit cleanup");
    return 1;
}

static int reduced_regression(const char* mode)
{
    static GraphSlot component;
    CNA_Handle game = CNA_INVALID_HANDLE;
    CNA_Handle device = CNA_INVALID_HANDLE;
    CNA_Handle standalone = CNA_INVALID_HANDLE;
    CNA_Handle target = CNA_INVALID_HANDLE;
    const CNA_GameCreateInfo info = {sizeof(CNA_GameCreateInfo), 1U, CNA_TRUE,
        {0U, 0U, 0U, 0U, 0U, 0U, 0U}, INT64_C(166667), {NULL, 0U}, NULL};
    CNA_PresentationParameters parameters;
    begin_sequence(mode, 0U);
    EXPECT_RESULT(cna_game_create(&info, &game), CNA_RESULT_SUCCESS);
    if (strcmp(mode, "borrow-regression") != 0) {
        EXPECT_RESULT(cna_presentation_parameters_init(&parameters), CNA_RESULT_SUCCESS);
        parameters.back_buffer_width = 32;
        parameters.back_buffer_height = 32;
        parameters.is_full_screen = CNA_FALSE;
        EXPECT_RESULT(cna_graphics_device_create(0U, CNA_GRAPHICS_PROFILE_REACH, &parameters, &standalone),
            CNA_RESULT_SUCCESS);
    }
    if (strcmp(mode, "device-platform-regression") == 0) {
        EXPECT_RESULT(cna_game_destroy(game), CNA_RESULT_SUCCESS);
        EXPECT_RESULT(cna_graphics_device_destroy(standalone), CNA_RESULT_SUCCESS);
        puts("game release before standalone device release: PASS");
        return 1;
    }
    EXPECT_RESULT(cna_game_get_graphics_device(game, &device), CNA_RESULT_SUCCESS);
    CNA_GameComponentCallbacks callbacks;
    EXPECT_RESULT(cna_game_component_callbacks_init(&callbacks), CNA_RESULT_SUCCESS);
    callbacks.dispose = on_component_dispose;
    component.game = game;
    callbacks.context = &component;
    EXPECT_RESULT(cna_game_component_create(game, &callbacks, &component.handle), CNA_RESULT_SUCCESS);
    EXPECT_RESULT(cna_game_component_dispose(component.handle), CNA_RESULT_SUCCESS);
    REQUIRE(component.dispose_calls == 1U && component.reentry_result == CNA_RESULT_INVALID_STATE);
    EXPECT_RESULT(cna_render_target2d_create(device, &target_info, &target), CNA_RESULT_INVALID_HANDLE);
    REQUIRE(target == CNA_INVALID_HANDLE);
    {
        char message[128];
        uint64_t bytes = 0U;
        static const char expected[] = "The graphics-device handle is invalid for this call.";
        EXPECT_RESULT(cna_error_copy_last_message(message, sizeof(message), &bytes), CNA_RESULT_SUCCESS);
        REQUIRE(bytes == sizeof(expected) - 1U && memcmp(message, expected, (size_t)bytes) == 0);
        printf("expired component-callback device loan: %.*s\n", (int)bytes, message);
    }
    if (strcmp(mode, "exit-regression") == 0) {
        puts("leaving game, component and standalone device for exit cleanup after failed creation");
        return 1;
    }
    CNA_Handle fresh = CNA_INVALID_HANDLE;
    EXPECT_RESULT(cna_game_get_graphics_device(game, &fresh), CNA_RESULT_SUCCESS);
    REQUIRE(fresh != device);
    EXPECT_RESULT(cna_render_target2d_create(fresh, &target_info, &target), CNA_RESULT_SUCCESS);
    EXPECT_RESULT(cna_render_target_destroy(target), CNA_RESULT_SUCCESS);
    EXPECT_RESULT(cna_game_component_destroy(component.handle), CNA_RESULT_SUCCESS);
    EXPECT_RESULT(cna_game_destroy(game), CNA_RESULT_SUCCESS);
    puts("fresh loan after component callback: PASS");
    return 1;
}

int main(int argc, char** argv)
{
    unsigned first, end;
    if (argc == 2 && strcmp(argv[1], "membership-regression") == 0) return membership_regression() ? 0 : 1;
    if (argc == 2 && strcmp(argv[1], "standalone-exit-regression") == 0) return standalone_exit_regression(0) ? 0 : 1;
    if (argc == 2 && strcmp(argv[1], "recreate-exit-regression") == 0) return standalone_exit_regression(1) ? 0 : 1;
    if (argc == 2 && (strcmp(argv[1], "borrow-regression") == 0 ||
        strcmp(argv[1], "exit-regression") == 0 || strcmp(argv[1], "device-platform-regression") == 0))
        return reduced_regression(argv[1]) ? 0 : 1;
    if (!sequence_range(argc, argv, &first, &end)) return 2;
    if (!run_campaign(first, end)) return 1;
    sequence_summary(end - first);
    return 0;
}
