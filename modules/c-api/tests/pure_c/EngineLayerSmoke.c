// SPDX-License-Identifier: MS-PL

/*
 * plans/plan_binding.md CBIND-084A. This suite runs in both builds and asserts different things in
 * each, because the ABI's promise is that both builds export the same routes and answer honestly:
 * with CNA_CNAEXT on, every route does its work; with it off, every route that needs a native
 * engine-layer object returns CNA_RESULT_NOT_SUPPORTED and touches nothing. A test that only ran
 * in one configuration would prove half the contract.
 */

#include <CNA/C/cna.h>

#include <stdint.h>
#include <string.h>

typedef struct EngineLayerState {
    CNA_Bool available;
    CNA_Bool had_compute;
    int validated;
    /* Which validator refused, so a failing exit code names the family rather than only the
     * callback. Set to the first one that fails; zero when everything passed. */
    int failed_stage;
} EngineLayerState;

static CNA_StringView string_view(const char* const text)
{
    const CNA_StringView result = {text, (uint64_t)strlen(text)};
    return result;
}

static int current_render_target_is(
    const CNA_Handle graphics_device,
    const CNA_Handle expected)
{
    CNA_RenderTargetBinding binding;
    uint64_t count = UINT64_C(7);
    if (expected == CNA_INVALID_HANDLE) {
        return cna_graphics_device_get_render_target_count(graphics_device, &count) ==
                CNA_RESULT_SUCCESS &&
            count == UINT64_C(0);
    }
    return cna_graphics_device_copy_render_targets(
               graphics_device, &binding, UINT64_C(1), &count) == CNA_RESULT_SUCCESS &&
        count == UINT64_C(1) && binding.render_target == expected;
}

/* --- identities and pure values, which work in either build ------------------------------- */

static int validate_identities(void)
{
    return CNA_DEPTH_ENCODING_AUTOMATIC == UINT32_C(0) &&
        CNA_DEPTH_ENCODING_PACKED == UINT32_C(1) &&
        CNA_DEPTH_ENCODING_HALF_FLOAT == UINT32_C(2) &&
        CNA_GRAPHICS_IMAGE_ACCESS_READ_ONLY == UINT32_C(0) &&
        CNA_GRAPHICS_IMAGE_ACCESS_WRITE_ONLY == UINT32_C(1) &&
        CNA_GRAPHICS_IMAGE_ACCESS_READ_WRITE == UINT32_C(2) &&
        CNA_GRAPHICS_MEMORY_BARRIER_NONE == UINT32_C(0) &&
        CNA_GRAPHICS_MEMORY_BARRIER_VERTEX_ATTRIB_ARRAY == (UINT32_C(1) << 0) &&
        CNA_GRAPHICS_MEMORY_BARRIER_ELEMENT_ARRAY == (UINT32_C(1) << 1) &&
        CNA_GRAPHICS_MEMORY_BARRIER_UNIFORM == (UINT32_C(1) << 2) &&
        CNA_GRAPHICS_MEMORY_BARRIER_TEXTURE_FETCH == (UINT32_C(1) << 3) &&
        CNA_GRAPHICS_MEMORY_BARRIER_SHADER_IMAGE_ACCESS == (UINT32_C(1) << 4) &&
        CNA_GRAPHICS_MEMORY_BARRIER_SHADER_STORAGE == (UINT32_C(1) << 5) &&
        CNA_GRAPHICS_MEMORY_BARRIER_BUFFER_UPDATE == (UINT32_C(1) << 6) &&
        CNA_GRAPHICS_MEMORY_BARRIER_FRAMEBUFFER == (UINT32_C(1) << 7) &&
        CNA_GRAPHICS_MEMORY_BARRIER_INDIRECT_COMMAND == (UINT32_C(1) << 8) &&
        CNA_GRAPHICS_MEMORY_BARRIER_ALL == UINT32_C(0x1FF);
}

/* The canonical HasBarrier is a containment test, not an intersection test: a mask containing only
 * one of two requested bits must answer false. That is the arm a `&`-and-compare-nonzero
 * implementation would get wrong, so it is the arm worth asserting. */
static int validate_barrier_containment(void)
{
    CNA_Bool contains = UINT8_C(9);
    const CNA_GraphicsMemoryBarrier pair = CNA_GRAPHICS_MEMORY_BARRIER_UNIFORM |
        CNA_GRAPHICS_MEMORY_BARRIER_TEXTURE_FETCH;

    if (cna_graphics_memory_barrier_has(
            CNA_GRAPHICS_MEMORY_BARRIER_ALL, pair, &contains) != CNA_RESULT_SUCCESS ||
        contains != CNA_TRUE) {
        return 0;
    }
    if (cna_graphics_memory_barrier_has(
            CNA_GRAPHICS_MEMORY_BARRIER_UNIFORM, pair, &contains) != CNA_RESULT_SUCCESS ||
        contains != CNA_FALSE) {
        return 0;
    }
    if (cna_graphics_memory_barrier_has(
            CNA_GRAPHICS_MEMORY_BARRIER_NONE,
            CNA_GRAPHICS_MEMORY_BARRIER_NONE,
            &contains) != CNA_RESULT_SUCCESS ||
        contains != CNA_TRUE) {
        return 0;
    }
    return cna_graphics_memory_barrier_has(
               CNA_GRAPHICS_MEMORY_BARRIER_ALL, pair, 0) == CNA_RESULT_INVALID_ARGUMENT;
}

/* The header's macro says what this translation unit compiled against; the route says what the
 * library it linked to reports. Equal in a build with the layer, and 0 from the library without
 * one -- which is the mismatch the pair exists to expose. */
static int validate_version(const CNA_Bool available)
{
    int32_t version = INT32_C(-1);
    char text[64];
    uint64_t bytes = UINT64_C(0);

    if (cna_engine_layer_get_version(&version) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    if (cna_engine_layer_get_version(0) != CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    if (available == CNA_TRUE) {
        if (version != CNA_ENGINE_LAYER_VERSION) {
            return 0;
        }
        if (cna_engine_layer_copy_version_string(0, UINT64_C(0), &bytes) !=
                CNA_RESULT_BUFFER_TOO_SMALL ||
            bytes == UINT64_C(0) || bytes > sizeof(text)) {
            return 0;
        }
        memset(text, 0, sizeof(text));
        if (cna_engine_layer_copy_version_string(text, sizeof(text), &bytes) !=
            CNA_RESULT_SUCCESS) {
            return 0;
        }
        return bytes > UINT64_C(0) && text[0] != '\0';
    }
    if (version != INT32_C(0)) {
        return 0;
    }
    return cna_engine_layer_copy_version_string(text, sizeof(text), &bytes) ==
        CNA_RESULT_NOT_SUPPORTED;
}

/* --- the layer-absent build: every object route refuses and writes nothing ----------------- */

/* CBIND-084C. The pass machinery and the material binding refuse the same way the resources do,
 * and a refused creation must leave the output handle invalid rather than merely unwritten. */
static int validate_passes_unavailable(const CNA_Handle graphics_device)
{
    CNA_FullscreenPassHandle fullscreen = (CNA_FullscreenPassHandle)UINT64_C(0x5A5A5A5A);
    CNA_PostProcessPassHandle pass = (CNA_PostProcessPassHandle)UINT64_C(0x5A5A5A5A);
    CNA_EffectHandle effect = (CNA_EffectHandle)UINT64_C(0x5A5A5A5A);
    CNA_PostProcessContext context;
    CNA_PbrMaterialEXT material;
    static const char name[] = "pass";
    const CNA_StringView name_view = {name, sizeof(name) - 1U};
    CNA_Bool flag = UINT8_C(9);
    uint64_t bytes = UINT64_C(7);
    char text[8];

    /* The two value routes work in every build, so they must succeed even here. */
    if (cna_post_process_context_init(&context) != CNA_RESULT_SUCCESS ||
        cna_pbr_material_ext_init(&material) != CNA_RESULT_SUCCESS) {
        return 0;
    }

    if (cna_fullscreen_pass_create(graphics_device, &fullscreen) != CNA_RESULT_NOT_SUPPORTED ||
        fullscreen != CNA_INVALID_HANDLE) {
        return 0;
    }
    if (cna_fullscreen_pass_draw(
            fullscreen, CNA_INVALID_HANDLE, CNA_INVALID_HANDLE, CNA_INVALID_HANDLE,
            INT32_C(4), INT32_C(4), 0) != CNA_RESULT_NOT_SUPPORTED ||
        cna_fullscreen_pass_draw_over_current_target(
            fullscreen, CNA_INVALID_HANDLE, CNA_INVALID_HANDLE, INT32_C(4), INT32_C(4), 0) !=
            CNA_RESULT_NOT_SUPPORTED ||
        cna_fullscreen_pass_destroy(fullscreen) != CNA_RESULT_NOT_SUPPORTED) {
        return 0;
    }

    if (cna_blit_pass_create(graphics_device, &pass) != CNA_RESULT_NOT_SUPPORTED ||
        pass != CNA_INVALID_HANDLE) {
        return 0;
    }
    pass = (CNA_PostProcessPassHandle)UINT64_C(0x5A5A5A5A);
    if (cna_post_process_effect_pass_create(graphics_device, effect, name_view, &pass) !=
            CNA_RESULT_NOT_SUPPORTED ||
        pass != CNA_INVALID_HANDLE) {
        return 0;
    }
    pass = (CNA_PostProcessPassHandle)UINT64_C(0x5A5A5A5A);
    if (cna_post_process_effect_pass_create_owning(graphics_device, effect, name_view, &pass) !=
            CNA_RESULT_NOT_SUPPORTED ||
        pass != CNA_INVALID_HANDLE) {
        return 0;
    }
    if (cna_post_process_effect_pass_get_effect(pass, &effect) != CNA_RESULT_NOT_SUPPORTED ||
        effect != CNA_INVALID_HANDLE) {
        return 0;
    }
    if (cna_post_process_effect_pass_set_effect(pass, effect) != CNA_RESULT_NOT_SUPPORTED ||
        cna_post_process_pass_apply(pass, &context) != CNA_RESULT_NOT_SUPPORTED ||
        cna_post_process_pass_copy_name(pass, text, sizeof(text), &bytes) !=
            CNA_RESULT_NOT_SUPPORTED ||
        cna_post_process_pass_is_supported(pass, graphics_device, &flag) !=
            CNA_RESULT_NOT_SUPPORTED ||
        cna_post_process_pass_destroy(pass) != CNA_RESULT_NOT_SUPPORTED) {
        return 0;
    }

    if (cna_pbr_effect_apply_material(effect, &material) != CNA_RESULT_NOT_SUPPORTED ||
        cna_skinned_pbr_effect_apply_material(effect, &material) != CNA_RESULT_NOT_SUPPORTED ||
        cna_pbr_effect_extract_material(effect, &material) != CNA_RESULT_NOT_SUPPORTED ||
        cna_skinned_pbr_effect_extract_material(effect, &material) != CNA_RESULT_NOT_SUPPORTED ||
        cna_pbr_material_apply_state(&material, graphics_device) != CNA_RESULT_NOT_SUPPORTED) {
        return 0;
    }
    /* The CNA_Bool output must be untouched by a refusal. `bytes` deliberately is not asserted:
     * a copy route reports a required byte count of zero when it refuses, which is the shape the
     * resource copy routes already established, and zero is not a value a caller can mistake for
     * a successful measurement. */
    return flag == UINT8_C(9) && bytes == UINT64_C(0);
}

static int validate_unavailable(const CNA_Handle graphics_device)
{
    CNA_StorageBufferHandle buffer = (CNA_StorageBufferHandle)UINT64_C(0x5A5A5A5A);
    CNA_ComputeShaderHandle shader = (CNA_ComputeShaderHandle)UINT64_C(0x5A5A5A5A);
    static const char source[] = "#version 310 es\nvoid main() {}\n";
    const CNA_StringView source_view = {source, sizeof(source) - 1U};
    uint64_t value = UINT64_C(7);
    CNA_Bool flag = UINT8_C(9);
    double milliseconds = 17.0;
    int32_t samples = INT32_C(19);
    char text[8];
    unsigned char bytes[4] = {1U, 2U, 3U, 4U};

    if (cna_storage_buffer_create(graphics_device, UINT64_C(16), &buffer) !=
            CNA_RESULT_NOT_SUPPORTED ||
        buffer != CNA_INVALID_HANDLE) {
        return 0;
    }
    if (cna_storage_buffer_create_typed(graphics_device, UINT64_C(4), UINT64_C(4), &buffer) !=
            CNA_RESULT_NOT_SUPPORTED ||
        buffer != CNA_INVALID_HANDLE) {
        return 0;
    }
    if (cna_storage_buffer_set_bytes(buffer, bytes, sizeof(bytes)) != CNA_RESULT_NOT_SUPPORTED ||
        cna_storage_buffer_get_bytes(buffer, bytes, sizeof(bytes)) != CNA_RESULT_NOT_SUPPORTED ||
        cna_storage_buffer_get_byte_size(buffer, &value) != CNA_RESULT_NOT_SUPPORTED ||
        cna_storage_buffer_set_elements(buffer, bytes, UINT64_C(1), UINT64_C(4)) !=
            CNA_RESULT_NOT_SUPPORTED ||
        cna_storage_buffer_get_elements(buffer, bytes, UINT64_C(1), UINT64_C(4)) !=
            CNA_RESULT_NOT_SUPPORTED ||
        cna_storage_buffer_get_element_count(buffer, &value) != CNA_RESULT_NOT_SUPPORTED ||
        cna_storage_buffer_get_element_byte_size(buffer, &value) != CNA_RESULT_NOT_SUPPORTED ||
        cna_storage_buffer_destroy(buffer) != CNA_RESULT_NOT_SUPPORTED) {
        return 0;
    }
    /* A refusal must leave the caller's outputs alone; a route that "helpfully" zeroed them would
     * be indistinguishable from one that succeeded and reported zero. */
    if (value != UINT64_C(7) || bytes[0] != 1U || bytes[3] != 4U) {
        return 0;
    }

    if (cna_compute_shader_create(graphics_device, source_view, &shader) !=
            CNA_RESULT_NOT_SUPPORTED ||
        shader != CNA_INVALID_HANDLE) {
        return 0;
    }
    if (cna_compute_shader_set_uniform_int(shader, source_view, INT32_C(1)) !=
            CNA_RESULT_NOT_SUPPORTED ||
        cna_compute_shader_set_uniform_float(shader, source_view, 1.0F) !=
            CNA_RESULT_NOT_SUPPORTED ||
        cna_compute_shader_bind_storage_buffer(shader, INT32_C(0), buffer) !=
            CNA_RESULT_NOT_SUPPORTED ||
        cna_compute_shader_bind_texture(shader, INT32_C(0), source_view, CNA_INVALID_HANDLE) !=
            CNA_RESULT_NOT_SUPPORTED ||
        cna_compute_shader_is_image_binding_supported(shader, &flag) != CNA_RESULT_NOT_SUPPORTED ||
        cna_compute_shader_bind_image(
            shader, INT32_C(0), CNA_INVALID_HANDLE, CNA_GRAPHICS_IMAGE_ACCESS_READ_WRITE) !=
            CNA_RESULT_NOT_SUPPORTED ||
        cna_compute_shader_dispatch(shader, INT32_C(1), INT32_C(1), INT32_C(1)) !=
            CNA_RESULT_NOT_SUPPORTED ||
        cna_compute_shader_barrier(shader, CNA_GRAPHICS_MEMORY_BARRIER_ALL) !=
            CNA_RESULT_NOT_SUPPORTED ||
        cna_compute_shader_is_valid(shader, &flag) != CNA_RESULT_NOT_SUPPORTED ||
        cna_compute_shader_copy_compile_error(shader, text, sizeof(text), &value) !=
            CNA_RESULT_NOT_SUPPORTED ||
        cna_compute_shader_destroy(shader) != CNA_RESULT_NOT_SUPPORTED) {
        return 0;
    }
    if (flag != UINT8_C(9)) {
        return 0;
    }
    value = UINT64_C(7);

    {
        CNA_GpuTimerHandle timer = UINT64_C(0x5A5A5A5A);
        if (cna_gpu_timer_create(graphics_device, &timer) != CNA_RESULT_NOT_SUPPORTED ||
            timer != CNA_INVALID_HANDLE ||
            cna_gpu_timer_is_supported(timer, &flag) != CNA_RESULT_NOT_SUPPORTED ||
            cna_gpu_timer_copy_unsupported_reason(timer, text, sizeof(text), &value) !=
                CNA_RESULT_NOT_SUPPORTED ||
            cna_gpu_timer_begin(timer) != CNA_RESULT_NOT_SUPPORTED ||
            cna_gpu_timer_end(timer) != CNA_RESULT_NOT_SUPPORTED ||
            cna_gpu_timer_is_result_available(timer, &flag) != CNA_RESULT_NOT_SUPPORTED ||
            cna_gpu_timer_poll(timer, &flag) != CNA_RESULT_NOT_SUPPORTED ||
            cna_gpu_timer_get_last_milliseconds(timer, &milliseconds) !=
                CNA_RESULT_NOT_SUPPORTED ||
            cna_gpu_timer_get_sample_count(timer, &samples) != CNA_RESULT_NOT_SUPPORTED ||
            cna_gpu_timer_is_open(timer, &flag) != CNA_RESULT_NOT_SUPPORTED ||
            cna_gpu_timer_destroy(timer) != CNA_RESULT_NOT_SUPPORTED) {
            return 0;
        }
    }
    {
        CNA_RenderTargetPoolHandle pool = UINT64_C(0x5A5A5A5A);
        CNA_Handle target = UINT64_C(0x5A5A5A5A);
        if (cna_render_target_pool_create(graphics_device, &pool) != CNA_RESULT_NOT_SUPPORTED ||
            pool != CNA_INVALID_HANDLE ||
            cna_render_target_pool_acquire(
                pool,
                INT32_C(2),
                INT32_C(3),
                CNA_SURFACE_FORMAT_COLOR,
                CNA_DEPTH_FORMAT_NONE,
                INT32_C(0),
                &target) != CNA_RESULT_NOT_SUPPORTED ||
            target != CNA_INVALID_HANDLE ||
            cna_render_target_pool_reset(pool) != CNA_RESULT_NOT_SUPPORTED ||
            cna_render_target_pool_get_target_count(pool, &value) != CNA_RESULT_NOT_SUPPORTED ||
            cna_render_target_pool_get_estimated_bytes(pool, &value) !=
                CNA_RESULT_NOT_SUPPORTED ||
            cna_render_target_pool_destroy(pool) != CNA_RESULT_NOT_SUPPORTED) {
            return 0;
        }
    }
    {
        CNA_ShaderEffectFactoryHandle factory = UINT64_C(0x5A5A5A5A);
        CNA_EffectHandle effect = UINT64_C(0x5A5A5A5A);
        if (cna_shader_effect_factory_create(graphics_device, &factory) !=
                CNA_RESULT_NOT_SUPPORTED ||
            factory != CNA_INVALID_HANDLE ||
            cna_shader_effect_factory_acquire(
                factory, source_view, source_view, source_view, &effect) !=
                CNA_RESULT_NOT_SUPPORTED ||
            effect != CNA_INVALID_HANDLE ||
            cna_shader_effect_factory_contains(factory, source_view, &flag) !=
                CNA_RESULT_NOT_SUPPORTED ||
            cna_shader_effect_factory_get_compile_count(factory, &value) !=
                CNA_RESULT_NOT_SUPPORTED ||
            cna_shader_effect_factory_clear(factory) != CNA_RESULT_NOT_SUPPORTED ||
            cna_shader_effect_factory_destroy(factory) != CNA_RESULT_NOT_SUPPORTED) {
            return 0;
        }
    }
    {
        CNA_ScopedRenderTargetHandle scope = UINT64_C(0x5A5A5A5A);
        if (cna_scoped_render_target_begin(
                graphics_device, CNA_INVALID_HANDLE, &scope) != CNA_RESULT_NOT_SUPPORTED ||
            scope != CNA_INVALID_HANDLE ||
            cna_scoped_render_target_get_has_recorded_previous(scope, &flag) !=
                CNA_RESULT_NOT_SUPPORTED ||
            cna_scoped_render_target_end(scope) != CNA_RESULT_NOT_SUPPORTED) {
            return 0;
        }
    }
    if (!validate_passes_unavailable(graphics_device)) {
        return 0;
    }
    return flag == UINT8_C(9) && value == UINT64_C(7) &&
        milliseconds == 17.0 && samples == INT32_C(19);
}

/* --- the layer-present build ---------------------------------------------------------------- */

/* StorageBuffer's constructor throws System::NotSupportedException where the renderer has no
 * compute support, and the firewall turns that into CNA_RESULT_NOT_SUPPORTED. That is a real
 * contract with a real arm, so it is asserted rather than skipped: on a renderer without compute
 * the refusal itself is what this suite proves, and it must leave the output handle invalid. */
static int validate_compute_absent(const CNA_Handle graphics_device)
{
    CNA_StorageBufferHandle buffer = (CNA_StorageBufferHandle)UINT64_C(0x5A5A5A5A);
    CNA_ComputeShaderHandle shader = (CNA_ComputeShaderHandle)UINT64_C(0x5A5A5A5A);
    static const char source[] = "#version 310 es\nvoid main() {}\n";
    const CNA_StringView source_view = {source, sizeof(source) - 1U};

    if (cna_storage_buffer_create(graphics_device, UINT64_C(16), &buffer) !=
            CNA_RESULT_NOT_SUPPORTED ||
        buffer != CNA_INVALID_HANDLE) {
        return 0;
    }
    if (cna_storage_buffer_create_typed(graphics_device, UINT64_C(4), UINT64_C(4), &buffer) !=
            CNA_RESULT_NOT_SUPPORTED ||
        buffer != CNA_INVALID_HANDLE) {
        return 0;
    }
    /* The compute shader itself is more forgiving than the buffer: it records a failure rather
     * than throwing, so accept either a refusal or a shader that reports itself invalid. */
    if (cna_compute_shader_create(graphics_device, source_view, &shader) ==
        CNA_RESULT_SUCCESS) {
        CNA_Bool valid = UINT8_C(9);
        const int reports_invalid =
            cna_compute_shader_is_valid(shader, &valid) == CNA_RESULT_SUCCESS &&
            valid == CNA_FALSE;
        return cna_compute_shader_destroy(shader) == CNA_RESULT_SUCCESS && reports_invalid;
    }
    return shader == CNA_INVALID_HANDLE;
}

static int validate_storage_buffer(const CNA_Handle graphics_device)
{
    CNA_StorageBufferHandle buffer = CNA_INVALID_HANDLE;
    uint64_t value = UINT64_C(0);
    unsigned char written[8] = {1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U};
    unsigned char read_back[8];

    if (cna_storage_buffer_create(graphics_device, sizeof(written), &buffer) !=
            CNA_RESULT_SUCCESS ||
        buffer == CNA_INVALID_HANDLE) {
        return 0;
    }
    if (cna_storage_buffer_get_byte_size(buffer, &value) != CNA_RESULT_SUCCESS ||
        value != (uint64_t)sizeof(written)) {
        return 0;
    }
    /* Created by byte size, so it has no element shape and the element routes must say so rather
     * than invent one. */
    if (cna_storage_buffer_get_element_count(buffer, &value) != CNA_RESULT_SUCCESS ||
        value != UINT64_C(0)) {
        return 0;
    }
    if (cna_storage_buffer_get_element_byte_size(buffer, &value) != CNA_RESULT_SUCCESS ||
        value != UINT64_C(0)) {
        return 0;
    }
    if (cna_storage_buffer_set_elements(buffer, written, UINT64_C(1), UINT64_C(4)) !=
        CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    if (cna_storage_buffer_set_bytes(buffer, written, sizeof(written)) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    memset(read_back, 0, sizeof(read_back));
    if (cna_storage_buffer_get_bytes(buffer, read_back, sizeof(read_back)) != CNA_RESULT_SUCCESS ||
        memcmp(read_back, written, sizeof(written)) != 0) {
        return 0;
    }
    if (cna_storage_buffer_set_bytes(buffer, written, (uint64_t)sizeof(written) + UINT64_C(1)) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_storage_buffer_get_bytes(buffer, read_back, (uint64_t)sizeof(read_back) + UINT64_C(1)) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_storage_buffer_set_bytes(buffer, 0, UINT64_C(4)) != CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    if (cna_storage_buffer_destroy(buffer) != CNA_RESULT_SUCCESS ||
        cna_storage_buffer_destroy(buffer) == CNA_RESULT_SUCCESS) {
        return 0;
    }
    return 1;
}

/* The C form of StorageBufferT<T>: the element type becomes an element size the buffer carries,
 * which is what lets it enforce the template's own "more elements than the buffer holds" refusal. */
static int validate_typed_storage_buffer(const CNA_Handle graphics_device)
{
    CNA_StorageBufferHandle buffer = CNA_INVALID_HANDLE;
    uint64_t value = UINT64_C(0);
    int32_t written[4] = {11, 22, 33, 44};
    int32_t read_back[4];

    if (cna_storage_buffer_create_typed(
            graphics_device, UINT64_C(4), sizeof(int32_t), &buffer) != CNA_RESULT_SUCCESS ||
        buffer == CNA_INVALID_HANDLE) {
        return 0;
    }
    if (cna_storage_buffer_get_element_count(buffer, &value) != CNA_RESULT_SUCCESS ||
        value != UINT64_C(4)) {
        return 0;
    }
    if (cna_storage_buffer_get_element_byte_size(buffer, &value) != CNA_RESULT_SUCCESS ||
        value != (uint64_t)sizeof(int32_t)) {
        return 0;
    }
    if (cna_storage_buffer_get_byte_size(buffer, &value) != CNA_RESULT_SUCCESS ||
        value != (uint64_t)sizeof(written)) {
        return 0;
    }
    if (cna_storage_buffer_set_elements(
            buffer, written, UINT64_C(4), sizeof(int32_t)) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    memset(read_back, 0, sizeof(read_back));
    if (cna_storage_buffer_get_elements(
            buffer, read_back, UINT64_C(4), sizeof(int32_t)) != CNA_RESULT_SUCCESS ||
        memcmp(read_back, written, sizeof(written)) != 0) {
        return 0;
    }
    /* setData accepts a shorter vector; getData always returns the whole range. */
    if (cna_storage_buffer_set_elements(
            buffer, written, UINT64_C(2), sizeof(int32_t)) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    if (cna_storage_buffer_get_elements(
            buffer, read_back, UINT64_C(2), sizeof(int32_t)) != CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    /* The template's own refusal, and the one a byte API cannot express. */
    if (cna_storage_buffer_set_elements(
            buffer, written, UINT64_C(5), sizeof(int32_t)) != CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    /* A disagreeing element size is an argument error, never a silent reinterpretation. */
    if (cna_storage_buffer_set_elements(
            buffer, written, UINT64_C(2), sizeof(int16_t)) != CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    if (cna_storage_buffer_destroy(buffer) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    /* A refused creation must not produce a handle: these two would otherwise leave an owned
     * resource behind, and cna_game_destroy refuses while one is outstanding -- which is exactly
     * how the leak this test first had was found. */
    buffer = (CNA_StorageBufferHandle)UINT64_C(0x5A5A5A5A);
    if (cna_storage_buffer_create_typed(graphics_device, UINT64_C(4), UINT64_C(0), &buffer) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        buffer != CNA_INVALID_HANDLE) {
        return 0;
    }
    buffer = (CNA_StorageBufferHandle)UINT64_C(0x5A5A5A5A);
    if (cna_storage_buffer_create_typed(
            graphics_device, UINT64_MAX, UINT64_C(4), &buffer) != CNA_RESULT_INVALID_ARGUMENT ||
        buffer != CNA_INVALID_HANDLE) {
        return 0;
    }
    return 1;
}

/* Creation succeeds for source that does not compile: the shader records the failure and reports
 * it, because a renderer without compute is a documented boundary rather than a defect. */
static int validate_compute_shader(const CNA_Handle graphics_device)
{
    CNA_ComputeShaderHandle shader = CNA_INVALID_HANDLE;
    CNA_StorageBufferHandle buffer = CNA_INVALID_HANDLE;
    static const char source[] =
        "#version 310 es\nlayout(local_size_x = 1) in;\nvoid main() {}\n";
    static const char name[] = "uValue";
    const CNA_StringView source_view = {source, sizeof(source) - 1U};
    const CNA_StringView name_view = {name, sizeof(name) - 1U};
    CNA_Bool flag = UINT8_C(9);
    uint64_t bytes = UINT64_C(0);
    char text[512];
    int valid = 0;
    int ok = 0;

    if (cna_compute_shader_create(graphics_device, source_view, &shader) != CNA_RESULT_SUCCESS ||
        shader == CNA_INVALID_HANDLE) {
        return 0;
    }
    if (cna_compute_shader_is_valid(shader, &flag) != CNA_RESULT_SUCCESS ||
        (flag != CNA_TRUE && flag != CNA_FALSE)) {
        return 0;
    }
    valid = flag == CNA_TRUE;

    /* Whether it compiled or not, the error text must be readable and must agree with is_valid. */
    if (cna_compute_shader_copy_compile_error(shader, 0, UINT64_C(0), &bytes) !=
            (valid ? CNA_RESULT_SUCCESS : CNA_RESULT_BUFFER_TOO_SMALL) ||
        (valid && bytes != UINT64_C(0)) || (!valid && bytes == UINT64_C(0))) {
        return 0;
    }
    if (bytes <= sizeof(text) &&
        cna_compute_shader_copy_compile_error(shader, text, sizeof(text), &bytes) !=
            CNA_RESULT_SUCCESS) {
        return 0;
    }

    if (cna_compute_shader_is_image_binding_supported(shader, &flag) != CNA_RESULT_SUCCESS ||
        (flag != CNA_TRUE && flag != CNA_FALSE)) {
        return 0;
    }
    if (cna_compute_shader_set_uniform_int(shader, name_view, INT32_C(3)) != CNA_RESULT_SUCCESS ||
        cna_compute_shader_set_uniform_float(shader, name_view, 0.5F) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    if (cna_storage_buffer_create(graphics_device, UINT64_C(16), &buffer) != CNA_RESULT_SUCCESS ||
        cna_compute_shader_bind_storage_buffer(shader, INT32_C(0), buffer) !=
            CNA_RESULT_SUCCESS) {
        return 0;
    }
    if (cna_compute_shader_barrier(shader, CNA_GRAPHICS_MEMORY_BARRIER_SHADER_STORAGE) !=
            CNA_RESULT_SUCCESS ||
        cna_compute_shader_dispatch(shader, INT32_C(1), INT32_C(1), INT32_C(1)) !=
            CNA_RESULT_SUCCESS) {
        return 0;
    }
    /* Undefined identity values are refused rather than cast through. */
    if (cna_compute_shader_barrier(shader, UINT32_C(0x400)) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_compute_shader_bind_image(
            shader, INT32_C(0), CNA_INVALID_HANDLE, UINT32_C(3)) != CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    ok = cna_compute_shader_bind_storage_buffer(shader, INT32_C(0), CNA_INVALID_HANDLE) !=
        CNA_RESULT_SUCCESS;
    ok = ok && cna_storage_buffer_destroy(buffer) == CNA_RESULT_SUCCESS;
    ok = ok && cna_compute_shader_destroy(shader) == CNA_RESULT_SUCCESS;
    /* A second destroy must refuse rather than release a second time. */
    return ok && cna_compute_shader_destroy(shader) != CNA_RESULT_SUCCESS;
}

static int validate_gpu_timer(const CNA_Handle graphics_device)
{
    CNA_GpuTimerHandle timer = CNA_INVALID_HANDLE;
    CNA_Bool supported = UINT8_C(9);
    CNA_Bool state = UINT8_C(9);
    uint64_t bytes = UINT64_C(0);
    char reason[512];
    double milliseconds = -1.0;
    int32_t samples = -1;

    if (cna_gpu_timer_create(graphics_device, &timer) != CNA_RESULT_SUCCESS ||
        timer == CNA_INVALID_HANDLE ||
        cna_gpu_timer_is_supported(timer, &supported) != CNA_RESULT_SUCCESS ||
        (supported != CNA_TRUE && supported != CNA_FALSE)) {
        return 0;
    }
    if (cna_gpu_timer_copy_unsupported_reason(timer, 0, UINT64_C(0), &bytes) !=
            (supported == CNA_TRUE ? CNA_RESULT_SUCCESS : CNA_RESULT_BUFFER_TOO_SMALL) ||
        (supported == CNA_TRUE && bytes != UINT64_C(0)) ||
        (supported == CNA_FALSE && bytes == UINT64_C(0)) || bytes > sizeof(reason)) {
        return 0;
    }
    if (cna_gpu_timer_copy_unsupported_reason(timer, reason, sizeof(reason), &bytes) !=
        CNA_RESULT_SUCCESS) {
        return 0;
    }
    if (cna_gpu_timer_is_open(timer, &state) != CNA_RESULT_SUCCESS || state != CNA_FALSE ||
        cna_gpu_timer_begin(timer) != CNA_RESULT_SUCCESS ||
        cna_gpu_timer_is_open(timer, &state) != CNA_RESULT_SUCCESS ||
        state != supported || cna_gpu_timer_begin(timer) != CNA_RESULT_SUCCESS ||
        cna_gpu_timer_end(timer) != CNA_RESULT_SUCCESS ||
        cna_gpu_timer_is_open(timer, &state) != CNA_RESULT_SUCCESS || state != CNA_FALSE ||
        cna_gpu_timer_is_result_available(timer, &state) != CNA_RESULT_SUCCESS ||
        (state != CNA_TRUE && state != CNA_FALSE) ||
        cna_gpu_timer_poll(timer, &state) != CNA_RESULT_SUCCESS ||
        (state != CNA_TRUE && state != CNA_FALSE) ||
        cna_gpu_timer_get_last_milliseconds(timer, &milliseconds) != CNA_RESULT_SUCCESS ||
        milliseconds < 0.0 ||
        cna_gpu_timer_get_sample_count(timer, &samples) != CNA_RESULT_SUCCESS || samples < 0 ||
        cna_gpu_timer_is_supported(timer, 0) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_gpu_timer_destroy(timer) != CNA_RESULT_SUCCESS ||
        cna_gpu_timer_destroy(timer) == CNA_RESULT_SUCCESS) {
        return 0;
    }
    return 1;
}

static int validate_render_target_pool_and_scopes(const CNA_Handle graphics_device)
{
    CNA_RenderTargetPoolHandle pool = CNA_INVALID_HANDLE;
    CNA_Handle first = CNA_INVALID_HANDLE;
    CNA_Handle duplicate = CNA_INVALID_HANDLE;
    CNA_Handle second = CNA_INVALID_HANDLE;
    CNA_ScopedRenderTargetHandle outer = CNA_INVALID_HANDLE;
    CNA_ScopedRenderTargetHandle inner = CNA_INVALID_HANDLE;
    CNA_RenderTargetInfo info = {0};
    CNA_Bool outer_recorded = UINT8_C(9);
    CNA_Bool inner_recorded = UINT8_C(9);
    uint64_t value = UINT64_C(0);

    info.struct_size = sizeof(CNA_RenderTargetInfo);
    info.struct_version = UINT32_C(1);
    if (cna_render_target_pool_create(graphics_device, &pool) != CNA_RESULT_SUCCESS ||
        pool == CNA_INVALID_HANDLE ||
        cna_render_target_pool_acquire(
            pool,
            INT32_C(2),
            INT32_C(3),
            CNA_SURFACE_FORMAT_COLOR,
            CNA_DEPTH_FORMAT_NONE,
            INT32_C(0),
            &first) != CNA_RESULT_SUCCESS ||
        first == CNA_INVALID_HANDLE ||
        cna_render_target_pool_get_target_count(pool, &value) != CNA_RESULT_SUCCESS ||
        value != UINT64_C(1) ||
        cna_render_target_pool_get_estimated_bytes(pool, &value) != CNA_RESULT_SUCCESS ||
        value != UINT64_C(24) ||
        cna_render_target_get_info(first, &info) != CNA_RESULT_SUCCESS ||
        info.kind != CNA_RENDER_TARGET_KIND_2D || info.width != UINT32_C(2) ||
        info.height != UINT32_C(3)) {
        return 0;
    }
    if (cna_render_target_pool_acquire(
            pool,
            INT32_C(2),
            INT32_C(3),
            CNA_SURFACE_FORMAT_COLOR,
            CNA_DEPTH_FORMAT_NONE,
            INT32_C(0),
            &duplicate) != CNA_RESULT_SUCCESS ||
        cna_render_target_pool_get_target_count(pool, &value) != CNA_RESULT_SUCCESS ||
        value != UINT64_C(1) ||
        cna_render_target_pool_acquire(
            pool,
            INT32_C(2),
            INT32_C(3),
            CNA_SURFACE_FORMAT_COLOR,
            CNA_DEPTH_FORMAT_NONE,
            INT32_C(1),
            &second) != CNA_RESULT_SUCCESS ||
        cna_render_target_pool_get_target_count(pool, &value) != CNA_RESULT_SUCCESS ||
        value != UINT64_C(2) ||
        cna_render_target_pool_reset(pool) != CNA_RESULT_INVALID_STATE ||
        cna_render_target_pool_destroy(pool) != CNA_RESULT_INVALID_STATE ||
        cna_graphics_resource_dispose(first) != CNA_RESULT_INVALID_STATE ||
        cna_render_target_destroy(duplicate) != CNA_RESULT_SUCCESS) {
        return 0;
    }

    if (cna_graphics_device_set_render_target2d(graphics_device, CNA_INVALID_HANDLE) !=
            CNA_RESULT_SUCCESS ||
        cna_scoped_render_target_begin(graphics_device, first, &outer) != CNA_RESULT_SUCCESS ||
        outer == CNA_INVALID_HANDLE ||
        cna_scoped_render_target_get_has_recorded_previous(outer, &outer_recorded) !=
            CNA_RESULT_SUCCESS ||
        (outer_recorded != CNA_TRUE && outer_recorded != CNA_FALSE) ||
        !current_render_target_is(graphics_device, first) ||
        cna_scoped_render_target_begin(graphics_device, second, &inner) != CNA_RESULT_SUCCESS ||
        inner == CNA_INVALID_HANDLE ||
        cna_scoped_render_target_get_has_recorded_previous(inner, &inner_recorded) !=
            CNA_RESULT_SUCCESS ||
        (inner_recorded != CNA_TRUE && inner_recorded != CNA_FALSE) ||
        !current_render_target_is(graphics_device, second)) {
        return 0;
    }
    /* The failed out-of-order end must be transactional: both scopes and the inner binding stay
       active, and the target hidden beneath the inner scope stays retained too. */
    if (cna_scoped_render_target_end(outer) != CNA_RESULT_INVALID_STATE ||
        !current_render_target_is(graphics_device, second) ||
        cna_render_target_destroy(first) != CNA_RESULT_INVALID_STATE ||
        cna_scoped_render_target_end(inner) != CNA_RESULT_SUCCESS ||
        !current_render_target_is(
            graphics_device,
            inner_recorded == CNA_TRUE ? first : CNA_INVALID_HANDLE) ||
        cna_scoped_render_target_end(outer) != CNA_RESULT_SUCCESS ||
        !current_render_target_is(graphics_device, CNA_INVALID_HANDLE) ||
        cna_scoped_render_target_end(outer) == CNA_RESULT_SUCCESS) {
        return 0;
    }
    if (cna_render_target_destroy(first) != CNA_RESULT_SUCCESS ||
        cna_render_target_destroy(second) != CNA_RESULT_SUCCESS ||
        cna_render_target_pool_reset(pool) != CNA_RESULT_SUCCESS ||
        cna_render_target_pool_get_target_count(pool, &value) != CNA_RESULT_SUCCESS ||
        value != UINT64_C(0) ||
        cna_render_target_pool_get_estimated_bytes(pool, &value) != CNA_RESULT_SUCCESS ||
        value != UINT64_C(0) ||
        cna_render_target_pool_destroy(pool) != CNA_RESULT_SUCCESS) {
        return 0;
    }

    first = UINT64_C(0x5A5A5A5A);
    return cna_render_target_pool_acquire(
               CNA_INVALID_HANDLE,
               INT32_C(0),
               INT32_C(1),
               UINT32_MAX,
               UINT32_MAX,
               INT32_C(0),
               &first) == CNA_RESULT_INVALID_ARGUMENT &&
        first == CNA_INVALID_HANDLE;
}

static int validate_shader_effect_factory(const CNA_Handle graphics_device)
{
    static const char Key[] = "EngineLayerSmoke.cached";
    static const char VertexSource[] = "void main() { }";
    static const char FragmentSource[] = "void main() { }";
    static const char IgnoredSource[] = "different source for an existing key";
    CNA_ShaderEffectFactoryHandle factory = CNA_INVALID_HANDLE;
    CNA_EffectHandle first = CNA_INVALID_HANDLE;
    CNA_EffectHandle second = CNA_INVALID_HANDLE;
    CNA_EffectParameterCollectionHandle parameters = CNA_INVALID_HANDLE;
    CNA_Bool contains = UINT8_C(9);
    uint64_t count = UINT64_C(0);

    if (cna_shader_effect_factory_create(graphics_device, &factory) != CNA_RESULT_SUCCESS ||
        factory == CNA_INVALID_HANDLE ||
        cna_shader_effect_factory_contains(factory, string_view(Key), &contains) !=
            CNA_RESULT_SUCCESS ||
        contains != CNA_FALSE ||
        cna_shader_effect_factory_acquire(
            factory,
            string_view(Key),
            string_view(VertexSource),
            string_view(FragmentSource),
            &first) != CNA_RESULT_SUCCESS ||
        first == CNA_INVALID_HANDLE ||
        cna_shader_effect_factory_contains(factory, string_view(Key), &contains) !=
            CNA_RESULT_SUCCESS ||
        contains != CNA_TRUE ||
        cna_shader_effect_factory_get_compile_count(factory, &count) != CNA_RESULT_SUCCESS ||
        count != UINT64_C(1) ||
        cna_shader_effect_factory_acquire(
            factory,
            string_view(Key),
            string_view(IgnoredSource),
            string_view(IgnoredSource),
            &second) != CNA_RESULT_SUCCESS ||
        second == CNA_INVALID_HANDLE ||
        cna_shader_effect_factory_get_compile_count(factory, &count) != CNA_RESULT_SUCCESS ||
        count != UINT64_C(1)) {
        return 0;
    }
    if (cna_effect_dispose(first) != CNA_RESULT_INVALID_STATE ||
        cna_graphics_resource_dispose(first) != CNA_RESULT_INVALID_STATE ||
        cna_effect_get_parameters(first, &parameters) != CNA_RESULT_SUCCESS ||
        cna_effect_destroy(first) != CNA_RESULT_SUCCESS ||
        cna_effect_destroy(second) != CNA_RESULT_SUCCESS ||
        cna_shader_effect_factory_clear(factory) != CNA_RESULT_INVALID_STATE ||
        cna_shader_effect_factory_destroy(factory) != CNA_RESULT_INVALID_STATE ||
        cna_effect_parameter_collection_destroy(parameters) != CNA_RESULT_SUCCESS ||
        cna_shader_effect_factory_clear(factory) != CNA_RESULT_SUCCESS ||
        cna_shader_effect_factory_contains(factory, string_view(Key), &contains) !=
            CNA_RESULT_SUCCESS ||
        contains != CNA_FALSE ||
        cna_shader_effect_factory_get_compile_count(factory, &count) != CNA_RESULT_SUCCESS ||
        count != UINT64_C(1) ||
        cna_shader_effect_factory_destroy(factory) != CNA_RESULT_SUCCESS) {
        return 0;
    }

    factory = UINT64_C(0x5A5A5A5A);
    return cna_shader_effect_factory_create(CNA_INVALID_HANDLE, &factory) != CNA_RESULT_SUCCESS &&
        factory == CNA_INVALID_HANDLE;
}


/* CBIND-084C. Passes need no compute, so unlike the storage/compute families this arm runs on any
 * renderer that has the layer -- which is what makes it worth asserting the real behaviour rather
 * than a refusal. */
static int validate_pass_machinery(const CNA_Handle graphics_device)
{
    CNA_PostProcessPassHandle blit = CNA_INVALID_HANDLE;
    CNA_PostProcessPassHandle effect_pass = CNA_INVALID_HANDLE;
    CNA_FullscreenPassHandle fullscreen = CNA_INVALID_HANDLE;
    CNA_EffectHandle basic = CNA_INVALID_HANDLE;
    CNA_EffectHandle read_back = (CNA_EffectHandle)UINT64_C(0x5A5A5A5A);
    CNA_PostProcessContext context;
    static const char name[] = "CApiPass";
    const CNA_StringView name_view = {name, sizeof(name) - 1U};
    CNA_Bool supported = UINT8_C(9);
    uint64_t bytes = UINT64_C(0);
    char text[128];

    if (cna_post_process_context_init(&context) != CNA_RESULT_SUCCESS ||
        context.struct_size != (uint32_t)sizeof(CNA_PostProcessContext) ||
        context.source != CNA_INVALID_HANDLE || context.destination != CNA_INVALID_HANDLE ||
        context.has_previous_frame != CNA_FALSE) {
        return 0;
    }
    /* Defaulted matrices are the identity, exactly as the canonical struct's are. */
    if (context.projection.m11 != 1.0F || context.projection.m12 != 0.0F ||
        context.projection.m44 != 1.0F) {
        return 0;
    }
    /* An uninitialized context is refused rather than read. */
    {
        CNA_PostProcessContext raw;
        memset(&raw, 0, sizeof(raw));
        if (cna_blit_pass_create(graphics_device, &blit) != CNA_RESULT_SUCCESS) {
            return 0;
        }
        if (cna_post_process_pass_apply(blit, &raw) != CNA_RESULT_INVALID_ARGUMENT ||
            cna_post_process_pass_apply(blit, 0) != CNA_RESULT_INVALID_ARGUMENT) {
            return 0;
        }
    }

    /* The abstract contract's three operations, driven through the concrete pass. */
    if (cna_post_process_pass_copy_name(blit, 0, UINT64_C(0), &bytes) !=
            CNA_RESULT_BUFFER_TOO_SMALL ||
        bytes == UINT64_C(0) || bytes > sizeof(text)) {
        return 0;
    }
    if (cna_post_process_pass_copy_name(blit, text, sizeof(text), &bytes) !=
        CNA_RESULT_SUCCESS) {
        return 0;
    }
    if (cna_post_process_pass_is_supported(blit, graphics_device, &supported) !=
            CNA_RESULT_SUCCESS ||
        (supported != CNA_TRUE && supported != CNA_FALSE)) {
        return 0;
    }
    /* A blit pass is not an effect pass, and the two effect-only routes must say so by argument
     * rather than by handle kind -- both are the same ObjectKind. */
    if (cna_post_process_effect_pass_get_effect(blit, &read_back) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_post_process_effect_pass_set_effect(blit, CNA_INVALID_HANDLE) !=
            CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    if (cna_post_process_pass_destroy(blit) != CNA_RESULT_SUCCESS ||
        cna_post_process_pass_destroy(blit) == CNA_RESULT_SUCCESS) {
        return 0;
    }

    /* A borrowed effect: the pass does not own it, so destroying the pass leaves it alive. */
    if (cna_basic_effect_create(graphics_device, &basic) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    if (cna_post_process_effect_pass_create(graphics_device, basic, name_view, &effect_pass) !=
        CNA_RESULT_SUCCESS) {
        return 0;
    }
    read_back = CNA_INVALID_HANDLE;
    if (cna_post_process_effect_pass_get_effect(effect_pass, &read_back) != CNA_RESULT_SUCCESS ||
        read_back == CNA_INVALID_HANDLE) {
        return 0;
    }
    if (cna_effect_destroy(read_back) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    if (cna_post_process_pass_copy_name(effect_pass, text, sizeof(text), &bytes) !=
            CNA_RESULT_SUCCESS ||
        bytes != (uint64_t)(sizeof(name) - 1U) || memcmp(text, name, sizeof(name) - 1U) != 0) {
        return 0;
    }
    if (cna_post_process_effect_pass_set_effect(effect_pass, CNA_INVALID_HANDLE) !=
        CNA_RESULT_SUCCESS) {
        return 0;
    }
    if (cna_post_process_pass_destroy(effect_pass) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    /* Borrowed, so it survived its pass and the caller still owns it. */
    if (cna_effect_destroy(basic) != CNA_RESULT_SUCCESS) {
        return 0;
    }

    /* An owning pass consumes the handle: it must be invalid afterwards, and destroying the pass
     * must not leave the effect stranded -- cna_game_destroy would refuse if it did. */
    basic = CNA_INVALID_HANDLE;
    if (cna_basic_effect_create(graphics_device, &basic) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    if (cna_post_process_effect_pass_create_owning(
            graphics_device, basic, name_view, &effect_pass) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    if (cna_effect_destroy(basic) == CNA_RESULT_SUCCESS) {
        return 0;
    }
    if (cna_post_process_pass_destroy(effect_pass) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    /* An owning pass needs something to take over. */
    if (cna_post_process_effect_pass_create_owning(
            graphics_device, CNA_INVALID_HANDLE, name_view, &effect_pass) !=
        CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }

    if (cna_fullscreen_pass_create(graphics_device, &fullscreen) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    if (cna_fullscreen_pass_destroy(fullscreen) != CNA_RESULT_SUCCESS ||
        cna_fullscreen_pass_destroy(fullscreen) == CNA_RESULT_SUCCESS) {
        return 0;
    }
    return 1;
}

/* Every non-texture field of the full material round-trips; the texture slots deliberately read
 * back invalid, because the engine layer stores raw pointers this ABI does not name. */
static int validate_material_binding(const CNA_Handle graphics_device)
{
    CNA_EffectHandle pbr = CNA_INVALID_HANDLE;
    CNA_EffectHandle basic = CNA_INVALID_HANDLE;
    CNA_PbrMaterialEXT material;
    CNA_PbrMaterialEXT round_trip;

    if (cna_pbr_material_ext_init(&material) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    material.metallic_factor = 0.25F;
    material.roughness_factor = 0.75F;
    material.ior = 1.75F;
    material.alpha_cutoff = 0.125F;
    material.double_sided = CNA_TRUE;
    material.texture_coordinate_sets[1] = INT32_C(1);
    material.texture_transforms[1].rotation = 0.5F;

    if (cna_pbr_effect_create(graphics_device, &pbr) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    if (cna_pbr_effect_apply_material(pbr, &material) != CNA_RESULT_SUCCESS ||
        cna_pbr_effect_apply_material(pbr, 0) != CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    if (cna_pbr_effect_extract_material(pbr, &round_trip) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    if (round_trip.metallic_factor != 0.25F || round_trip.roughness_factor != 0.75F ||
        round_trip.double_sided != CNA_TRUE ||
        round_trip.albedo_texture != CNA_INVALID_HANDLE) {
        return 0;
    }
    if (cna_pbr_material_apply_state(&material, graphics_device) != CNA_RESULT_SUCCESS ||
        cna_pbr_material_apply_state(0, graphics_device) != CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    /* An uninitialized material is refused rather than reinterpreted. */
    {
        CNA_PbrMaterialEXT raw;
        memset(&raw, 0, sizeof(raw));
        if (cna_pbr_effect_apply_material(pbr, &raw) != CNA_RESULT_INVALID_ARGUMENT) {
            return 0;
        }
    }
    /* The wrong concrete effect type is an argument error, not a silent no-op. */
    if (cna_basic_effect_create(graphics_device, &basic) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    if (cna_pbr_effect_apply_material(basic, &material) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_skinned_pbr_effect_apply_material(pbr, &material) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_pbr_effect_extract_material(basic, &round_trip) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_skinned_pbr_effect_extract_material(pbr, &round_trip) != CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    return cna_effect_destroy(basic) == CNA_RESULT_SUCCESS &&
        cna_effect_destroy(pbr) == CNA_RESULT_SUCCESS;
}

static CNA_Result on_load(
    CNA_Handle game,
    const CNA_GameTime* game_time,
    void* context,
    CNA_CallbackError* out_error)
{
    (void)out_error;
    EngineLayerState* const state = (EngineLayerState*)context;
    CNA_Handle graphics_device = CNA_INVALID_HANDLE;
    int ok = 0;

    if (game_time != 0 ||
        cna_game_get_graphics_device(game, &graphics_device) != CNA_RESULT_SUCCESS ||
        cna_graphics_ext_is_available(&state->available) != CNA_RESULT_SUCCESS) {
        state->failed_stage = 8;
        return CNA_RESULT_INVALID_STATE;
    }
    if (!validate_version(state->available)) {
        state->failed_stage = 1;
        return CNA_RESULT_INVALID_STATE;
    }

    if (state->available == CNA_TRUE) {
        CNA_Bool compute = CNA_FALSE;
        if (cna_graphics_device_supports_capability(
                graphics_device, CNA_GRAPHICS_CAPABILITY_COMPUTE_SHADERS, &compute) !=
            CNA_RESULT_SUCCESS) {
            state->failed_stage = 6;
            return CNA_RESULT_INVALID_STATE;
        }
        state->had_compute = compute;
        if (!validate_pass_machinery(graphics_device)) {
            state->failed_stage = 9;
            return CNA_RESULT_INVALID_STATE;
        }
        if (!validate_material_binding(graphics_device)) {
            state->failed_stage = 10;
            return CNA_RESULT_INVALID_STATE;
        }
        if (compute != CNA_TRUE) {
            if (!validate_compute_absent(graphics_device)) {
                state->failed_stage = 7;
            }
        } else if (!validate_storage_buffer(graphics_device)) {
            state->failed_stage = 2;
        } else if (!validate_typed_storage_buffer(graphics_device)) {
            state->failed_stage = 3;
        } else if (!validate_compute_shader(graphics_device)) {
            state->failed_stage = 4;
        }
        if (state->failed_stage == 0 && !validate_gpu_timer(graphics_device)) {
            state->failed_stage = 12;
        } else if (state->failed_stage == 0 &&
                   !validate_render_target_pool_and_scopes(graphics_device)) {
            state->failed_stage = 13;
        } else if (state->failed_stage == 0 &&
                   !validate_shader_effect_factory(graphics_device)) {
            state->failed_stage = 14;
        }
    } else if (!validate_unavailable(graphics_device)) {
        state->failed_stage = 5;
    }
    ok = state->failed_stage == 0;
    if (!ok) {
        return CNA_RESULT_INVALID_STATE;
    }
    state->validated = 1;
    return CNA_RESULT_SUCCESS;
}

static int validate_in_game(int* const out_stage)
{
    EngineLayerState state = {CNA_FALSE, CNA_FALSE, 0, 0};
    CNA_GameCallbacks callbacks = {
        sizeof(CNA_GameCallbacks), UINT32_C(1), on_load, 0, 0, 0, 0, &state};
    static const char title[] = "C API engine layer";
    const CNA_GameCreateInfo create_info = {
        sizeof(CNA_GameCreateInfo),
        UINT32_C(1),
        CNA_TRUE,
        {0U, 0U, 0U, 0U, 0U, 0U, 0U},
        INT64_C(166667),
        {title, sizeof(title) - 1U},
        &callbacks};
    CNA_Handle game = CNA_INVALID_HANDLE;
    int ran = 0;

    if (cna_game_create(&create_info, &game) != CNA_RESULT_SUCCESS) {
        *out_stage = 9;
        return 0;
    }
    if (cna_game_run_one_frame(game) != CNA_RESULT_SUCCESS) {
        state.failed_stage = state.failed_stage != 0 ? state.failed_stage : 10;
    }
    ran = state.failed_stage == 0 && state.validated == 1;
    *out_stage = state.failed_stage != 0 ? state.failed_stage : (ran ? 0 : 11);
    return cna_game_destroy(game) == CNA_RESULT_SUCCESS && ran;
}

int main(void)
{
    /* One code per validator, so a failure names the family it came from. */
    if (!validate_identities()) {
        return 1;
    }
    if (!validate_barrier_containment()) {
        return 2;
    }
    {
        int stage = 0;
        if (!validate_in_game(&stage)) {
            /* 3 = the game itself did not run; 4..8 = the validator that refused. */
            return stage == 0 ? 3 : 3 + stage;
        }
    }
    return 0;
}
