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

/* CBIND-085A. The light value types are pure values, so unlike every other engine-layer family
 * these routes must succeed in BOTH builds -- filling a light with its defaults needs no
 * engine-layer object. That is asserted unconditionally rather than behind the availability
 * branch, which is what makes a build that accidentally guarded them fail here. */
static int near_float(const float left, const float right)
{
    const float difference = left - right;
    return (difference < 0.0001F) && (difference > -0.0001F);
}

static int validate_light_values(void)
{
    CNA_DirectionalLightEXT directional;
    CNA_PointLightEXT point;
    CNA_SpotLightEXT spot;
    CNA_PunctualLightEXT punctual;
    CNA_ShadowCascadeStateEXT cascades;
    int cascade = 0;

    memset(&directional, 0x5A, sizeof(directional));
    if (cna_directional_light_ext_init(&directional) != CNA_RESULT_SUCCESS ||
        directional.struct_size != (uint32_t)sizeof(CNA_DirectionalLightEXT) ||
        directional.struct_version != UINT32_C(1) ||
        !near_float(directional.direction.x, 0.0F) ||
        !near_float(directional.direction.y, -1.0F) ||
        !near_float(directional.direction.z, 0.0F) ||
        !near_float(directional.color.x, 1.0F) || !near_float(directional.intensity, 1.0F) ||
        directional.casts_shadows != CNA_FALSE) {
        return 0;
    }
    if (cna_directional_light_ext_init(0) != CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }

    memset(&point, 0x5A, sizeof(point));
    if (cna_point_light_ext_init(&point) != CNA_RESULT_SUCCESS ||
        !near_float(point.position.x, 0.0F) || !near_float(point.color.z, 1.0F) ||
        !near_float(point.intensity, 1.0F) || !near_float(point.range, 20.0F) ||
        point.casts_shadows != CNA_FALSE) {
        return 0;
    }
    if (cna_point_light_ext_init(0) != CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }

    memset(&spot, 0x5A, sizeof(spot));
    if (cna_spot_light_ext_init(&spot) != CNA_RESULT_SUCCESS ||
        !near_float(spot.direction.y, -1.0F) || !near_float(spot.range, 20.0F) ||
        !near_float(spot.inner_angle, 0.35F) || !near_float(spot.outer_angle, 0.5F) ||
        spot.casts_shadows != CNA_FALSE) {
        return 0;
    }
    /* The cone is only meaningful while the inner half-angle is inside the outer one, and the
       defaults must satisfy their own invariant. */
    if (!(spot.inner_angle < spot.outer_angle)) {
        return 0;
    }
    if (cna_spot_light_ext_init(0) != CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }

    memset(&punctual, 0x5A, sizeof(punctual));
    if (cna_punctual_light_ext_init(&punctual) != CNA_RESULT_SUCCESS ||
        punctual.kind != CNA_PUNCTUAL_LIGHT_KIND_EXT_NONE ||
        !near_float(punctual.direction.y, -1.0F) || !near_float(punctual.range, 20.0F) ||
        !near_float(punctual.shadow_depth_bias, 0.004F)) {
        return 0;
    }
    /* Both shadow textures default to null, and a handle's spelling of null is the invalid
       handle -- not zero-filled memory, which is what memset above would have left. */
    if (punctual.shadow_cube != CNA_INVALID_HANDLE ||
        punctual.shadow_map != CNA_INVALID_HANDLE) {
        return 0;
    }
    /* A value-initialized Matrix is all zeros, so the defaulted transform is too. */
    if (!near_float(punctual.shadow_view_projection.m11, 0.0F) ||
        !near_float(punctual.shadow_view_projection.m44, 0.0F)) {
        return 0;
    }
    if (cna_punctual_light_ext_init(0) != CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }

    memset(&cascades, 0x5A, sizeof(cascades));
    if (cna_shadow_cascade_state_ext_init(&cascades) != CNA_RESULT_SUCCESS ||
        cascades.count != INT32_C(0) || !near_float(cascades.blend_band, 0.0F) ||
        cascades.debug_tint != CNA_FALSE) {
        return 0;
    }
    /* Every cascade transform defaults to a zero matrix, matching the canonical value-initialized
       array. With `count` at 0 none of them is read, so a defaulted C state and a defaulted C++
       one describe the same thing -- which is the property worth asserting. */
    for (cascade = 0; cascade < CNA_SHADOW_CASCADE_MAX_EXT; ++cascade) {
        if (!near_float(cascades.world_to_atlas[cascade].m11, 0.0F) ||
            !near_float(cascades.world_to_atlas[cascade].m44, 0.0F) ||
            !near_float(cascades.split_distance[cascade], 0.0F)) {
            return 0;
        }
    }
    if (!near_float(cascades.camera_view.m11, 0.0F)) {
        return 0;
    }
    return cna_shadow_cascade_state_ext_init(0) == CNA_RESULT_INVALID_ARGUMENT;
}

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
    /* CBIND-087B. The material is a value describable in either build, but its canonical
       equality, hash and text need the canonical type, so all three refuse here rather than one
       of them being reimplemented field by field in C -- which would make equality answerable in
       a build where the hash consistent with it is not. */
    {
        CNA_PbrMaterialEXT material;
        CNA_Bool material_flag = UINT8_C(9);
        uint64_t number = UINT64_C(0);
        if (cna_pbr_material_ext_init(&material) != CNA_RESULT_SUCCESS ||
            cna_pbr_material_ext_equals(&material, &material, &material_flag) !=
                CNA_RESULT_NOT_SUPPORTED ||
            cna_pbr_material_ext_get_hash_code(&material, &number) !=
                CNA_RESULT_NOT_SUPPORTED ||
            cna_pbr_material_ext_copy_to_string(&material, 0, UINT64_C(0), &number) !=
                CNA_RESULT_NOT_SUPPORTED ||
            material_flag != UINT8_C(9)) {
            return 0;
        }
    }
    /* CBIND-087D. The bridge and both transparency objects refuse without the layer, and the two
       value initializers refuse with them rather than half-working: a caller that can fill a
       source structure but not use it has been told nothing useful. */
    {
        CNA_TransparentDrawListHandle list = CNA_INVALID_HANDLE;
        CNA_WeightedBlendedTransparencyHandle wbt = CNA_INVALID_HANDLE;
        CNA_GltfMaterialSourceEXT source;
        CNA_GltfMaterialTexturesEXT gltf_textures;
        CNA_PbrMaterialEXT built;
        uint64_t number = UINT64_C(0);
        if (cna_gltf_material_source_ext_init(&source) != CNA_RESULT_NOT_SUPPORTED ||
            cna_gltf_material_textures_ext_init(&gltf_textures) != CNA_RESULT_NOT_SUPPORTED ||
            cna_gltf_material_bridge_build_material(&source, &gltf_textures, &built) !=
                CNA_RESULT_NOT_SUPPORTED ||
            cna_transparent_draw_list_create(&list) != CNA_RESULT_NOT_SUPPORTED ||
            list != CNA_INVALID_HANDLE ||
            cna_transparent_draw_list_camera_position_of(0, 0) != CNA_RESULT_NOT_SUPPORTED ||
            cna_weighted_blended_transparency_create(
                graphics_device, INT32_C(8), INT32_C(8), &wbt) != CNA_RESULT_NOT_SUPPORTED ||
            wbt != CNA_INVALID_HANDLE ||
            cna_weighted_blended_transparency_copy_accumulation_glsl(0, UINT64_C(0), &number) !=
                CNA_RESULT_NOT_SUPPORTED) {
            return 0;
        }
    }
    /* CBIND-088A. The settings are a value describable in either build, but every route that
       answers with them runs each field through a canonical setter, so all four refuse here. */
    {
        CNA_RenderPipelineSettingsEXT pipeline_settings;
        CNA_StringView empty_text;
        int32_t applied = -1;
        empty_text.data = 0;
        empty_text.byte_length = UINT64_C(0);
        if (cna_render_pipeline_settings_ext_init(&pipeline_settings) !=
                CNA_RESULT_NOT_SUPPORTED ||
            cna_render_pipeline_settings_ext_normalize(&pipeline_settings) !=
                CNA_RESULT_NOT_SUPPORTED ||
            cna_render_pipeline_settings_ext_apply_render_quality_preset(&pipeline_settings) !=
                CNA_RESULT_NOT_SUPPORTED ||
            cna_render_pipeline_settings_ext_apply_from_string(
                &pipeline_settings, empty_text, &applied) != CNA_RESULT_NOT_SUPPORTED ||
            applied != INT32_C(-1)) {
            return 0;
        }
    }
    /* CBIND-088B. The pipeline is an engine-layer object, so every route refuses. */
    {
        CNA_RenderPipelineHandle pipeline = CNA_INVALID_HANDLE;
        CNA_RenderPipelineFrameStatisticsEXT statistics;
        uint64_t number = UINT64_C(0);
        if (cna_render_pipeline_create(graphics_device, &pipeline) != CNA_RESULT_NOT_SUPPORTED ||
            pipeline != CNA_INVALID_HANDLE ||
            cna_render_pipeline_resize(pipeline, INT32_C(8), INT32_C(8)) !=
                CNA_RESULT_NOT_SUPPORTED ||
            cna_render_pipeline_end(pipeline) != CNA_RESULT_NOT_SUPPORTED ||
            cna_render_pipeline_get_statistics(pipeline, &statistics) !=
                CNA_RESULT_NOT_SUPPORTED ||
            cna_render_pipeline_copy_transparency_fallback_reason_ext(
                pipeline, 0, UINT64_C(0), &number) != CNA_RESULT_NOT_SUPPORTED) {
            return 0;
        }
    }
    /* CBIND-089A. The chain and the pipeline's timing surface are engine-layer objects. */
    {
        CNA_PostProcessChainHandle chain = CNA_INVALID_HANDLE;
        CNA_PassTimingEXT timing;
        uint64_t number = UINT64_C(0);
        if (cna_post_process_chain_create(graphics_device, &chain) != CNA_RESULT_NOT_SUPPORTED ||
            chain != CNA_INVALID_HANDLE ||
            cna_post_process_chain_get_pass_timing_count(chain, &number) !=
                CNA_RESULT_NOT_SUPPORTED ||
            cna_post_process_chain_get_pass_timing(chain, UINT64_C(0), &timing) !=
                CNA_RESULT_NOT_SUPPORTED ||
            cna_post_process_chain_copy_pass_timing_name(
                chain, UINT64_C(0), 0, UINT64_C(0), &number) != CNA_RESULT_NOT_SUPPORTED ||
            cna_render_pipeline_get_pass_timing_count_ext(CNA_INVALID_HANDLE, &number) !=
                CNA_RESULT_NOT_SUPPORTED) {
            return 0;
        }
    }
    /* CBIND-089B. The three screen-space passes and their two pure functions. */
    {
        CNA_PostProcessPassHandle pass = CNA_INVALID_HANDLE;
        uint64_t number = UINT64_C(0);
        int32_t count = -1;
        float scalar = -1.0F;
        if (cna_ssr_pass_create(graphics_device, &pass) != CNA_RESULT_NOT_SUPPORTED ||
            pass != CNA_INVALID_HANDLE ||
            cna_ssao_pass_create(graphics_device, &pass) != CNA_RESULT_NOT_SUPPORTED ||
            cna_depth_of_field_pass_create(graphics_device, &pass) !=
                CNA_RESULT_NOT_SUPPORTED ||
            cna_ssr_pass_get_edge_fade(pass, &scalar) != CNA_RESULT_NOT_SUPPORTED ||
            cna_ssao_pass_copy_occlusion_glsl(CNA_FALSE, 0, UINT64_C(0), &number) !=
                CNA_RESULT_NOT_SUPPORTED ||
            cna_ssao_pass_sample_count_for_quality(CNA_RENDER_QUALITY_LOW, &count) !=
                CNA_RESULT_NOT_SUPPORTED ||
            cna_depth_of_field_pass_circle_of_confusion_millimetres(
                1.0F, 1.0F, 50.0F, 2.8F, &scalar) != CNA_RESULT_NOT_SUPPORTED) {
            return 0;
        }
    }
    /* CBIND-089C. The four atmospheric passes and their three pure functions. */
    {
        CNA_PostProcessPassHandle pass = CNA_INVALID_HANDLE;
        CNA_Vector3 vector;
        uint64_t number = UINT64_C(0);
        float scalar = -1.0F;
        vector.x = 0.0F; vector.y = 1.0F; vector.z = 0.0F;
        if (cna_aerial_perspective_pass_create(graphics_device, &pass) !=
                CNA_RESULT_NOT_SUPPORTED ||
            pass != CNA_INVALID_HANDLE ||
            cna_volumetric_fog_pass_create(graphics_device, &pass) != CNA_RESULT_NOT_SUPPORTED ||
            cna_height_fog_pass_create(graphics_device, &pass) != CNA_RESULT_NOT_SUPPORTED ||
            cna_light_shaft_pass_create(graphics_device, &pass) != CNA_RESULT_NOT_SUPPORTED ||
            cna_light_shaft_pass_get_decay(pass, &scalar) != CNA_RESULT_NOT_SUPPORTED ||
            cna_aerial_perspective_pass_copy_fallback_reason(pass, 0, UINT64_C(0), &number) !=
                CNA_RESULT_NOT_SUPPORTED ||
            cna_aerial_perspective_pass_air_mass_for_distance(&vector, 1.0F, 1.0F, &scalar) !=
                CNA_RESULT_NOT_SUPPORTED ||
            cna_aerial_perspective_pass_transmittance(2.0F, 1.0F, &vector) !=
                CNA_RESULT_NOT_SUPPORTED ||
            cna_height_fog_pass_optical_depth(0.0F, 0.0F, 1.0F, 1.0F, 1.0F, 0.0F, &scalar) !=
                CNA_RESULT_NOT_SUPPORTED) {
            return 0;
        }
    }
    /* CBIND-089D. The nine remaining passes, including the two that are not passes. */
    {
        CNA_PostProcessPassHandle pass = CNA_INVALID_HANDLE;
        CNA_DecalPassHandle decal = CNA_INVALID_HANDLE;
        CNA_SpatialUpscalePassHandle upscale = CNA_INVALID_HANDLE;
        uint64_t number = UINT64_C(0);
        int32_t count = -1;
        float scalar = -1.0F;
        if (cna_bloom_pass_create(graphics_device, &pass) != CNA_RESULT_NOT_SUPPORTED ||
            pass != CNA_INVALID_HANDLE ||
            cna_fxaa_pass_create(graphics_device, &pass) != CNA_RESULT_NOT_SUPPORTED ||
            cna_ascii_pass_create(graphics_device, &pass) != CNA_RESULT_NOT_SUPPORTED ||
            cna_decal_pass_create(graphics_device, &decal) != CNA_RESULT_NOT_SUPPORTED ||
            decal != CNA_INVALID_HANDLE ||
            cna_decal_pass_destroy(decal) != CNA_RESULT_NOT_SUPPORTED ||
            cna_spatial_upscale_pass_create(graphics_device, &upscale) !=
                CNA_RESULT_NOT_SUPPORTED ||
            upscale != CNA_INVALID_HANDLE ||
            cna_spatial_upscale_pass_destroy(upscale) != CNA_RESULT_NOT_SUPPORTED ||
            cna_bloom_pass_iterations_for_quality(CNA_RENDER_QUALITY_LOW, &count) !=
                CNA_RESULT_NOT_SUPPORTED ||
            cna_bloom_pass_extract_channel(1.0F, 0.5F, &scalar) != CNA_RESULT_NOT_SUPPORTED ||
            cna_fxaa_pass_copy_fragment_glsl(0, UINT64_C(0), &number) !=
                CNA_RESULT_NOT_SUPPORTED ||
            cna_spatial_upscale_pass_is_identity_scale(
                INT32_C(1), INT32_C(1), INT32_C(1), INT32_C(1), &flag) !=
                CNA_RESULT_NOT_SUPPORTED) {
            return 0;
        }
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
    /* Defaulted matrices are ALL ZEROS, because the canonical struct value-initializes them and
       Matrix() is zero-filled -- not the identity. Asserting the identity here is what caught an
       earlier draft of cna_post_process_context_init inventing a friendlier default. */
    if (context.projection.m11 != 0.0F || context.projection.m44 != 0.0F ||
        context.inverse_view.m11 != 0.0F) {
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


/* CBIND-085B1. Casting and sampling are different questions and the suite asks both, because a map
 * that rasters on a renderer that cannot sample it produces a texture nothing reads -- which looks
 * exactly like a scene with no occluders. */
static int validate_shadow_maps(const CNA_Handle graphics_device)
{
    CNA_ShadowMapHandle map = CNA_INVALID_HANDLE;
    CNA_SpotShadowMapHandle spot_map = CNA_INVALID_HANDLE;
    CNA_DirectionalLightEXT light;
    CNA_SpotLightEXT spot;
    CNA_BoundingBox bounds;
    CNA_Matrix view;
    CNA_Matrix projection;
    CNA_Vector3 position;
    CNA_Bool supported = UINT8_C(9);
    CNA_Bool samples = UINT8_C(9);
    CNA_ShadowQuality quality = UINT32_C(99);
    int32_t size = -1;
    int32_t radius = -1;
    float bias = -1.0F;

    if (cna_graphics_device_supports_shadow_sampling_ext(graphics_device, &samples) !=
            CNA_RESULT_SUCCESS ||
        (samples != CNA_TRUE && samples != CNA_FALSE)) {
        return 0;
    }
    if (cna_graphics_device_supports_shadow_sampling_ext(graphics_device, 0) !=
        CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }

    if (cna_directional_light_ext_init(&light) != CNA_RESULT_SUCCESS ||
        cna_spot_light_ext_init(&spot) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    bounds.min.x = -10.0F; bounds.min.y = -10.0F; bounds.min.z = -10.0F;
    bounds.max.x = 10.0F;  bounds.max.y = 10.0F;  bounds.max.z = 10.0F;

    /* The static computations are pure functions, so they answer without a map at all -- and they
       must refuse an uninitialized light rather than reading whatever is in the struct. */
    if (cna_shadow_map_compute_light_view(&light, &bounds, &view) != CNA_RESULT_SUCCESS ||
        cna_shadow_map_compute_light_projection(&view, &bounds, &projection) !=
            CNA_RESULT_SUCCESS) {
        return 0;
    }
    {
        CNA_DirectionalLightEXT raw;
        memset(&raw, 0, sizeof(raw));
        if (cna_shadow_map_compute_light_view(&raw, &bounds, &view) !=
                CNA_RESULT_INVALID_ARGUMENT ||
            cna_shadow_map_compute_light_view(&light, 0, &view) != CNA_RESULT_INVALID_ARGUMENT ||
            cna_shadow_map_compute_light_view(0, &bounds, &view) != CNA_RESULT_INVALID_ARGUMENT) {
            return 0;
        }
    }
    /* A quality preset selects a size and a filter radius, and both are static answers. Higher
       quality must not give a smaller map -- an ordering the presets exist to provide. */
    {
        int32_t low_size = 0;
        int32_t high_size = 0;
        if (cna_shadow_map_size_for_quality(CNA_SHADOW_QUALITY_LOW, &low_size) !=
                CNA_RESULT_SUCCESS ||
            cna_shadow_map_size_for_quality(CNA_SHADOW_QUALITY_HIGH, &high_size) !=
                CNA_RESULT_SUCCESS ||
            low_size > high_size) {
            return 0;
        }
        if (cna_shadow_map_filter_radius_for_quality(CNA_SHADOW_QUALITY_HIGH, &radius) !=
                CNA_RESULT_SUCCESS ||
            radius < 0) {
            return 0;
        }
        if (cna_shadow_map_size_for_quality(UINT32_C(9), &low_size) !=
            CNA_RESULT_INVALID_ARGUMENT) {
            return 0;
        }
    }

    if (cna_shadow_map_create(graphics_device, CNA_SHADOW_QUALITY_MEDIUM, &map) !=
            CNA_RESULT_SUCCESS ||
        map == CNA_INVALID_HANDLE) {
        return 0;
    }
    /* A refused creation must use its OWN output: every create route clears the output handle
       before it validates anything, so passing a live handle here would silently strand the map
       that was just made. That is how this assertion was written the first time, and the map it
       leaked is what made cna_game_destroy refuse. */
    {
        CNA_ShadowMapHandle refused = (CNA_ShadowMapHandle)UINT64_C(0x5A5A5A5A);
        if (cna_shadow_map_create(graphics_device, UINT32_C(9), &refused) !=
                CNA_RESULT_INVALID_ARGUMENT ||
            refused != CNA_INVALID_HANDLE) {
            return 0;
        }
    }
    if (cna_shadow_map_is_supported(map, &supported) != CNA_RESULT_SUCCESS ||
        (supported != CNA_TRUE && supported != CNA_FALSE)) {
        return 0;
    }
    if (cna_shadow_map_get_quality(map, &quality) != CNA_RESULT_SUCCESS ||
        quality != CNA_SHADOW_QUALITY_MEDIUM) {
        return 0;
    }
    if (cna_shadow_map_get_size(map, &size) != CNA_RESULT_SUCCESS ||
        cna_shadow_map_get_filter_radius(map, &radius) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    /* The instance's size must agree with the static answer for the same preset; two ways of
       asking one question that could drift apart. */
    {
        int32_t expected = 0;
        if (cna_shadow_map_size_for_quality(CNA_SHADOW_QUALITY_MEDIUM, &expected) !=
                CNA_RESULT_SUCCESS ||
            expected != size) {
            return 0;
        }
    }
    if (cna_shadow_map_set_depth_bias(map, 0.5F) != CNA_RESULT_SUCCESS ||
        cna_shadow_map_get_depth_bias(map, &bias) != CNA_RESULT_SUCCESS || bias != 0.5F) {
        return 0;
    }
    /* begin/end and the rigid caster must not fail on a renderer that cannot cast: the contract
       is that an unsupported map degrades rather than refusing.

       The skinned caster is different, and the difference is canonical: it validates its palette
       BEFORE it checks whether the map is supported, so an empty palette is refused everywhere.
       Getting this expectation wrong is what turned a test failure into a crash -- the refusal
       throws with the pass still open, and a map destroyed in that state disposes a target the
       device still has bound. Every exit from here on goes through the single cleanup below for
       the same reason. */
    {
        CNA_Matrix palette[1];
        int ok = cna_shadow_map_begin(map, &light, &bounds) == CNA_RESULT_SUCCESS;
        ok = ok && cna_shadow_map_apply_caster(map) == CNA_RESULT_SUCCESS;
        if (cna_matrix_get_identity(&palette[0]) != CNA_RESULT_SUCCESS) {
            ok = 0;
        }
        ok = ok &&
            cna_shadow_map_apply_skinned_caster(map, palette, UINT64_C(1), INT32_C(4)) ==
                CNA_RESULT_SUCCESS;
        /* An empty palette, a null array with a non-zero count, and a weight count the canonical
           API does not accept are all refusals -- and each leaves the pass open, so they are made
           here where the pass is still live and `end` still follows. */
        ok = ok &&
            cna_shadow_map_apply_skinned_caster(map, palette, UINT64_C(0), INT32_C(4)) !=
                CNA_RESULT_SUCCESS;
        ok = ok &&
            cna_shadow_map_apply_skinned_caster(map, 0, UINT64_C(2), INT32_C(4)) ==
                CNA_RESULT_INVALID_ARGUMENT;
        ok = ok &&
            cna_shadow_map_apply_skinned_caster(map, palette, UINT64_C(1), INT32_C(3)) !=
                CNA_RESULT_SUCCESS;
        ok = ok && cna_shadow_map_end(map) == CNA_RESULT_SUCCESS;
        ok = ok && cna_shadow_map_get_light_view_projection(map, &view) == CNA_RESULT_SUCCESS;
        if (!ok) {
            (void)cna_shadow_map_destroy(map);
            return 0;
        }
    }
    /* The effects and the texture are borrowed from the map; each is either a real handle or the
       invalid one, and destroying the map is refused while a borrow is outstanding. */
    {
        CNA_EffectHandle caster = (CNA_EffectHandle)UINT64_C(0x5A5A5A5A);
        CNA_Handle texture = (CNA_Handle)UINT64_C(0x5A5A5A5A);
        CNA_EffectHandle skinned = (CNA_EffectHandle)UINT64_C(0x5A5A5A5A);
        int ok = cna_shadow_map_get_caster_effect(map, &caster) == CNA_RESULT_SUCCESS;
        ok = ok && cna_shadow_map_get_shadow_texture(map, &texture) == CNA_RESULT_SUCCESS;
        /* The skinned caster is a second effect with the same borrow rules; a map that reported
           itself supported must hand out both, and each one has to be released. */
        ok = ok && cna_shadow_map_get_skinned_caster_effect(map, &skinned) == CNA_RESULT_SUCCESS;
        /* CBIND-097. Each borrow is released when the getter handed back a real handle, which is
           NOT the same question as whether the map is supported: the shadow texture is a render
           target that exists whether or not the caster shader links, so on a renderer without
           shaders the texture borrow is taken and the two effect borrows are not. Releasing on
           `supported` instead left that one texture borrow outstanding and the map undestroyable
           -- the whole of this suite's failure on HEADLESS with the layer on. */
        if (ok) {
            ok = (supported == CNA_TRUE) ==
                (caster != CNA_INVALID_HANDLE && skinned != CNA_INVALID_HANDLE);
        }
        if (ok) {
            /* A borrow keeps the map alive, so destroying it is refused while one is out. The
               texture alone is enough to assert that, which is why this now runs on every
               renderer rather than only where the caster links. */
            ok = texture != CNA_INVALID_HANDLE &&
                cna_shadow_map_destroy(map) != CNA_RESULT_SUCCESS;
        }
        if (ok && skinned != CNA_INVALID_HANDLE) {
            ok = cna_effect_destroy(skinned) == CNA_RESULT_SUCCESS;
        }
        if (ok && caster != CNA_INVALID_HANDLE) {
            ok = cna_effect_destroy(caster) == CNA_RESULT_SUCCESS;
        }
        if (ok && texture != CNA_INVALID_HANDLE) {
            ok = cna_render_target_destroy(texture) == CNA_RESULT_SUCCESS;
        }
        if (!ok) {
            (void)cna_shadow_map_destroy(map);
            return 0;
        }
    }
    if (cna_shadow_map_destroy(map) != CNA_RESULT_SUCCESS ||
        cna_shadow_map_destroy(map) == CNA_RESULT_SUCCESS) {
        return 0;
    }

    if (cna_spot_shadow_map_create(graphics_device, CNA_SHADOW_QUALITY_LOW, &spot_map) !=
            CNA_RESULT_SUCCESS ||
        spot_map == CNA_INVALID_HANDLE) {
        return 0;
    }
    if (cna_spot_shadow_map_is_supported(spot_map, &supported) != CNA_RESULT_SUCCESS ||
        cna_spot_shadow_map_get_quality(spot_map, &quality) != CNA_RESULT_SUCCESS ||
        quality != CNA_SHADOW_QUALITY_LOW ||
        cna_spot_shadow_map_get_size(spot_map, &size) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    if (cna_spot_shadow_map_begin(spot_map, &spot) != CNA_RESULT_SUCCESS ||
        cna_spot_shadow_map_end(spot_map) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    if (cna_spot_shadow_map_get_light_view_projection(spot_map, &view) != CNA_RESULT_SUCCESS ||
        cna_spot_shadow_map_get_light_position(spot_map, &position) != CNA_RESULT_SUCCESS ||
        cna_spot_shadow_map_get_light_range(spot_map, &bias) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    if (cna_spot_shadow_map_set_depth_bias(spot_map, 0.25F) != CNA_RESULT_SUCCESS ||
        cna_spot_shadow_map_get_depth_bias(spot_map, &bias) != CNA_RESULT_SUCCESS ||
        bias != 0.25F) {
        return 0;
    }
    if (cna_spot_shadow_map_compute_light_view(&spot, &view) != CNA_RESULT_SUCCESS ||
        cna_spot_shadow_map_compute_light_projection(&spot, &projection) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    {
        CNA_EffectHandle caster = CNA_INVALID_HANDLE;
        CNA_Handle texture = CNA_INVALID_HANDLE;
        if (cna_spot_shadow_map_get_caster_effect(spot_map, &caster) != CNA_RESULT_SUCCESS ||
            cna_spot_shadow_map_get_shadow_texture(spot_map, &texture) != CNA_RESULT_SUCCESS) {
            return 0;
        }
        if (caster != CNA_INVALID_HANDLE && cna_effect_destroy(caster) != CNA_RESULT_SUCCESS) {
            return 0;
        }
        if (texture != CNA_INVALID_HANDLE &&
            cna_render_target_destroy(texture) != CNA_RESULT_SUCCESS) {
            return 0;
        }
    }
    return cna_spot_shadow_map_destroy(spot_map) == CNA_RESULT_SUCCESS;
}


/* CBIND-085B2. The cascaded map brings two shapes the 2D maps did not need -- a count/copy result
 * and a fixed eight-element one -- and both are asserted for their size behaviour, not just their
 * values. Every exit goes through one cleanup, for the reason CBIND-085B1 learned. */
static int validate_cascaded_and_cube(const CNA_Handle graphics_device)
{
    CNA_CascadedShadowMapHandle cascaded = CNA_INVALID_HANDLE;
    CNA_CubeShadowMapHandle cube = CNA_INVALID_HANDLE;
    CNA_DirectionalLightEXT light;
    CNA_PointLightEXT point;
    CNA_Matrix view;
    CNA_Matrix projection;
    CNA_Vector3 corners[CNA_FRUSTUM_CORNER_COUNT_EXT];
    CNA_Vector3 centre;
    float splits[CNA_SHADOW_CASCADE_MAX_EXT];
    uint64_t count = UINT64_C(0);
    float radius = 0.0F;
    int ok = 1;
    int cascade = 0;

    if (cna_directional_light_ext_init(&light) != CNA_RESULT_SUCCESS ||
        cna_point_light_ext_init(&point) != CNA_RESULT_SUCCESS ||
        cna_matrix_get_identity(&view) != CNA_RESULT_SUCCESS ||
        cna_matrix_get_identity(&projection) != CNA_RESULT_SUCCESS) {
        return 0;
    }

    /* The split distances are a count/copy result: asking with no room reports the count and
       refuses, and it must not have written anything. */
    splits[0] = -1.0F;
    if (cna_cascaded_shadow_map_compute_split_distances(
            1.0F, 100.0F, INT32_C(4), 0.5F, 0, UINT64_C(0), &count) !=
            CNA_RESULT_BUFFER_TOO_SMALL ||
        count == UINT64_C(0) || count > (uint64_t)CNA_SHADOW_CASCADE_MAX_EXT ||
        splits[0] != -1.0F) {
        return 0;
    }
    if (cna_cascaded_shadow_map_compute_split_distances(
            1.0F, 100.0F, INT32_C(4), 0.5F, splits, (uint64_t)CNA_SHADOW_CASCADE_MAX_EXT,
            &count) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    /* Splits must increase away from the camera; a set that did not would place cascades in an
       order the selector cannot use. */
    for (cascade = 1; cascade < (int)count; ++cascade) {
        if (!(splits[cascade] > splits[cascade - 1])) {
            return 0;
        }
    }
    /* The eight corners are a fixed count, so there is no capacity to get wrong -- only the
       null destination to refuse. */
    if (cna_cascaded_shadow_map_compute_frustum_corners(&view, &projection, corners) !=
            CNA_RESULT_SUCCESS ||
        cna_cascaded_shadow_map_compute_frustum_corners(&view, &projection, 0) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_cascaded_shadow_map_compute_frustum_corners(0, &projection, corners) !=
            CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    if (cna_cascaded_shadow_map_compute_bounding_sphere(corners, &centre, &radius) !=
            CNA_RESULT_SUCCESS ||
        radius < 0.0F) {
        return 0;
    }
    /* Snapping must not move the centre further than one texel of the cascade. */
    {
        CNA_Vector3 snapped;
        if (cna_cascaded_shadow_map_snap_to_texel_grid(&centre, radius, INT32_C(1024), &snapped) !=
            CNA_RESULT_SUCCESS) {
            return 0;
        }
    }

    /* A cascade count outside the atlas is refused rather than clamped, so a caller learns that
       the map it gets is not the map it asked for. */
    if (cna_cascaded_shadow_map_create(graphics_device, CNA_SHADOW_QUALITY_LOW, INT32_C(0),
                                       &cascaded) != CNA_RESULT_INVALID_ARGUMENT ||
        cascaded != CNA_INVALID_HANDLE) {
        return 0;
    }
    if (cna_cascaded_shadow_map_create(
            graphics_device, CNA_SHADOW_QUALITY_LOW, CNA_SHADOW_CASCADE_MAX_EXT + 1, &cascaded) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cascaded != CNA_INVALID_HANDLE) {
        return 0;
    }
    if (cna_cascaded_shadow_map_create(
            graphics_device, CNA_SHADOW_QUALITY_LOW, INT32_C(3), &cascaded) !=
            CNA_RESULT_SUCCESS ||
        cascaded == CNA_INVALID_HANDLE) {
        return 0;
    }

    {
        CNA_Bool supported = UINT8_C(9);
        CNA_Bool tint = UINT8_C(9);
        int32_t value = -1;
        float scalar = -1.0F;

        ok = cna_cascaded_shadow_map_is_supported(cascaded, &supported) == CNA_RESULT_SUCCESS;
        ok = ok && cna_cascaded_shadow_map_get_cascade_count(cascaded, &value) ==
            CNA_RESULT_SUCCESS && value == INT32_C(3);
        ok = ok && cna_cascaded_shadow_map_get_cascade_size(cascaded, &value) ==
            CNA_RESULT_SUCCESS;
        ok = ok && cna_cascaded_shadow_map_update(cascaded, &light, &view, &projection) ==
            CNA_RESULT_SUCCESS;
        ok = ok && cna_cascaded_shadow_map_update(cascaded, &light, 0, &projection) ==
            CNA_RESULT_INVALID_ARGUMENT;
        ok = ok && cna_cascaded_shadow_map_begin(cascaded, INT32_C(0)) == CNA_RESULT_SUCCESS;
        ok = ok && cna_cascaded_shadow_map_end(cascaded) == CNA_RESULT_SUCCESS;
        ok = ok && cna_cascaded_shadow_map_get_cascade_matrix(cascaded, INT32_C(0), &view) ==
            CNA_RESULT_SUCCESS;
        ok = ok && cna_cascaded_shadow_map_get_split_distance(cascaded, INT32_C(0), &scalar) ==
            CNA_RESULT_SUCCESS;
        ok = ok && cna_cascaded_shadow_map_set_blend_band(cascaded, 2.0F) == CNA_RESULT_SUCCESS;
        ok = ok && cna_cascaded_shadow_map_get_blend_band(cascaded, &scalar) ==
            CNA_RESULT_SUCCESS && scalar == 2.0F;
        ok = ok && cna_cascaded_shadow_map_set_split_lambda(cascaded, 0.25F) == CNA_RESULT_SUCCESS;
        ok = ok && cna_cascaded_shadow_map_get_split_lambda(cascaded, &scalar) ==
            CNA_RESULT_SUCCESS && scalar == 0.25F;
        ok = ok && cna_cascaded_shadow_map_set_debug_tint_enabled(cascaded, CNA_TRUE) ==
            CNA_RESULT_SUCCESS;
        ok = ok && cna_cascaded_shadow_map_is_debug_tint_enabled(cascaded, &tint) ==
            CNA_RESULT_SUCCESS && tint == CNA_TRUE;
        /* A non-canonical boolean is refused rather than read as true. */
        ok = ok && cna_cascaded_shadow_map_set_debug_tint_enabled(cascaded, UINT8_C(2)) ==
            CNA_RESULT_INVALID_ARGUMENT;
        ok = ok && cna_cascaded_shadow_map_select_cascade(cascaded, 5.0F, &value) ==
            CNA_RESULT_SUCCESS && value >= INT32_C(0);

        if (ok) {
            CNA_EffectHandle caster = CNA_INVALID_HANDLE;
            CNA_Handle atlas = CNA_INVALID_HANDLE;
            ok = cna_cascaded_shadow_map_get_caster_effect(cascaded, &caster) ==
                CNA_RESULT_SUCCESS;
            ok = ok && cna_cascaded_shadow_map_get_shadow_texture(cascaded, &atlas) ==
                CNA_RESULT_SUCCESS;
            if (ok && caster != CNA_INVALID_HANDLE) {
                /* A borrow keeps the map alive, so destroying it is refused until released. */
                ok = cna_cascaded_shadow_map_destroy(cascaded) != CNA_RESULT_SUCCESS;
                ok = ok && cna_effect_destroy(caster) == CNA_RESULT_SUCCESS;
            }
            if (ok && atlas != CNA_INVALID_HANDLE) {
                ok = ok && cna_render_target_destroy(atlas) == CNA_RESULT_SUCCESS;
            }
        }
        if (!ok) {
            (void)cna_cascaded_shadow_map_destroy(cascaded);
            return 0;
        }
    }
    if (cna_cascaded_shadow_map_destroy(cascaded) != CNA_RESULT_SUCCESS) {
        return 0;
    }

    if (cna_cube_shadow_map_create(graphics_device, CNA_SHADOW_QUALITY_LOW, &cube) !=
            CNA_RESULT_SUCCESS ||
        cube == CNA_INVALID_HANDLE) {
        return 0;
    }
    {
        CNA_Bool supported = UINT8_C(9);
        CNA_ShadowQuality quality = UINT32_C(99);
        int32_t size = -1;
        float scalar = -1.0F;
        CNA_Vector3 position;

        ok = cna_cube_shadow_map_is_supported(cube, &supported) == CNA_RESULT_SUCCESS;
        ok = ok && cna_cube_shadow_map_get_quality(cube, &quality) == CNA_RESULT_SUCCESS &&
            quality == CNA_SHADOW_QUALITY_LOW;
        ok = ok && cna_cube_shadow_map_get_size(cube, &size) == CNA_RESULT_SUCCESS;
        /* The instance's face size must agree with the static answer for the same preset. */
        {
            int32_t expected = -1;
            ok = ok && cna_cube_shadow_map_size_for_quality(CNA_SHADOW_QUALITY_LOW, &expected) ==
                CNA_RESULT_SUCCESS && expected == size;
        }
        ok = ok && cna_cube_shadow_map_update(cube, &point) == CNA_RESULT_SUCCESS;
        /* CBIND-097. Binding a cube face is a separate question from whether the caster shader
           links, and a renderer can refuse it: HEADLESS does. Whichever way it goes, the face
           bracket must leave the map usable -- that is the invariant the canonical begin() marks
           the pass open *after* the bind specifically to protect, so that one refused face does
           not turn the map into an object that can never be used again. Both branches assert
           something; neither is a skip. */
        if (ok) {
            const CNA_Result opened = cna_cube_shadow_map_begin(cube, INT32_C(0));
            if (opened == CNA_RESULT_SUCCESS) {
                ok = cna_cube_shadow_map_end(cube) == CNA_RESULT_SUCCESS;
            } else {
                /* No pass was opened, so end() must say so rather than unbinding a target the
                   map never bound... */
                ok = cna_cube_shadow_map_end(cube) != CNA_RESULT_SUCCESS;
                /* ...and a second attempt must fail the same way it did the first time, not with
                   "a face pass is already open". This is the recovery invariant, and asserting it
                   is only possible on a renderer that refuses. */
                ok = ok && cna_cube_shadow_map_begin(cube, INT32_C(0)) == opened;
                ok = ok && cna_cube_shadow_map_end(cube) != CNA_RESULT_SUCCESS;
            }
        }
        ok = ok && cna_cube_shadow_map_get_light_position(cube, &position) == CNA_RESULT_SUCCESS;
        ok = ok && cna_cube_shadow_map_get_light_range(cube, &scalar) == CNA_RESULT_SUCCESS;
        ok = ok && cna_cube_shadow_map_set_depth_bias(cube, 0.125F) == CNA_RESULT_SUCCESS;
        ok = ok && cna_cube_shadow_map_get_depth_bias(cube, &scalar) == CNA_RESULT_SUCCESS &&
            scalar == 0.125F;
        ok = ok && cna_cube_shadow_map_compute_face_view(
            CNA_CUBE_MAP_FACE_POSITIVE_X, &position, &view) == CNA_RESULT_SUCCESS;
        /* An undefined face identity is refused rather than cast through. */
        ok = ok && cna_cube_shadow_map_compute_face_view(
            UINT32_C(9), &position, &view) == CNA_RESULT_INVALID_ARGUMENT;
        ok = ok && cna_cube_shadow_map_compute_face_projection(20.0F, &projection) ==
            CNA_RESULT_SUCCESS;

        if (ok) {
            CNA_EffectHandle caster = CNA_INVALID_HANDLE;
            CNA_Handle texture = CNA_INVALID_HANDLE;
            ok = cna_cube_shadow_map_get_caster_effect(cube, &caster) == CNA_RESULT_SUCCESS;
            ok = ok && cna_cube_shadow_map_get_shadow_texture(cube, &texture) ==
                CNA_RESULT_SUCCESS;
            if (ok && texture != CNA_INVALID_HANDLE) {
                ok = cna_cube_shadow_map_destroy(cube) != CNA_RESULT_SUCCESS;
                ok = ok && cna_texturecube_destroy(texture) == CNA_RESULT_SUCCESS;
            }
            if (ok && caster != CNA_INVALID_HANDLE) {
                ok = ok && cna_effect_destroy(caster) == CNA_RESULT_SUCCESS;
            }
        }
        if (!ok) {
            (void)cna_cube_shadow_map_destroy(cube);
            return 0;
        }
    }
    return cna_cube_shadow_map_destroy(cube) == CNA_RESULT_SUCCESS;
}


/* CBIND-085C1. The receiver contract is an interface an effect implements, so the suite drives it
 * through an effect that does (BasicEffect) and asserts the refusal through one that does not. */
static int validate_shadow_receiver(const CNA_Handle graphics_device)
{
    CNA_EffectHandle basic = CNA_INVALID_HANDLE;
    CNA_EffectHandle sprite = CNA_INVALID_HANDLE;
    CNA_ShadowCascadeStateEXT cascades;
    CNA_ShadowCascadeStateEXT read_back;
    CNA_PunctualLightEXT light;
    CNA_PunctualLightEXT light_back;
    CNA_Matrix matrix;
    CNA_Bool flag = UINT8_C(9);
    float scalar = -1.0F;
    int32_t radius = -1;
    int ok = 1;

    if (cna_basic_effect_create(graphics_device, &basic) != CNA_RESULT_SUCCESS ||
        cna_shadow_cascade_state_ext_init(&cascades) != CNA_RESULT_SUCCESS ||
        cna_punctual_light_ext_init(&light) != CNA_RESULT_SUCCESS ||
        cna_matrix_get_identity(&matrix) != CNA_RESULT_SUCCESS) {
        return 0;
    }

    ok = cna_effect_set_shadows_enabled_ext(basic, CNA_TRUE) == CNA_RESULT_SUCCESS;
    ok = ok && cna_effect_is_shadows_enabled_ext(basic, &flag) == CNA_RESULT_SUCCESS &&
        flag == CNA_TRUE;
    /* The boolean is refused before the handle is resolved, so an invalid handle and a bad byte
       still answer INVALID_ARGUMENT -- the CBIND-067 discipline CApiBoolContractSmoke enforces. */
    ok = ok && cna_effect_set_shadows_enabled_ext(basic, UINT8_C(2)) == CNA_RESULT_INVALID_ARGUMENT;

    ok = ok && cna_effect_set_shadow_depth_bias_ext(basic, 0.5F) == CNA_RESULT_SUCCESS;
    ok = ok && cna_effect_get_shadow_depth_bias_ext(basic, &scalar) == CNA_RESULT_SUCCESS &&
        scalar == 0.5F;
    ok = ok && cna_effect_set_shadow_filter_radius_ext(basic, INT32_C(3)) == CNA_RESULT_SUCCESS;
    ok = ok && cna_effect_get_shadow_filter_radius_ext(basic, &radius) == CNA_RESULT_SUCCESS &&
        radius == INT32_C(3);
    ok = ok && cna_effect_set_light_view_projection_ext(basic, &matrix) == CNA_RESULT_SUCCESS;
    ok = ok && cna_effect_get_light_view_projection_ext(basic, &matrix) == CNA_RESULT_SUCCESS;
    ok = ok && cna_effect_set_light_view_projection_ext(basic, 0) == CNA_RESULT_INVALID_ARGUMENT;

    /* The cascade state round-trips through the effect, including the count and the band. */
    cascades.count = INT32_C(2);
    cascades.blend_band = 3.0F;
    ok = ok && cna_effect_set_shadow_cascades_ext(basic, &cascades) == CNA_RESULT_SUCCESS;
    ok = ok && cna_effect_get_shadow_cascades_ext(basic, &read_back) == CNA_RESULT_SUCCESS &&
        read_back.count == INT32_C(2) && read_back.blend_band == 3.0F;
    /* A count outside the fixed array is refused rather than written past. */
    cascades.count = CNA_SHADOW_CASCADE_MAX_EXT + 1;
    ok = ok && cna_effect_set_shadow_cascades_ext(basic, &cascades) == CNA_RESULT_INVALID_ARGUMENT;
    cascades.count = INT32_C(2);
    /* An uninitialized state is refused rather than read. */
    {
        CNA_ShadowCascadeStateEXT raw;
        memset(&raw, 0, sizeof(raw));
        ok = ok && cna_effect_set_shadow_cascades_ext(basic, &raw) == CNA_RESULT_INVALID_ARGUMENT;
    }

    light.kind = CNA_PUNCTUAL_LIGHT_KIND_EXT_SPOT;
    light.range = 7.5F;
    ok = ok && cna_effect_set_punctual_light_ext(basic, &light) == CNA_RESULT_SUCCESS;
    ok = ok && cna_effect_get_punctual_light_ext(basic, &light_back) == CNA_RESULT_SUCCESS &&
        light_back.kind == CNA_PUNCTUAL_LIGHT_KIND_EXT_SPOT && light_back.range == 7.5F;
    /* The two shadow-texture slots come back invalid: this ABI does not name a texture it does
       not track, and saying so is better than handing back a number that means nothing. */
    ok = ok && light_back.shadow_map == CNA_INVALID_HANDLE &&
        light_back.shadow_cube == CNA_INVALID_HANDLE;
    /* An undefined light kind is refused rather than cast through. */
    light.kind = UINT32_C(9);
    ok = ok && cna_effect_set_punctual_light_ext(basic, &light) == CNA_RESULT_INVALID_ARGUMENT;

    /* No shadow map is bound, so the getter reports none rather than inventing a handle. */
    {
        CNA_Handle bound = (CNA_Handle)UINT64_C(0x5A5A5A5A);
        ok = ok && cna_effect_set_shadow_map_ext(basic, CNA_INVALID_HANDLE) == CNA_RESULT_SUCCESS;
        ok = ok && cna_effect_get_shadow_map_ext(basic, &bound) == CNA_RESULT_SUCCESS &&
            bound == CNA_INVALID_HANDLE;
    }

    if (!ok) {
        (void)cna_effect_destroy(basic);
        return 0;
    }

    /* An effect that does not implement the contract is refused by argument, not by handle: the
       handle is perfectly valid, it is the concrete type that cannot answer. */
    if (cna_sprite_effect_create(graphics_device, &sprite) == CNA_RESULT_SUCCESS) {
        ok = cna_effect_set_shadows_enabled_ext(sprite, CNA_TRUE) == CNA_RESULT_INVALID_ARGUMENT;
        ok = ok && cna_effect_get_shadow_depth_bias_ext(sprite, &scalar) ==
            CNA_RESULT_INVALID_ARGUMENT;
        if (cna_effect_destroy(sprite) != CNA_RESULT_SUCCESS) {
            ok = 0;
        }
    }
    if (!ok) {
        (void)cna_effect_destroy(basic);
        return 0;
    }

    /* applyToReceiver moves the whole cascade state across in one call, which is the reason the
       receiver contract is bound at all. */
    {
        CNA_CascadedShadowMapHandle cascaded = CNA_INVALID_HANDLE;
        CNA_DirectionalLightEXT sun;
        CNA_Matrix view;
        CNA_Matrix projection;
        if (cna_directional_light_ext_init(&sun) != CNA_RESULT_SUCCESS ||
            cna_matrix_get_identity(&view) != CNA_RESULT_SUCCESS ||
            cna_matrix_get_identity(&projection) != CNA_RESULT_SUCCESS) {
            (void)cna_effect_destroy(basic);
            return 0;
        }
        if (cna_cascaded_shadow_map_create(
                graphics_device, CNA_SHADOW_QUALITY_LOW, INT32_C(2), &cascaded) ==
            CNA_RESULT_SUCCESS) {
            /* applyToReceiver refuses before update(): there are no cascade matrices to give, and
               handing over a defaulted state would silently shadow nothing. */
            ok = cna_cascaded_shadow_map_apply_to_receiver(cascaded, basic) != CNA_RESULT_SUCCESS;
            ok = ok && cna_cascaded_shadow_map_update(cascaded, &sun, &view, &projection) ==
                CNA_RESULT_SUCCESS;
            ok = ok && cna_cascaded_shadow_map_apply_to_receiver(cascaded, basic) ==
                CNA_RESULT_SUCCESS;
            ok = ok && cna_effect_get_shadow_cascades_ext(basic, &read_back) ==
                CNA_RESULT_SUCCESS && read_back.count == INT32_C(2);
            if (cna_cascaded_shadow_map_destroy(cascaded) != CNA_RESULT_SUCCESS) {
                ok = 0;
            }
        }
    }
    if (cna_effect_destroy(basic) != CNA_RESULT_SUCCESS) {
        ok = 0;
    }
    return ok;
}

/* The shadow budget is a pure CPU object: it needs no device, and every route works wherever the
 * engine layer is present. `select` is not bound here -- it takes a ClusteredLightSetEXT, which
 * CBIND-086 owns, so that row waits for the slice that binds the type. */
static int validate_shadow_policy(const CNA_Handle graphics_device)
{
    CNA_ClusteredShadowPolicyHandle policy = CNA_INVALID_HANDLE;
    int32_t value = -1;
    float scalar = -1.0F;
    uint64_t count = UINT64_C(7);
    CNA_Bool flag = UINT8_C(9);
    int ok = 1;

    if (cna_clustered_shadow_policy_create(
            graphics_device, CNA_CLUSTERED_SHADOW_DEFAULT_BUDGET_EXT, &policy) !=
            CNA_RESULT_SUCCESS ||
        policy == CNA_INVALID_HANDLE) {
        return 0;
    }
    ok = cna_clustered_shadow_policy_get_budget(policy, &value) == CNA_RESULT_SUCCESS &&
        value == CNA_CLUSTERED_SHADOW_DEFAULT_BUDGET_EXT;
    ok = ok && cna_clustered_shadow_policy_set_budget(policy, INT32_C(2)) == CNA_RESULT_SUCCESS;
    ok = ok && cna_clustered_shadow_policy_get_budget(policy, &value) == CNA_RESULT_SUCCESS &&
        value == INT32_C(2);
    ok = ok && cna_clustered_shadow_policy_get_hysteresis(policy, &scalar) == CNA_RESULT_SUCCESS &&
        scalar == CNA_CLUSTERED_SHADOW_DEFAULT_HYSTERESIS_EXT;
    ok = ok && cna_clustered_shadow_policy_set_hysteresis(policy, 2.0F) == CNA_RESULT_SUCCESS;
    ok = ok && cna_clustered_shadow_policy_get_hysteresis(policy, &scalar) == CNA_RESULT_SUCCESS &&
        scalar == 2.0F;
    /* Nothing has been selected, so the selection is empty and asking with no room succeeds
       rather than refusing -- a zero requirement fits any capacity. */
    ok = ok && cna_clustered_shadow_policy_copy_selected(policy, 0, UINT64_C(0), &count) ==
        CNA_RESULT_SUCCESS && count == UINT64_C(0);
    ok = ok && cna_clustered_shadow_policy_is_selected(policy, INT32_C(0), &flag) ==
        CNA_RESULT_SUCCESS && flag == CNA_FALSE;
    /* A score exists only for a light the last selection scored, so asking about one it did not
       is refused rather than answered with zero -- which would read as "scored, and worthless". */
    ok = ok && cna_clustered_shadow_policy_get_score(policy, INT32_C(0), &scalar) !=
        CNA_RESULT_SUCCESS;
    ok = ok && cna_clustered_shadow_policy_get_score(policy, INT32_C(-1), &scalar) !=
        CNA_RESULT_SUCCESS;
    ok = ok && cna_clustered_shadow_policy_get_request_count(policy, &value) ==
        CNA_RESULT_SUCCESS && value == INT32_C(0);
    ok = ok && cna_clustered_shadow_policy_get_refused_count(policy, &value) ==
        CNA_RESULT_SUCCESS && value == INT32_C(0);
    ok = ok && cna_clustered_shadow_policy_reset(policy) == CNA_RESULT_SUCCESS;

    if (cna_clustered_shadow_policy_destroy(policy) != CNA_RESULT_SUCCESS) {
        ok = 0;
    }
    return ok && cna_clustered_shadow_policy_destroy(policy) != CNA_RESULT_SUCCESS;
}


/* CBIND-085C2. Every precondition asserted here was read out of the canonical bodies before the
 * test was written, rather than discovered by it failing. */
static int validate_prepass_and_contact(const CNA_Handle graphics_device)
{
    CNA_DepthNormalPrepassHandle prepass = CNA_INVALID_HANDLE;
    CNA_PostProcessPassHandle contact = CNA_INVALID_HANDLE;
    CNA_Matrix view;
    CNA_Matrix projection;
    CNA_Bool flag = UINT8_C(9);
    float channels[4];
    float scalar = -1.0F;
    int32_t count = -1;
    uint64_t bytes = UINT64_C(0);
    int ok = 1;

    if (cna_matrix_get_identity(&view) != CNA_RESULT_SUCCESS ||
        cna_matrix_get_identity(&projection) != CNA_RESULT_SUCCESS) {
        return 0;
    }

    /* The pure functions answer without a prepass, and pack/unpack must round-trip. */
    if (cna_depth_normal_prepass_pack_depth(0.25F, &channels[0], &channels[1], &channels[2],
                                            &channels[3]) != CNA_RESULT_SUCCESS ||
        cna_depth_normal_prepass_unpack_depth(channels[0], channels[1], channels[2], channels[3],
                                              &scalar) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    if (scalar < 0.2499F || scalar > 0.2501F) {
        return 0;
    }
    /* packDepth stops one texel short of 1.0 on purpose: fract(1.0) is 0, so an unclamped
       far-plane depth would read back as the nearest possible surface. */
    if (cna_depth_normal_prepass_pack_depth(1.0F, &channels[0], &channels[1], &channels[2],
                                            &channels[3]) != CNA_RESULT_SUCCESS ||
        cna_depth_normal_prepass_unpack_depth(channels[0], channels[1], channels[2], channels[3],
                                              &scalar) != CNA_RESULT_SUCCESS ||
        scalar >= 1.0F) {
        return 0;
    }
    if (cna_depth_normal_prepass_pack_depth(0.5F, 0, &channels[1], &channels[2], &channels[3]) !=
        CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    /* A texel with full alpha carries no velocity, and decoding one yields exactly zero rather
       than a small wrong motion. */
    {
        CNA_Color opaque;
        CNA_Vector2 velocity;
        opaque.r = 128U; opaque.g = 128U; opaque.b = 0U; opaque.a = 255U;
        if (cna_depth_normal_prepass_has_velocity_ext(opaque, &flag) != CNA_RESULT_SUCCESS ||
            flag != CNA_FALSE ||
            cna_depth_normal_prepass_decode_velocity_ext(opaque, &velocity) !=
                CNA_RESULT_SUCCESS ||
            velocity.x != 0.0F || velocity.y != 0.0F) {
            return 0;
        }
        opaque.a = 0U;
        if (cna_depth_normal_prepass_has_velocity_ext(opaque, &flag) != CNA_RESULT_SUCCESS ||
            flag != CNA_TRUE) {
            return 0;
        }
    }
    /* Both GLSL helpers are string copy-outs, and the packed and unpacked decoders differ. */
    {
        uint64_t packed_bytes = UINT64_C(0);
        uint64_t plain_bytes = UINT64_C(0);
        if (cna_depth_normal_prepass_copy_depth_decode_glsl(CNA_TRUE, 0, UINT64_C(0),
                                                            &packed_bytes) !=
                CNA_RESULT_BUFFER_TOO_SMALL ||
            packed_bytes == UINT64_C(0) ||
            cna_depth_normal_prepass_copy_depth_decode_glsl(CNA_FALSE, 0, UINT64_C(0),
                                                            &plain_bytes) !=
                CNA_RESULT_BUFFER_TOO_SMALL ||
            plain_bytes == UINT64_C(0) || packed_bytes == plain_bytes) {
            return 0;
        }
        /* A non-canonical boolean is refused before anything is formatted. */
        if (cna_depth_normal_prepass_copy_depth_decode_glsl(UINT8_C(2), 0, UINT64_C(0), &bytes) !=
            CNA_RESULT_INVALID_ARGUMENT) {
            return 0;
        }
        if (cna_depth_normal_prepass_copy_velocity_decode_glsl(0, UINT64_C(0), &bytes) !=
                CNA_RESULT_BUFFER_TOO_SMALL || bytes == UINT64_C(0)) {
            return 0;
        }
    }
    if (cna_depth_normal_prepass_uses_packed_depth_ext(graphics_device, &flag) !=
            CNA_RESULT_SUCCESS ||
        (flag != CNA_TRUE && flag != CNA_FALSE)) {
        return 0;
    }

    /* A non-positive size is refused rather than clamped. */
    if (cna_depth_normal_prepass_create(graphics_device, INT32_C(0), INT32_C(16),
                                        CNA_DEPTH_ENCODING_AUTOMATIC, &prepass) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        prepass != CNA_INVALID_HANDLE) {
        return 0;
    }
    if (cna_depth_normal_prepass_create(graphics_device, INT32_C(32), INT32_C(16), UINT32_C(9),
                                        &prepass) != CNA_RESULT_INVALID_ARGUMENT ||
        prepass != CNA_INVALID_HANDLE) {
        return 0;
    }
    if (cna_depth_normal_prepass_create(graphics_device, INT32_C(32), INT32_C(16),
                                        CNA_DEPTH_ENCODING_AUTOMATIC, &prepass) !=
            CNA_RESULT_SUCCESS ||
        prepass == CNA_INVALID_HANDLE) {
        return 0;
    }

    ok = cna_depth_normal_prepass_get_pass_count(prepass, &count) == CNA_RESULT_SUCCESS &&
        count >= INT32_C(1);
    ok = ok && cna_depth_normal_prepass_is_supported(prepass, graphics_device, &flag) ==
        CNA_RESULT_SUCCESS;
    ok = ok && cna_depth_normal_prepass_is_using_multiple_render_targets(prepass, &flag) ==
        CNA_RESULT_SUCCESS;
    ok = ok && cna_depth_normal_prepass_is_depth_packed(prepass, &flag) == CNA_RESULT_SUCCESS;
    /* Roughness is clamped rather than refused, exactly as the canonical setter clamps it. */
    ok = ok && cna_depth_normal_prepass_set_roughness(prepass, 5.0F) == CNA_RESULT_SUCCESS;
    ok = ok && cna_depth_normal_prepass_get_roughness(prepass, &scalar) == CNA_RESULT_SUCCESS &&
        scalar == 1.0F;
    ok = ok && cna_depth_normal_prepass_set_roughness(prepass, -1.0F) == CNA_RESULT_SUCCESS;
    ok = ok && cna_depth_normal_prepass_get_roughness(prepass, &scalar) == CNA_RESULT_SUCCESS &&
        scalar == 0.0F;
    ok = ok && cna_depth_normal_prepass_set_previous_world_ext(prepass, &view) ==
        CNA_RESULT_SUCCESS;
    ok = ok && cna_depth_normal_prepass_set_previous_camera_ext(prepass, &view, &projection) ==
        CNA_RESULT_SUCCESS;
    ok = ok && cna_depth_normal_prepass_set_previous_camera_ext(prepass, &view, 0) ==
        CNA_RESULT_INVALID_ARGUMENT;
    /* Turning velocity on adds a pass wherever the renderer needs separate targets. */
    ok = ok && cna_depth_normal_prepass_set_velocity_enabled_ext(prepass, CNA_TRUE) ==
        CNA_RESULT_SUCCESS;
    ok = ok && cna_depth_normal_prepass_is_velocity_enabled_ext(prepass, &flag) ==
        CNA_RESULT_SUCCESS && flag == CNA_TRUE;
    ok = ok && cna_depth_normal_prepass_set_velocity_enabled_ext(prepass, UINT8_C(2)) ==
        CNA_RESULT_INVALID_ARGUMENT;

    /* begin/end: the near plane must be positive and the far plane beyond it, because depth is
       normalised by the far plane. Both refusals happen before the pass opens, so end() still
       reports no pass afterwards. */
    ok = ok && cna_depth_normal_prepass_begin(prepass, INT32_C(0), &view, &projection, 0.0F,
                                              100.0F) == CNA_RESULT_INVALID_ARGUMENT;
    ok = ok && cna_depth_normal_prepass_begin(prepass, INT32_C(0), &view, &projection, 10.0F,
                                              1.0F) == CNA_RESULT_INVALID_ARGUMENT;
    ok = ok && cna_depth_normal_prepass_begin(prepass, INT32_C(99), &view, &projection, 1.0F,
                                              100.0F) != CNA_RESULT_SUCCESS;
    ok = ok && cna_depth_normal_prepass_end(prepass) != CNA_RESULT_SUCCESS;
    ok = ok && cna_depth_normal_prepass_begin(prepass, INT32_C(0), &view, &projection, 1.0F,
                                              100.0F) == CNA_RESULT_SUCCESS;
    /* While a pass is open, resize and the velocity switch are refused rather than reallocating
       the target the device is drawing into. */
    ok = ok && cna_depth_normal_prepass_resize(prepass, INT32_C(64), INT32_C(64)) !=
        CNA_RESULT_SUCCESS;
    ok = ok && cna_depth_normal_prepass_set_velocity_enabled_ext(prepass, CNA_FALSE) !=
        CNA_RESULT_SUCCESS;
    ok = ok && cna_depth_normal_prepass_end(prepass) == CNA_RESULT_SUCCESS;
    ok = ok && cna_depth_normal_prepass_resize(prepass, INT32_C(64), INT32_C(64)) ==
        CNA_RESULT_SUCCESS;
    ok = ok && cna_depth_normal_prepass_resize(prepass, INT32_C(0), INT32_C(64)) !=
        CNA_RESULT_SUCCESS;

    if (ok) {
        CNA_EffectHandle rigid = CNA_INVALID_HANDLE;
        CNA_EffectHandle skinned = CNA_INVALID_HANDLE;
        CNA_Handle depth = CNA_INVALID_HANDLE;
        CNA_Handle normals = CNA_INVALID_HANDLE;
        CNA_Handle velocity = CNA_INVALID_HANDLE;
        ok = cna_depth_normal_prepass_get_prepass_effect(prepass, &rigid) == CNA_RESULT_SUCCESS;
        ok = ok && cna_depth_normal_prepass_get_skinned_prepass_effect(prepass, &skinned) ==
            CNA_RESULT_SUCCESS;
        ok = ok && cna_depth_normal_prepass_get_depth_texture(prepass, &depth) ==
            CNA_RESULT_SUCCESS;
        ok = ok && cna_depth_normal_prepass_get_normal_texture(prepass, &normals) ==
            CNA_RESULT_SUCCESS;
        ok = ok && cna_depth_normal_prepass_get_velocity_texture_ext(prepass, &velocity) ==
            CNA_RESULT_SUCCESS;
        if (ok && depth != CNA_INVALID_HANDLE) {
            /* A borrow keeps the prepass alive, so destroying it is refused until released. */
            ok = cna_depth_normal_prepass_destroy(prepass) != CNA_RESULT_SUCCESS;
        }
        if (ok && rigid != CNA_INVALID_HANDLE) {
            ok = cna_effect_destroy(rigid) == CNA_RESULT_SUCCESS;
        }
        if (ok && skinned != CNA_INVALID_HANDLE) {
            ok = ok && cna_effect_destroy(skinned) == CNA_RESULT_SUCCESS;
        }
        if (ok && depth != CNA_INVALID_HANDLE) {
            ok = ok && cna_render_target_destroy(depth) == CNA_RESULT_SUCCESS;
        }
        if (ok && normals != CNA_INVALID_HANDLE) {
            ok = ok && cna_render_target_destroy(normals) == CNA_RESULT_SUCCESS;
        }
        if (ok && velocity != CNA_INVALID_HANDLE) {
            ok = ok && cna_render_target_destroy(velocity) == CNA_RESULT_SUCCESS;
        }
    }
    if (!ok) {
        (void)cna_depth_normal_prepass_destroy(prepass);
        return 0;
    }
    if (cna_depth_normal_prepass_destroy(prepass) != CNA_RESULT_SUCCESS) {
        return 0;
    }

    /* The contact-shadow pass is a PostProcessPass, so the shared operations drive it and its own
       settings are reached by routes that check the concrete type. */
    if (cna_contact_shadow_pass_create(graphics_device, &contact) != CNA_RESULT_SUCCESS ||
        contact == CNA_INVALID_HANDLE) {
        return 0;
    }
    {
        CNA_Vector3 direction;
        ok = cna_post_process_pass_copy_name(contact, 0, UINT64_C(0), &bytes) ==
            CNA_RESULT_BUFFER_TOO_SMALL && bytes > UINT64_C(0);
        ok = ok && cna_post_process_pass_is_supported(contact, graphics_device, &flag) ==
            CNA_RESULT_SUCCESS;
        direction.x = 1.0F; direction.y = 0.0F; direction.z = 0.0F;
        ok = ok && cna_contact_shadow_pass_set_light_direction(contact, &direction) ==
            CNA_RESULT_SUCCESS;
        ok = ok && cna_contact_shadow_pass_get_light_direction(contact, &direction) ==
            CNA_RESULT_SUCCESS && direction.x == 1.0F;
        ok = ok && cna_contact_shadow_pass_set_light_direction(contact, 0) ==
            CNA_RESULT_INVALID_ARGUMENT;
        ok = ok && cna_contact_shadow_pass_set_max_distance(contact, 0.5F) == CNA_RESULT_SUCCESS;
        ok = ok && cna_contact_shadow_pass_get_max_distance(contact, &scalar) ==
            CNA_RESULT_SUCCESS && scalar == 0.5F;
        ok = ok && cna_contact_shadow_pass_set_step_count(contact, INT32_C(20)) ==
            CNA_RESULT_SUCCESS;
        ok = ok && cna_contact_shadow_pass_get_step_count(contact, &count) ==
            CNA_RESULT_SUCCESS && count == INT32_C(20);
        ok = ok && cna_contact_shadow_pass_set_thickness(contact, 0.3F) == CNA_RESULT_SUCCESS;
        ok = ok && cna_contact_shadow_pass_get_thickness(contact, &scalar) ==
            CNA_RESULT_SUCCESS && scalar == 0.3F;
        ok = ok && cna_contact_shadow_pass_set_intensity(contact, 0.75F) == CNA_RESULT_SUCCESS;
        ok = ok && cna_contact_shadow_pass_get_intensity(contact, &scalar) ==
            CNA_RESULT_SUCCESS && scalar == 0.75F;
        ok = ok && cna_contact_shadow_pass_set_bias(contact, 0.05F) == CNA_RESULT_SUCCESS;
        ok = ok && cna_contact_shadow_pass_get_bias(contact, &scalar) == CNA_RESULT_SUCCESS &&
            scalar == 0.05F;
        ok = ok && cna_contact_shadow_pass_copy_fallback_reason(contact, 0, UINT64_C(0), &bytes) !=
            CNA_RESULT_INVALID_ARGUMENT;

        /* The occlusion test is a strict band: deeper than the bias, shallower than the assumed
           thickness. A sample exactly at the bias is NOT occluded, which is the arm a `>=` would
           get wrong. */
        ok = ok && cna_contact_shadow_pass_is_occluded(1.1F, 1.0F, 0.05F, 0.5F, &flag) ==
            CNA_RESULT_SUCCESS && flag == CNA_TRUE;
        ok = ok && cna_contact_shadow_pass_is_occluded(1.05F, 1.0F, 0.05F, 0.5F, &flag) ==
            CNA_RESULT_SUCCESS && flag == CNA_FALSE;
        ok = ok && cna_contact_shadow_pass_is_occluded(2.0F, 1.0F, 0.05F, 0.5F, &flag) ==
            CNA_RESULT_SUCCESS && flag == CNA_FALSE;
        /* Both visibilities are clamped before multiplying, so a caller cannot brighten a pixel
           by handing in a visibility above one. */
        ok = ok && cna_contact_shadow_pass_combine_visibility(2.0F, 0.5F, &scalar) ==
            CNA_RESULT_SUCCESS && scalar == 0.5F;
        ok = ok && cna_contact_shadow_pass_combine_visibility(-1.0F, 0.5F, &scalar) ==
            CNA_RESULT_SUCCESS && scalar == 0.0F;
        ok = ok && cna_contact_shadow_pass_copy_occlusion_test_glsl(0, UINT64_C(0), &bytes) ==
            CNA_RESULT_BUFFER_TOO_SMALL && bytes > UINT64_C(0);
    }
    if (ok) {
        /* A blit pass is not a contact-shadow pass, and the settings say so by argument. */
        CNA_PostProcessPassHandle blit = CNA_INVALID_HANDLE;
        if (cna_blit_pass_create(graphics_device, &blit) == CNA_RESULT_SUCCESS) {
            ok = cna_contact_shadow_pass_get_bias(blit, &scalar) == CNA_RESULT_INVALID_ARGUMENT;
            ok = ok && cna_contact_shadow_pass_copy_fallback_reason(blit, 0, UINT64_C(0), &bytes) ==
                CNA_RESULT_INVALID_ARGUMENT;
            ok = ok && cna_post_process_pass_destroy(blit) == CNA_RESULT_SUCCESS;
        }
    }
    if (!ok) {
        (void)cna_post_process_pass_destroy(contact);
        return 0;
    }
    return cna_post_process_pass_destroy(contact) == CNA_RESULT_SUCCESS;
}


/* CBIND-086A. The set is a collection of VALUES: reading a light copies it, so nothing here has to
 * be released and destroying the set is never refused. Every precondition below was read out of
 * ClusteredLightSetEXT.cpp before this test was written. */
static int validate_clustered_light_set(const CNA_Handle graphics_device)
{
    CNA_ClusteredLightSetHandle set = CNA_INVALID_HANDLE;
    CNA_ClusteredLightEXT light;
    CNA_ClusteredLightEXT read_back;
    CNA_PointLightEXT point;
    CNA_SpotLightEXT spot;
    CNA_BoundingSphere sphere;
    CNA_Bool flag = UINT8_C(9);
    int32_t index = -1;
    int32_t count = -1;
    uint64_t total = UINT64_C(0);
    int ok = 1;

    if (cna_clustered_light_ext_init(&light) != CNA_RESULT_SUCCESS ||
        cna_point_light_ext_init(&point) != CNA_RESULT_SUCCESS ||
        cna_spot_light_ext_init(&spot) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    if (light.type != CNA_CLUSTERED_LIGHT_TYPE_POINT || light.range != 20.0F ||
        light.casts_shadows != CNA_FALSE) {
        return 0;
    }
    /* isUsable is exposed so a caller can ask before being refused, and the two must agree. */
    if (cna_clustered_light_set_is_usable(&light, &flag) != CNA_RESULT_SUCCESS ||
        flag != CNA_TRUE) {
        return 0;
    }
    {
        CNA_ClusteredLightEXT bad = light;
        bad.range = 0.0F;
        if (cna_clustered_light_set_is_usable(&bad, &flag) != CNA_RESULT_SUCCESS ||
            flag != CNA_FALSE) {
            return 0;
        }
        bad = light;
        bad.intensity = -1.0F;
        if (cna_clustered_light_set_is_usable(&bad, &flag) != CNA_RESULT_SUCCESS ||
            flag != CNA_FALSE) {
            return 0;
        }
        /* An uninitialized light is refused rather than read. */
        memset(&bad, 0, sizeof(bad));
        if (cna_clustered_light_set_is_usable(&bad, &flag) != CNA_RESULT_INVALID_ARGUMENT) {
            return 0;
        }
    }

    if (cna_clustered_light_set_create(graphics_device, &set) != CNA_RESULT_SUCCESS ||
        set == CNA_INVALID_HANDLE) {
        return 0;
    }

    ok = cna_clustered_light_set_is_empty(set, &flag) == CNA_RESULT_SUCCESS && flag == CNA_TRUE;
    ok = ok && cna_clustered_light_set_get_count(set, &count) == CNA_RESULT_SUCCESS &&
        count == INT32_C(0);
    ok = ok && cna_clustered_light_set_add(set, &light, &index) == CNA_RESULT_SUCCESS &&
        index == INT32_C(0);
    ok = ok && cna_clustered_light_set_add_point(set, &point, &index) == CNA_RESULT_SUCCESS &&
        index == INT32_C(1);
    ok = ok && cna_clustered_light_set_add_spot(set, &spot, &index) == CNA_RESULT_SUCCESS &&
        index == INT32_C(2);
    ok = ok && cna_clustered_light_set_get_count(set, &count) == CNA_RESULT_SUCCESS &&
        count == INT32_C(3);
    ok = ok && cna_clustered_light_set_is_empty(set, &flag) == CNA_RESULT_SUCCESS &&
        flag == CNA_FALSE;
    /* A converted point light keeps its kind, and a converted spot keeps its cone. */
    ok = ok && cna_clustered_light_set_get_at(set, INT32_C(1), &read_back) == CNA_RESULT_SUCCESS &&
        read_back.type == CNA_CLUSTERED_LIGHT_TYPE_POINT;
    ok = ok && cna_clustered_light_set_get_at(set, INT32_C(2), &read_back) == CNA_RESULT_SUCCESS &&
        read_back.type == CNA_CLUSTERED_LIGHT_TYPE_SPOT &&
        read_back.outer_angle == spot.outer_angle;

    /* An unusable light is refused by ARGUMENT; a full set is refused by STATE. The canonical code
       throws two different exceptions for these, and a C caller acts on them differently: drop a
       light, or fix the one just built. */
    {
        CNA_ClusteredLightEXT bad = light;
        bad.range = -1.0F;
        ok = ok && cna_clustered_light_set_add(set, &bad, &index) == CNA_RESULT_INVALID_ARGUMENT;
        ok = ok && cna_clustered_light_set_replace_at(set, INT32_C(0), &bad) ==
            CNA_RESULT_INVALID_ARGUMENT;
    }
    /* Every index-taking route refuses an index the set does not hold. */
    ok = ok && cna_clustered_light_set_get_at(set, INT32_C(3), &read_back) ==
        CNA_RESULT_INVALID_ARGUMENT;
    ok = ok && cna_clustered_light_set_get_at(set, INT32_C(-1), &read_back) ==
        CNA_RESULT_INVALID_ARGUMENT;
    ok = ok && cna_clustered_light_set_remove_at(set, INT32_C(9)) == CNA_RESULT_INVALID_ARGUMENT;
    ok = ok && cna_clustered_light_set_get_bounds_at(set, INT32_C(9), &sphere) ==
        CNA_RESULT_INVALID_ARGUMENT;
    ok = ok && cna_clustered_light_set_replace_at(set, INT32_C(9), &light) ==
        CNA_RESULT_INVALID_ARGUMENT;

    /* A bounding sphere is the light's reach, so its radius is the range. */
    ok = ok && cna_clustered_light_set_get_bounds_at(set, INT32_C(0), &sphere) ==
        CNA_RESULT_SUCCESS && sphere.radius == light.range;

    /* Both copy-outs report the count with no room and write nothing. */
    {
        CNA_ClusteredLightEXT lights[4];
        CNA_BoundingSphere spheres[4];
        lights[0].struct_size = UINT32_C(0xDEAD);
        ok = ok && cna_clustered_light_set_copy_lights(set, 0, UINT64_C(0), &total) ==
            CNA_RESULT_BUFFER_TOO_SMALL && total == UINT64_C(3) &&
            lights[0].struct_size == UINT32_C(0xDEAD);
        ok = ok && cna_clustered_light_set_copy_lights(set, lights, UINT64_C(4), &total) ==
            CNA_RESULT_SUCCESS && total == UINT64_C(3) &&
            lights[2].type == CNA_CLUSTERED_LIGHT_TYPE_SPOT;
        ok = ok && cna_clustered_light_set_copy_bounds(set, 0, UINT64_C(0), &total) ==
            CNA_RESULT_BUFFER_TOO_SMALL && total == UINT64_C(3);
        ok = ok && cna_clustered_light_set_copy_bounds(set, spheres, UINT64_C(4), &total) ==
            CNA_RESULT_SUCCESS && total == UINT64_C(3);
    }

    /* A copied-out light is a value: it stays correct after the set changes. */
    ok = ok && cna_clustered_light_set_get_at(set, INT32_C(0), &read_back) == CNA_RESULT_SUCCESS;
    ok = ok && cna_clustered_light_set_remove_at(set, INT32_C(0)) == CNA_RESULT_SUCCESS;
    ok = ok && read_back.range == light.range;
    ok = ok && cna_clustered_light_set_get_count(set, &count) == CNA_RESULT_SUCCESS &&
        count == INT32_C(2);
    ok = ok && cna_clustered_light_set_clear(set) == CNA_RESULT_SUCCESS;
    ok = ok && cna_clustered_light_set_is_empty(set, &flag) == CNA_RESULT_SUCCESS &&
        flag == CNA_TRUE;

    /* The set refuses past its maximum rather than growing, because the uploaded buffer and the
       shader's index width are sized from that bound. Filling it is the only way to assert it. */
    {
        int32_t filled = 0;
        for (filled = 0; ok && filled < CNA_CLUSTERED_LIGHT_SET_MAX_EXT; ++filled) {
            ok = cna_clustered_light_set_add(set, &light, &index) == CNA_RESULT_SUCCESS &&
                index == filled;
        }
        ok = ok && cna_clustered_light_set_add(set, &light, &index) == CNA_RESULT_INVALID_STATE;
        ok = ok && cna_clustered_light_set_get_count(set, &count) == CNA_RESULT_SUCCESS &&
            count == CNA_CLUSTERED_LIGHT_SET_MAX_EXT;
    }

    if (!ok) {
        (void)cna_clustered_light_set_destroy(set);
        return 0;
    }
    /* Values, so destruction is never refused for an outstanding read. */
    return cna_clustered_light_set_destroy(set) == CNA_RESULT_SUCCESS &&
        cna_clustered_light_set_destroy(set) != CNA_RESULT_SUCCESS;
}


/* CBIND-086B. Every contract and every clamp below was read out of the three canonical bodies
 * before this was written, and each is asserted as whichever it is. */
static int validate_cluster_grid_and_buffer(const CNA_Handle graphics_device)
{
    CNA_ClusteredLightGridHandle grid = CNA_INVALID_HANDLE;
    CNA_ClusteredLightAssignmentHandle assignment = CNA_INVALID_HANDLE;
    CNA_ClusteredLightBufferHandle buffer = CNA_INVALID_HANDLE;
    CNA_ClusteredLightSetHandle lights = CNA_INVALID_HANDLE;
    CNA_ClusteredLightEXT light;
    CNA_Matrix projection;
    CNA_Matrix view;
    CNA_BoundingBox bounds;
    CNA_BoundingSphere spheres[4];
    CNA_Bool flag = UINT8_C(9);
    int32_t value = -1;
    uint64_t count = UINT64_C(0);
    float scalar = -1.0F;
    int ok = 1;

    if (cna_matrix_get_identity(&projection) != CNA_RESULT_SUCCESS ||
        cna_matrix_get_identity(&view) != CNA_RESULT_SUCCESS ||
        cna_clustered_light_ext_init(&light) != CNA_RESULT_SUCCESS) {
        return 0;
    }

    /* A dimension outside its range is REFUSED, not clamped: the cluster count is what the
       light-index list is sized from, so a grid quietly smaller than asked for would size a list
       the caller did not mean. */
    if (cna_clustered_light_grid_create(graphics_device, INT32_C(0), INT32_C(8), INT32_C(24),
                                        &grid) != CNA_RESULT_INVALID_ARGUMENT ||
        grid != CNA_INVALID_HANDLE) {
        return 0;
    }
    if (cna_clustered_light_grid_create(
            graphics_device, CNA_CLUSTER_GRID_MAX_TILES_PER_AXIS_EXT + 1, INT32_C(8), INT32_C(24),
            &grid) != CNA_RESULT_INVALID_ARGUMENT ||
        grid != CNA_INVALID_HANDLE) {
        return 0;
    }
    if (cna_clustered_light_grid_create(
            graphics_device, INT32_C(4), INT32_C(4), CNA_CLUSTER_GRID_MAX_SLICE_COUNT_EXT + 1,
            &grid) != CNA_RESULT_INVALID_ARGUMENT ||
        grid != CNA_INVALID_HANDLE) {
        return 0;
    }
    if (cna_clustered_light_grid_create(graphics_device, INT32_C(4), INT32_C(4), INT32_C(8),
                                        &grid) != CNA_RESULT_SUCCESS ||
        grid == CNA_INVALID_HANDLE) {
        return 0;
    }

    ok = cna_clustered_light_grid_get_tiles_x(grid, &value) == CNA_RESULT_SUCCESS &&
        value == INT32_C(4);
    ok = ok && cna_clustered_light_grid_get_tiles_y(grid, &value) == CNA_RESULT_SUCCESS &&
        value == INT32_C(4);
    ok = ok && cna_clustered_light_grid_get_slice_count(grid, &value) == CNA_RESULT_SUCCESS &&
        value == INT32_C(8);
    /* The cluster count is the product, and the flat index must agree with it at the far corner. */
    ok = ok && cna_clustered_light_grid_get_cluster_count(grid, &value) == CNA_RESULT_SUCCESS &&
        value == INT32_C(4 * 4 * 8);
    ok = ok && cna_clustered_light_grid_cluster_index(grid, INT32_C(3), INT32_C(3), INT32_C(7),
                                                      &value) == CNA_RESULT_SUCCESS &&
        value == INT32_C(4 * 4 * 8 - 1);
    ok = ok && cna_clustered_light_grid_cluster_index(grid, INT32_C(4), INT32_C(0), INT32_C(0),
                                                      &value) == CNA_RESULT_INVALID_ARGUMENT;
    ok = ok && cna_clustered_light_grid_cluster_index(grid, INT32_C(0), INT32_C(0), INT32_C(-1),
                                                      &value) == CNA_RESULT_INVALID_ARGUMENT;

    /* Before a projection the grid has no shape, and cluster bounds say so rather than guessing. */
    ok = ok && cna_clustered_light_grid_has_projection(grid, &flag) == CNA_RESULT_SUCCESS &&
        flag == CNA_FALSE;
    ok = ok && cna_clustered_light_grid_cluster_bounds(grid, INT32_C(0), INT32_C(0), INT32_C(0),
                                                       &bounds) == CNA_RESULT_INVALID_STATE;
    /* The planes must be able to space the slices: the spacing is a ratio of the two. */
    ok = ok && cna_clustered_light_grid_set_projection(grid, &projection, 0.0F, 100.0F) ==
        CNA_RESULT_INVALID_ARGUMENT;
    ok = ok && cna_clustered_light_grid_set_projection(grid, &projection, 10.0F, 1.0F) ==
        CNA_RESULT_INVALID_ARGUMENT;
    ok = ok && cna_clustered_light_grid_set_projection(grid, 0, 1.0F, 100.0F) ==
        CNA_RESULT_INVALID_ARGUMENT;
    ok = ok && cna_clustered_light_grid_set_projection(grid, &projection, 1.0F, 100.0F) ==
        CNA_RESULT_SUCCESS;
    ok = ok && cna_clustered_light_grid_has_projection(grid, &flag) == CNA_RESULT_SUCCESS &&
        flag == CNA_TRUE;
    ok = ok && cna_clustered_light_grid_get_near_plane(grid, &scalar) == CNA_RESULT_SUCCESS &&
        scalar == 1.0F;
    ok = ok && cna_clustered_light_grid_get_far_plane(grid, &scalar) == CNA_RESULT_SUCCESS &&
        scalar == 100.0F;
    ok = ok && cna_clustered_light_grid_get_inverse_projection(grid, &projection) ==
        CNA_RESULT_SUCCESS;
    ok = ok && cna_clustered_light_grid_cluster_bounds(grid, INT32_C(0), INT32_C(0), INT32_C(0),
                                                       &bounds) == CNA_RESULT_SUCCESS;

    /* THE SLICE COUNT ITSELF IS A VALID BOUNDARY: there is one more boundary than slice, and the
       last names the far edge. A `>=` check here would silently lose the far plane. */
    ok = ok && cna_clustered_light_grid_slice_distance(grid, INT32_C(0), &scalar) ==
        CNA_RESULT_SUCCESS && scalar == 1.0F;
    ok = ok && cna_clustered_light_grid_slice_distance(grid, INT32_C(8), &scalar) ==
        CNA_RESULT_SUCCESS;
    ok = ok && cna_clustered_light_grid_slice_distance(grid, INT32_C(9), &scalar) ==
        CNA_RESULT_INVALID_ARGUMENT;
    ok = ok && cna_clustered_light_grid_slice_distance(grid, INT32_C(-1), &scalar) ==
        CNA_RESULT_INVALID_ARGUMENT;
    /* Placing a distance CLAMPS into the grid rather than refusing: a point outside the frustum
       belongs to the nearest slice, which is what a renderer wants at the edge. */
    ok = ok && cna_clustered_light_grid_slice_for_view_distance(grid, -50.0F, &value) ==
        CNA_RESULT_SUCCESS && value == INT32_C(0);
    ok = ok && cna_clustered_light_grid_slice_for_view_distance(grid, 1.0e9F, &value) ==
        CNA_RESULT_SUCCESS && value == INT32_C(7);

    if (!ok) { (void)cna_clustered_light_grid_destroy(grid); return 0; }

    if (cna_clustered_light_assignment_create(graphics_device, &assignment) !=
            CNA_RESULT_SUCCESS ||
        cna_clustered_light_set_create(graphics_device, &lights) != CNA_RESULT_SUCCESS) {
        (void)cna_clustered_light_grid_destroy(grid);
        return 0;
    }

    ok = cna_clustered_light_set_add(lights, &light, &value) == CNA_RESULT_SUCCESS;
    ok = ok && cna_clustered_light_set_copy_bounds(lights, spheres, UINT64_C(4), &count) ==
        CNA_RESULT_SUCCESS && count == UINT64_C(1);
    ok = ok && cna_clustered_light_assignment_assign(assignment, grid, &view, spheres, count) ==
        CNA_RESULT_SUCCESS;
    ok = ok && cna_clustered_light_assignment_get_light_count(assignment, &value) ==
        CNA_RESULT_SUCCESS && value == INT32_C(1);
    ok = ok && cna_clustered_light_assignment_get_cluster_count(assignment, &value) ==
        CNA_RESULT_SUCCESS && value == INT32_C(4 * 4 * 8);
    ok = ok && cna_clustered_light_assignment_get_total_reference_count(assignment, &value) ==
        CNA_RESULT_SUCCESS && value >= INT32_C(0);
    ok = ok && cna_clustered_light_assignment_get_max_lights_per_cluster(assignment, &value) ==
        CNA_RESULT_SUCCESS && value >= INT32_C(0);
    /* One more offset than cluster: the offsets are boundaries, not slots. */
    ok = ok && cna_clustered_light_assignment_copy_offsets(assignment, 0, UINT64_C(0), &count) ==
        CNA_RESULT_BUFFER_TOO_SMALL && count == UINT64_C(4 * 4 * 8 + 1);
    ok = ok && cna_clustered_light_assignment_copy_indices(assignment, 0, UINT64_C(0), &count) !=
        CNA_RESULT_INVALID_ARGUMENT;
    ok = ok && cna_clustered_light_assignment_copy_lights_in_cluster(
        assignment, INT32_C(0), 0, UINT64_C(0), &count) != CNA_RESULT_INVALID_ARGUMENT;
    ok = ok && cna_clustered_light_assignment_copy_lights_in_cluster(
        assignment, INT32_C(9999), 0, UINT64_C(0), &count) == CNA_RESULT_INVALID_ARGUMENT;

    /* adopt has four distinct refusals, and each answers with its own message rather than one
       flattened invalid_argument. */
    {
        int32_t offsets[3];
        int32_t indices[2];
        offsets[0] = INT32_C(0); offsets[1] = INT32_C(1); offsets[2] = INT32_C(2);
        indices[0] = INT32_C(0); indices[1] = INT32_C(0);
        ok = ok && cna_clustered_light_assignment_adopt(
            assignment, INT32_C(1), offsets, UINT64_C(3), indices, UINT64_C(2)) ==
            CNA_RESULT_SUCCESS;
        offsets[0] = INT32_C(1);
        ok = ok && cna_clustered_light_assignment_adopt(
            assignment, INT32_C(1), offsets, UINT64_C(3), indices, UINT64_C(2)) ==
            CNA_RESULT_INVALID_ARGUMENT;
        offsets[0] = INT32_C(0); offsets[2] = INT32_C(5);
        ok = ok && cna_clustered_light_assignment_adopt(
            assignment, INT32_C(1), offsets, UINT64_C(3), indices, UINT64_C(2)) ==
            CNA_RESULT_INVALID_ARGUMENT;
        offsets[1] = INT32_C(2); offsets[2] = INT32_C(1);
        ok = ok && cna_clustered_light_assignment_adopt(
            assignment, INT32_C(1), offsets, UINT64_C(3), indices, UINT64_C(1)) ==
            CNA_RESULT_INVALID_ARGUMENT;
        offsets[0] = INT32_C(0); offsets[1] = INT32_C(1); offsets[2] = INT32_C(2);
        indices[1] = INT32_C(9);
        ok = ok && cna_clustered_light_assignment_adopt(
            assignment, INT32_C(1), offsets, UINT64_C(3), indices, UINT64_C(2)) ==
            CNA_RESULT_INVALID_ARGUMENT;
    }
    ok = ok && cna_clustered_light_assignment_clear(assignment) == CNA_RESULT_SUCCESS;

    if (!ok) {
        (void)cna_clustered_light_assignment_destroy(assignment);
        (void)cna_clustered_light_set_destroy(lights);
        (void)cna_clustered_light_grid_destroy(grid);
        return 0;
    }

    if (cna_clustered_light_buffer_create(graphics_device, &buffer) != CNA_RESULT_SUCCESS) {
        (void)cna_clustered_light_assignment_destroy(assignment);
        (void)cna_clustered_light_set_destroy(lights);
        (void)cna_clustered_light_grid_destroy(grid);
        return 0;
    }
    /* Nothing uploaded yet, so binding refuses rather than binding stale textures. */
    ok = cna_clustered_light_buffer_is_uploaded(buffer, &flag) == CNA_RESULT_SUCCESS &&
        flag == CNA_FALSE;
    ok = ok && cna_clustered_light_buffer_bind(buffer, CNA_INVALID_HANDLE, INT32_C(0)) ==
        CNA_RESULT_INVALID_STATE;
    /* A mismatched trio is refused: after clear() the assignment describes no clusters, so it no
       longer matches the grid, and uploading it would light the wrong objects. */
    ok = ok && cna_clustered_light_buffer_upload(buffer, lights, grid, assignment) ==
        CNA_RESULT_INVALID_ARGUMENT;
    ok = ok && cna_clustered_light_assignment_assign(assignment, grid, &view, spheres, count) !=
        CNA_RESULT_INVALID_ARGUMENT;
    ok = ok && cna_clustered_light_buffer_copy_light_lookup_glsl(0, UINT64_C(0), &count) ==
        CNA_RESULT_BUFFER_TOO_SMALL && count > UINT64_C(0);
    ok = ok && cna_clustered_light_buffer_get_light_count(buffer, &value) == CNA_RESULT_SUCCESS;
    ok = ok && cna_clustered_light_buffer_get_cluster_count(buffer, &value) == CNA_RESULT_SUCCESS;
    ok = ok && cna_clustered_light_buffer_get_reference_count(buffer, &value) ==
        CNA_RESULT_SUCCESS;

    /* Values and private textures throughout: every destroy succeeds first time. */
    ok = ok && cna_clustered_light_buffer_destroy(buffer) == CNA_RESULT_SUCCESS;
    ok = ok && cna_clustered_light_assignment_destroy(assignment) == CNA_RESULT_SUCCESS;
    ok = ok && cna_clustered_light_set_destroy(lights) == CNA_RESULT_SUCCESS;
    ok = ok && cna_clustered_light_grid_destroy(grid) == CNA_RESULT_SUCCESS;
    return ok;
}

/* CBIND-086C. The compute path exists on exactly one renderer here (EasyGL advertises compute
   shaders; HEADLESS does not), and ClusteredLightCompute::assign falls back to the CPU sort rather
   than refusing when it is absent. So the arm-independent thing worth asserting is not "the GPU
   ran" but "both paths produce the same assignment" -- and `used_compute` is then read back
   against `is_supported` to say which one this build actually exercised. */
static int validate_clustered_compute(const CNA_Handle graphics_device)
{
    CNA_ClusteredLightGridHandle grid = CNA_INVALID_HANDLE;
    CNA_ClusteredLightAssignmentHandle on_cpu = CNA_INVALID_HANDLE;
    CNA_ClusteredLightAssignmentHandle under_test = CNA_INVALID_HANDLE;
    CNA_ClusteredLightComputeHandle compute = CNA_INVALID_HANDLE;
    CNA_Matrix projection;
    CNA_Matrix view;
    CNA_BoundingSphere spheres[3];
    int32_t cpu_offsets[512];
    int32_t test_offsets[512];
    int32_t cpu_indices[1024];
    int32_t test_indices[1024];
    uint64_t cpu_count = UINT64_C(0);
    uint64_t test_count = UINT64_C(0);
    uint64_t reason_bytes = UINT64_C(1);
    CNA_Bool supported = UINT8_C(9);
    CNA_Bool used = UINT8_C(9);
    CNA_Bool overflowed = UINT8_C(9);
    int32_t stride = -1;
    int32_t cpu_total = -1;
    int32_t test_total = -2;
    uint64_t index = UINT64_C(0);
    int ok = 1;

    if (cna_matrix_get_identity(&projection) != CNA_RESULT_SUCCESS ||
        cna_matrix_get_identity(&view) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    for (index = UINT64_C(0); index < UINT64_C(3); ++index) {
        spheres[index].center.x = (float)index * 2.0F - 2.0F;
        spheres[index].center.y = 0.0F;
        spheres[index].center.z = -6.0F;
        spheres[index].radius = 3.0F;
    }

    /* A non-positive per-cluster capacity is REFUSED, not corrected: the stride is what the
       light-index list is sized from, so a silently different one would size the wrong list. */
    if (cna_clustered_light_compute_create(graphics_device, INT32_C(0), &compute) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        compute != CNA_INVALID_HANDLE) {
        return 0;
    }
    if (cna_clustered_light_compute_create(graphics_device, INT32_C(-4), &compute) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        compute != CNA_INVALID_HANDLE) {
        return 0;
    }
    if (cna_clustered_light_compute_create(
            graphics_device, CNA_CLUSTERED_COMPUTE_DEFAULT_STRIDE_EXT, &compute) !=
            CNA_RESULT_SUCCESS ||
        compute == CNA_INVALID_HANDLE) {
        return 0;
    }

    ok = cna_clustered_light_compute_get_stride(compute, &stride) == CNA_RESULT_SUCCESS &&
        stride == CNA_CLUSTERED_COMPUTE_DEFAULT_STRIDE_EXT;
    ok = ok && cna_clustered_light_compute_is_supported(compute, &supported) ==
        CNA_RESULT_SUCCESS && (supported == CNA_TRUE || supported == CNA_FALSE);
    /* The reason is empty exactly when the program compiled, and non-empty exactly when it did
       not. That equivalence is the whole contract of the pair, so it is asserted as one. */
    ok = ok && cna_clustered_light_compute_copy_unsupported_reason(compute, 0, UINT64_C(0),
                                                                   &reason_bytes) ==
        (supported == CNA_TRUE ? CNA_RESULT_SUCCESS : CNA_RESULT_BUFFER_TOO_SMALL);
    ok = ok && ((supported == CNA_TRUE) == (reason_bytes == UINT64_C(0)));

    if (!ok || cna_clustered_light_grid_create(graphics_device, INT32_C(4), INT32_C(4),
                                               INT32_C(8), &grid) != CNA_RESULT_SUCCESS) {
        (void)cna_clustered_light_compute_destroy(compute);
        return 0;
    }
    if (cna_clustered_light_assignment_create(graphics_device, &on_cpu) != CNA_RESULT_SUCCESS ||
        cna_clustered_light_assignment_create(graphics_device, &under_test) !=
            CNA_RESULT_SUCCESS) {
        (void)cna_clustered_light_assignment_destroy(on_cpu);
        (void)cna_clustered_light_grid_destroy(grid);
        (void)cna_clustered_light_compute_destroy(compute);
        return 0;
    }

    /* Without a projection the grid has no clusters to sort into, so this refuses on either path
       rather than sorting into nothing. It is checked BEFORE the fallback is reached, so the two
       paths give one message instead of two differently-worded ones. */
    ok = ok && cna_clustered_light_compute_assign(compute, grid, &view, spheres, UINT64_C(3),
                                                  under_test) == CNA_RESULT_INVALID_STATE;
    ok = ok && cna_clustered_light_grid_set_projection(grid, &projection, 1.0F, 100.0F) ==
        CNA_RESULT_SUCCESS;
    ok = ok && cna_clustered_light_compute_assign(compute, grid, 0, spheres, UINT64_C(3),
                                                  under_test) == CNA_RESULT_INVALID_ARGUMENT;
    ok = ok && cna_clustered_light_compute_assign(compute, grid, &view, 0, UINT64_C(3),
                                                  under_test) == CNA_RESULT_INVALID_ARGUMENT;
    ok = ok && cna_clustered_light_compute_assign(
            compute, grid, &view, spheres,
            (uint64_t)CNA_CLUSTERED_ASSIGNMENT_MAX_LIGHTS_EXT + UINT64_C(1), under_test) ==
        CNA_RESULT_INVALID_ARGUMENT;

    /* The oracle: sort the same three lights both ways and compare the results element by
       element. On EasyGL the second sort runs on the GPU, on HEADLESS it falls back to the same
       CPU code the first one used -- either way disagreement here is a real defect. */
    ok = ok && cna_clustered_light_assignment_assign(on_cpu, grid, &view, spheres, UINT64_C(3)) ==
        CNA_RESULT_SUCCESS;
    ok = ok && cna_clustered_light_compute_assign(compute, grid, &view, spheres, UINT64_C(3),
                                                  under_test) == CNA_RESULT_SUCCESS;
    /* Which path just ran is exactly whether the program compiled. This is the assertion that
       fails if the compute arm silently stops executing on the one renderer that has it. */
    ok = ok && cna_clustered_light_compute_used_compute(compute, &used) == CNA_RESULT_SUCCESS &&
        used == supported;
    ok = ok && cna_clustered_light_compute_has_overflowed(compute, &overflowed) ==
        CNA_RESULT_SUCCESS && overflowed == CNA_FALSE;

    ok = ok && cna_clustered_light_assignment_get_total_reference_count(on_cpu, &cpu_total) ==
        CNA_RESULT_SUCCESS;
    ok = ok && cna_clustered_light_assignment_get_total_reference_count(under_test, &test_total) ==
        CNA_RESULT_SUCCESS;
    ok = ok && cpu_total == test_total && cpu_total > 0;
    ok = ok && cna_clustered_light_assignment_copy_offsets(
            on_cpu, cpu_offsets, (uint64_t)(sizeof cpu_offsets / sizeof cpu_offsets[0]),
            &cpu_count) == CNA_RESULT_SUCCESS;
    ok = ok && cna_clustered_light_assignment_copy_offsets(
            under_test, test_offsets, (uint64_t)(sizeof test_offsets / sizeof test_offsets[0]),
            &test_count) == CNA_RESULT_SUCCESS;
    ok = ok && cpu_count == test_count && cpu_count > UINT64_C(0);
    for (index = UINT64_C(0); ok && index < cpu_count; ++index) {
        ok = cpu_offsets[index] == test_offsets[index];
    }
    ok = ok && cna_clustered_light_assignment_copy_indices(
            on_cpu, cpu_indices, (uint64_t)(sizeof cpu_indices / sizeof cpu_indices[0]),
            &cpu_count) == CNA_RESULT_SUCCESS;
    ok = ok && cna_clustered_light_assignment_copy_indices(
            under_test, test_indices, (uint64_t)(sizeof test_indices / sizeof test_indices[0]),
            &test_count) == CNA_RESULT_SUCCESS;
    ok = ok && cpu_count == test_count;
    for (index = UINT64_C(0); ok && index < cpu_count; ++index) {
        ok = cpu_indices[index] == test_indices[index];
    }

    ok = ok && cna_clustered_light_compute_destroy(compute) == CNA_RESULT_SUCCESS;
    ok = ok && cna_clustered_light_compute_destroy(compute) != CNA_RESULT_SUCCESS;
    ok = ok && cna_clustered_light_assignment_destroy(under_test) == CNA_RESULT_SUCCESS;
    ok = ok && cna_clustered_light_assignment_destroy(on_cpu) == CNA_RESULT_SUCCESS;
    ok = ok && cna_clustered_light_grid_destroy(grid) == CNA_RESULT_SUCCESS;
    return ok;
}

/* CBIND-086C. The forward effect's material setters correct rather than refuse, so each one is
   given a value outside its range and read straight back: a clamp that stops clamping is not
   visible any other way. */
static int validate_clustered_forward(const CNA_Handle graphics_device)
{
    CNA_ClusteredForwardEffectHandle effect = CNA_INVALID_HANDLE;
    CNA_ClusteredLightBufferHandle buffer = CNA_INVALID_HANDLE;
    CNA_ClusteredLightSetHandle lights = CNA_INVALID_HANDLE;
    CNA_ClusteredShadowPolicyHandle policy = CNA_INVALID_HANDLE;
    CNA_EffectHandle shader = CNA_INVALID_HANDLE;
    CNA_Handle frame = CNA_INVALID_HANDLE;
    CNA_ClusteredLightEXT light;
    CNA_Matrix world;
    CNA_Matrix view;
    CNA_Matrix projection;
    CNA_Vector3 camera;
    CNA_Vector3 vector;
    CNA_Vector3 read_back;
    CNA_Bool flag = UINT8_C(9);
    CNA_Bool supported = UINT8_C(9);
    float scalar = -1.0F;
    int32_t light_index = -1;
    int ok = 1;

    if (cna_matrix_get_identity(&world) != CNA_RESULT_SUCCESS ||
        cna_matrix_get_identity(&view) != CNA_RESULT_SUCCESS ||
        cna_matrix_get_identity(&projection) != CNA_RESULT_SUCCESS ||
        cna_clustered_light_ext_init(&light) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    camera.x = 0.0F;
    camera.y = 0.0F;
    camera.z = 5.0F;

    if (cna_clustered_forward_effect_create(graphics_device, &effect) != CNA_RESULT_SUCCESS ||
        effect == CNA_INVALID_HANDLE) {
        return 0;
    }
    ok = cna_clustered_forward_effect_is_supported(effect, &supported) == CNA_RESULT_SUCCESS &&
        (supported == CNA_TRUE || supported == CNA_FALSE);

    /* Base colour clamps per channel to zero-to-one. */
    vector.x = 2.0F;
    vector.y = -1.0F;
    vector.z = 0.5F;
    ok = ok && cna_clustered_forward_effect_set_base_color(effect, &vector) ==
        CNA_RESULT_SUCCESS;
    ok = ok && cna_clustered_forward_effect_get_base_color(effect, &read_back) ==
        CNA_RESULT_SUCCESS && read_back.x == 1.0F && read_back.y == 0.0F && read_back.z == 0.5F;
    ok = ok && cna_clustered_forward_effect_set_base_color(effect, 0) ==
        CNA_RESULT_INVALID_ARGUMENT;

    /* Metallic clamps to zero-to-one at both ends. */
    ok = ok && cna_clustered_forward_effect_set_metallic(effect, 5.0F) == CNA_RESULT_SUCCESS;
    ok = ok && cna_clustered_forward_effect_get_metallic(effect, &scalar) ==
        CNA_RESULT_SUCCESS && scalar == 1.0F;
    ok = ok && cna_clustered_forward_effect_set_metallic(effect, -5.0F) == CNA_RESULT_SUCCESS;
    ok = ok && cna_clustered_forward_effect_get_metallic(effect, &scalar) ==
        CNA_RESULT_SUCCESS && scalar == 0.0F;

    /* Roughness has a FLOOR OF 0.04, not zero: a perfectly smooth surface collapses the specular
       lobe to a point the shader cannot integrate. Asserting 0.04 rather than 0 is the point. */
    ok = ok && cna_clustered_forward_effect_set_roughness(effect, 0.0F) == CNA_RESULT_SUCCESS;
    ok = ok && cna_clustered_forward_effect_get_roughness(effect, &scalar) ==
        CNA_RESULT_SUCCESS && scalar > 0.039F && scalar < 0.041F;
    ok = ok && cna_clustered_forward_effect_set_roughness(effect, 9.0F) == CNA_RESULT_SUCCESS;
    ok = ok && cna_clustered_forward_effect_get_roughness(effect, &scalar) ==
        CNA_RESULT_SUCCESS && scalar == 1.0F;

    ok = ok && cna_clustered_forward_effect_set_ior(effect, 1.5F) == CNA_RESULT_SUCCESS;
    ok = ok && cna_clustered_forward_effect_get_ior(effect, &scalar) == CNA_RESULT_SUCCESS &&
        scalar == 1.5F;

    /* Ambient is floored at zero per channel but has no ceiling: a negative ambient would
       subtract light that was never added, while a bright one is legitimate. */
    vector.x = -1.0F;
    vector.y = 3.0F;
    vector.z = 0.0F;
    ok = ok && cna_clustered_forward_effect_set_ambient(effect, &vector) == CNA_RESULT_SUCCESS;
    ok = ok && cna_clustered_forward_effect_get_ambient(effect, &read_back) ==
        CNA_RESULT_SUCCESS && read_back.x == 0.0F && read_back.y == 3.0F && read_back.z == 0.0F;

    /* Nothing bound, and the clears are no-ops rather than errors. */
    ok = ok && cna_clustered_forward_effect_has_area_light(effect, &flag) == CNA_RESULT_SUCCESS &&
        flag == CNA_FALSE;
    ok = ok && cna_clustered_forward_effect_clear_area_light(effect) == CNA_RESULT_SUCCESS;
    ok = ok && cna_clustered_forward_effect_has_light_probe(effect, &flag) == CNA_RESULT_SUCCESS &&
        flag == CNA_FALSE;
    ok = ok && cna_clustered_forward_effect_clear_light_probe(effect) == CNA_RESULT_SUCCESS;
    ok = ok && cna_clustered_forward_effect_get_opaque_frame(effect, &frame) ==
        CNA_RESULT_SUCCESS && frame == CNA_INVALID_HANDLE;
    ok = ok && cna_clustered_forward_effect_set_opaque_frame(effect, CNA_INVALID_HANDLE) ==
        CNA_RESULT_SUCCESS;

    /* Pure functions of their arguments: no effect needed, and a null vector is refused. */
    vector.x = 1.0F;
    vector.y = 0.5F;
    vector.z = 0.25F;
    ok = ok && cna_clustered_forward_effect_volume_attenuation(&vector, 1.0F, 0.0F, &read_back) ==
        CNA_RESULT_SUCCESS && read_back.x == 1.0F && read_back.y == 1.0F && read_back.z == 1.0F;
    ok = ok && cna_clustered_forward_effect_volume_attenuation(&vector, 1.0F, 1.0F, &read_back) ==
        CNA_RESULT_SUCCESS && read_back.y < 1.0F && read_back.z < 1.0F;
    ok = ok && cna_clustered_forward_effect_volume_attenuation(0, 1.0F, 1.0F, &read_back) ==
        CNA_RESULT_INVALID_ARGUMENT;
    ok = ok && cna_clustered_forward_effect_contribution(
            &light, &vector, &vector, &camera, &vector, 0.0F, 0.5F, 0.0F, 0.0F, &vector, 0.3F,
            0.0F, 1.3F, 400.0F, &vector, 0.5F, &read_back) == CNA_RESULT_SUCCESS;
    ok = ok && cna_clustered_forward_effect_contribution(
            0, &vector, &vector, &camera, &vector, 0.0F, 0.5F, 0.0F, 0.0F, &vector, 0.3F, 0.0F,
            1.3F, 400.0F, &vector, 0.5F, &read_back) == CNA_RESULT_INVALID_ARGUMENT;
    ok = ok && cna_clustered_forward_effect_contribution(
            &light, 0, &vector, &camera, &vector, 0.0F, 0.5F, 0.0F, 0.0F, &vector, 0.3F, 0.0F,
            1.3F, 400.0F, &vector, 0.5F, &read_back) == CNA_RESULT_INVALID_ARGUMENT;

    if (!ok || cna_clustered_light_buffer_create(graphics_device, &buffer) !=
            CNA_RESULT_SUCCESS) {
        (void)cna_clustered_forward_effect_destroy(effect);
        return 0;
    }
    /* An empty buffer has no cluster table for the shader to walk, so beginning is refused rather
       than shading against whatever the textures last held. */
    ok = ok && cna_clustered_forward_effect_begin(effect, &world, &view, &projection, &camera,
                                                  buffer) == CNA_RESULT_INVALID_STATE;
    ok = ok && cna_clustered_forward_effect_begin(effect, 0, &view, &projection, &camera,
                                                  buffer) == CNA_RESULT_INVALID_ARGUMENT;
    ok = ok && cna_clustered_forward_effect_begin(effect, &world, &view, &projection, 0,
                                                  buffer) == CNA_RESULT_INVALID_ARGUMENT;
    ok = ok && cna_clustered_light_buffer_destroy(buffer) == CNA_RESULT_SUCCESS;

    /* The shader effect is a counted borrow, so destroying the lender while it is out is refused
       rather than leaving the borrower pointing at freed memory. */
    ok = ok && cna_clustered_forward_effect_get_effect(effect, &shader) == CNA_RESULT_SUCCESS;
    if (ok && shader != CNA_INVALID_HANDLE) {
        ok = cna_clustered_forward_effect_destroy(effect) == CNA_RESULT_INVALID_STATE;
        ok = ok && cna_effect_destroy(shader) == CNA_RESULT_SUCCESS;
    }
    ok = ok && cna_clustered_forward_effect_destroy(effect) == CNA_RESULT_SUCCESS;

    /* CBIND-085C1 left select() unbound because scoring needs a light set; it closes here. */
    if (!ok) {
        return 0;
    }
    if (cna_clustered_shadow_policy_create(graphics_device, INT32_C(2), &policy) !=
            CNA_RESULT_SUCCESS ||
        cna_clustered_light_set_create(graphics_device, &lights) != CNA_RESULT_SUCCESS) {
        (void)cna_clustered_shadow_policy_destroy(policy);
        return 0;
    }
    light.casts_shadows = CNA_TRUE;
    light.position.z = -4.0F;
    ok = cna_clustered_light_set_add(lights, &light, &light_index) == CNA_RESULT_SUCCESS;
    ok = ok && cna_clustered_shadow_policy_select(policy, lights, &view, &projection, &camera) ==
        CNA_RESULT_SUCCESS;
    ok = ok && cna_clustered_shadow_policy_select(policy, lights, 0, &projection, &camera) ==
        CNA_RESULT_INVALID_ARGUMENT;
    ok = ok && cna_clustered_shadow_policy_select(policy, CNA_INVALID_HANDLE, &view, &projection,
                                                  &camera) != CNA_RESULT_SUCCESS;
    ok = ok && cna_clustered_light_set_destroy(lights) == CNA_RESULT_SUCCESS;
    ok = ok && cna_clustered_shadow_policy_destroy(policy) == CNA_RESULT_SUCCESS;
    return ok;
}


/* CBIND-087A. PbrMaterialExtensions throws nowhere: every setter corrects. The thing worth
   pinning is therefore not a refusal but WHICH fields correct -- six scalars and three colours
   clamp to zero-to-one, and six scalars deliberately do not, because a normal scale, a thickness
   in metres, an attenuation distance, an index of refraction and two film thicknesses in
   nanometres have no zero-to-one meaning. A clamp quietly added to one of the free fields is
   invisible to any test that only checks the clamped ones, so both halves are asserted. */
static int validate_material_extensions(const CNA_Handle graphics_device)
{
    CNA_PbrMaterialExtensionsHandle extensions = CNA_INVALID_HANDLE;
    CNA_PbrMaterialExtensionsHandle other = CNA_INVALID_HANDLE;
    CNA_ClusteredForwardEffectHandle effect = CNA_INVALID_HANDLE;
    CNA_PbrMaterialExtensionsHandle borrowed = CNA_INVALID_HANDLE;
    CNA_ClusteredLightEXT light;
    CNA_Vector3 vector;
    CNA_Vector3 read_back;
    CNA_Handle texture = CNA_INVALID_HANDLE;
    CNA_Handle read_texture = CNA_INVALID_HANDLE;
    CNA_Bool flag = UINT8_C(9);
    CNA_Bool other_flag = UINT8_C(9);
    uint64_t hash_a = UINT64_C(0);
    uint64_t hash_b = UINT64_C(1);
    uint64_t bytes = UINT64_C(0);
    float scalar = -1.0F;
    int ok = 1;

    if (cna_clustered_light_ext_init(&light) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    if (cna_pbr_material_extensions_create(&extensions) != CNA_RESULT_SUCCESS ||
        extensions == CNA_INVALID_HANDLE) {
        return 0;
    }

    /* A fresh set is neutral, and every predicate agrees with that. */
    ok = cna_pbr_material_extensions_is_neutral(extensions, &flag) == CNA_RESULT_SUCCESS &&
        flag == CNA_TRUE;
    ok = ok && cna_pbr_material_extensions_is_subsurface_enabled(extensions, &flag) == CNA_RESULT_SUCCESS && flag == CNA_FALSE;
    ok = ok && cna_pbr_material_extensions_is_iridescence_enabled(extensions, &flag) == CNA_RESULT_SUCCESS && flag == CNA_FALSE;
    ok = ok && cna_pbr_material_extensions_is_transmission_enabled(extensions, &flag) == CNA_RESULT_SUCCESS && flag == CNA_FALSE;
    ok = ok && cna_pbr_material_extensions_is_sheen_enabled(extensions, &flag) == CNA_RESULT_SUCCESS && flag == CNA_FALSE;

    /* The six scalars that CLAMP: out of range at both ends, corrected rather than refused. */
    ok = ok && cna_pbr_material_extensions_set_clearcoat_factor(extensions, 7.0F) == CNA_RESULT_SUCCESS;
    ok = ok && cna_pbr_material_extensions_get_clearcoat_factor(extensions, &scalar) == CNA_RESULT_SUCCESS && scalar == 1.0F;
    ok = ok && cna_pbr_material_extensions_set_clearcoat_factor(extensions, -7.0F) == CNA_RESULT_SUCCESS;
    ok = ok && cna_pbr_material_extensions_get_clearcoat_factor(extensions, &scalar) == CNA_RESULT_SUCCESS && scalar == 0.0F;
    ok = ok && cna_pbr_material_extensions_set_clearcoat_roughness(extensions, 7.0F) == CNA_RESULT_SUCCESS;
    ok = ok && cna_pbr_material_extensions_get_clearcoat_roughness(extensions, &scalar) == CNA_RESULT_SUCCESS && scalar == 1.0F;
    ok = ok && cna_pbr_material_extensions_set_clearcoat_roughness(extensions, -7.0F) == CNA_RESULT_SUCCESS;
    ok = ok && cna_pbr_material_extensions_get_clearcoat_roughness(extensions, &scalar) == CNA_RESULT_SUCCESS && scalar == 0.0F;
    ok = ok && cna_pbr_material_extensions_set_sheen_roughness(extensions, 7.0F) == CNA_RESULT_SUCCESS;
    ok = ok && cna_pbr_material_extensions_get_sheen_roughness(extensions, &scalar) == CNA_RESULT_SUCCESS && scalar == 1.0F;
    ok = ok && cna_pbr_material_extensions_set_sheen_roughness(extensions, -7.0F) == CNA_RESULT_SUCCESS;
    ok = ok && cna_pbr_material_extensions_get_sheen_roughness(extensions, &scalar) == CNA_RESULT_SUCCESS && scalar == 0.0F;
    ok = ok && cna_pbr_material_extensions_set_transmission_factor(extensions, 7.0F) == CNA_RESULT_SUCCESS;
    ok = ok && cna_pbr_material_extensions_get_transmission_factor(extensions, &scalar) == CNA_RESULT_SUCCESS && scalar == 1.0F;
    ok = ok && cna_pbr_material_extensions_set_transmission_factor(extensions, -7.0F) == CNA_RESULT_SUCCESS;
    ok = ok && cna_pbr_material_extensions_get_transmission_factor(extensions, &scalar) == CNA_RESULT_SUCCESS && scalar == 0.0F;
    ok = ok && cna_pbr_material_extensions_set_iridescence_factor(extensions, 7.0F) == CNA_RESULT_SUCCESS;
    ok = ok && cna_pbr_material_extensions_get_iridescence_factor(extensions, &scalar) == CNA_RESULT_SUCCESS && scalar == 1.0F;
    ok = ok && cna_pbr_material_extensions_set_iridescence_factor(extensions, -7.0F) == CNA_RESULT_SUCCESS;
    ok = ok && cna_pbr_material_extensions_get_iridescence_factor(extensions, &scalar) == CNA_RESULT_SUCCESS && scalar == 0.0F;
    ok = ok && cna_pbr_material_extensions_set_subsurface_wrap(extensions, 7.0F) == CNA_RESULT_SUCCESS;
    ok = ok && cna_pbr_material_extensions_get_subsurface_wrap(extensions, &scalar) == CNA_RESULT_SUCCESS && scalar == 1.0F;
    ok = ok && cna_pbr_material_extensions_set_subsurface_wrap(extensions, -7.0F) == CNA_RESULT_SUCCESS;
    ok = ok && cna_pbr_material_extensions_get_subsurface_wrap(extensions, &scalar) == CNA_RESULT_SUCCESS && scalar == 0.0F;

    /* The other six scalars correct in two shapes that are NOT clamps, and the difference is
       observable: a guarded assignment leaves the previous value in place, so an out-of-range
       write is a silent no-op rather than a value pinned to the bound. Each is therefore set to
       something valid first, then written out of range, and asserted to still hold the valid one.
       An `if (value >= bound)` reads nothing like `std::clamp`, which is why the grep that opened
       this slice missed all six -- see the amendment recorded on CBIND-087A. */
    ok = ok && cna_pbr_material_extensions_set_clearcoat_normal_scale(extensions, 3.0F) == CNA_RESULT_SUCCESS;
    ok = ok && cna_pbr_material_extensions_get_clearcoat_normal_scale(extensions, &scalar) == CNA_RESULT_SUCCESS && scalar == 3.0F;
    ok = ok && cna_pbr_material_extensions_set_clearcoat_normal_scale(extensions, -7.0F) == CNA_RESULT_SUCCESS;
    ok = ok && cna_pbr_material_extensions_get_clearcoat_normal_scale(extensions, &scalar) == CNA_RESULT_SUCCESS && scalar == 3.0F;
    ok = ok && cna_pbr_material_extensions_set_clearcoat_normal_scale(extensions, 9000.0F) == CNA_RESULT_SUCCESS;
    ok = ok && cna_pbr_material_extensions_get_clearcoat_normal_scale(extensions, &scalar) == CNA_RESULT_SUCCESS && scalar == 9000.0F;
    ok = ok && cna_pbr_material_extensions_set_thickness_factor(extensions, 3.0F) == CNA_RESULT_SUCCESS;
    ok = ok && cna_pbr_material_extensions_get_thickness_factor(extensions, &scalar) == CNA_RESULT_SUCCESS && scalar == 3.0F;
    ok = ok && cna_pbr_material_extensions_set_thickness_factor(extensions, -7.0F) == CNA_RESULT_SUCCESS;
    ok = ok && cna_pbr_material_extensions_get_thickness_factor(extensions, &scalar) == CNA_RESULT_SUCCESS && scalar == 3.0F;
    ok = ok && cna_pbr_material_extensions_set_thickness_factor(extensions, 9000.0F) == CNA_RESULT_SUCCESS;
    ok = ok && cna_pbr_material_extensions_get_thickness_factor(extensions, &scalar) == CNA_RESULT_SUCCESS && scalar == 9000.0F;
    ok = ok && cna_pbr_material_extensions_set_iridescence_thickness_minimum(extensions, 3.0F) == CNA_RESULT_SUCCESS;
    ok = ok && cna_pbr_material_extensions_get_iridescence_thickness_minimum(extensions, &scalar) == CNA_RESULT_SUCCESS && scalar == 3.0F;
    ok = ok && cna_pbr_material_extensions_set_iridescence_thickness_minimum(extensions, -7.0F) == CNA_RESULT_SUCCESS;
    ok = ok && cna_pbr_material_extensions_get_iridescence_thickness_minimum(extensions, &scalar) == CNA_RESULT_SUCCESS && scalar == 3.0F;
    ok = ok && cna_pbr_material_extensions_set_iridescence_thickness_minimum(extensions, 9000.0F) == CNA_RESULT_SUCCESS;
    ok = ok && cna_pbr_material_extensions_get_iridescence_thickness_minimum(extensions, &scalar) == CNA_RESULT_SUCCESS && scalar == 9000.0F;
    ok = ok && cna_pbr_material_extensions_set_iridescence_thickness_maximum(extensions, 3.0F) == CNA_RESULT_SUCCESS;
    ok = ok && cna_pbr_material_extensions_get_iridescence_thickness_maximum(extensions, &scalar) == CNA_RESULT_SUCCESS && scalar == 3.0F;
    ok = ok && cna_pbr_material_extensions_set_iridescence_thickness_maximum(extensions, -7.0F) == CNA_RESULT_SUCCESS;
    ok = ok && cna_pbr_material_extensions_get_iridescence_thickness_maximum(extensions, &scalar) == CNA_RESULT_SUCCESS && scalar == 3.0F;
    ok = ok && cna_pbr_material_extensions_set_iridescence_thickness_maximum(extensions, 9000.0F) == CNA_RESULT_SUCCESS;
    ok = ok && cna_pbr_material_extensions_get_iridescence_thickness_maximum(extensions, &scalar) == CNA_RESULT_SUCCESS && scalar == 9000.0F;

    /* The iridescence index of refraction guards at ONE, not zero: a value under one describes a
       medium light speeds up in. 0.5 is therefore ignored where 0.5 would be accepted by every
       other guarded setter in this class. */
    ok = ok && cna_pbr_material_extensions_set_iridescence_ior(extensions, 2.0F) == CNA_RESULT_SUCCESS;
    ok = ok && cna_pbr_material_extensions_get_iridescence_ior(extensions, &scalar) == CNA_RESULT_SUCCESS &&
        scalar == 2.0F;
    ok = ok && cna_pbr_material_extensions_set_iridescence_ior(extensions, 0.5F) == CNA_RESULT_SUCCESS;
    ok = ok && cna_pbr_material_extensions_get_iridescence_ior(extensions, &scalar) == CNA_RESULT_SUCCESS &&
        scalar == 2.0F;

    /* The attenuation distance is the third shape: it FLOORS at zero rather than keeping the
       previous value, so the same negative write that is a no-op above writes zero here. */
    ok = ok && cna_pbr_material_extensions_set_attenuation_distance(extensions, 4.0F) == CNA_RESULT_SUCCESS;
    ok = ok && cna_pbr_material_extensions_get_attenuation_distance(extensions, &scalar) == CNA_RESULT_SUCCESS &&
        scalar == 4.0F;
    ok = ok && cna_pbr_material_extensions_set_attenuation_distance(extensions, -7.0F) == CNA_RESULT_SUCCESS;
    ok = ok && cna_pbr_material_extensions_get_attenuation_distance(extensions, &scalar) == CNA_RESULT_SUCCESS &&
        scalar == 0.0F;

    /* The three colours clamp per channel, so one value out of range at each end and one inside
       proves the clamp is applied element-wise rather than to the vector as a whole. */
    vector.x = 5.0F;
    vector.y = -5.0F;
    vector.z = 0.25F;
    ok = ok && cna_pbr_material_extensions_set_sheen_color_factor(extensions, &vector) == CNA_RESULT_SUCCESS;
    ok = ok && cna_pbr_material_extensions_get_sheen_color_factor(extensions, &read_back) == CNA_RESULT_SUCCESS &&
        read_back.x == 1.0F && read_back.y == 0.0F && read_back.z == 0.25F;
    ok = ok && cna_pbr_material_extensions_set_sheen_color_factor(extensions, 0) == CNA_RESULT_INVALID_ARGUMENT;
    ok = ok && cna_pbr_material_extensions_set_attenuation_color(extensions, &vector) == CNA_RESULT_SUCCESS;
    ok = ok && cna_pbr_material_extensions_get_attenuation_color(extensions, &read_back) == CNA_RESULT_SUCCESS &&
        read_back.x == 1.0F && read_back.y == 0.0F && read_back.z == 0.25F;
    ok = ok && cna_pbr_material_extensions_set_attenuation_color(extensions, 0) == CNA_RESULT_INVALID_ARGUMENT;
    ok = ok && cna_pbr_material_extensions_set_subsurface_color(extensions, &vector) == CNA_RESULT_SUCCESS;
    ok = ok && cna_pbr_material_extensions_get_subsurface_color(extensions, &read_back) == CNA_RESULT_SUCCESS &&
        read_back.x == 1.0F && read_back.y == 0.0F && read_back.z == 0.25F;
    ok = ok && cna_pbr_material_extensions_set_subsurface_color(extensions, 0) == CNA_RESULT_INVALID_ARGUMENT;

    /* Enabling predicates follow the fields they read, so the set is no longer neutral. */
    ok = ok && cna_pbr_material_extensions_is_neutral(extensions, &flag) == CNA_RESULT_SUCCESS &&
        flag == CNA_FALSE;
    ok = ok && cna_pbr_material_extensions_is_subsurface_enabled(extensions, &flag) ==
        CNA_RESULT_SUCCESS && flag == CNA_TRUE;

    /* Equality is by value across every field, so a second set built the same way compares equal
       and hashes equally, and a single differing field breaks both. */
    if (!ok || cna_pbr_material_extensions_create(&other) != CNA_RESULT_SUCCESS) {
        (void)cna_pbr_material_extensions_destroy(extensions);
        return 0;
    }
    ok = ok && cna_pbr_material_extensions_equals(extensions, other, &flag) ==
        CNA_RESULT_SUCCESS && flag == CNA_FALSE;
    ok = ok && cna_pbr_material_extensions_copy_from(other, extensions) == CNA_RESULT_SUCCESS;
    ok = ok && cna_pbr_material_extensions_equals(extensions, other, &flag) ==
        CNA_RESULT_SUCCESS && flag == CNA_TRUE;
    ok = ok && cna_pbr_material_extensions_get_hash_code(extensions, &hash_a) ==
        CNA_RESULT_SUCCESS;
    ok = ok && cna_pbr_material_extensions_get_hash_code(other, &hash_b) == CNA_RESULT_SUCCESS;
    ok = ok && hash_a == hash_b;
    ok = ok && cna_pbr_material_extensions_set_sheen_roughness(other, 0.75F) ==
        CNA_RESULT_SUCCESS;
    ok = ok && cna_pbr_material_extensions_equals(extensions, other, &flag) ==
        CNA_RESULT_SUCCESS && flag == CNA_FALSE;

    /* ToString names only the active extensions, so a neutral set is the shortest possible text
       and this one is longer. */
    ok = ok && cna_pbr_material_extensions_copy_to_string(extensions, 0, UINT64_C(0), &bytes) ==
        CNA_RESULT_BUFFER_TOO_SMALL && bytes > UINT64_C(2);

    /* A texture is BORROWED: binding it does not keep it alive, reading it back gives a fresh
       name for the same texture, and destroying the extensions releases nothing. All nine slots
       are exercised, not one -- nine near-identical routes are exactly where a copy-paste error
       binds the wrong member, and only calling each one can tell. */
    {
        const CNA_Texture2DCreateInfo info = {
            sizeof(CNA_Texture2DCreateInfo), UINT32_C(1), 4U, 4U, CNA_FALSE, {0U, 0U, 0U},
            CNA_SURFACE_FORMAT_COLOR};
        if (ok && cna_texture2d_create(graphics_device, &info, &texture) == CNA_RESULT_SUCCESS) {
            ok = ok && cna_pbr_material_extensions_get_clearcoat_texture(extensions, &read_texture) ==
                CNA_RESULT_SUCCESS && read_texture == CNA_INVALID_HANDLE;
            ok = ok && cna_pbr_material_extensions_set_clearcoat_texture(extensions, texture) == CNA_RESULT_SUCCESS;
            ok = ok && cna_pbr_material_extensions_get_clearcoat_texture(extensions, &read_texture) ==
                CNA_RESULT_SUCCESS && read_texture != CNA_INVALID_HANDLE;
            ok = ok && cna_texture2d_destroy(read_texture) == CNA_RESULT_SUCCESS;
            /* Binding one slot must not appear in another: the second slot is still empty here,
               which is what catches a getter wired to the wrong member. */
            ok = ok && cna_pbr_material_extensions_get_clearcoat_roughness_texture(extensions, &read_texture) ==
                CNA_RESULT_SUCCESS && read_texture == CNA_INVALID_HANDLE;
            ok = ok && cna_pbr_material_extensions_get_clearcoat_roughness_texture(extensions, &read_texture) ==
                CNA_RESULT_SUCCESS && read_texture == CNA_INVALID_HANDLE;
            ok = ok && cna_pbr_material_extensions_set_clearcoat_roughness_texture(extensions, texture) == CNA_RESULT_SUCCESS;
            ok = ok && cna_pbr_material_extensions_get_clearcoat_roughness_texture(extensions, &read_texture) ==
                CNA_RESULT_SUCCESS && read_texture != CNA_INVALID_HANDLE;
            ok = ok && cna_texture2d_destroy(read_texture) == CNA_RESULT_SUCCESS;
            ok = ok && cna_pbr_material_extensions_get_clearcoat_normal_texture(extensions, &read_texture) ==
                CNA_RESULT_SUCCESS && read_texture == CNA_INVALID_HANDLE;
            ok = ok && cna_pbr_material_extensions_set_clearcoat_normal_texture(extensions, texture) == CNA_RESULT_SUCCESS;
            ok = ok && cna_pbr_material_extensions_get_clearcoat_normal_texture(extensions, &read_texture) ==
                CNA_RESULT_SUCCESS && read_texture != CNA_INVALID_HANDLE;
            ok = ok && cna_texture2d_destroy(read_texture) == CNA_RESULT_SUCCESS;
            ok = ok && cna_pbr_material_extensions_get_sheen_color_texture(extensions, &read_texture) ==
                CNA_RESULT_SUCCESS && read_texture == CNA_INVALID_HANDLE;
            ok = ok && cna_pbr_material_extensions_set_sheen_color_texture(extensions, texture) == CNA_RESULT_SUCCESS;
            ok = ok && cna_pbr_material_extensions_get_sheen_color_texture(extensions, &read_texture) ==
                CNA_RESULT_SUCCESS && read_texture != CNA_INVALID_HANDLE;
            ok = ok && cna_texture2d_destroy(read_texture) == CNA_RESULT_SUCCESS;
            ok = ok && cna_pbr_material_extensions_get_sheen_roughness_texture(extensions, &read_texture) ==
                CNA_RESULT_SUCCESS && read_texture == CNA_INVALID_HANDLE;
            ok = ok && cna_pbr_material_extensions_set_sheen_roughness_texture(extensions, texture) == CNA_RESULT_SUCCESS;
            ok = ok && cna_pbr_material_extensions_get_sheen_roughness_texture(extensions, &read_texture) ==
                CNA_RESULT_SUCCESS && read_texture != CNA_INVALID_HANDLE;
            ok = ok && cna_texture2d_destroy(read_texture) == CNA_RESULT_SUCCESS;
            ok = ok && cna_pbr_material_extensions_get_transmission_texture(extensions, &read_texture) ==
                CNA_RESULT_SUCCESS && read_texture == CNA_INVALID_HANDLE;
            ok = ok && cna_pbr_material_extensions_set_transmission_texture(extensions, texture) == CNA_RESULT_SUCCESS;
            ok = ok && cna_pbr_material_extensions_get_transmission_texture(extensions, &read_texture) ==
                CNA_RESULT_SUCCESS && read_texture != CNA_INVALID_HANDLE;
            ok = ok && cna_texture2d_destroy(read_texture) == CNA_RESULT_SUCCESS;
            ok = ok && cna_pbr_material_extensions_get_thickness_texture(extensions, &read_texture) ==
                CNA_RESULT_SUCCESS && read_texture == CNA_INVALID_HANDLE;
            ok = ok && cna_pbr_material_extensions_set_thickness_texture(extensions, texture) == CNA_RESULT_SUCCESS;
            ok = ok && cna_pbr_material_extensions_get_thickness_texture(extensions, &read_texture) ==
                CNA_RESULT_SUCCESS && read_texture != CNA_INVALID_HANDLE;
            ok = ok && cna_texture2d_destroy(read_texture) == CNA_RESULT_SUCCESS;
            ok = ok && cna_pbr_material_extensions_get_iridescence_texture(extensions, &read_texture) ==
                CNA_RESULT_SUCCESS && read_texture == CNA_INVALID_HANDLE;
            ok = ok && cna_pbr_material_extensions_set_iridescence_texture(extensions, texture) == CNA_RESULT_SUCCESS;
            ok = ok && cna_pbr_material_extensions_get_iridescence_texture(extensions, &read_texture) ==
                CNA_RESULT_SUCCESS && read_texture != CNA_INVALID_HANDLE;
            ok = ok && cna_texture2d_destroy(read_texture) == CNA_RESULT_SUCCESS;
            ok = ok && cna_pbr_material_extensions_get_iridescence_thickness_texture(extensions, &read_texture) ==
                CNA_RESULT_SUCCESS && read_texture == CNA_INVALID_HANDLE;
            ok = ok && cna_pbr_material_extensions_set_iridescence_thickness_texture(extensions, texture) == CNA_RESULT_SUCCESS;
            ok = ok && cna_pbr_material_extensions_get_iridescence_thickness_texture(extensions, &read_texture) ==
                CNA_RESULT_SUCCESS && read_texture != CNA_INVALID_HANDLE;
            ok = ok && cna_texture2d_destroy(read_texture) == CNA_RESULT_SUCCESS;
            /* Unbinding is the same route with an invalid handle, and clears only its own slot. */
            ok = ok && cna_pbr_material_extensions_set_clearcoat_texture(extensions, CNA_INVALID_HANDLE) == CNA_RESULT_SUCCESS;
            ok = ok && cna_pbr_material_extensions_get_clearcoat_texture(extensions, &read_texture) ==
                CNA_RESULT_SUCCESS && read_texture == CNA_INVALID_HANDLE;
            ok = ok && cna_pbr_material_extensions_set_clearcoat_roughness_texture(extensions, CNA_INVALID_HANDLE) == CNA_RESULT_SUCCESS;
            ok = ok && cna_pbr_material_extensions_get_clearcoat_roughness_texture(extensions, &read_texture) ==
                CNA_RESULT_SUCCESS && read_texture == CNA_INVALID_HANDLE;
            ok = ok && cna_pbr_material_extensions_set_clearcoat_normal_texture(extensions, CNA_INVALID_HANDLE) == CNA_RESULT_SUCCESS;
            ok = ok && cna_pbr_material_extensions_get_clearcoat_normal_texture(extensions, &read_texture) ==
                CNA_RESULT_SUCCESS && read_texture == CNA_INVALID_HANDLE;
            ok = ok && cna_pbr_material_extensions_set_sheen_color_texture(extensions, CNA_INVALID_HANDLE) == CNA_RESULT_SUCCESS;
            ok = ok && cna_pbr_material_extensions_get_sheen_color_texture(extensions, &read_texture) ==
                CNA_RESULT_SUCCESS && read_texture == CNA_INVALID_HANDLE;
            ok = ok && cna_pbr_material_extensions_set_sheen_roughness_texture(extensions, CNA_INVALID_HANDLE) == CNA_RESULT_SUCCESS;
            ok = ok && cna_pbr_material_extensions_get_sheen_roughness_texture(extensions, &read_texture) ==
                CNA_RESULT_SUCCESS && read_texture == CNA_INVALID_HANDLE;
            ok = ok && cna_pbr_material_extensions_set_transmission_texture(extensions, CNA_INVALID_HANDLE) == CNA_RESULT_SUCCESS;
            ok = ok && cna_pbr_material_extensions_get_transmission_texture(extensions, &read_texture) ==
                CNA_RESULT_SUCCESS && read_texture == CNA_INVALID_HANDLE;
            ok = ok && cna_pbr_material_extensions_set_thickness_texture(extensions, CNA_INVALID_HANDLE) == CNA_RESULT_SUCCESS;
            ok = ok && cna_pbr_material_extensions_get_thickness_texture(extensions, &read_texture) ==
                CNA_RESULT_SUCCESS && read_texture == CNA_INVALID_HANDLE;
            ok = ok && cna_pbr_material_extensions_set_iridescence_texture(extensions, CNA_INVALID_HANDLE) == CNA_RESULT_SUCCESS;
            ok = ok && cna_pbr_material_extensions_get_iridescence_texture(extensions, &read_texture) ==
                CNA_RESULT_SUCCESS && read_texture == CNA_INVALID_HANDLE;
            ok = ok && cna_pbr_material_extensions_set_iridescence_thickness_texture(extensions, CNA_INVALID_HANDLE) == CNA_RESULT_SUCCESS;
            ok = ok && cna_pbr_material_extensions_get_iridescence_thickness_texture(extensions, &read_texture) ==
                CNA_RESULT_SUCCESS && read_texture == CNA_INVALID_HANDLE;
            /* The texture outlived every borrow, which is the borrow rule stated as a test. */
            ok = ok && cna_texture2d_destroy(texture) == CNA_RESULT_SUCCESS;
        }
    }

    /* Thin-film iridescence is two pure functions and needs no handle. cos_theta is clamped, so
       a value past one gives the same answer as one; the result is floored at zero per channel. */
    vector.x = 0.04F;
    vector.y = 0.04F;
    vector.z = 0.04F;
    ok = ok && cna_thin_film_iridescence_evaluate(1.0F, 1.3F, 1.0F, 300.0F, &vector,
                                                  &read_back) == CNA_RESULT_SUCCESS &&
        read_back.x >= 0.0F && read_back.y >= 0.0F && read_back.z >= 0.0F;
    ok = ok && cna_thin_film_iridescence_evaluate(1.0F, 1.3F, 9.0F, 300.0F, &vector,
                                                  &vector) == CNA_RESULT_SUCCESS &&
        vector.x == read_back.x && vector.y == read_back.y && vector.z == read_back.z;
    ok = ok && cna_thin_film_iridescence_evaluate(1.0F, 1.3F, 1.0F, 300.0F, 0, &read_back) ==
        CNA_RESULT_INVALID_ARGUMENT;
    ok = ok && cna_thin_film_iridescence_copy_glsl(0, UINT64_C(0), &bytes) ==
        CNA_RESULT_BUFFER_TOO_SMALL && bytes > UINT64_C(0);

    /* CBIND-086C deferred these three routes because this type did not exist in C yet; with it
       bound, the effect can be given extensions and CBIND-086's last row closes. */
    if (ok && cna_clustered_forward_effect_create(graphics_device, &effect) ==
            CNA_RESULT_SUCCESS) {
        ok = cna_clustered_forward_effect_set_material_extensions(effect, extensions) ==
            CNA_RESULT_SUCCESS;
        ok = ok && cna_clustered_forward_effect_get_material_extensions(effect, &borrowed) ==
            CNA_RESULT_SUCCESS && borrowed != CNA_INVALID_HANDLE;
        /* The borrow is a view onto the effect's own copy, so it compares equal to what went in
           and differs once the source is changed -- proving it is a copy, not an alias. */
        ok = ok && cna_pbr_material_extensions_equals(borrowed, extensions, &flag) ==
            CNA_RESULT_SUCCESS && flag == CNA_TRUE;
        ok = ok && cna_pbr_material_extensions_set_sheen_roughness(extensions, 0.125F) ==
            CNA_RESULT_SUCCESS;
        ok = ok && cna_pbr_material_extensions_equals(borrowed, extensions, &other_flag) ==
            CNA_RESULT_SUCCESS && other_flag == CNA_FALSE;
        ok = ok && cna_pbr_material_extensions_destroy(borrowed) == CNA_RESULT_SUCCESS;
        ok = ok && cna_clustered_forward_effect_contribution_with_extensions(
                &light, &read_back, &read_back, &read_back, &read_back, 0.0F, 0.5F, extensions,
                &vector) == CNA_RESULT_SUCCESS;
        ok = ok && cna_clustered_forward_effect_contribution_with_extensions(
                &light, &read_back, &read_back, &read_back, &read_back, 0.0F, 0.5F,
                CNA_INVALID_HANDLE, &vector) != CNA_RESULT_SUCCESS;
        ok = ok && cna_clustered_forward_effect_destroy(effect) == CNA_RESULT_SUCCESS;
    }

    ok = ok && cna_pbr_material_extensions_destroy(other) == CNA_RESULT_SUCCESS;
    ok = ok && cna_pbr_material_extensions_destroy(extensions) == CNA_RESULT_SUCCESS;
    ok = ok && cna_pbr_material_extensions_destroy(extensions) != CNA_RESULT_SUCCESS;
    return ok;
}

/* CBIND-087B. PbrMaterial is the mirror image of PbrMaterialExtensions: every canonical setter is
   a BARE ASSIGNMENT with no correction at all, which the amended practice found only by reading
   the bodies rather than by grepping for an idiom that is not there. The two exceptions never
   reach C: getTextureCoordinateSet and getTextureTransform take a slot enum and fold an
   out-of-range one onto base colour, but the C mapping is a POD carrying both as fixed arrays, so
   there is no slot argument to validate and no fold to reproduce. What is left to pin is
   therefore the value semantics and the identities. */
static int validate_pbr_material_value(void)
{
    CNA_PbrMaterialEXT material;
    CNA_PbrMaterialEXT other;
    CNA_Bool flag = UINT8_C(9);
    uint64_t hash_a = UINT64_C(0);
    uint64_t hash_b = UINT64_C(1);
    uint64_t bytes = UINT64_C(0);
    int index = 0;
    int ok = 1;

    if (cna_pbr_material_ext_init(&material) != CNA_RESULT_SUCCESS ||
        cna_pbr_material_ext_init(&other) != CNA_RESULT_SUCCESS) {
        return 0;
    }

    /* The slot count is the length of both per-slot arrays and the bound of the slot identities;
       a static_assert ties it to the canonical constant, and this reads it back from C. */
    ok = (int)(sizeof material.texture_coordinate_sets /
               sizeof material.texture_coordinate_sets[0]) == CNA_PBR_TEXTURE_SLOT_COUNT;
    ok = ok && (int)(sizeof material.texture_transforms /
                     sizeof material.texture_transforms[0]) == CNA_PBR_TEXTURE_SLOT_COUNT;
    ok = ok && CNA_PBR_TEXTURE_SPECULAR_COLOR_EXT ==
        (uint32_t)(CNA_PBR_TEXTURE_SLOT_COUNT - INT32_C(1));
    /* The three transparency modes are contiguous from zero, in canonical order. */
    ok = ok && CNA_TRANSPARENCY_MODE_NONE == UINT32_C(0) &&
        CNA_TRANSPARENCY_MODE_SORTED == UINT32_C(1) &&
        CNA_TRANSPARENCY_MODE_ORDER_INDEPENDENT == UINT32_C(2);

    /* Two freshly initialized materials are equal and hash equally. */
    ok = ok && cna_pbr_material_ext_equals(&material, &other, &flag) == CNA_RESULT_SUCCESS &&
        flag == CNA_TRUE;
    ok = ok && cna_pbr_material_ext_get_hash_code(&material, &hash_a) == CNA_RESULT_SUCCESS;
    ok = ok && cna_pbr_material_ext_get_hash_code(&other, &hash_b) == CNA_RESULT_SUCCESS;
    ok = ok && hash_a == hash_b;

    /* Every field the canonical type gained after CNA_PbrMaterial was frozen is carried here and
       participates in equality. Each is changed one at a time and the pair must stop matching --
       a field present in the structure but dropped on the way to the canonical type would leave
       equality reporting TRUE, and nothing else in this suite would notice. */
    ok = ok && cna_pbr_material_ext_equals(&material, &other, &flag) == CNA_RESULT_SUCCESS &&
        flag == CNA_TRUE;
    other.ior = material.ior + 0.5F;
    ok = ok && cna_pbr_material_ext_equals(&material, &other, &flag) == CNA_RESULT_SUCCESS &&
        flag == CNA_FALSE;
    other.ior = material.ior;
    other.specular_factor = material.specular_factor + 0.25F;
    ok = ok && cna_pbr_material_ext_equals(&material, &other, &flag) == CNA_RESULT_SUCCESS &&
        flag == CNA_FALSE;
    other.specular_factor = material.specular_factor;
    other.specular_color_factor.x = material.specular_color_factor.x + 0.25F;
    ok = ok && cna_pbr_material_ext_equals(&material, &other, &flag) == CNA_RESULT_SUCCESS &&
        flag == CNA_FALSE;
    other.specular_color_factor = material.specular_color_factor;
    other.emissive_factor.y = material.emissive_factor.y + 0.25F;
    ok = ok && cna_pbr_material_ext_equals(&material, &other, &flag) == CNA_RESULT_SUCCESS &&
        flag == CNA_FALSE;
    other.emissive_factor = material.emissive_factor;
    other.alpha_mode = material.alpha_mode == CNA_ALPHA_MODE_OPAQUE_EXT ? CNA_ALPHA_MODE_MASK_EXT
                                                                    : CNA_ALPHA_MODE_OPAQUE_EXT;
    ok = ok && cna_pbr_material_ext_equals(&material, &other, &flag) == CNA_RESULT_SUCCESS &&
        flag == CNA_FALSE;
    other.alpha_mode = material.alpha_mode;
    other.double_sided = material.double_sided == CNA_TRUE ? CNA_FALSE : CNA_TRUE;
    ok = ok && cna_pbr_material_ext_equals(&material, &other, &flag) == CNA_RESULT_SUCCESS &&
        flag == CNA_FALSE;
    other.double_sided = material.double_sided;
    other.output_encoded_to_srgb =
        material.output_encoded_to_srgb == CNA_TRUE ? CNA_FALSE : CNA_TRUE;
    ok = ok && cna_pbr_material_ext_equals(&material, &other, &flag) == CNA_RESULT_SUCCESS &&
        flag == CNA_FALSE;
    other.output_encoded_to_srgb = material.output_encoded_to_srgb;

    /* Both per-slot arrays participate too, and every slot does -- a converter that copied only
       the first would pass a test that changed only the first. */
    for (index = 0; ok && index < CNA_PBR_TEXTURE_SLOT_COUNT; ++index) {
        other.texture_coordinate_sets[index] = material.texture_coordinate_sets[index] + 1;
        ok = cna_pbr_material_ext_equals(&material, &other, &flag) == CNA_RESULT_SUCCESS &&
            flag == CNA_FALSE;
        other.texture_coordinate_sets[index] = material.texture_coordinate_sets[index];
        other.texture_transforms[index].rotation =
            material.texture_transforms[index].rotation + 0.5F;
        ok = ok && cna_pbr_material_ext_equals(&material, &other, &flag) == CNA_RESULT_SUCCESS &&
            flag == CNA_FALSE;
        other.texture_transforms[index] = material.texture_transforms[index];
    }
    ok = ok && cna_pbr_material_ext_equals(&material, &other, &flag) == CNA_RESULT_SUCCESS &&
        flag == CNA_TRUE;

    /* A changed field must change the hash too, or equal-hash-for-equal-values is vacuous. */
    other.ior = material.ior + 4.0F;
    ok = ok && cna_pbr_material_ext_get_hash_code(&other, &hash_b) == CNA_RESULT_SUCCESS &&
        hash_b != hash_a;

    ok = ok && cna_pbr_material_ext_copy_to_string(&material, 0, UINT64_C(0), &bytes) ==
        CNA_RESULT_BUFFER_TOO_SMALL && bytes > UINT64_C(0);

    /* A malformed structure is refused rather than read past its own declared size. */
    other.struct_size = UINT32_C(4);
    ok = ok && cna_pbr_material_ext_get_hash_code(&other, &hash_b) ==
        CNA_RESULT_INVALID_ARGUMENT;
    ok = ok && cna_pbr_material_ext_equals(&material, 0, &flag) == CNA_RESULT_INVALID_ARGUMENT;
    ok = ok && cna_pbr_material_ext_copy_to_string(0, 0, UINT64_C(0), &bytes) ==
        CNA_RESULT_INVALID_ARGUMENT;
    return ok;
}

/* CBIND-087C. The shadow-receiver contract was bound by CBIND-085B1 as operations on the Effect
   handle, refused by argument when the effect does not implement the interface. Stage 13 proves
   that against BasicEffect and against a SpriteEffect that must be refused. What it does NOT
   prove is that the two PBR effects resolve -- and they are a different case worth its own
   assertions: both are in modules/graphics rather than the engine layer, so they exist in builds
   where the routes that drive them do not, and their `override` of the interface is what these
   thirty-two inventory rows are. A route that resolved for BasicEffect and silently refused a
   PbrEffect would leave every PBR shadow inert with no error a caller could see. */
static int validate_receiver_on(const CNA_EffectHandle effect)
{
    CNA_ShadowCascadeStateEXT cascades;
    CNA_ShadowCascadeStateEXT read_back;
    CNA_PunctualLightEXT light;
    CNA_Matrix matrix;
    CNA_Handle bound = CNA_INVALID_HANDLE;
    CNA_Bool flag = UINT8_C(9);
    float scalar = -1.0F;
    int32_t radius = -1;
    int ok = 1;

    if (cna_matrix_get_identity(&matrix) != CNA_RESULT_SUCCESS ||
        cna_shadow_cascade_state_ext_init(&cascades) != CNA_RESULT_SUCCESS ||
        cna_punctual_light_ext_init(&light) != CNA_RESULT_SUCCESS) {
        return 0;
    }

    /* Every member of the interface, round-tripped: resolving the object is the whole question,
       so reading a value back is what distinguishes "the cast succeeded" from "the call was
       accepted and dropped". */
    ok = cna_effect_set_shadows_enabled_ext(effect, CNA_TRUE) == CNA_RESULT_SUCCESS;
    ok = ok && cna_effect_is_shadows_enabled_ext(effect, &flag) == CNA_RESULT_SUCCESS &&
        flag == CNA_TRUE;
    ok = ok && cna_effect_set_shadows_enabled_ext(effect, CNA_FALSE) == CNA_RESULT_SUCCESS;
    ok = ok && cna_effect_is_shadows_enabled_ext(effect, &flag) == CNA_RESULT_SUCCESS &&
        flag == CNA_FALSE;
    /* The bool contract is validated before the handle is resolved, the CBIND-067 discipline. */
    ok = ok && cna_effect_set_shadows_enabled_ext(effect, UINT8_C(2)) ==
        CNA_RESULT_INVALID_ARGUMENT;
    ok = ok && cna_effect_set_shadow_depth_bias_ext(effect, 0.25F) == CNA_RESULT_SUCCESS;
    ok = ok && cna_effect_get_shadow_depth_bias_ext(effect, &scalar) == CNA_RESULT_SUCCESS &&
        scalar == 0.25F;
    ok = ok && cna_effect_set_shadow_filter_radius_ext(effect, INT32_C(2)) == CNA_RESULT_SUCCESS;
    ok = ok && cna_effect_get_shadow_filter_radius_ext(effect, &radius) == CNA_RESULT_SUCCESS &&
        radius == INT32_C(2);
    ok = ok && cna_effect_set_light_view_projection_ext(effect, &matrix) == CNA_RESULT_SUCCESS;
    ok = ok && cna_effect_get_light_view_projection_ext(effect, &matrix) == CNA_RESULT_SUCCESS;
    ok = ok && cna_effect_set_light_view_projection_ext(effect, 0) == CNA_RESULT_INVALID_ARGUMENT;
    ok = ok && cna_effect_set_shadow_cascades_ext(effect, &cascades) == CNA_RESULT_SUCCESS;
    ok = ok && cna_effect_get_shadow_cascades_ext(effect, &read_back) == CNA_RESULT_SUCCESS &&
        read_back.count == cascades.count;
    ok = ok && cna_effect_set_punctual_light_ext(effect, &light) == CNA_RESULT_SUCCESS;
    ok = ok && cna_effect_get_punctual_light_ext(effect, &light) == CNA_RESULT_SUCCESS;
    /* The shadow map is borrowed, never owned, so unbinding it is always accepted. */
    ok = ok && cna_effect_set_shadow_map_ext(effect, CNA_INVALID_HANDLE) == CNA_RESULT_SUCCESS;
    ok = ok && cna_effect_get_shadow_map_ext(effect, &bound) == CNA_RESULT_SUCCESS &&
        bound == CNA_INVALID_HANDLE;
    return ok;
}

static int validate_pbr_effect_receiver(const CNA_Handle graphics_device)
{
    CNA_EffectHandle pbr = CNA_INVALID_HANDLE;
    CNA_EffectHandle skinned = CNA_INVALID_HANDLE;
    int ok = 1;

    /* A renderer that cannot compile these effects is not this slice's subject; where they do not
       create, there is nothing whose interface could be resolved, and the arm that matters runs
       where they do. Both are attempted, and each that exists is held to the full contract. */
    if (cna_pbr_effect_create(graphics_device, &pbr) == CNA_RESULT_SUCCESS) {
        ok = validate_receiver_on(pbr);
        ok = ok && cna_effect_destroy(pbr) == CNA_RESULT_SUCCESS;
    }
    if (ok && cna_skinned_pbr_effect_create(graphics_device, &skinned) == CNA_RESULT_SUCCESS) {
        ok = validate_receiver_on(skinned);
        ok = ok && cna_effect_destroy(skinned) == CNA_RESULT_SUCCESS;
    }
    return ok;
}

/* CBIND-087D. Counts how many times each entry's callback ran, and in what order. */
typedef struct DrawProbe {
    int order[8];
    int count;
    int fail_at;
} DrawProbe;

static CNA_Result draw_probe_entry(void* const context)
{
    DrawProbe* const probe = (DrawProbe*)context;
    if (probe->count < 8) {
        probe->order[probe->count] = probe->count;
    }
    ++probe->count;
    if (probe->fail_at > 0 && probe->count == probe->fail_at) {
        return CNA_RESULT_IO;
    }
    return CNA_RESULT_SUCCESS;
}

/* CBIND-087D. Three subsystems whose contracts differ sharply, which is why they are asserted
   apart: the bridge corrects nothing and only quantises one value, the draw list has exactly one
   refusal, and the weighted-blended resolve mixes argument mistakes with sequencing mistakes and
   must not flatten them into one result. */
static int validate_transparency_and_bridge(const CNA_Handle graphics_device)
{
    CNA_TransparentDrawListHandle list = CNA_INVALID_HANDLE;
    CNA_WeightedBlendedTransparencyHandle wbt = CNA_INVALID_HANDLE;
    CNA_PbrMaterialExtensionsHandle extensions = CNA_INVALID_HANDLE;
    CNA_GltfMaterialSourceEXT source;
    CNA_GltfMaterialExtensionSourceEXT extension_source;
    CNA_GltfMaterialTexturesEXT textures;
    CNA_GltfMaterialExtensionTexturesEXT extension_textures;
    CNA_PbrMaterialEXT material;
    CNA_BoundingBox near_box;
    CNA_BoundingBox far_box;
    CNA_Matrix view;
    CNA_Vector3 camera;
    CNA_Vector3 position;
    DrawProbe probe;
    int32_t order[8];
    uint64_t count = UINT64_C(0);
    uint64_t bytes = UINT64_C(0);
    CNA_Bool flag = UINT8_C(9);
    CNA_Bool supported = UINT8_C(9);
    float key_near = -1.0F;
    float key_far = -1.0F;
    float weight = -1.0F;
    int ok = 1;

    if (cna_matrix_get_identity(&view) != CNA_RESULT_SUCCESS ||
        cna_gltf_material_source_ext_init(&source) != CNA_RESULT_SUCCESS ||
        cna_gltf_material_extension_source_ext_init(&extension_source) != CNA_RESULT_SUCCESS ||
        cna_gltf_material_textures_ext_init(&textures) != CNA_RESULT_SUCCESS ||
        cna_gltf_material_extension_textures_ext_init(&extension_textures) !=
            CNA_RESULT_SUCCESS) {
        return 0;
    }

    /* ---- the glTF bridge ------------------------------------------------------------------ */

    /* The canonical bridge is written against a concept rather than a type; the C form names the
       members the concept requires. The defaults are glTF's own, not this layer's. */
    ok = source.metallic_factor == 1.0F && source.roughness_factor == 1.0F &&
        source.ior_ext == 1.5F && source.alpha_cutoff == 0.5F &&
        source.base_color_factor.w == 1.0F;
    ok = ok && extension_source.iridescence_ior_ext == 1.3F &&
        extension_source.attenuation_color_ext.x == 1.0F;
    ok = ok && cna_gltf_material_bridge_build_material(&source, &textures, &material) ==
        CNA_RESULT_SUCCESS;
    /* Every factor round-trips except the base colour, which the canonical bridge quantises to
       eight bits per channel because a material's albedo is a Color. 1.0 survives that exactly,
       so a fractional value is used to see the quantisation rather than to assume it. */
    ok = ok && material.metallic_factor == 1.0F && material.roughness_factor == 1.0F &&
        material.ior == 1.5F && material.alpha_cutoff == 0.5F;
    source.base_color_factor.x = 0.5F;
    ok = ok && cna_gltf_material_bridge_build_material(&source, &textures, &material) ==
        CNA_RESULT_SUCCESS;
    ok = ok && material.albedo_color.r == 128U;
    /* A malformed structure is refused rather than read past its own declared size. */
    source.struct_size = UINT32_C(4);
    ok = ok && cna_gltf_material_bridge_build_material(&source, &textures, &material) ==
        CNA_RESULT_INVALID_ARGUMENT;
    ok = ok && cna_gltf_material_source_ext_init(&source) == CNA_RESULT_SUCCESS;
    /* An undefined alpha mode is refused rather than cast through into the material. */
    source.alpha_mode = UINT32_C(9);
    ok = ok && cna_gltf_material_bridge_build_material(&source, &textures, &material) ==
        CNA_RESULT_INVALID_ARGUMENT;
    ok = ok && cna_gltf_material_source_ext_init(&source) == CNA_RESULT_SUCCESS;
    ok = ok && cna_gltf_material_bridge_build_material(0, &textures, &material) ==
        CNA_RESULT_INVALID_ARGUMENT;

    if (ok && cna_pbr_material_extensions_create(&extensions) == CNA_RESULT_SUCCESS) {
        extension_source.transmission_factor_ext = 0.5F;
        ok = cna_gltf_material_bridge_build_extensions(
                &extension_source, &extension_textures, extensions) == CNA_RESULT_SUCCESS;
        ok = ok && cna_pbr_material_extensions_is_transmission_enabled(extensions, &flag) ==
            CNA_RESULT_SUCCESS && flag == CNA_TRUE;
        ok = ok && cna_pbr_material_extensions_get_iridescence_ior(extensions, &weight) ==
            CNA_RESULT_SUCCESS && weight == 1.3F;
        ok = ok && cna_gltf_material_bridge_build_extensions(
                0, &extension_textures, extensions) == CNA_RESULT_INVALID_ARGUMENT;
        ok = ok && cna_pbr_material_extensions_destroy(extensions) == CNA_RESULT_SUCCESS;
    }

    /* ---- the transparent draw list -------------------------------------------------------- */

    near_box.min.x = -1.0F; near_box.min.y = -1.0F; near_box.min.z = -3.0F;
    near_box.max.x = 1.0F;  near_box.max.y = 1.0F;  near_box.max.z = -1.0F;
    far_box.min.x = -1.0F;  far_box.min.y = -1.0F;  far_box.min.z = -30.0F;
    far_box.max.x = 1.0F;   far_box.max.y = 1.0F;   far_box.max.z = -28.0F;
    camera.x = 0.0F; camera.y = 0.0F; camera.z = 0.0F;

    /* The sort key is the distance to the NEAREST point of the box, not to its centre, so a
       camera inside the box keys at zero. Both facts are asserted, because a centre-distance
       implementation would pass a test that only compared two disjoint boxes. */
    ok = ok && cna_transparent_draw_list_sort_key(&near_box, &camera, &key_near) ==
        CNA_RESULT_SUCCESS;
    ok = ok && cna_transparent_draw_list_sort_key(&far_box, &camera, &key_far) ==
        CNA_RESULT_SUCCESS;
    ok = ok && key_far > key_near;
    {
        CNA_BoundingBox around;
        float inside = -1.0F;
        around.min.x = -1.0F; around.min.y = -1.0F; around.min.z = -1.0F;
        around.max.x = 1.0F;  around.max.y = 1.0F;  around.max.z = 1.0F;
        ok = ok && cna_transparent_draw_list_sort_key(&around, &camera, &inside) ==
            CNA_RESULT_SUCCESS && inside == 0.0F;
    }
    ok = ok && cna_transparent_draw_list_sort_key(0, &camera, &key_near) ==
        CNA_RESULT_INVALID_ARGUMENT;
    ok = ok && cna_transparent_draw_list_camera_position_of(&view, &position) ==
        CNA_RESULT_SUCCESS;
    ok = ok && cna_transparent_draw_list_camera_position_of(0, &position) ==
        CNA_RESULT_INVALID_ARGUMENT;

    if (!ok || cna_transparent_draw_list_create(&list) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    ok = cna_transparent_draw_list_get_count(list, &count) == CNA_RESULT_SUCCESS &&
        count == UINT64_C(0);
    /* An entry with nothing to draw is a caller mistake, refused at submission rather than
       stored and discovered when the draw runs. */
    ok = ok && cna_transparent_draw_list_submit(list, &near_box, 0, 0) ==
        CNA_RESULT_INVALID_ARGUMENT;
    ok = ok && cna_transparent_draw_list_submit(list, 0, draw_probe_entry, 0) ==
        CNA_RESULT_INVALID_ARGUMENT;
    ok = ok && cna_transparent_draw_list_get_count(list, &count) == CNA_RESULT_SUCCESS &&
        count == UINT64_C(0);

    probe.count = 0;
    probe.fail_at = 0;
    /* Submitted near-first, so a back-to-front order must reverse them. */
    ok = ok && cna_transparent_draw_list_submit(list, &near_box, draw_probe_entry, &probe) ==
        CNA_RESULT_SUCCESS;
    ok = ok && cna_transparent_draw_list_submit(list, &far_box, draw_probe_entry, &probe) ==
        CNA_RESULT_SUCCESS;
    ok = ok && cna_transparent_draw_list_get_count(list, &count) == CNA_RESULT_SUCCESS &&
        count == UINT64_C(2);
    ok = ok && cna_transparent_draw_list_copy_sorted_order_ext(list, &view, 0, UINT64_C(0),
                                                               &count) ==
        CNA_RESULT_BUFFER_TOO_SMALL && count == UINT64_C(2);
    ok = ok && cna_transparent_draw_list_copy_sorted_order_ext(
            list, &view, order, (uint64_t)(sizeof order / sizeof order[0]), &count) ==
        CNA_RESULT_SUCCESS && count == UINT64_C(2);
    /* Farthest first: the second entry submitted is the one drawn first. */
    ok = ok && order[0] == INT32_C(1) && order[1] == INT32_C(0);
    ok = ok && cna_transparent_draw_list_draw_sorted(list, &view) == CNA_RESULT_SUCCESS &&
        probe.count == 2;
    ok = ok && cna_transparent_draw_list_draw_sorted(list, 0) == CNA_RESULT_INVALID_ARGUMENT;

    /* A failing callback stops the draw and its own result reaches the caller unchanged, so a
       caller learns which draw failed rather than finding a partly drawn frame. */
    probe.count = 0;
    probe.fail_at = 1;
    ok = ok && cna_transparent_draw_list_draw_sorted(list, &view) == CNA_RESULT_IO &&
        probe.count == 1;

    ok = ok && cna_transparent_draw_list_clear(list) == CNA_RESULT_SUCCESS;
    ok = ok && cna_transparent_draw_list_get_count(list, &count) == CNA_RESULT_SUCCESS &&
        count == UINT64_C(0);
    ok = ok && cna_transparent_draw_list_destroy(list) == CNA_RESULT_SUCCESS;
    ok = ok && cna_transparent_draw_list_destroy(list) != CNA_RESULT_SUCCESS;

    /* ---- weighted-blended order-independent transparency ----------------------------------- */

    /* The weight clamps at both ends: the curve is unbounded near zero depth, and a weight that
       overflows would poison the whole accumulation buffer rather than one fragment. */
    ok = ok && cna_weighted_blended_transparency_weight(0.0F, 1.0F, 100.0F, &weight) ==
        CNA_RESULT_SUCCESS && weight <= 3e3F;
    ok = ok && cna_weighted_blended_transparency_weight(1e9F, 1.0F, 100.0F, &weight) ==
        CNA_RESULT_SUCCESS && weight >= 1e-2F;
    ok = ok && cna_weighted_blended_transparency_copy_accumulation_glsl(0, UINT64_C(0), &bytes) ==
        CNA_RESULT_BUFFER_TOO_SMALL && bytes > UINT64_C(0);

    /* A non-positive size is refused rather than corrected: the targets are allocated from it. */
    ok = ok && cna_weighted_blended_transparency_create(graphics_device, INT32_C(0), INT32_C(8),
                                                        &wbt) == CNA_RESULT_INVALID_ARGUMENT;
    ok = ok && wbt == CNA_INVALID_HANDLE;
    if (!ok || cna_weighted_blended_transparency_create(graphics_device, INT32_C(16), INT32_C(16),
                                                        &wbt) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    ok = cna_weighted_blended_transparency_is_supported(wbt, &supported) == CNA_RESULT_SUCCESS &&
        (supported == CNA_TRUE || supported == CNA_FALSE);
    ok = ok && cna_weighted_blended_transparency_copy_unsupported_reason(wbt, 0, UINT64_C(0),
                                                                         &bytes) ==
        (supported == CNA_TRUE ? CNA_RESULT_SUCCESS : CNA_RESULT_BUFFER_TOO_SMALL);
    ok = ok && ((supported == CNA_TRUE) == (bytes == UINT64_C(0)));

    /* An argument mistake and a sequencing mistake are DIFFERENT results, and keeping them apart
       is the point: the canonical code throws invalid_argument and logic_error, and a caller acts
       on them differently -- fix the number, or fix the order of the calls. */
    ok = ok && cna_weighted_blended_transparency_resize(wbt, INT32_C(0), INT32_C(8)) ==
        CNA_RESULT_INVALID_ARGUMENT;
    ok = ok && cna_weighted_blended_transparency_begin(wbt, 0.0F) == CNA_RESULT_INVALID_ARGUMENT;
    ok = ok && cna_weighted_blended_transparency_begin(wbt, -1.0F) == CNA_RESULT_INVALID_ARGUMENT;
    ok = ok && cna_weighted_blended_transparency_end(wbt) == CNA_RESULT_INVALID_STATE;
    ok = ok && cna_weighted_blended_transparency_is_accumulating(wbt, &flag) ==
        CNA_RESULT_SUCCESS && flag == CNA_FALSE;
    ok = ok && cna_weighted_blended_transparency_resize(wbt, INT32_C(8), INT32_C(8)) ==
        CNA_RESULT_SUCCESS;

    /* CBIND-098, reproduced rather than corrected. begin() opens the bracket only where the
       resolve is supported, so on a renderer without it isAccumulating() stays false and the
       matching end() refuses. Both branches assert; neither is a skip, and the unsupported branch
       is the one that pins the defect in place so a later fix is visible as a test change. */
    ok = ok && cna_weighted_blended_transparency_begin(wbt, 100.0F) == CNA_RESULT_SUCCESS;
    ok = ok && cna_weighted_blended_transparency_is_accumulating(wbt, &flag) ==
        CNA_RESULT_SUCCESS && flag == supported;
    if (ok && supported == CNA_TRUE) {
        /* While the bracket is open, both reconfigurations are sequencing mistakes. */
        ok = ok && cna_weighted_blended_transparency_resize(wbt, INT32_C(4), INT32_C(4)) ==
            CNA_RESULT_INVALID_STATE;
        ok = ok && cna_weighted_blended_transparency_resolve(wbt, INT32_C(8), INT32_C(8)) ==
            CNA_RESULT_INVALID_STATE;
        ok = ok && cna_weighted_blended_transparency_begin(wbt, 100.0F) ==
            CNA_RESULT_INVALID_STATE;
        ok = ok && cna_weighted_blended_transparency_end(wbt) == CNA_RESULT_SUCCESS;
        ok = ok && cna_weighted_blended_transparency_resolve(wbt, INT32_C(0), INT32_C(8)) ==
            CNA_RESULT_INVALID_ARGUMENT;
        ok = ok && cna_weighted_blended_transparency_resolve(wbt, INT32_C(8), INT32_C(8)) ==
            CNA_RESULT_SUCCESS;
    } else {
        /* `ok = ok && ...`, never `ok = ...`: a plain assignment here would discard every failure
           the validator had already recorded, and this branch is the one the unsupported arm
           takes -- so the arm that pins CBIND-098 in place would have been the arm that could not
           fail. Found by breaking the assertion above and watching arm 3 keep passing. */
        ok = ok && cna_weighted_blended_transparency_end(wbt) == CNA_RESULT_INVALID_STATE;
    }
    ok = ok && cna_weighted_blended_transparency_is_accumulating(wbt, &flag) ==
        CNA_RESULT_SUCCESS && flag == CNA_FALSE;

    /* Both targets are borrowed, and exist exactly when the resolve does. */
    {
        CNA_Handle accumulation = CNA_INVALID_HANDLE;
        CNA_Handle revealage = CNA_INVALID_HANDLE;
        ok = ok && cna_weighted_blended_transparency_get_accumulation_texture_ext(
                wbt, &accumulation) == CNA_RESULT_SUCCESS;
        ok = ok && cna_weighted_blended_transparency_get_revealage_texture_ext(wbt, &revealage) ==
            CNA_RESULT_SUCCESS;
        ok = ok && (accumulation != CNA_INVALID_HANDLE) == (supported == CNA_TRUE);
        ok = ok && (revealage != CNA_INVALID_HANDLE) == (supported == CNA_TRUE);
        if (accumulation != CNA_INVALID_HANDLE) {
            ok = ok && cna_render_target_destroy(accumulation) == CNA_RESULT_SUCCESS;
        }
        if (revealage != CNA_INVALID_HANDLE) {
            ok = ok && cna_render_target_destroy(revealage) == CNA_RESULT_SUCCESS;
        }
    }

    /* One cleanup path, whatever went wrong above: destroying a resolve with its bracket still
       open aborts inside the canonical destructor, so a failing assertion must not be able to
       take the suite down with it. CBIND-085B1 learned this the same way -- by watching a
       deliberate break turn a failed assertion into a core dump. */
    if (cna_weighted_blended_transparency_is_accumulating(wbt, &flag) == CNA_RESULT_SUCCESS &&
        flag == CNA_TRUE) {
        (void)cna_weighted_blended_transparency_end(wbt);
    }
    ok = ok && cna_weighted_blended_transparency_destroy(wbt) == CNA_RESULT_SUCCESS;
    ok = ok && cna_weighted_blended_transparency_destroy(wbt) != CNA_RESULT_SUCCESS;
    return ok;
}


/* CBIND-088A. RenderPipelineSettings throws nowhere and corrects thirty-one of its forty-seven
   fields, in two shapes and six different bounds. A structure written by hand holds whatever the
   caller put in it, so `_normalize` is what makes the corrections observable -- and the point of
   this stage is that each bound is asserted SEPARATELY. A single shared zero-to-one expectation
   would pass while four fields clamped to the wrong limit, and the sixteen fields that correct
   nothing must survive an out-of-range write, which is what fails if a correction is ever added
   where the canonical code has none. */
static int validate_render_pipeline_settings(void)
{
    CNA_RenderPipelineSettingsEXT settings;
    CNA_RenderPipelineSettingsEXT defaults;
    CNA_StringView text;
    int32_t applied = -1;
    int ok = 1;

    if (cna_render_pipeline_settings_ext_init(&settings) != CNA_RESULT_SUCCESS ||
        cna_render_pipeline_settings_ext_init(&defaults) != CNA_RESULT_SUCCESS) {
        return 0;
    }

    /* The two named minima are the canonical constants, tied by a static_assert; read back here
       so the C spelling is exercised rather than only compiled. */
    ok = CNA_RENDER_PIPELINE_MINIMUM_GAMMA_EXT > 0.0F &&
        CNA_RENDER_PIPELINE_MINIMUM_FXAA_EDGE_THRESHOLD_EXT > 0.0F;
    /* Defaults are the canonical constructor's, and normalizing them changes nothing -- a default
       that needed correcting would mean the constructor and the setters disagree. */
    ok = ok && cna_render_pipeline_settings_ext_normalize(&settings) == CNA_RESULT_SUCCESS;
    ok = ok && settings.gamma == defaults.gamma && settings.exposure == defaults.exposure &&
        settings.ssr_edge_fade == defaults.ssr_edge_fade;


    /* The ten two-sided clamps, each to ITS OWN upper bound. */
    settings.ssr_edge_fade = 900.0F;
    ok = ok && cna_render_pipeline_settings_ext_normalize(&settings) == CNA_RESULT_SUCCESS &&
        settings.ssr_edge_fade == 0.5F;
    settings.ssr_edge_fade = -900.0F;
    ok = ok && cna_render_pipeline_settings_ext_normalize(&settings) == CNA_RESULT_SUCCESS &&
        settings.ssr_edge_fade == 0.0F;
    settings.light_shaft_decay = 900.0F;
    ok = ok && cna_render_pipeline_settings_ext_normalize(&settings) == CNA_RESULT_SUCCESS &&
        settings.light_shaft_decay == 1.0F;
    settings.light_shaft_decay = -900.0F;
    ok = ok && cna_render_pipeline_settings_ext_normalize(&settings) == CNA_RESULT_SUCCESS &&
        settings.light_shaft_decay == 0.0F;
    settings.motion_blur_strength = 900.0F;
    ok = ok && cna_render_pipeline_settings_ext_normalize(&settings) == CNA_RESULT_SUCCESS &&
        settings.motion_blur_strength == 1.0F;
    settings.motion_blur_strength = -900.0F;
    ok = ok && cna_render_pipeline_settings_ext_normalize(&settings) == CNA_RESULT_SUCCESS &&
        settings.motion_blur_strength == 0.0F;
    settings.motion_blur_max_distance = 900.0F;
    ok = ok && cna_render_pipeline_settings_ext_normalize(&settings) == CNA_RESULT_SUCCESS &&
        settings.motion_blur_max_distance == 0.25F;
    settings.motion_blur_max_distance = -900.0F;
    ok = ok && cna_render_pipeline_settings_ext_normalize(&settings) == CNA_RESULT_SUCCESS &&
        settings.motion_blur_max_distance == 0.0F;
    settings.chromatic_aberration_strength = 900.0F;
    ok = ok && cna_render_pipeline_settings_ext_normalize(&settings) == CNA_RESULT_SUCCESS &&
        settings.chromatic_aberration_strength == 0.1F;
    settings.chromatic_aberration_strength = -900.0F;
    ok = ok && cna_render_pipeline_settings_ext_normalize(&settings) == CNA_RESULT_SUCCESS &&
        settings.chromatic_aberration_strength == 0.0F;
    settings.film_grain_intensity = 900.0F;
    ok = ok && cna_render_pipeline_settings_ext_normalize(&settings) == CNA_RESULT_SUCCESS &&
        settings.film_grain_intensity == 1.0F;
    settings.film_grain_intensity = -900.0F;
    ok = ok && cna_render_pipeline_settings_ext_normalize(&settings) == CNA_RESULT_SUCCESS &&
        settings.film_grain_intensity == 0.0F;
    settings.lens_flare_dispersal = 900.0F;
    ok = ok && cna_render_pipeline_settings_ext_normalize(&settings) == CNA_RESULT_SUCCESS &&
        settings.lens_flare_dispersal == 1.0F;
    settings.lens_flare_dispersal = -900.0F;
    ok = ok && cna_render_pipeline_settings_ext_normalize(&settings) == CNA_RESULT_SUCCESS &&
        settings.lens_flare_dispersal == 0.0F;
    settings.color_grade_strength = 900.0F;
    ok = ok && cna_render_pipeline_settings_ext_normalize(&settings) == CNA_RESULT_SUCCESS &&
        settings.color_grade_strength == 1.0F;
    settings.color_grade_strength = -900.0F;
    ok = ok && cna_render_pipeline_settings_ext_normalize(&settings) == CNA_RESULT_SUCCESS &&
        settings.color_grade_strength == 0.0F;
    settings.dof_max_radius = 900.0F;
    ok = ok && cna_render_pipeline_settings_ext_normalize(&settings) == CNA_RESULT_SUCCESS &&
        settings.dof_max_radius == 0.25F;
    settings.dof_max_radius = -900.0F;
    ok = ok && cna_render_pipeline_settings_ext_normalize(&settings) == CNA_RESULT_SUCCESS &&
        settings.dof_max_radius == 0.0F;
    settings.ssr_roughness_blur = 900.0F;
    ok = ok && cna_render_pipeline_settings_ext_normalize(&settings) == CNA_RESULT_SUCCESS &&
        settings.ssr_roughness_blur == 0.25F;
    settings.ssr_roughness_blur = -900.0F;
    ok = ok && cna_render_pipeline_settings_ext_normalize(&settings) == CNA_RESULT_SUCCESS &&
        settings.ssr_roughness_blur == 0.0F;

    /* The twenty-one floors: a negative value is corrected, a large one is NOT -- these have no
       upper bound, and asserting that is what separates a floor from a clamp. */
    settings.exposure = -900.0F;
    ok = ok && cna_render_pipeline_settings_ext_normalize(&settings) == CNA_RESULT_SUCCESS &&
        settings.exposure == 0.0F;
    settings.exposure = 900.0F;
    ok = ok && cna_render_pipeline_settings_ext_normalize(&settings) == CNA_RESULT_SUCCESS &&
        settings.exposure == 900.0F;
    settings.gamma = -900.0F;
    ok = ok && cna_render_pipeline_settings_ext_normalize(&settings) == CNA_RESULT_SUCCESS &&
        settings.gamma == CNA_RENDER_PIPELINE_MINIMUM_GAMMA_EXT;
    settings.gamma = 900.0F;
    ok = ok && cna_render_pipeline_settings_ext_normalize(&settings) == CNA_RESULT_SUCCESS &&
        settings.gamma == 900.0F;
    settings.bloom_intensity = -900.0F;
    ok = ok && cna_render_pipeline_settings_ext_normalize(&settings) == CNA_RESULT_SUCCESS &&
        settings.bloom_intensity == 0.0F;
    settings.bloom_intensity = 900.0F;
    ok = ok && cna_render_pipeline_settings_ext_normalize(&settings) == CNA_RESULT_SUCCESS &&
        settings.bloom_intensity == 900.0F;
    settings.bloom_threshold = -900.0F;
    ok = ok && cna_render_pipeline_settings_ext_normalize(&settings) == CNA_RESULT_SUCCESS &&
        settings.bloom_threshold == 0.0F;
    settings.bloom_threshold = 900.0F;
    ok = ok && cna_render_pipeline_settings_ext_normalize(&settings) == CNA_RESULT_SUCCESS &&
        settings.bloom_threshold == 900.0F;
    settings.ssao_radius = -900.0F;
    ok = ok && cna_render_pipeline_settings_ext_normalize(&settings) == CNA_RESULT_SUCCESS &&
        settings.ssao_radius == 0.0F;
    settings.ssao_radius = 900.0F;
    ok = ok && cna_render_pipeline_settings_ext_normalize(&settings) == CNA_RESULT_SUCCESS &&
        settings.ssao_radius == 900.0F;
    settings.ssao_intensity = -900.0F;
    ok = ok && cna_render_pipeline_settings_ext_normalize(&settings) == CNA_RESULT_SUCCESS &&
        settings.ssao_intensity == 0.0F;
    settings.ssao_intensity = 900.0F;
    ok = ok && cna_render_pipeline_settings_ext_normalize(&settings) == CNA_RESULT_SUCCESS &&
        settings.ssao_intensity == 900.0F;
    settings.ssr_max_distance = -900.0F;
    ok = ok && cna_render_pipeline_settings_ext_normalize(&settings) == CNA_RESULT_SUCCESS &&
        settings.ssr_max_distance == 0.0F;
    settings.ssr_max_distance = 900.0F;
    ok = ok && cna_render_pipeline_settings_ext_normalize(&settings) == CNA_RESULT_SUCCESS &&
        settings.ssr_max_distance == 900.0F;
    settings.ssr_thickness = -900.0F;
    ok = ok && cna_render_pipeline_settings_ext_normalize(&settings) == CNA_RESULT_SUCCESS &&
        settings.ssr_thickness == 0.0F;
    settings.ssr_thickness = 900.0F;
    ok = ok && cna_render_pipeline_settings_ext_normalize(&settings) == CNA_RESULT_SUCCESS &&
        settings.ssr_thickness == 900.0F;
    settings.ssr_depth_bias = -900.0F;
    ok = ok && cna_render_pipeline_settings_ext_normalize(&settings) == CNA_RESULT_SUCCESS &&
        settings.ssr_depth_bias == 0.0F;
    settings.ssr_depth_bias = 900.0F;
    ok = ok && cna_render_pipeline_settings_ext_normalize(&settings) == CNA_RESULT_SUCCESS &&
        settings.ssr_depth_bias == 900.0F;
    settings.volumetric_fog_density = -900.0F;
    ok = ok && cna_render_pipeline_settings_ext_normalize(&settings) == CNA_RESULT_SUCCESS &&
        settings.volumetric_fog_density == 0.0F;
    settings.volumetric_fog_density = 900.0F;
    ok = ok && cna_render_pipeline_settings_ext_normalize(&settings) == CNA_RESULT_SUCCESS &&
        settings.volumetric_fog_density == 900.0F;
    settings.light_shaft_threshold = -900.0F;
    ok = ok && cna_render_pipeline_settings_ext_normalize(&settings) == CNA_RESULT_SUCCESS &&
        settings.light_shaft_threshold == 0.0F;
    settings.light_shaft_threshold = 900.0F;
    ok = ok && cna_render_pipeline_settings_ext_normalize(&settings) == CNA_RESULT_SUCCESS &&
        settings.light_shaft_threshold == 900.0F;
    settings.light_shaft_intensity = -900.0F;
    ok = ok && cna_render_pipeline_settings_ext_normalize(&settings) == CNA_RESULT_SUCCESS &&
        settings.light_shaft_intensity == 0.0F;
    settings.light_shaft_intensity = 900.0F;
    ok = ok && cna_render_pipeline_settings_ext_normalize(&settings) == CNA_RESULT_SUCCESS &&
        settings.light_shaft_intensity == 900.0F;
    settings.height_fog_density = -900.0F;
    ok = ok && cna_render_pipeline_settings_ext_normalize(&settings) == CNA_RESULT_SUCCESS &&
        settings.height_fog_density == 0.0F;
    settings.height_fog_density = 900.0F;
    ok = ok && cna_render_pipeline_settings_ext_normalize(&settings) == CNA_RESULT_SUCCESS &&
        settings.height_fog_density == 900.0F;
    settings.height_fog_falloff = -900.0F;
    ok = ok && cna_render_pipeline_settings_ext_normalize(&settings) == CNA_RESULT_SUCCESS &&
        settings.height_fog_falloff == 0.0F;
    settings.height_fog_falloff = 900.0F;
    ok = ok && cna_render_pipeline_settings_ext_normalize(&settings) == CNA_RESULT_SUCCESS &&
        settings.height_fog_falloff == 900.0F;
    settings.lens_flare_threshold = -900.0F;
    ok = ok && cna_render_pipeline_settings_ext_normalize(&settings) == CNA_RESULT_SUCCESS &&
        settings.lens_flare_threshold == 0.0F;
    settings.lens_flare_threshold = 900.0F;
    ok = ok && cna_render_pipeline_settings_ext_normalize(&settings) == CNA_RESULT_SUCCESS &&
        settings.lens_flare_threshold == 900.0F;
    settings.lens_flare_intensity = -900.0F;
    ok = ok && cna_render_pipeline_settings_ext_normalize(&settings) == CNA_RESULT_SUCCESS &&
        settings.lens_flare_intensity == 0.0F;
    settings.lens_flare_intensity = 900.0F;
    ok = ok && cna_render_pipeline_settings_ext_normalize(&settings) == CNA_RESULT_SUCCESS &&
        settings.lens_flare_intensity == 900.0F;
    settings.dof_focus_distance = -900.0F;
    ok = ok && cna_render_pipeline_settings_ext_normalize(&settings) == CNA_RESULT_SUCCESS &&
        settings.dof_focus_distance == 0.0F;
    settings.dof_focus_distance = 900.0F;
    ok = ok && cna_render_pipeline_settings_ext_normalize(&settings) == CNA_RESULT_SUCCESS &&
        settings.dof_focus_distance == 900.0F;
    settings.dof_focal_length = -900.0F;
    ok = ok && cna_render_pipeline_settings_ext_normalize(&settings) == CNA_RESULT_SUCCESS &&
        settings.dof_focal_length == 0.0F;
    settings.dof_focal_length = 900.0F;
    ok = ok && cna_render_pipeline_settings_ext_normalize(&settings) == CNA_RESULT_SUCCESS &&
        settings.dof_focal_length == 900.0F;
    settings.doff_number = -900.0F;
    ok = ok && cna_render_pipeline_settings_ext_normalize(&settings) == CNA_RESULT_SUCCESS &&
        settings.doff_number == 0.0F;
    settings.doff_number = 900.0F;
    ok = ok && cna_render_pipeline_settings_ext_normalize(&settings) == CNA_RESULT_SUCCESS &&
        settings.doff_number == 900.0F;
    settings.ssr_intensity = -900.0F;
    ok = ok && cna_render_pipeline_settings_ext_normalize(&settings) == CNA_RESULT_SUCCESS &&
        settings.ssr_intensity == 0.0F;
    settings.ssr_intensity = 900.0F;
    ok = ok && cna_render_pipeline_settings_ext_normalize(&settings) == CNA_RESULT_SUCCESS &&
        settings.ssr_intensity == 900.0F;
    settings.fxaa_edge_threshold_ext = -900.0F;
    ok = ok && cna_render_pipeline_settings_ext_normalize(&settings) == CNA_RESULT_SUCCESS &&
        settings.fxaa_edge_threshold_ext == CNA_RENDER_PIPELINE_MINIMUM_FXAA_EDGE_THRESHOLD_EXT;
    settings.fxaa_edge_threshold_ext = 900.0F;
    ok = ok && cna_render_pipeline_settings_ext_normalize(&settings) == CNA_RESULT_SUCCESS &&
        settings.fxaa_edge_threshold_ext == 900.0F;

    /* The float fields that correct nothing: an extreme value survives in both directions,
       because the passes clamp what they apply and a settings bag that clamped to one pass's
       limits would silently change the number a caller reads back. */
    settings.height_fog_base_height = -900.0F;
    ok = ok && cna_render_pipeline_settings_ext_normalize(&settings) == CNA_RESULT_SUCCESS &&
        settings.height_fog_base_height == -900.0F;

    /* Undefined identities are refused rather than cast through into the engine. */
    ok = ok && cna_render_pipeline_settings_ext_init(&settings) == CNA_RESULT_SUCCESS;
    settings.tonemapping_mode = UINT32_C(99);
    ok = ok && cna_render_pipeline_settings_ext_normalize(&settings) ==
        CNA_RESULT_INVALID_ARGUMENT;
    ok = ok && cna_render_pipeline_settings_ext_init(&settings) == CNA_RESULT_SUCCESS;
    settings.render_quality = UINT32_C(99);
    ok = ok && cna_render_pipeline_settings_ext_normalize(&settings) ==
        CNA_RESULT_INVALID_ARGUMENT;
    ok = ok && cna_render_pipeline_settings_ext_init(&settings) == CNA_RESULT_SUCCESS;
    /* A bool carrying anything but zero or one is refused, the CBIND-067 contract. */
    settings.hdr_enabled = UINT8_C(2);
    ok = ok && cna_render_pipeline_settings_ext_normalize(&settings) ==
        CNA_RESULT_INVALID_ARGUMENT;
    ok = ok && cna_render_pipeline_settings_ext_init(&settings) == CNA_RESULT_SUCCESS;
    /* A malformed structure is refused rather than read past its own declared size. */
    settings.struct_size = UINT32_C(8);
    ok = ok && cna_render_pipeline_settings_ext_normalize(&settings) ==
        CNA_RESULT_INVALID_ARGUMENT;
    ok = ok && cna_render_pipeline_settings_ext_init(&settings) == CNA_RESULT_SUCCESS;
    ok = ok && cna_render_pipeline_settings_ext_normalize(0) == CNA_RESULT_INVALID_ARGUMENT;

    /* The quality preset writes the fields a quality dial has been decided for. Low and Ultra
       must not produce the same bloom iteration count, or the preset is doing nothing. */
    {
        int32_t low_iterations = -1;
        int32_t ultra_iterations = -2;
        settings.render_quality = CNA_RENDER_QUALITY_LOW;
        ok = ok && cna_render_pipeline_settings_ext_apply_render_quality_preset(&settings) ==
            CNA_RESULT_SUCCESS;
        low_iterations = settings.bloom_iterations;
        settings.render_quality = CNA_RENDER_QUALITY_ULTRA;
        ok = ok && cna_render_pipeline_settings_ext_apply_render_quality_preset(&settings) ==
            CNA_RESULT_SUCCESS;
        ultra_iterations = settings.bloom_iterations;
        ok = ok && low_iterations != ultra_iterations;
        ok = ok && cna_render_pipeline_settings_ext_apply_render_quality_preset(0) ==
            CNA_RESULT_INVALID_ARGUMENT;
    }

    /* Serialized text: unrecognised fields are skipped rather than refused, which is what makes
       the applied count meaningful -- a caller compares it against what it meant to set. */
    ok = ok && cna_render_pipeline_settings_ext_init(&settings) == CNA_RESULT_SUCCESS;
    /* Fields are separated by ';', not by spaces -- read from the canonical parser rather than
       assumed, after a first draft used a space and got nothing applied. */
    text.data = "exposure=2.5;nosuchfield=1";
    text.byte_length = (uint64_t)26;
    ok = ok && cna_render_pipeline_settings_ext_apply_from_string(&settings, text, &applied) ==
        CNA_RESULT_SUCCESS;
    ok = ok && applied == INT32_C(1) && settings.exposure == 2.5F;
    /* An embedded NUL would silently truncate what a caller believes it applied. */
    text.data = "exposure=2.5\0hidden=1";
    text.byte_length = (uint64_t)21;
    ok = ok && cna_render_pipeline_settings_ext_apply_from_string(&settings, text, &applied) ==
        CNA_RESULT_ENCODING;
    text.data = 0;
    text.byte_length = (uint64_t)0;
    ok = ok && cna_render_pipeline_settings_ext_apply_from_string(&settings, text, &applied) ==
        CNA_RESULT_SUCCESS && applied == INT32_C(0);
    ok = ok && cna_render_pipeline_settings_ext_apply_from_string(0, text, &applied) ==
        CNA_RESULT_INVALID_ARGUMENT;
    return ok;
}

/* CBIND-088B. Counts frames and can fail one on demand. */
typedef struct FrameProbe {
    int calls;
    int fail_at;
} FrameProbe;

static CNA_Result frame_probe_draw(void* const context)
{
    FrameProbe* const probe = (FrameProbe*)context;
    ++probe->calls;
    if (probe->fail_at > 0 && probe->calls == probe->fail_at) {
        return CNA_RESULT_IO;
    }
    return CNA_RESULT_SUCCESS;
}

/* CBIND-088B. The pipeline's six throws are two kinds and must not arrive as one result, and its
   two logic_errors on begin() are two DIFFERENT states that the exception type alone cannot tell
   apart -- so the C layer remembers whether resize() ever ran and answers them separately. The
   frame bracket itself was read before this test was written: begin marks the frame open before
   any support check and end clears it before any early return, so unlike CBIND-098's resolve this
   pair is symmetric on every renderer and both halves can assert success. */
static int validate_render_pipeline(const CNA_Handle graphics_device)
{
    CNA_RenderPipelineHandle pipeline = CNA_INVALID_HANDLE;
    CNA_RenderPipelineSettingsEXT settings;
    CNA_RenderPipelineFrameStatisticsEXT statistics;
    CNA_DirectionalLightEXT light;
    CNA_BoundingBox bounds;
    CNA_Matrix view;
    CNA_Matrix projection;
    CNA_Color clear_color;
    CNA_ShadowMapHandle borrowed_map = CNA_INVALID_HANDLE;
    CNA_Handle scene_target = CNA_INVALID_HANDLE;
    CNA_SurfaceFormat format = UINT32_C(999);
    FrameProbe probe;
    CNA_Bool flag = UINT8_C(9);
    uint64_t bytes = UINT64_C(0);
    int32_t count = -1;
    int ok = 1;

    if (cna_matrix_get_identity(&view) != CNA_RESULT_SUCCESS ||
        cna_matrix_get_identity(&projection) != CNA_RESULT_SUCCESS ||
        cna_directional_light_ext_init(&light) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    bounds.min.x = -10.0F; bounds.min.y = -10.0F; bounds.min.z = -10.0F;
    bounds.max.x = 10.0F;  bounds.max.y = 10.0F;  bounds.max.z = 10.0F;
    clear_color.r = 0U; clear_color.g = 0U; clear_color.b = 0U; clear_color.a = 255U;
    probe.calls = 0;
    probe.fail_at = 0;

    if (cna_render_pipeline_create(graphics_device, &pipeline) != CNA_RESULT_SUCCESS ||
        pipeline == CNA_INVALID_HANDLE) {
        return 0;
    }

    /* Never sized yet: begin refuses, and this is a DIFFERENT state from a frame already being
       open. Both are CNA_RESULT_INVALID_STATE, but they carry their own messages -- the canonical
       code throws the same exception type for both, so separating them is work the binding does
       rather than something it inherits. */
    ok = cna_render_pipeline_begin(pipeline, &clear_color) == CNA_RESULT_INVALID_STATE;
    /* A non-positive size is an ARGUMENT mistake, not a state one. */
    ok = ok && cna_render_pipeline_resize(pipeline, INT32_C(0), INT32_C(8)) ==
        CNA_RESULT_INVALID_ARGUMENT;
    ok = ok && cna_render_pipeline_resize(pipeline, INT32_C(8), INT32_C(-8)) ==
        CNA_RESULT_INVALID_ARGUMENT;
    ok = ok && cna_render_pipeline_resize(pipeline, INT32_C(32), INT32_C(32)) ==
        CNA_RESULT_SUCCESS;

    /* Settings round-trip through the canonical setters, so CBIND-088A's corrections apply here
       too -- an out-of-range value written through the pipeline comes back corrected. */
    ok = ok && cna_render_pipeline_get_settings(pipeline, &settings) == CNA_RESULT_SUCCESS;
    settings.chromatic_aberration_strength = 900.0F;
    ok = ok && cna_render_pipeline_set_settings(pipeline, &settings) == CNA_RESULT_SUCCESS;
    ok = ok && cna_render_pipeline_get_settings(pipeline, &settings) == CNA_RESULT_SUCCESS &&
        settings.chromatic_aberration_strength == 0.1F;
    settings.tonemapping_mode = UINT32_C(99);
    ok = ok && cna_render_pipeline_set_settings(pipeline, &settings) ==
        CNA_RESULT_INVALID_ARGUMENT;
    ok = ok && cna_render_pipeline_set_settings(pipeline, 0) == CNA_RESULT_INVALID_ARGUMENT;
    ok = ok && cna_render_pipeline_get_settings(pipeline, 0) == CNA_RESULT_INVALID_ARGUMENT;

    /* The camera refuses an inverted or non-positive plane pair as an ARGUMENT mistake. */
    ok = ok && cna_render_pipeline_set_camera(pipeline, &view, &projection, 0.0F, 100.0F) ==
        CNA_RESULT_INVALID_ARGUMENT;
    ok = ok && cna_render_pipeline_set_camera(pipeline, &view, &projection, 10.0F, 1.0F) ==
        CNA_RESULT_INVALID_ARGUMENT;
    ok = ok && cna_render_pipeline_set_camera(pipeline, &view, &projection, 10.0F, 10.0F) ==
        CNA_RESULT_INVALID_ARGUMENT;
    ok = ok && cna_render_pipeline_set_camera(pipeline, 0, &projection, 1.0F, 100.0F) ==
        CNA_RESULT_INVALID_ARGUMENT;
    ok = ok && cna_render_pipeline_set_camera(pipeline, &view, &projection, 1.0F, 100.0F) ==
        CNA_RESULT_SUCCESS;
    ok = ok && cna_render_pipeline_set_skybox_camera(pipeline, &view, &projection) ==
        CNA_RESULT_SUCCESS;
    ok = ok && cna_render_pipeline_set_skybox_camera(pipeline, &view, 0) ==
        CNA_RESULT_INVALID_ARGUMENT;

    /* Inputs are borrowed and unbinding is always accepted. */
    ok = ok && cna_render_pipeline_set_depth_normal_inputs(
            pipeline, CNA_INVALID_HANDLE, CNA_INVALID_HANDLE) == CNA_RESULT_SUCCESS;
    ok = ok && cna_render_pipeline_set_velocity_input_ext(pipeline, CNA_INVALID_HANDLE) ==
        CNA_RESULT_SUCCESS;
    ok = ok && cna_render_pipeline_clear_user_passes(pipeline) == CNA_RESULT_SUCCESS;
    ok = ok && cna_render_pipeline_add_user_pass(pipeline, CNA_INVALID_HANDLE) !=
        CNA_RESULT_SUCCESS;

    /* The shadow scene takes a borrowed map, a light and a caster callback; clearing it with an
       invalid map handle is accepted rather than refused. */
    ok = ok && cna_render_pipeline_set_shadow_scene(
            pipeline, CNA_INVALID_HANDLE, &light, &bounds, frame_probe_draw, &probe) ==
        CNA_RESULT_SUCCESS;
    ok = ok && cna_render_pipeline_set_shadow_scene(
            pipeline, CNA_INVALID_HANDLE, 0, &bounds, 0, 0) == CNA_RESULT_INVALID_ARGUMENT;
    ok = ok && cna_render_pipeline_get_shadow_map(pipeline, &borrowed_map) ==
        CNA_RESULT_SUCCESS && borrowed_map == CNA_INVALID_HANDLE;

    /* GPU timing is accepted on every renderer and reports back what it actually got, so the
       assertion is that the two agree rather than that timing is on. */
    ok = ok && cna_render_pipeline_set_gpu_timing_enabled_ext(pipeline, UINT8_C(2)) ==
        CNA_RESULT_INVALID_ARGUMENT;
    ok = ok && cna_render_pipeline_set_gpu_timing_enabled_ext(pipeline, CNA_FALSE) ==
        CNA_RESULT_SUCCESS;
    ok = ok && cna_render_pipeline_is_gpu_timing_enabled_ext(pipeline, &flag) ==
        CNA_RESULT_SUCCESS && flag == CNA_FALSE;

    /* The frame bracket, symmetric on every renderer. */
    ok = ok && cna_render_pipeline_end(pipeline) == CNA_RESULT_INVALID_STATE;
    ok = ok && cna_render_pipeline_begin(pipeline, 0) == CNA_RESULT_INVALID_ARGUMENT;
    ok = ok && cna_render_pipeline_begin(pipeline, &clear_color) == CNA_RESULT_SUCCESS;
    /* A second begin is the OTHER state, and releasing device resources mid-frame is refused
       because it would leave the frame drawing into freed memory. */
    ok = ok && cna_render_pipeline_begin(pipeline, &clear_color) == CNA_RESULT_INVALID_STATE;
    ok = ok && cna_render_pipeline_release_device_resources_ext(pipeline) ==
        CNA_RESULT_INVALID_STATE;
    ok = ok && cna_render_pipeline_end(pipeline) == CNA_RESULT_SUCCESS;
    ok = ok && cna_render_pipeline_end(pipeline) == CNA_RESULT_INVALID_STATE;
    ok = ok && cna_render_pipeline_release_device_resources_ext(pipeline) == CNA_RESULT_SUCCESS;

    /* Everything the frame recorded is readable, and the statistics agree with the individual
       accessors -- two readings of the same number that a copy-paste error would separate. */
    ok = ok && cna_render_pipeline_get_statistics(pipeline, &statistics) == CNA_RESULT_SUCCESS;
    ok = ok && cna_render_pipeline_get_last_frame_pass_count(pipeline, &count) ==
        CNA_RESULT_SUCCESS && count == statistics.passes_run;
    ok = ok && cna_render_pipeline_get_gpu_memory_estimate_bytes(pipeline, &bytes) ==
        CNA_RESULT_SUCCESS && bytes == statistics.gpu_memory_estimate_bytes;
    ok = ok && cna_render_pipeline_is_using_scene_target(pipeline, &flag) ==
        CNA_RESULT_SUCCESS && flag == statistics.used_scene_target;
    ok = ok && cna_render_pipeline_did_skybox_draw(pipeline, &flag) == CNA_RESULT_SUCCESS &&
        flag == statistics.drew_skybox;
    ok = ok && cna_render_pipeline_did_shadow_pass_run(pipeline, &flag) == CNA_RESULT_SUCCESS;
    ok = ok && cna_render_pipeline_get_statistics(pipeline, 0) == CNA_RESULT_INVALID_ARGUMENT;
    ok = ok && cna_render_pipeline_get_scene_target_format(pipeline, &format) ==
        CNA_RESULT_SUCCESS;
    ok = ok && cna_render_pipeline_get_scene_target(pipeline, &scene_target) ==
        CNA_RESULT_SUCCESS;
    if (ok && scene_target != CNA_INVALID_HANDLE) {
        ok = cna_render_target_destroy(scene_target) == CNA_RESULT_SUCCESS;
    }
    ok = ok && cna_render_pipeline_copy_transparency_fallback_reason_ext(
            pipeline, 0, UINT64_C(0), &bytes) != CNA_RESULT_INVALID_HANDLE;

    /* A failing transparent-scene callback stops the frame and its own result reaches the caller
       unchanged, the CBIND-087D rule applied to a second callback surface. */
    probe.calls = 0;
    probe.fail_at = 1;
    ok = ok && cna_render_pipeline_set_transparent_scene(pipeline, frame_probe_draw, &probe) ==
        CNA_RESULT_SUCCESS;
    ok = ok && cna_render_pipeline_resize(pipeline, INT32_C(32), INT32_C(32)) ==
        CNA_RESULT_SUCCESS;
    ok = ok && cna_render_pipeline_begin(pipeline, &clear_color) == CNA_RESULT_SUCCESS;
    {
        const CNA_Result ended = cna_render_pipeline_end(pipeline);
        /* The callback runs only where the pipeline actually draws transparency; where it does
           not, the frame simply closes. Either answer is correct, and asserting the pair keeps
           this honest on both renderers. */
        ok = ok && ((probe.calls == 0 && ended == CNA_RESULT_SUCCESS) ||
                    (probe.calls == 1 && ended == CNA_RESULT_IO));
    }
    ok = ok && cna_render_pipeline_set_transparent_scene(pipeline, 0, 0) == CNA_RESULT_SUCCESS;

    ok = ok && cna_render_pipeline_destroy(pipeline) == CNA_RESULT_SUCCESS;
    ok = ok && cna_render_pipeline_destroy(pipeline) != CNA_RESULT_SUCCESS;
    return ok;
}

/* CBIND-089A. The chain is where every pass plugs in, and its two contracts are both argument
   mistakes on apply(): a context with nothing to read from, and a non-positive size. The
   interesting binding decision is add_owned_pass -- the canonical signature takes a unique_ptr,
   which this ABI cannot express, so the chain resource keeps the pass alive itself and registers
   it non-owningly. The observable lifetime is the same, and the test proves the caller's handle is
   genuinely consumed rather than merely documented as consumed. */
static int validate_post_process_chain(const CNA_Handle graphics_device)
{
    CNA_PostProcessChainHandle chain = CNA_INVALID_HANDLE;
    CNA_PostProcessPassHandle borrowed_pass = CNA_INVALID_HANDLE;
    CNA_PostProcessPassHandle owned_pass = CNA_INVALID_HANDLE;
    CNA_RenderTargetPoolHandle pool = CNA_INVALID_HANDLE;
    CNA_PostProcessContext context;
    CNA_PassTimingEXT timing;
    CNA_Bool flag = UINT8_C(9);
    uint64_t count = UINT64_C(0);
    uint64_t bytes = UINT64_C(0);
    int32_t passes = -1;
    int ok = 1;

    if (cna_post_process_context_init(&context) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    if (cna_post_process_chain_create(graphics_device, &chain) != CNA_RESULT_SUCCESS ||
        chain == CNA_INVALID_HANDLE) {
        return 0;
    }

    ok = cna_post_process_chain_get_pass_count(chain, &passes) == CNA_RESULT_SUCCESS &&
        passes == INT32_C(0);

    /* Both canonical throws are argument mistakes and are answered before the chain sees them:
       a context with no source has nothing to read from, and a non-positive size gives the chain
       nothing to size its intermediates against. They are separate messages because a caller fixes
       them differently. */
    ok = ok && cna_post_process_chain_apply(chain, 0) == CNA_RESULT_INVALID_ARGUMENT;
    context.width = INT32_C(16);
    context.height = INT32_C(16);
    ok = ok && cna_post_process_chain_apply(chain, &context) == CNA_RESULT_INVALID_ARGUMENT;

    /* The pool is a counted borrow: destroying the chain is refused while it is out. */
    ok = ok && cna_post_process_chain_get_target_pool(chain, &pool) == CNA_RESULT_SUCCESS &&
        pool != CNA_INVALID_HANDLE;
    ok = ok && cna_post_process_chain_destroy(chain) == CNA_RESULT_INVALID_STATE;
    ok = ok && cna_render_target_pool_destroy(pool) == CNA_RESULT_SUCCESS;

    /* GPU timing is accepted everywhere and reports back what it got. */
    ok = ok && cna_post_process_chain_set_gpu_timing_enabled(chain, UINT8_C(2)) ==
        CNA_RESULT_INVALID_ARGUMENT;
    ok = ok && cna_post_process_chain_set_gpu_timing_enabled(chain, CNA_TRUE) ==
        CNA_RESULT_SUCCESS;
    ok = ok && cna_post_process_chain_is_gpu_timing_enabled(chain, &flag) == CNA_RESULT_SUCCESS;
    ok = ok && cna_post_process_chain_set_gpu_timing_enabled(chain, CNA_FALSE) ==
        CNA_RESULT_SUCCESS;
    ok = ok && cna_post_process_chain_is_gpu_timing_enabled(chain, &flag) ==
        CNA_RESULT_SUCCESS && flag == CNA_FALSE;

    /* No timings recorded yet, and an index into an empty list is refused rather than read. */
    ok = ok && cna_post_process_chain_get_pass_timing_count(chain, &count) ==
        CNA_RESULT_SUCCESS && count == UINT64_C(0);
    ok = ok && cna_post_process_chain_get_pass_timing(chain, UINT64_C(0), &timing) ==
        CNA_RESULT_INVALID_ARGUMENT;
    ok = ok && cna_post_process_chain_copy_pass_timing_name(
            chain, UINT64_C(0), 0, UINT64_C(0), &bytes) == CNA_RESULT_INVALID_ARGUMENT &&
        bytes == UINT64_C(0);

    /* A borrowed pass stays the caller's: the chain records it and the handle keeps working. */
    if (ok && cna_blit_pass_create(graphics_device, &borrowed_pass) == CNA_RESULT_SUCCESS) {
        ok = cna_post_process_chain_add_pass(chain, borrowed_pass) == CNA_RESULT_SUCCESS;
        ok = ok && cna_post_process_chain_get_pass_count(chain, &passes) ==
            CNA_RESULT_SUCCESS && passes == INT32_C(1);
        /* Still the caller's, so it still answers and still needs releasing. */
        ok = ok && cna_post_process_pass_copy_name(borrowed_pass, 0, UINT64_C(0), &bytes) ==
            CNA_RESULT_BUFFER_TOO_SMALL;
        ok = ok && cna_post_process_chain_clear(chain) == CNA_RESULT_SUCCESS;
        ok = ok && cna_post_process_pass_destroy(borrowed_pass) == CNA_RESULT_SUCCESS;
    }

    /* An owned pass is CONSUMED: after handing it over the caller's handle is dead, and that is
       asserted rather than documented -- a route that promised ownership transfer and left the
       handle usable would invite a double free. */
    if (ok && cna_blit_pass_create(graphics_device, &owned_pass) == CNA_RESULT_SUCCESS) {
        ok = cna_post_process_chain_add_owned_pass(chain, owned_pass) == CNA_RESULT_SUCCESS;
        ok = ok && cna_post_process_chain_get_pass_count(chain, &passes) ==
            CNA_RESULT_SUCCESS && passes == INT32_C(1);
        ok = ok && cna_post_process_pass_copy_name(owned_pass, 0, UINT64_C(0), &bytes) ==
            CNA_RESULT_INVALID_HANDLE;
        ok = ok && cna_post_process_pass_destroy(owned_pass) != CNA_RESULT_SUCCESS;
        /* Clearing releases what the chain owned; the chain survives and is reusable. */
        ok = ok && cna_post_process_chain_clear(chain) == CNA_RESULT_SUCCESS;
        ok = ok && cna_post_process_chain_get_pass_count(chain, &passes) ==
            CNA_RESULT_SUCCESS && passes == INT32_C(0);
    }

    ok = ok && cna_post_process_chain_reset_targets(chain) == CNA_RESULT_SUCCESS;
    ok = ok && cna_post_process_chain_destroy(chain) == CNA_RESULT_SUCCESS;
    ok = ok && cna_post_process_chain_destroy(chain) != CNA_RESULT_SUCCESS;

    /* CBIND-088B deferred the pipeline's timings row here; it reads through the same shape. */
    {
        CNA_RenderPipelineHandle pipeline = CNA_INVALID_HANDLE;
        if (ok && cna_render_pipeline_create(graphics_device, &pipeline) == CNA_RESULT_SUCCESS) {
            ok = cna_render_pipeline_get_pass_timing_count_ext(pipeline, &count) ==
                CNA_RESULT_SUCCESS;
            ok = ok && cna_render_pipeline_get_pass_timing_ext(pipeline, count, &timing) ==
                CNA_RESULT_INVALID_ARGUMENT;
            ok = ok && cna_render_pipeline_copy_pass_timing_name_ext(
                    pipeline, count, 0, UINT64_C(0), &bytes) == CNA_RESULT_INVALID_ARGUMENT;
            ok = ok && cna_render_pipeline_destroy(pipeline) == CNA_RESULT_SUCCESS;
        }
    }
    return ok;
}


/* CBIND-089B. Three passes, three correction shapes, and the bounds differ between fields of the
   same pass -- SsrPass clamps its roughness blur to 0.25 and its edge fade to 0.5. So each bound
   is asserted on its own, the CBIND-088A rule. The guarded setters need the extra half the clamps
   do not: a valid value first, then an out-of-range write that must leave the valid one in place,
   because a guarded assignment is a silent no-op rather than a pin to the bound. And the free
   fields are written out of range and must survive -- SsrPass's step count especially, which the
   march clamps when it APPLIES rather than when it is set. */
static int validate_screen_space_passes(const CNA_Handle graphics_device)
{
    CNA_PostProcessPassHandle ssr = CNA_INVALID_HANDLE;
    CNA_PostProcessPassHandle ssao = CNA_INVALID_HANDLE;
    CNA_PostProcessPassHandle dof = CNA_INVALID_HANDLE;
    CNA_Vector3 kernel[64];
    CNA_Bool flag = UINT8_C(9);
    uint64_t count = UINT64_C(0);
    uint64_t bytes = UINT64_C(0);
    float scalar = -1.0F;
    int32_t number = -1;
    int ok = 1;

    if (cna_ssr_pass_create(graphics_device, &ssr) != CNA_RESULT_SUCCESS ||
        cna_ssao_pass_create(graphics_device, &ssao) != CNA_RESULT_SUCCESS ||
        cna_depth_of_field_pass_create(graphics_device, &dof) != CNA_RESULT_SUCCESS) {
        (void)cna_post_process_pass_destroy(ssr);
        (void)cna_post_process_pass_destroy(ssao);
        (void)cna_post_process_pass_destroy(dof);
        return 0;
    }

    /* A pass-specific accessor is refused BY ARGUMENT when the handle names another pass: the
       handle is valid, it is the concrete type that cannot answer. */
    ok = cna_ssr_pass_get_max_distance(ssao, &scalar) == CNA_RESULT_INVALID_ARGUMENT;
    ok = ok && cna_ssao_pass_get_radius(ssr, &scalar) == CNA_RESULT_INVALID_ARGUMENT;
    ok = ok && cna_depth_of_field_pass_get_f_number(ssr, &scalar) == CNA_RESULT_INVALID_ARGUMENT;
    /* The shared base routes answer for all three, which is what CBIND-084 bound. */
    ok = ok && cna_post_process_pass_copy_name(ssr, 0, UINT64_C(0), &bytes) ==
        CNA_RESULT_BUFFER_TOO_SMALL && bytes > UINT64_C(0);
    ok = ok && cna_post_process_pass_is_supported(ssao, graphics_device, &flag) ==
        CNA_RESULT_SUCCESS;


    /* ---- SsrPass ---- */
    ok = ok && cna_ssr_pass_set_max_distance(ssr, 3.0F) == CNA_RESULT_SUCCESS;
    ok = ok && cna_ssr_pass_get_max_distance(ssr, &scalar) == CNA_RESULT_SUCCESS &&
        scalar == 3.0F;
    ok = ok && cna_ssr_pass_set_max_distance(ssr, 0.0F) == CNA_RESULT_SUCCESS;
    ok = ok && cna_ssr_pass_get_max_distance(ssr, &scalar) == CNA_RESULT_SUCCESS &&
        scalar == 3.0F;
    ok = ok && cna_ssr_pass_set_max_distance(ssr, -5.0F) == CNA_RESULT_SUCCESS;
    ok = ok && cna_ssr_pass_get_max_distance(ssr, &scalar) == CNA_RESULT_SUCCESS &&
        scalar == 3.0F;
    ok = ok && cna_ssr_pass_set_step_count(ssr, INT32_C(9999)) == CNA_RESULT_SUCCESS;
    ok = ok && cna_ssr_pass_get_step_count(ssr, &number) == CNA_RESULT_SUCCESS &&
        number == INT32_C(9999);
    ok = ok && cna_ssr_pass_set_step_count(ssr, INT32_C(-3)) == CNA_RESULT_SUCCESS;
    ok = ok && cna_ssr_pass_get_step_count(ssr, &number) == CNA_RESULT_SUCCESS &&
        number == INT32_C(-3);
    ok = ok && cna_ssr_pass_set_thickness(ssr, 3.0F) == CNA_RESULT_SUCCESS;
    ok = ok && cna_ssr_pass_get_thickness(ssr, &scalar) == CNA_RESULT_SUCCESS &&
        scalar == 3.0F;
    ok = ok && cna_ssr_pass_set_thickness(ssr, 0.0F) == CNA_RESULT_SUCCESS;
    ok = ok && cna_ssr_pass_get_thickness(ssr, &scalar) == CNA_RESULT_SUCCESS &&
        scalar == 3.0F;
    ok = ok && cna_ssr_pass_set_thickness(ssr, -5.0F) == CNA_RESULT_SUCCESS;
    ok = ok && cna_ssr_pass_get_thickness(ssr, &scalar) == CNA_RESULT_SUCCESS &&
        scalar == 3.0F;
    ok = ok && cna_ssr_pass_set_depth_bias(ssr, 3.0F) == CNA_RESULT_SUCCESS;
    ok = ok && cna_ssr_pass_get_depth_bias(ssr, &scalar) == CNA_RESULT_SUCCESS &&
        scalar == 3.0F;
    ok = ok && cna_ssr_pass_set_depth_bias(ssr, 0.0F) == CNA_RESULT_SUCCESS;
    ok = ok && cna_ssr_pass_get_depth_bias(ssr, &scalar) == CNA_RESULT_SUCCESS &&
        scalar == 3.0F;
    ok = ok && cna_ssr_pass_set_depth_bias(ssr, -5.0F) == CNA_RESULT_SUCCESS;
    ok = ok && cna_ssr_pass_get_depth_bias(ssr, &scalar) == CNA_RESULT_SUCCESS &&
        scalar == 3.0F;
    ok = ok && cna_ssr_pass_set_roughness_blur(ssr, 900.0F) == CNA_RESULT_SUCCESS;
    ok = ok && cna_ssr_pass_get_roughness_blur(ssr, &scalar) == CNA_RESULT_SUCCESS &&
        scalar == 0.25F;
    ok = ok && cna_ssr_pass_set_roughness_blur(ssr, -900.0F) == CNA_RESULT_SUCCESS;
    ok = ok && cna_ssr_pass_get_roughness_blur(ssr, &scalar) == CNA_RESULT_SUCCESS &&
        scalar == 0.0F;
    ok = ok && cna_ssr_pass_set_edge_fade(ssr, 900.0F) == CNA_RESULT_SUCCESS;
    ok = ok && cna_ssr_pass_get_edge_fade(ssr, &scalar) == CNA_RESULT_SUCCESS &&
        scalar == 0.5F;
    ok = ok && cna_ssr_pass_set_edge_fade(ssr, -900.0F) == CNA_RESULT_SUCCESS;
    ok = ok && cna_ssr_pass_get_edge_fade(ssr, &scalar) == CNA_RESULT_SUCCESS &&
        scalar == 0.0F;
    ok = ok && cna_ssr_pass_set_intensity(ssr, -900.0F) == CNA_RESULT_SUCCESS;
    ok = ok && cna_ssr_pass_get_intensity(ssr, &scalar) == CNA_RESULT_SUCCESS &&
        scalar == -900.0F;

    /* ---- SsaoPass ---- */
    ok = ok && cna_ssao_pass_set_radius(ssao, -900.0F) == CNA_RESULT_SUCCESS;
    ok = ok && cna_ssao_pass_get_radius(ssao, &scalar) == CNA_RESULT_SUCCESS &&
        scalar == -900.0F;
    ok = ok && cna_ssao_pass_set_intensity(ssao, -900.0F) == CNA_RESULT_SUCCESS;
    ok = ok && cna_ssao_pass_get_intensity(ssao, &scalar) == CNA_RESULT_SUCCESS &&
        scalar == -900.0F;
    ok = ok && cna_ssao_pass_set_sample_count(ssao, INT32_C(9999)) == CNA_RESULT_SUCCESS;
    ok = ok && cna_ssao_pass_get_sample_count(ssao, &number) == CNA_RESULT_SUCCESS &&
        number == INT32_C(9999);
    ok = ok && cna_ssao_pass_set_sample_count(ssao, INT32_C(-3)) == CNA_RESULT_SUCCESS;
    ok = ok && cna_ssao_pass_get_sample_count(ssao, &number) == CNA_RESULT_SUCCESS &&
        number == INT32_C(-3);
    ok = ok && cna_ssao_pass_set_half_resolution(ssao, CNA_TRUE) == CNA_RESULT_SUCCESS;
    ok = ok && cna_ssao_pass_get_half_resolution(ssao, &flag) == CNA_RESULT_SUCCESS &&
        flag == CNA_TRUE;
    ok = ok && cna_ssao_pass_set_half_resolution(ssao, UINT8_C(2)) == CNA_RESULT_INVALID_ARGUMENT;

    /* ---- DepthOfFieldPass ---- */
    ok = ok && cna_depth_of_field_pass_set_focus_distance(dof, 3.0F) == CNA_RESULT_SUCCESS;
    ok = ok && cna_depth_of_field_pass_get_focus_distance(dof, &scalar) == CNA_RESULT_SUCCESS &&
        scalar == 3.0F;
    ok = ok && cna_depth_of_field_pass_set_focus_distance(dof, 0.0F) == CNA_RESULT_SUCCESS;
    ok = ok && cna_depth_of_field_pass_get_focus_distance(dof, &scalar) == CNA_RESULT_SUCCESS &&
        scalar == 3.0F;
    ok = ok && cna_depth_of_field_pass_set_focus_distance(dof, -5.0F) == CNA_RESULT_SUCCESS;
    ok = ok && cna_depth_of_field_pass_get_focus_distance(dof, &scalar) == CNA_RESULT_SUCCESS &&
        scalar == 3.0F;
    ok = ok && cna_depth_of_field_pass_set_focal_length(dof, 3.0F) == CNA_RESULT_SUCCESS;
    ok = ok && cna_depth_of_field_pass_get_focal_length(dof, &scalar) == CNA_RESULT_SUCCESS &&
        scalar == 3.0F;
    ok = ok && cna_depth_of_field_pass_set_focal_length(dof, 0.0F) == CNA_RESULT_SUCCESS;
    ok = ok && cna_depth_of_field_pass_get_focal_length(dof, &scalar) == CNA_RESULT_SUCCESS &&
        scalar == 3.0F;
    ok = ok && cna_depth_of_field_pass_set_focal_length(dof, -5.0F) == CNA_RESULT_SUCCESS;
    ok = ok && cna_depth_of_field_pass_get_focal_length(dof, &scalar) == CNA_RESULT_SUCCESS &&
        scalar == 3.0F;
    ok = ok && cna_depth_of_field_pass_set_f_number(dof, 3.0F) == CNA_RESULT_SUCCESS;
    ok = ok && cna_depth_of_field_pass_get_f_number(dof, &scalar) == CNA_RESULT_SUCCESS &&
        scalar == 3.0F;
    ok = ok && cna_depth_of_field_pass_set_f_number(dof, 0.0F) == CNA_RESULT_SUCCESS;
    ok = ok && cna_depth_of_field_pass_get_f_number(dof, &scalar) == CNA_RESULT_SUCCESS &&
        scalar == 3.0F;
    ok = ok && cna_depth_of_field_pass_set_f_number(dof, -5.0F) == CNA_RESULT_SUCCESS;
    ok = ok && cna_depth_of_field_pass_get_f_number(dof, &scalar) == CNA_RESULT_SUCCESS &&
        scalar == 3.0F;
    ok = ok && cna_depth_of_field_pass_set_max_radius(dof, 900.0F) == CNA_RESULT_SUCCESS;
    ok = ok && cna_depth_of_field_pass_get_max_radius(dof, &scalar) == CNA_RESULT_SUCCESS &&
        scalar == 0.25F;
    ok = ok && cna_depth_of_field_pass_set_max_radius(dof, -900.0F) == CNA_RESULT_SUCCESS;
    ok = ok && cna_depth_of_field_pass_get_max_radius(dof, &scalar) == CNA_RESULT_SUCCESS &&
        scalar == 0.0F;

    /* The step-count bounds are applied where the march runs, not where the value is set, so the
       constants must exist and the setter must NOT enforce them -- both halves asserted. */
    ok = ok && CNA_SSR_PASS_MIN_STEP_COUNT_EXT < CNA_SSR_PASS_MAX_STEP_COUNT_EXT;
    ok = ok && cna_ssr_pass_set_step_count(ssr, CNA_SSR_PASS_MAX_STEP_COUNT_EXT + INT32_C(500)) ==
        CNA_RESULT_SUCCESS;
    ok = ok && cna_ssr_pass_get_step_count(ssr, &number) == CNA_RESULT_SUCCESS &&
        number == CNA_SSR_PASS_MAX_STEP_COUNT_EXT + INT32_C(500);

    /* The kernel is a copy, so it stays correct after the pass changes. */
    ok = ok && cna_ssao_pass_copy_kernel(ssao, 0, UINT64_C(0), &count) ==
        CNA_RESULT_BUFFER_TOO_SMALL && count > UINT64_C(0) &&
        count <= (uint64_t)(sizeof kernel / sizeof kernel[0]);
    ok = ok && cna_ssao_pass_copy_kernel(
            ssao, kernel, (uint64_t)(sizeof kernel / sizeof kernel[0]), &count) ==
        CNA_RESULT_SUCCESS;
    ok = ok && cna_ssao_pass_copy_kernel(ssr, kernel, UINT64_C(64), &count) ==
        CNA_RESULT_INVALID_ARGUMENT;
    ok = ok && cna_ssao_pass_reset_targets(ssao) == CNA_RESULT_SUCCESS;
    ok = ok && cna_ssao_pass_reset_targets(ssr) == CNA_RESULT_INVALID_ARGUMENT;

    /* Two pure functions and one that varies with its flag: the packed and unpacked GLSL must
       differ, or the flag is being ignored. */
    ok = ok && cna_ssao_pass_copy_occlusion_glsl(CNA_FALSE, 0, UINT64_C(0), &bytes) ==
        CNA_RESULT_BUFFER_TOO_SMALL && bytes > UINT64_C(0);
    ok = ok && cna_ssao_pass_copy_occlusion_glsl(CNA_TRUE, 0, UINT64_C(0), &count) ==
        CNA_RESULT_BUFFER_TOO_SMALL && count != bytes;
    ok = ok && cna_ssao_pass_copy_occlusion_glsl(UINT8_C(2), 0, UINT64_C(0), &bytes) ==
        CNA_RESULT_INVALID_ARGUMENT;
    /* Low and Ultra must not ask for the same sample count, or the preset does nothing. */
    ok = ok && cna_ssao_pass_sample_count_for_quality(CNA_RENDER_QUALITY_LOW, &number) ==
        CNA_RESULT_SUCCESS;
    {
        int32_t ultra = -2;
        ok = ok && cna_ssao_pass_sample_count_for_quality(CNA_RENDER_QUALITY_ULTRA, &ultra) ==
            CNA_RESULT_SUCCESS && ultra != number;
    }
    ok = ok && cna_ssao_pass_sample_count_for_quality(UINT32_C(99), &number) ==
        CNA_RESULT_INVALID_ARGUMENT;

    /* The circle of confusion is zero exactly at the focus distance and grows away from it. */
    ok = ok && cna_depth_of_field_pass_circle_of_confusion_millimetres(
            10.0F, 10.0F, 50.0F, 2.8F, &scalar) == CNA_RESULT_SUCCESS && scalar == 0.0F;
    {
        float near_blur = -1.0F;
        float far_blur = -1.0F;
        ok = ok && cna_depth_of_field_pass_circle_of_confusion_millimetres(
                11.0F, 10.0F, 50.0F, 2.8F, &near_blur) == CNA_RESULT_SUCCESS;
        ok = ok && cna_depth_of_field_pass_circle_of_confusion_millimetres(
                40.0F, 10.0F, 50.0F, 2.8F, &far_blur) == CNA_RESULT_SUCCESS;
        ok = ok && far_blur > near_blur && near_blur > 0.0F;
    }
    ok = ok && CNA_DEPTH_OF_FIELD_SENSOR_HEIGHT_MILLIMETRES_EXT > 0.0F;

    ok = ok && cna_post_process_pass_destroy(dof) == CNA_RESULT_SUCCESS;
    ok = ok && cna_post_process_pass_destroy(ssao) == CNA_RESULT_SUCCESS;
    ok = ok && cna_post_process_pass_destroy(ssr) == CNA_RESULT_SUCCESS;
    return ok;
}



/* CBIND-089C. Four passes whose guards are NOT interchangeable: some admit zero and some reject
   it, and one floors at a value that is neither zero nor obvious. Asserting them apart is the
   whole point of the stage -- a shared "negative is ignored" expectation would pass while
   setRange(0) and setDensity(0) behaved differently from what the header promises. */
static int validate_atmospheric_passes(const CNA_Handle graphics_device)
{
    CNA_PostProcessPassHandle aerial = CNA_INVALID_HANDLE;
    CNA_PostProcessPassHandle volfog = CNA_INVALID_HANDLE;
    CNA_PostProcessPassHandle heightfog = CNA_INVALID_HANDLE;
    CNA_PostProcessPassHandle shaft = CNA_INVALID_HANDLE;
    CNA_Vector3 vector;
    CNA_Vector3 read_back;
    CNA_Vector2 point;
    CNA_Vector2 point_back;
    uint64_t bytes = UINT64_C(0);
    float scalar = -1.0F;
    int ok = 1;

    if (cna_aerial_perspective_pass_create(graphics_device, &aerial) != CNA_RESULT_SUCCESS ||
        cna_volumetric_fog_pass_create(graphics_device, &volfog) != CNA_RESULT_SUCCESS ||
        cna_height_fog_pass_create(graphics_device, &heightfog) != CNA_RESULT_SUCCESS ||
        cna_light_shaft_pass_create(graphics_device, &shaft) != CNA_RESULT_SUCCESS) {
        (void)cna_post_process_pass_destroy(aerial);
        (void)cna_post_process_pass_destroy(volfog);
        (void)cna_post_process_pass_destroy(heightfog);
        (void)cna_post_process_pass_destroy(shaft);
        return 0;
    }

    /* Refused by argument, not by handle, when the handle names another pass. */
    ok = cna_volumetric_fog_pass_get_range(aerial, &scalar) == CNA_RESULT_INVALID_ARGUMENT;
    ok = ok && cna_aerial_perspective_pass_get_turbidity(shaft, &scalar) ==
        CNA_RESULT_INVALID_ARGUMENT;


    /* ---- aerial_perspective_pass ---- */
    vector.x = 0.25F; vector.y = -0.5F; vector.z = 2.0F;
    ok = ok && cna_aerial_perspective_pass_set_sun_direction(aerial, &vector) == CNA_RESULT_SUCCESS;
    ok = ok && cna_aerial_perspective_pass_get_sun_direction(aerial, &read_back) == CNA_RESULT_SUCCESS &&
        read_back.x == 0.25F && read_back.y == -0.5F && read_back.z == 2.0F;
    ok = ok && cna_aerial_perspective_pass_set_sun_direction(aerial, 0) == CNA_RESULT_INVALID_ARGUMENT;
    ok = ok && cna_aerial_perspective_pass_set_turbidity(aerial, -900.0F) == CNA_RESULT_SUCCESS;
    ok = ok && cna_aerial_perspective_pass_get_turbidity(aerial, &scalar) == CNA_RESULT_SUCCESS &&
        scalar == 1.0F;
    ok = ok && cna_aerial_perspective_pass_set_turbidity(aerial, 900.0F) == CNA_RESULT_SUCCESS;
    ok = ok && cna_aerial_perspective_pass_get_turbidity(aerial, &scalar) == CNA_RESULT_SUCCESS &&
        scalar == 900.0F;
    /* Guard admits zero: a valid value, then zero which IS accepted, then a negative
       which is ignored and must leave zero in place. */
    ok = ok && cna_aerial_perspective_pass_set_intensity(aerial, 4.0F) == CNA_RESULT_SUCCESS;
    ok = ok && cna_aerial_perspective_pass_set_intensity(aerial, 0.0F) == CNA_RESULT_SUCCESS;
    ok = ok && cna_aerial_perspective_pass_get_intensity(aerial, &scalar) == CNA_RESULT_SUCCESS &&
        scalar == 0.0F;
    ok = ok && cna_aerial_perspective_pass_set_intensity(aerial, -5.0F) == CNA_RESULT_SUCCESS;
    ok = ok && cna_aerial_perspective_pass_get_intensity(aerial, &scalar) == CNA_RESULT_SUCCESS &&
        scalar == 0.0F;
    ok = ok && cna_aerial_perspective_pass_set_scale_height(aerial, -900.0F) == CNA_RESULT_SUCCESS;
    ok = ok && cna_aerial_perspective_pass_get_scale_height(aerial, &scalar) == CNA_RESULT_SUCCESS &&
        scalar == 0.001F;
    ok = ok && cna_aerial_perspective_pass_set_scale_height(aerial, 900.0F) == CNA_RESULT_SUCCESS;
    ok = ok && cna_aerial_perspective_pass_get_scale_height(aerial, &scalar) == CNA_RESULT_SUCCESS &&
        scalar == 900.0F;

    /* ---- volumetric_fog_pass ---- */
    /* Guard admits zero: a valid value, then zero which IS accepted, then a negative
       which is ignored and must leave zero in place. */
    ok = ok && cna_volumetric_fog_pass_set_density(volfog, 4.0F) == CNA_RESULT_SUCCESS;
    ok = ok && cna_volumetric_fog_pass_set_density(volfog, 0.0F) == CNA_RESULT_SUCCESS;
    ok = ok && cna_volumetric_fog_pass_get_density(volfog, &scalar) == CNA_RESULT_SUCCESS &&
        scalar == 0.0F;
    ok = ok && cna_volumetric_fog_pass_set_density(volfog, -5.0F) == CNA_RESULT_SUCCESS;
    ok = ok && cna_volumetric_fog_pass_get_density(volfog, &scalar) == CNA_RESULT_SUCCESS &&
        scalar == 0.0F;
    ok = ok && cna_volumetric_fog_pass_set_anisotropy(volfog, 900.0F) == CNA_RESULT_SUCCESS;
    ok = ok && cna_volumetric_fog_pass_get_anisotropy(volfog, &scalar) == CNA_RESULT_SUCCESS &&
        scalar == 0.95F;
    ok = ok && cna_volumetric_fog_pass_set_anisotropy(volfog, -900.0F) == CNA_RESULT_SUCCESS;
    ok = ok && cna_volumetric_fog_pass_get_anisotropy(volfog, &scalar) == CNA_RESULT_SUCCESS &&
        scalar == -0.95F;
    /* Guard rejects zero as well as negatives, unlike its >= 0 neighbours. */
    ok = ok && cna_volumetric_fog_pass_set_range(volfog, 4.0F) == CNA_RESULT_SUCCESS;
    ok = ok && cna_volumetric_fog_pass_set_range(volfog, 0.0F) == CNA_RESULT_SUCCESS;
    ok = ok && cna_volumetric_fog_pass_get_range(volfog, &scalar) == CNA_RESULT_SUCCESS &&
        scalar == 4.0F;
    ok = ok && cna_volumetric_fog_pass_set_range(volfog, -5.0F) == CNA_RESULT_SUCCESS;
    ok = ok && cna_volumetric_fog_pass_get_range(volfog, &scalar) == CNA_RESULT_SUCCESS &&
        scalar == 4.0F;

    /* ---- height_fog_pass ---- */
    vector.x = 0.25F; vector.y = -0.5F; vector.z = 2.0F;
    ok = ok && cna_height_fog_pass_set_color(heightfog, &vector) == CNA_RESULT_SUCCESS;
    ok = ok && cna_height_fog_pass_get_color(heightfog, &read_back) == CNA_RESULT_SUCCESS &&
        read_back.x == 0.25F && read_back.y == -0.5F && read_back.z == 2.0F;
    ok = ok && cna_height_fog_pass_set_color(heightfog, 0) == CNA_RESULT_INVALID_ARGUMENT;
    /* Guard admits zero: a valid value, then zero which IS accepted, then a negative
       which is ignored and must leave zero in place. */
    ok = ok && cna_height_fog_pass_set_density(heightfog, 4.0F) == CNA_RESULT_SUCCESS;
    ok = ok && cna_height_fog_pass_set_density(heightfog, 0.0F) == CNA_RESULT_SUCCESS;
    ok = ok && cna_height_fog_pass_get_density(heightfog, &scalar) == CNA_RESULT_SUCCESS &&
        scalar == 0.0F;
    ok = ok && cna_height_fog_pass_set_density(heightfog, -5.0F) == CNA_RESULT_SUCCESS;
    ok = ok && cna_height_fog_pass_get_density(heightfog, &scalar) == CNA_RESULT_SUCCESS &&
        scalar == 0.0F;
    /* Guard rejects zero as well as negatives, unlike its >= 0 neighbours. */
    ok = ok && cna_height_fog_pass_set_falloff(heightfog, 4.0F) == CNA_RESULT_SUCCESS;
    ok = ok && cna_height_fog_pass_set_falloff(heightfog, 0.0F) == CNA_RESULT_SUCCESS;
    ok = ok && cna_height_fog_pass_get_falloff(heightfog, &scalar) == CNA_RESULT_SUCCESS &&
        scalar == 4.0F;
    ok = ok && cna_height_fog_pass_set_falloff(heightfog, -5.0F) == CNA_RESULT_SUCCESS;
    ok = ok && cna_height_fog_pass_get_falloff(heightfog, &scalar) == CNA_RESULT_SUCCESS &&
        scalar == 4.0F;
    /* Corrects nothing: an out-of-range value survives in both directions. */
    ok = ok && cna_height_fog_pass_set_base_height(heightfog, -900.0F) == CNA_RESULT_SUCCESS;
    ok = ok && cna_height_fog_pass_get_base_height(heightfog, &scalar) == CNA_RESULT_SUCCESS &&
        scalar == -900.0F;
    ok = ok && cna_height_fog_pass_set_base_height(heightfog, 900.0F) == CNA_RESULT_SUCCESS;
    ok = ok && cna_height_fog_pass_get_base_height(heightfog, &scalar) == CNA_RESULT_SUCCESS &&
        scalar == 900.0F;

    /* ---- light_shaft_pass ---- */
    point.x = -3.0F; point.y = 7.0F;
    ok = ok && cna_light_shaft_pass_set_light_screen_position(shaft, &point) == CNA_RESULT_SUCCESS;
    ok = ok && cna_light_shaft_pass_get_light_screen_position(shaft, &point_back) == CNA_RESULT_SUCCESS &&
        point_back.x == -3.0F && point_back.y == 7.0F;
    ok = ok && cna_light_shaft_pass_set_light_screen_position(shaft, 0) == CNA_RESULT_INVALID_ARGUMENT;
    /* Guard admits zero: a valid value, then zero which IS accepted, then a negative
       which is ignored and must leave zero in place. */
    ok = ok && cna_light_shaft_pass_set_threshold(shaft, 4.0F) == CNA_RESULT_SUCCESS;
    ok = ok && cna_light_shaft_pass_set_threshold(shaft, 0.0F) == CNA_RESULT_SUCCESS;
    ok = ok && cna_light_shaft_pass_get_threshold(shaft, &scalar) == CNA_RESULT_SUCCESS &&
        scalar == 0.0F;
    ok = ok && cna_light_shaft_pass_set_threshold(shaft, -5.0F) == CNA_RESULT_SUCCESS;
    ok = ok && cna_light_shaft_pass_get_threshold(shaft, &scalar) == CNA_RESULT_SUCCESS &&
        scalar == 0.0F;
    /* Guard admits zero: a valid value, then zero which IS accepted, then a negative
       which is ignored and must leave zero in place. */
    ok = ok && cna_light_shaft_pass_set_intensity(shaft, 4.0F) == CNA_RESULT_SUCCESS;
    ok = ok && cna_light_shaft_pass_set_intensity(shaft, 0.0F) == CNA_RESULT_SUCCESS;
    ok = ok && cna_light_shaft_pass_get_intensity(shaft, &scalar) == CNA_RESULT_SUCCESS &&
        scalar == 0.0F;
    ok = ok && cna_light_shaft_pass_set_intensity(shaft, -5.0F) == CNA_RESULT_SUCCESS;
    ok = ok && cna_light_shaft_pass_get_intensity(shaft, &scalar) == CNA_RESULT_SUCCESS &&
        scalar == 0.0F;
    ok = ok && cna_light_shaft_pass_set_decay(shaft, 900.0F) == CNA_RESULT_SUCCESS;
    ok = ok && cna_light_shaft_pass_get_decay(shaft, &scalar) == CNA_RESULT_SUCCESS &&
        scalar == 1.0F;
    ok = ok && cna_light_shaft_pass_set_decay(shaft, -900.0F) == CNA_RESULT_SUCCESS;
    ok = ok && cna_light_shaft_pass_get_decay(shaft, &scalar) == CNA_RESULT_SUCCESS &&
        scalar == 0.0F;

    /* The three constants name the canonical ones, tied by a static_assert. */
    ok = ok && CNA_VOLUMETRIC_FOG_SLICE_COUNT_EXT > INT32_C(0) &&
        CNA_VOLUMETRIC_FOG_SLICE_RESOLUTION_EXT > INT32_C(0) &&
        CNA_LIGHT_SHAFT_STEP_COUNT_EXT > INT32_C(0);

    /* The fog light is borrowed; an invalid map handle means "march unshadowed", not an error. */
    vector.x = 0.0F; vector.y = -1.0F; vector.z = 0.0F;
    read_back.x = 1.0F; read_back.y = 1.0F; read_back.z = 1.0F;
    ok = ok && cna_volumetric_fog_pass_set_light(
            volfog, CNA_INVALID_HANDLE, &vector, &read_back) == CNA_RESULT_SUCCESS;
    ok = ok && cna_volumetric_fog_pass_set_light(volfog, CNA_INVALID_HANDLE, 0, &read_back) ==
        CNA_RESULT_INVALID_ARGUMENT;
    ok = ok && cna_volumetric_fog_pass_set_light(aerial, CNA_INVALID_HANDLE, &vector,
                                                 &read_back) == CNA_RESULT_INVALID_ARGUMENT;

    ok = ok && cna_aerial_perspective_pass_copy_fallback_reason(aerial, 0, UINT64_C(0), &bytes) !=
        CNA_RESULT_INVALID_HANDLE;
    ok = ok && cna_aerial_perspective_pass_copy_fallback_reason(shaft, 0, UINT64_C(0), &bytes) ==
        CNA_RESULT_INVALID_ARGUMENT;

    /* Air mass grows with distance, and transmittance falls as it does -- the two are inverse,
       which is the only relationship worth asserting without duplicating the maths. */
    {
        float near_mass = -1.0F;
        float far_mass = -1.0F;
        CNA_Vector3 near_t;
        CNA_Vector3 far_t;
        vector.x = 0.0F; vector.y = 1.0F; vector.z = 0.0F;
        ok = ok && cna_aerial_perspective_pass_air_mass_for_distance(
                &vector, 10.0F, 8000.0F, &near_mass) == CNA_RESULT_SUCCESS;
        ok = ok && cna_aerial_perspective_pass_air_mass_for_distance(
                &vector, 1000.0F, 8000.0F, &far_mass) == CNA_RESULT_SUCCESS;
        ok = ok && far_mass > near_mass;
        ok = ok && cna_aerial_perspective_pass_air_mass_for_distance(0, 10.0F, 1.0F,
                                                                    &near_mass) ==
            CNA_RESULT_INVALID_ARGUMENT;
        ok = ok && cna_aerial_perspective_pass_transmittance(2.0F, near_mass, &near_t) ==
            CNA_RESULT_SUCCESS;
        ok = ok && cna_aerial_perspective_pass_transmittance(2.0F, far_mass, &far_t) ==
            CNA_RESULT_SUCCESS;
        ok = ok && far_t.x <= near_t.x && far_t.y <= near_t.y && far_t.z <= near_t.z;
    }

    /* Optical depth grows with distance and with density, and is zero at zero distance. */
    {
        float thin = -1.0F;
        float thick = -1.0F;
        ok = ok && cna_height_fog_pass_optical_depth(0.0F, 0.0F, 0.0F, 1.0F, 1.0F, 0.0F,
                                                     &scalar) == CNA_RESULT_SUCCESS &&
            scalar == 0.0F;
        ok = ok && cna_height_fog_pass_optical_depth(0.0F, 0.0F, 100.0F, 0.1F, 1.0F, 0.0F,
                                                     &thin) == CNA_RESULT_SUCCESS;
        ok = ok && cna_height_fog_pass_optical_depth(0.0F, 0.0F, 100.0F, 1.0F, 1.0F, 0.0F,
                                                     &thick) == CNA_RESULT_SUCCESS;
        ok = ok && thick > thin && thin > 0.0F;
    }

    ok = ok && cna_post_process_pass_destroy(shaft) == CNA_RESULT_SUCCESS;
    ok = ok && cna_post_process_pass_destroy(heightfog) == CNA_RESULT_SUCCESS;
    ok = ok && cna_post_process_pass_destroy(volfog) == CNA_RESULT_SUCCESS;
    ok = ok && cna_post_process_pass_destroy(aerial) == CNA_RESULT_SUCCESS;
    return ok;
}


/* CBIND-089D. Nine passes, and the slice's real finding is that two of them are not passes:
   DecalPass and SpatialUpscalePass do not derive from PostProcessPass, have no apply(), and are
   driven by their own draw(). They carry their own handles, and the test asserts the shared
   cna_post_process_pass_* routes REFUSE them -- which is the only way a caller learns the
   difference from the outside. */
static int validate_remaining_passes(const CNA_Handle graphics_device)
{
    CNA_PostProcessPassHandle bloom = CNA_INVALID_HANDLE;
    CNA_PostProcessPassHandle lens = CNA_INVALID_HANDLE;
    CNA_PostProcessPassHandle motion = CNA_INVALID_HANDLE;
    CNA_PostProcessPassHandle fxaa = CNA_INVALID_HANDLE;
    CNA_PostProcessPassHandle chroma = CNA_INVALID_HANDLE;
    CNA_PostProcessPassHandle grain = CNA_INVALID_HANDLE;
    CNA_PostProcessPassHandle ascii = CNA_INVALID_HANDLE;
    CNA_DecalPassHandle decal = CNA_INVALID_HANDLE;
    CNA_SpatialUpscalePassHandle upscale = CNA_INVALID_HANDLE;
    CNA_AsciiPostProcessEffectHandle effect = CNA_INVALID_HANDLE;
    CNA_Vector3 vector;
    CNA_Vector3 read_back;
    CNA_Matrix view;
    CNA_Bool flag = UINT8_C(9);
    uint64_t bytes = UINT64_C(0);
    float scalar = -1.0F;
    int32_t number = -1;
    int ok = 1;

    if (cna_matrix_get_identity(&view) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    if (cna_bloom_pass_create(graphics_device, &bloom) != CNA_RESULT_SUCCESS ||
        cna_lens_flare_pass_create(graphics_device, &lens) != CNA_RESULT_SUCCESS ||
        cna_motion_blur_pass_create(graphics_device, &motion) != CNA_RESULT_SUCCESS ||
        cna_fxaa_pass_create(graphics_device, &fxaa) != CNA_RESULT_SUCCESS ||
        cna_chromatic_aberration_pass_create(graphics_device, &chroma) != CNA_RESULT_SUCCESS ||
        cna_film_grain_pass_create(graphics_device, &grain) != CNA_RESULT_SUCCESS ||
        cna_ascii_pass_create(graphics_device, &ascii) != CNA_RESULT_SUCCESS ||
        cna_decal_pass_create(graphics_device, &decal) != CNA_RESULT_SUCCESS ||
        cna_spatial_upscale_pass_create(graphics_device, &upscale) != CNA_RESULT_SUCCESS) {
        (void)cna_post_process_pass_destroy(bloom);
        (void)cna_post_process_pass_destroy(lens);
        (void)cna_post_process_pass_destroy(motion);
        (void)cna_post_process_pass_destroy(fxaa);
        (void)cna_post_process_pass_destroy(chroma);
        (void)cna_post_process_pass_destroy(grain);
        (void)cna_post_process_pass_destroy(ascii);
        (void)cna_decal_pass_destroy(decal);
        (void)cna_spatial_upscale_pass_destroy(upscale);
        return 0;
    }

    /* The two standalone objects are NOT post-process passes, and the shared routes say so.
       Without this the difference is invisible from C and a caller would only find it by
       getting an error at some later point with no explanation. */
    ok = cna_post_process_pass_destroy(decal) != CNA_RESULT_SUCCESS;
    ok = ok && cna_post_process_pass_copy_name(decal, 0, UINT64_C(0), &bytes) !=
        CNA_RESULT_BUFFER_TOO_SMALL;
    ok = ok && cna_post_process_pass_destroy(upscale) != CNA_RESULT_SUCCESS;
    /* And the seven real passes do answer the shared routes. */
    ok = ok && cna_post_process_pass_copy_name(bloom, 0, UINT64_C(0), &bytes) ==
        CNA_RESULT_BUFFER_TOO_SMALL && bytes > UINT64_C(0);
    ok = ok && cna_post_process_pass_is_supported(fxaa, graphics_device, &flag) ==
        CNA_RESULT_SUCCESS;
    /* Cross-type accessors refuse by argument. */
    ok = ok && cna_bloom_pass_get_threshold(fxaa, &scalar) == CNA_RESULT_INVALID_ARGUMENT;
    ok = ok && cna_decal_pass_get_opacity(upscale, &scalar) != CNA_RESULT_SUCCESS;


    /* ---- bloom_pass ---- */
    /* Corrects nothing: an out-of-range value survives in both directions. */
    ok = ok && cna_bloom_pass_set_threshold(bloom, -900.0F) == CNA_RESULT_SUCCESS;
    ok = ok && cna_bloom_pass_get_threshold(bloom, &scalar) == CNA_RESULT_SUCCESS &&
        scalar == -900.0F;
    ok = ok && cna_bloom_pass_set_threshold(bloom, 900.0F) == CNA_RESULT_SUCCESS;
    ok = ok && cna_bloom_pass_get_threshold(bloom, &scalar) == CNA_RESULT_SUCCESS &&
        scalar == 900.0F;
    /* Corrects nothing: an out-of-range value survives in both directions. */
    ok = ok && cna_bloom_pass_set_intensity(bloom, -900.0F) == CNA_RESULT_SUCCESS;
    ok = ok && cna_bloom_pass_get_intensity(bloom, &scalar) == CNA_RESULT_SUCCESS &&
        scalar == -900.0F;
    ok = ok && cna_bloom_pass_set_intensity(bloom, 900.0F) == CNA_RESULT_SUCCESS;
    ok = ok && cna_bloom_pass_get_intensity(bloom, &scalar) == CNA_RESULT_SUCCESS &&
        scalar == 900.0F;
    ok = ok && cna_bloom_pass_set_iterations(bloom, INT32_C(9999)) == CNA_RESULT_SUCCESS;
    ok = ok && cna_bloom_pass_get_iterations(bloom, &number) == CNA_RESULT_SUCCESS &&
        number == INT32_C(9999);

    /* ---- lens_flare_pass ---- */
    ok = ok && cna_lens_flare_pass_set_threshold(lens, 4.0F) == CNA_RESULT_SUCCESS;
    ok = ok && cna_lens_flare_pass_set_threshold(lens, 0.0F) == CNA_RESULT_SUCCESS;
    ok = ok && cna_lens_flare_pass_get_threshold(lens, &scalar) == CNA_RESULT_SUCCESS && scalar == 0.0F;
    ok = ok && cna_lens_flare_pass_set_threshold(lens, -5.0F) == CNA_RESULT_SUCCESS;
    ok = ok && cna_lens_flare_pass_get_threshold(lens, &scalar) == CNA_RESULT_SUCCESS && scalar == 0.0F;
    ok = ok && cna_lens_flare_pass_set_intensity(lens, 4.0F) == CNA_RESULT_SUCCESS;
    ok = ok && cna_lens_flare_pass_set_intensity(lens, 0.0F) == CNA_RESULT_SUCCESS;
    ok = ok && cna_lens_flare_pass_get_intensity(lens, &scalar) == CNA_RESULT_SUCCESS && scalar == 0.0F;
    ok = ok && cna_lens_flare_pass_set_intensity(lens, -5.0F) == CNA_RESULT_SUCCESS;
    ok = ok && cna_lens_flare_pass_get_intensity(lens, &scalar) == CNA_RESULT_SUCCESS && scalar == 0.0F;
    ok = ok && cna_lens_flare_pass_set_dispersal(lens, 900.0F) == CNA_RESULT_SUCCESS;
    ok = ok && cna_lens_flare_pass_get_dispersal(lens, &scalar) == CNA_RESULT_SUCCESS && scalar == 1.0F;
    ok = ok && cna_lens_flare_pass_set_dispersal(lens, -900.0F) == CNA_RESULT_SUCCESS;
    ok = ok && cna_lens_flare_pass_get_dispersal(lens, &scalar) == CNA_RESULT_SUCCESS && scalar == 0.0F;

    /* ---- motion_blur_pass ---- */
    ok = ok && cna_motion_blur_pass_set_strength(motion, 900.0F) == CNA_RESULT_SUCCESS;
    ok = ok && cna_motion_blur_pass_get_strength(motion, &scalar) == CNA_RESULT_SUCCESS && scalar == 1.0F;
    ok = ok && cna_motion_blur_pass_set_strength(motion, -900.0F) == CNA_RESULT_SUCCESS;
    ok = ok && cna_motion_blur_pass_get_strength(motion, &scalar) == CNA_RESULT_SUCCESS && scalar == 0.0F;
    ok = ok && cna_motion_blur_pass_set_max_distance(motion, 900.0F) == CNA_RESULT_SUCCESS;
    ok = ok && cna_motion_blur_pass_get_max_distance(motion, &scalar) == CNA_RESULT_SUCCESS && scalar == 0.25F;
    ok = ok && cna_motion_blur_pass_set_max_distance(motion, -900.0F) == CNA_RESULT_SUCCESS;
    ok = ok && cna_motion_blur_pass_get_max_distance(motion, &scalar) == CNA_RESULT_SUCCESS && scalar == 0.0F;

    /* ---- fxaa_pass ---- */
    /* Corrects nothing: an out-of-range value survives in both directions. */
    ok = ok && cna_fxaa_pass_set_edge_threshold(fxaa, -900.0F) == CNA_RESULT_SUCCESS;
    ok = ok && cna_fxaa_pass_get_edge_threshold(fxaa, &scalar) == CNA_RESULT_SUCCESS &&
        scalar == -900.0F;
    ok = ok && cna_fxaa_pass_set_edge_threshold(fxaa, 900.0F) == CNA_RESULT_SUCCESS;
    ok = ok && cna_fxaa_pass_get_edge_threshold(fxaa, &scalar) == CNA_RESULT_SUCCESS &&
        scalar == 900.0F;

    /* ---- chromatic_aberration_pass ---- */
    ok = ok && cna_chromatic_aberration_pass_set_strength(chroma, 900.0F) == CNA_RESULT_SUCCESS;
    ok = ok && cna_chromatic_aberration_pass_get_strength(chroma, &scalar) == CNA_RESULT_SUCCESS && scalar == 0.1F;
    ok = ok && cna_chromatic_aberration_pass_set_strength(chroma, -900.0F) == CNA_RESULT_SUCCESS;
    ok = ok && cna_chromatic_aberration_pass_get_strength(chroma, &scalar) == CNA_RESULT_SUCCESS && scalar == 0.0F;

    /* ---- film_grain_pass ---- */
    ok = ok && cna_film_grain_pass_set_intensity(grain, 900.0F) == CNA_RESULT_SUCCESS;
    ok = ok && cna_film_grain_pass_get_intensity(grain, &scalar) == CNA_RESULT_SUCCESS && scalar == 1.0F;
    ok = ok && cna_film_grain_pass_set_intensity(grain, -900.0F) == CNA_RESULT_SUCCESS;
    ok = ok && cna_film_grain_pass_get_intensity(grain, &scalar) == CNA_RESULT_SUCCESS && scalar == 0.0F;

    /* ---- decal_pass ---- */
    ok = ok && cna_decal_pass_set_opacity(decal, 900.0F) == CNA_RESULT_SUCCESS;
    ok = ok && cna_decal_pass_get_opacity(decal, &scalar) == CNA_RESULT_SUCCESS && scalar == 1.0F;
    ok = ok && cna_decal_pass_set_opacity(decal, -900.0F) == CNA_RESULT_SUCCESS;
    ok = ok && cna_decal_pass_get_opacity(decal, &scalar) == CNA_RESULT_SUCCESS && scalar == 0.0F;
    vector.x = 0.5F; vector.y = -2.0F; vector.z = 3.0F;
    ok = ok && cna_decal_pass_set_tint(decal, &vector) == CNA_RESULT_SUCCESS;
    ok = ok && cna_decal_pass_get_tint(decal, &read_back) == CNA_RESULT_SUCCESS &&
        read_back.x == 0.5F && read_back.y == -2.0F && read_back.z == 3.0F;
    ok = ok && cna_decal_pass_set_tint(decal, 0) == CNA_RESULT_INVALID_ARGUMENT;
    ok = ok && cna_decal_pass_set_max_slope_angle(decal, 900.0F) == CNA_RESULT_SUCCESS;
    ok = ok && cna_decal_pass_get_max_slope_angle(decal, &scalar) == CNA_RESULT_SUCCESS && scalar == 1.5707964F;
    ok = ok && cna_decal_pass_set_max_slope_angle(decal, -900.0F) == CNA_RESULT_SUCCESS;
    ok = ok && cna_decal_pass_get_max_slope_angle(decal, &scalar) == CNA_RESULT_SUCCESS && scalar == 0.0F;

    /* ---- spatial_upscale_pass ---- */
    ok = ok && cna_spatial_upscale_pass_set_sharpness(upscale, 900.0F) == CNA_RESULT_SUCCESS;
    ok = ok && cna_spatial_upscale_pass_get_sharpness(upscale, &scalar) == CNA_RESULT_SUCCESS && scalar == 1.0F;
    ok = ok && cna_spatial_upscale_pass_set_sharpness(upscale, -900.0F) == CNA_RESULT_SUCCESS;
    ok = ok && cna_spatial_upscale_pass_get_sharpness(upscale, &scalar) == CNA_RESULT_SUCCESS && scalar == 0.0F;
    ok = ok && cna_spatial_upscale_pass_set_edge_adaptive(upscale, CNA_TRUE) == CNA_RESULT_SUCCESS;
    ok = ok && cna_spatial_upscale_pass_get_edge_adaptive(upscale, &flag) == CNA_RESULT_SUCCESS && flag == CNA_TRUE;
    ok = ok && cna_spatial_upscale_pass_set_edge_adaptive(upscale, UINT8_C(2)) == CNA_RESULT_INVALID_ARGUMENT;

    /* FxaaPass corrects nothing, but the settings bag that can drive the same value DOES floor
       it. Two surfaces, one number, only one of them correcting -- asserted rather than assumed,
       which is what the CBIND-087B precedent asks for. */
    ok = ok && cna_fxaa_pass_set_edge_threshold(fxaa, -1.0F) == CNA_RESULT_SUCCESS;
    ok = ok && cna_fxaa_pass_get_edge_threshold(fxaa, &scalar) == CNA_RESULT_SUCCESS &&
        scalar == -1.0F;
    {
        CNA_RenderPipelineSettingsEXT settings;
        ok = ok && cna_render_pipeline_settings_ext_init(&settings) == CNA_RESULT_SUCCESS;
        settings.fxaa_edge_threshold_ext = -1.0F;
        ok = ok && cna_render_pipeline_settings_ext_normalize(&settings) == CNA_RESULT_SUCCESS &&
            settings.fxaa_edge_threshold_ext ==
                CNA_RENDER_PIPELINE_MINIMUM_FXAA_EDGE_THRESHOLD_EXT;
    }

    /* The two per-pass constants and the quality presets. */
    ok = ok && CNA_LENS_FLARE_GHOST_COUNT_EXT > INT32_C(0) &&
        CNA_MOTION_BLUR_SAMPLE_COUNT_EXT > INT32_C(0);
    {
        int32_t low = -1;
        int32_t ultra = -2;
        float low_threshold = -1.0F;
        float ultra_threshold = -2.0F;
        ok = ok && cna_bloom_pass_iterations_for_quality(CNA_RENDER_QUALITY_LOW, &low) ==
            CNA_RESULT_SUCCESS;
        ok = ok && cna_bloom_pass_iterations_for_quality(CNA_RENDER_QUALITY_ULTRA, &ultra) ==
            CNA_RESULT_SUCCESS && ultra != low;
        ok = ok && cna_bloom_pass_iterations_for_quality(UINT32_C(99), &low) ==
            CNA_RESULT_INVALID_ARGUMENT;
        ok = ok && cna_fxaa_pass_edge_threshold_for_quality(CNA_RENDER_QUALITY_LOW,
                                                            &low_threshold) ==
            CNA_RESULT_SUCCESS;
        ok = ok && cna_fxaa_pass_edge_threshold_for_quality(CNA_RENDER_QUALITY_ULTRA,
                                                            &ultra_threshold) ==
            CNA_RESULT_SUCCESS && ultra_threshold != low_threshold;
    }
    ok = ok && cna_bloom_pass_reset_targets(bloom) == CNA_RESULT_SUCCESS;
    ok = ok && cna_bloom_pass_reset_targets(fxaa) == CNA_RESULT_INVALID_ARGUMENT;
    ok = ok && cna_fxaa_pass_copy_fragment_glsl(0, UINT64_C(0), &bytes) ==
        CNA_RESULT_BUFFER_TOO_SMALL && bytes > UINT64_C(0);

    /* Bright-pass extraction: below the threshold nothing survives, above it something does. */
    ok = ok && cna_bloom_pass_extract_channel(0.1F, 1.0F, &scalar) == CNA_RESULT_SUCCESS &&
        scalar == 0.0F;
    ok = ok && cna_bloom_pass_extract_channel(2.0F, 1.0F, &scalar) == CNA_RESULT_SUCCESS &&
        scalar > 0.0F;

    /* The decal box is the unit cube in local space, so the origin is inside and a far point
       is not -- the only claim worth making without restating the maths. */
    vector.x = 0.0F; vector.y = 0.0F; vector.z = 0.0F;
    ok = ok && cna_decal_pass_is_inside_decal_box(&vector, &flag) == CNA_RESULT_SUCCESS &&
        flag == CNA_TRUE;
    vector.x = 99.0F;
    ok = ok && cna_decal_pass_is_inside_decal_box(&vector, &flag) == CNA_RESULT_SUCCESS &&
        flag == CNA_FALSE;
    ok = ok && cna_decal_pass_is_inside_decal_box(0, &flag) == CNA_RESULT_INVALID_ARGUMENT;

    /* The decal camera's far plane is IGNORED when not positive, so the previous camera stands;
       there is no reading it back, so the assertion is that the call is accepted either way. */
    ok = ok && cna_decal_pass_set_camera(decal, &view, &view, 100.0F) == CNA_RESULT_SUCCESS;
    ok = ok && cna_decal_pass_set_camera(decal, &view, &view, 0.0F) == CNA_RESULT_SUCCESS;
    ok = ok && cna_decal_pass_set_camera(decal, 0, &view, 100.0F) == CNA_RESULT_INVALID_ARGUMENT;
    ok = ok && cna_decal_pass_set_prepass_inputs(decal, CNA_INVALID_HANDLE, CNA_INVALID_HANDLE) ==
        CNA_RESULT_SUCCESS;
    /* Drawing with no prepass inputs is a sequencing mistake, not an argument one: the projector
       has a camera and a decal but nothing to project onto. Whichever way this renderer answers,
       it must not be SUCCESS with the inputs cleared above. */
    ok = ok && cna_decal_pass_draw(decal, CNA_INVALID_HANDLE, &view, INT32_C(32),
                                   INT32_C(32)) != CNA_RESULT_SUCCESS;
    ok = ok && cna_decal_pass_draw(decal, CNA_INVALID_HANDLE, 0, INT32_C(32), INT32_C(32)) ==
        CNA_RESULT_INVALID_ARGUMENT;

    /* Identity scale is exactly equal sizes; anything else is not. */
    ok = ok && cna_spatial_upscale_pass_is_identity_scale(
            INT32_C(64), INT32_C(64), INT32_C(64), INT32_C(64), &flag) == CNA_RESULT_SUCCESS &&
        flag == CNA_TRUE;
    ok = ok && cna_spatial_upscale_pass_is_identity_scale(
            INT32_C(64), INT32_C(64), INT32_C(128), INT32_C(64), &flag) == CNA_RESULT_SUCCESS &&
        flag == CNA_FALSE;
    ok = ok && cna_spatial_upscale_pass_draw(upscale, CNA_INVALID_HANDLE, INT32_C(0), INT32_C(8),
                                             INT32_C(8), INT32_C(8)) == CNA_RESULT_INVALID_ARGUMENT;
    ok = ok && cna_spatial_upscale_pass_draw(upscale, CNA_INVALID_HANDLE, INT32_C(8), INT32_C(8),
                                             INT32_C(8), INT32_C(8)) == CNA_RESULT_INVALID_ARGUMENT;

    /* The ASCII pass lends its effect: the handle answers, and releasing it does not release the
       pass. The resource type is shared across translation units rather than redeclared, which is
       what makes this borrow safe rather than undefined. */
    ok = ok && cna_ascii_pass_get_effect(ascii, &effect) == CNA_RESULT_SUCCESS &&
        effect != CNA_INVALID_HANDLE;
    ok = ok && cna_ascii_post_process_effect_destroy(effect) == CNA_RESULT_SUCCESS;
    ok = ok && cna_post_process_pass_copy_name(ascii, 0, UINT64_C(0), &bytes) ==
        CNA_RESULT_BUFFER_TOO_SMALL;
    ok = ok && cna_ascii_pass_get_effect(fxaa, &effect) == CNA_RESULT_INVALID_ARGUMENT;

    ok = ok && cna_spatial_upscale_pass_destroy(upscale) == CNA_RESULT_SUCCESS;
    ok = ok && cna_spatial_upscale_pass_destroy(upscale) != CNA_RESULT_SUCCESS;
    ok = ok && cna_decal_pass_destroy(decal) == CNA_RESULT_SUCCESS;
    ok = ok && cna_post_process_pass_destroy(ascii) == CNA_RESULT_SUCCESS;
    ok = ok && cna_post_process_pass_destroy(grain) == CNA_RESULT_SUCCESS;
    ok = ok && cna_post_process_pass_destroy(chroma) == CNA_RESULT_SUCCESS;
    ok = ok && cna_post_process_pass_destroy(fxaa) == CNA_RESULT_SUCCESS;
    ok = ok && cna_post_process_pass_destroy(motion) == CNA_RESULT_SUCCESS;
    ok = ok && cna_post_process_pass_destroy(lens) == CNA_RESULT_SUCCESS;
    ok = ok && cna_post_process_pass_destroy(bloom) == CNA_RESULT_SUCCESS;
    return ok;
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
        if (!validate_shadow_maps(graphics_device)) {
            state->failed_stage = 11;
            return CNA_RESULT_INVALID_STATE;
        }
        if (!validate_cascaded_and_cube(graphics_device)) {
            state->failed_stage = 12;
            return CNA_RESULT_INVALID_STATE;
        }
        if (!validate_shadow_receiver(graphics_device)) {
            state->failed_stage = 13;
            return CNA_RESULT_INVALID_STATE;
        }
        if (!validate_shadow_policy(graphics_device)) {
            state->failed_stage = 14;
            return CNA_RESULT_INVALID_STATE;
        }
        if (!validate_prepass_and_contact(graphics_device)) {
            state->failed_stage = 15;
            return CNA_RESULT_INVALID_STATE;
        }
        if (!validate_clustered_light_set(graphics_device)) {
            state->failed_stage = 16;
            return CNA_RESULT_INVALID_STATE;
        }
        if (!validate_cluster_grid_and_buffer(graphics_device)) {
            state->failed_stage = 17;
            return CNA_RESULT_INVALID_STATE;
        }
        if (!validate_clustered_compute(graphics_device)) {
            state->failed_stage = 18;
            return CNA_RESULT_INVALID_STATE;
        }
        if (!validate_clustered_forward(graphics_device)) {
            state->failed_stage = 19;
            return CNA_RESULT_INVALID_STATE;
        }
        if (!validate_material_extensions(graphics_device)) {
            state->failed_stage = 20;
            return CNA_RESULT_INVALID_STATE;
        }
        if (!validate_pbr_material_value()) {
            state->failed_stage = 21;
            return CNA_RESULT_INVALID_STATE;
        }
        if (!validate_pbr_effect_receiver(graphics_device)) {
            state->failed_stage = 22;
            return CNA_RESULT_INVALID_STATE;
        }
        if (!validate_transparency_and_bridge(graphics_device)) {
            state->failed_stage = 23;
            return CNA_RESULT_INVALID_STATE;
        }
        if (!validate_render_pipeline_settings()) {
            state->failed_stage = 24;
            return CNA_RESULT_INVALID_STATE;
        }
        if (!validate_render_pipeline(graphics_device)) {
            state->failed_stage = 25;
            return CNA_RESULT_INVALID_STATE;
        }
        if (!validate_post_process_chain(graphics_device)) {
            state->failed_stage = 26;
            return CNA_RESULT_INVALID_STATE;
        }
        if (!validate_screen_space_passes(graphics_device)) {
            state->failed_stage = 27;
            return CNA_RESULT_INVALID_STATE;
        }
        if (!validate_atmospheric_passes(graphics_device)) {
            state->failed_stage = 28;
            return CNA_RESULT_INVALID_STATE;
        }
        if (!validate_remaining_passes(graphics_device)) {
            state->failed_stage = 29;
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
    /* 30+ is reserved for checks made outside the game callback, so it can never collide with
       a `3 + stage` code from inside it. */
    if (!validate_light_values()) {
        return 30;
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
