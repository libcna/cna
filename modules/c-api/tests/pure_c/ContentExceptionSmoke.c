/* SPDX-License-Identifier: MS-PL */
/* ISSUE-103: all failed content-loader routes preserve canonical exception metadata. */
#include <CNA/C/cna.h>
#include <stdio.h>
#include <string.h>
typedef struct ProbeState {
    int checks;
    int failures;
    const char* root;
} ProbeState;

static int probe_check(ProbeState* state, int condition, const char* label)
{
    ++state->checks;
    if (!condition) {
        ++state->failures;
        fprintf(stderr, "FAILED: %s\n", label);
    }
    return condition;
}

static int probe_check_result(ProbeState* state, CNA_Result result, const char* label)
{
    if (result != CNA_RESULT_SUCCESS)
        fprintf(stderr, "%s returned %u\n", label, (unsigned)result);
    return probe_check(state, result == CNA_RESULT_SUCCESS, label);
}

static int probe_finish(const ProbeState* state)
{
    printf("checks=%d failures=%d\n", state->checks, state->failures);
    return state->failures == 0 ? 0 : 1;
}

static CNA_StringView view(const char* value)
{
    const CNA_StringView result = {value, strlen(value)};
    return result;
}

static void check_missing(ProbeState* demo, const char* route, CNA_Result result, int cleared)
{
    static const char expected[] = "Microsoft.Xna.Framework.Content.ContentLoadException";
    CNA_ErrorInfo info = {0};
    uint64_t bytes = UINT64_MAX, copied = UINT64_MAX, parameter_bytes = UINT64_MAX;
    char type[128] = {0};
    info.struct_size = (uint32_t)sizeof(info);
    info.struct_version = UINT32_C(1);
    const CNA_Result info_result = cna_error_get_last_info(&info);
    const CNA_Result size_result = cna_error_get_last_exception_type_size_ext(&bytes);
    const CNA_Result copy_result = cna_error_copy_last_exception_type_ext(type, sizeof(type), &copied);
    const CNA_Result parameter_result = cna_error_get_last_exception_param_name_size_ext(&parameter_bytes);
    printf("route=%s result=%u exception=%.*s\n", route, (unsigned)result,
           copied < sizeof(type) ? (int)copied : 0, type);
    probe_check(demo, result == CNA_RESULT_IO, "failed native asset load returns IO");
    probe_check(demo, cleared, "failed load clears every output handle");
    probe_check(demo, info_result == CNA_RESULT_SUCCESS && info.result == CNA_RESULT_IO &&
                   info.category == CNA_ERROR_CATEGORY_IO && info.message_byte_length > 0U,
                   "structured IO result, category and nonempty message are retained");
    probe_check(demo, size_result == CNA_RESULT_SUCCESS && copy_result == CNA_RESULT_SUCCESS &&
                   bytes == sizeof(expected) - 1U && copied == bytes &&
                   memcmp(type, expected, sizeof(expected) - 1U) == 0,
                   "the canonical ContentLoadException identity survives translation");
    probe_check(demo, parameter_result == CNA_RESULT_SUCCESS && parameter_bytes == 0U,
                   "a failed asset load has no argument parameter name");
}

static CNA_Result load(CNA_Handle game, const CNA_GameTime* time, void* context,
                       CNA_CallbackError* error)
{
    ProbeState* demo = (ProbeState*)context;
    CNA_Handle device = CNA_INVALID_HANDLE, manager = CNA_INVALID_HANDLE;
    CNA_ContentManagerCreateInfo create = {0};
    CNA_Result result;
    (void)time;
    (void)error;
    result = cna_game_get_graphics_device(game, &device);
    if (result != CNA_RESULT_SUCCESS) return result;
    create.struct_size = (uint32_t)sizeof(create);
    create.struct_version = UINT32_C(1);
    const char* fixture_root = demo->root;
    create.root_directory = view(fixture_root);
    result = cna_content_manager_create(device, &create, &manager);
    if (result != CNA_RESULT_SUCCESS) return result;
    const CNA_StringView asset = view("invalid");
    struct Route {
        const char* name;
        CNA_Result (*invoke)(CNA_Handle, CNA_StringView, CNA_Handle*);
    };
    static const struct Route routes[] = {
        {"Texture2D", cna_content_manager_load_texture2d},
        {"TextureCube", cna_content_manager_load_texture_cube},
        {"SoundEffect", cna_content_manager_load_sound_effect},
        {"Effect", cna_content_manager_load_effect},
        {"Model", cna_content_manager_load_model},
        {"ObjectDictionary", cna_content_manager_load_object_dictionary_ext}
    };
    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); ++i) {
        CNA_Handle output = UINT64_C(99);
        result = routes[i].invoke(manager, asset, &output);
        check_missing(demo, routes[i].name, result, output == CNA_INVALID_HANDLE);
    }
    {
        CNA_Handle font = UINT64_C(99), atlas = UINT64_C(99);
        result = cna_content_manager_load_sprite_font(manager, asset, &font, &atlas);
        check_missing(demo, "SpriteFont", result,
                      font == CNA_INVALID_HANDLE && atlas == CNA_INVALID_HANDLE);
    }
    {
        int sentinel = 0;
        void* object = &sentinel;
        result = cna_content_manager_load_foreign_ext(manager, asset, &object);
        check_missing(demo, "ForeignObject", result, object == NULL);
    }
    result = cna_content_manager_destroy(manager);
    if (result != CNA_RESULT_SUCCESS) return result;
    return cna_game_request_exit(game);
}

int main(int argc, char** argv)
{
    ProbeState demo = {0};
    CNA_GameCallbacks callbacks = {0};
    CNA_GameCreateInfo create = {0};
    CNA_Handle game = CNA_INVALID_HANDLE;
    if (argc != 2) {
        fprintf(stderr, "Expected the directory containing invalid.xnb\n");
        return 2;
    }
    demo.root = argv[1];
    callbacks.struct_size = (uint32_t)sizeof(callbacks);
    callbacks.struct_version = UINT32_C(1);
    callbacks.load_content = load;
    callbacks.context = &demo;
    create.struct_size = (uint32_t)sizeof(create);
    create.struct_version = UINT32_C(1);
    create.is_fixed_time_step = CNA_TRUE;
    create.target_elapsed_time_ticks = INT64_C(166667);
    create.callbacks = &callbacks;
    if (!probe_check_result(&demo, cna_game_create(&create, &game), "create probe game"))
        return probe_finish(&demo);
    probe_check_result(&demo, cna_game_run(game), "execute every missing-asset probe");
    probe_check_result(&demo, cna_game_destroy(game), "destroy probe game");
    probe_check(&demo, demo.checks == 43, "all eight five-check routes and setup controls ran");
    return probe_finish(&demo);
}
