// SPDX-License-Identifier: MS-PL

/*
 * plans/plan_binding.md CBIND-109 -- the `.cnb` model schema.
 *
 * The model is the one asset whose C shape is a decision rather than a transcription: the
 * canonical description is a graph of nested vectors, and this ABI reaches it through a single
 * owned handle whose nodes are addressed by index. So this suite does three things a plain round
 * trip would not.
 *
 * It builds a model with **every level occupied at once** -- bones, parts, a material with both of
 * its texture orderings populated, morph targets with a weight track, meshes, a skeleton with a
 * root prefix, an animation, a light -- and reads all of it back after an encode/decode cycle, so
 * a level dropped by either half is visible.
 *
 * It pins the **two texture orderings apart**. The eight named slots and the seven per-slot arrays
 * are different index spaces, and a binding that confused them would still round trip, because
 * both halves would be wrong together. The test writes distinguishable values into both and reads
 * each back through the other's route to prove the routes do not overlap.
 *
 * And it drives the refusals: an out-of-range index at every level, a stream selector that names
 * nothing, a structure whose version is not this one, a model without a skeleton, and a compiled
 * `.cnj` result whose model has already been taken.
 */

#include <CNA/C/cna.h>

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define REQUIRE(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "CnbModelSmoke failure at line %d: %s\n", __LINE__, #condition); \
        return 0; \
    } \
} while (0)

static CNA_StringView view(const char* const text)
{
    const CNA_StringView result = {text, (uint64_t)strlen(text)};
    return result;
}

/*
 * A failing route says why. An exit code alone leaves the reader of a CI log guessing which of
 * several canonical validations refused, and this family has many of them.
 */
static void report_last_error(const char* const what)
{
    char message[512];
    uint64_t produced = 0U;
    if (cna_error_copy_last_message(message, sizeof(message) - 1U, &produced) ==
        CNA_RESULT_SUCCESS) {
        message[produced] = '\0';
        fprintf(stderr, "%s: %s\n", what, message);
    }
}

static int near_enough(const float actual, const float expected)
{
    return fabsf(actual - expected) < 1.0e-6f;
}

static void identity(float* const matrix, const float diagonal)
{
    memset(matrix, 0, 16U * sizeof(float));
    matrix[0] = diagonal;
    matrix[5] = diagonal;
    matrix[10] = diagonal;
    matrix[15] = 1.0f;
}

/* The identities are wire format: a renumbering must break here, not in a file written months on. */
static int validate_identities(void)
{
    REQUIRE(sizeof(CNA_CnbEffectKind) == sizeof(uint32_t));
    REQUIRE(sizeof(CNA_CnbMaterialTextureSlot) == sizeof(uint32_t));
    REQUIRE(CNA_CNB_EFFECT_KIND_BASIC == UINT32_C(0));
    REQUIRE(CNA_CNB_EFFECT_KIND_EXTERNAL == UINT32_C(5));
    REQUIRE(CNA_CNB_EFFECT_KIND_MAXIMUM == CNA_CNB_EFFECT_KIND_EXTERNAL);
    REQUIRE(CNA_CNB_MATERIAL_TEXTURE_BASE_COLOR == UINT32_C(0));
    REQUIRE(CNA_CNB_MATERIAL_TEXTURE_SPECULAR_COLOR == UINT32_C(7));
    REQUIRE(CNA_CNB_MATERIAL_TEXTURE_MAXIMUM == CNA_CNB_MATERIAL_TEXTURE_SPECULAR_COLOR);
    /* Eight named slots against seven per-slot array entries: the trap this suite exists to pin. */
    REQUIRE(CNA_CNB_TEXTURE_SLOT_COUNT == UINT32_C(7));
    REQUIRE(CNA_CNB_MATERIAL_TEXTURE_MAXIMUM + UINT32_C(1) == UINT32_C(8));
    REQUIRE(CNA_CNB_MODEL_SCHEMA_VERSION == UINT32_C(1));
    REQUIRE(CNA_CNB_MODEL_BONE_STRIDE == UINT32_C(72));
    REQUIRE(CNA_CNB_MODEL_MESH_STRIDE == UINT32_C(16));
    REQUIRE(CNA_CNB_MODEL_PART_STRIDE == UINT32_C(56));
    REQUIRE(CNA_CNB_MODEL_MATERIAL_STRIDE == UINT32_C(368));
    REQUIRE(CNA_CNB_NO_INDEX == UINT32_C(0xFFFFFFFF));
    return 1;
}

static int build_reference_model(CNA_CnbModelDataHandle* const outModel)
{
    CNA_CnbModelDataHandle model = CNA_INVALID_HANDLE;
    REQUIRE(cna_cnb_model_create(&model) == CNA_RESULT_SUCCESS);
    REQUIRE(model != CNA_INVALID_HANDLE);
    REQUIRE(cna_cnb_model_set_flags(model, CNA_TRUE, CNA_TRUE) == CNA_RESULT_SUCCESS);

    float transform[16];
    identity(transform, 1.0f);
    uint64_t root = UINT64_MAX;
    REQUIRE(cna_cnb_model_add_bone(model, view("root"), -1, transform, &root) ==
            CNA_RESULT_SUCCESS);
    REQUIRE(root == 0U);
    identity(transform, 2.0f);
    uint64_t child = UINT64_MAX;
    REQUIRE(cna_cnb_model_add_bone(model, view("child"), 0, transform, &child) ==
            CNA_RESULT_SUCCESS);
    REQUIRE(child == 1U);

    CNA_CnbModelPartInfo part;
    memset(&part, 0, sizeof(part));
    part.struct_size = (uint32_t)sizeof(part);
    part.struct_version = CNA_CNB_MODEL_PART_INFO_STRUCT_VERSION;
    part.vertex_stride = 12U;
    part.vertex_count = 3U;
    part.index_count = 3U;
    part.index_element_size = 2U;
    part.primitive_topology = 4U;
    part.primitive_count = 1U;
    part.effect_kind = CNA_CNB_EFFECT_KIND_PBR;
    part.vertex_color_enabled = CNA_TRUE;
    part.unlit = CNA_FALSE;
    uint64_t partIndex = UINT64_MAX;
    REQUIRE(cna_cnb_model_add_part(model, &part, view("triangle"), view(""), &partIndex) ==
            CNA_RESULT_SUCCESS);
    REQUIRE(partIndex == 0U);

    static const float vertices[9] = {
        0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f};
    static const uint16_t indices[3] = {0U, 1U, 2U};
    REQUIRE(cna_cnb_model_set_part_vertex_bytes(
                model, 0U, (const uint8_t*)vertices, sizeof(vertices)) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_cnb_model_set_part_index_bytes(
                model, 0U, (const uint8_t*)indices, sizeof(indices)) == CNA_RESULT_SUCCESS);

    CNA_CnbMaterialInfo material;
    memset(&material, 0, sizeof(material));
    material.struct_size = (uint32_t)sizeof(material);
    material.struct_version = CNA_CNB_MATERIAL_INFO_STRUCT_VERSION;
    material.base_color_factor[0] = 0.25f;
    material.base_color_factor[1] = 0.5f;
    material.base_color_factor[2] = 0.75f;
    material.base_color_factor[3] = 1.0f;
    material.emissive_factor[0] = 0.125f;
    material.specular_color_factor[0] = 0.875f;
    material.metallic_factor = 0.4f;
    material.roughness_factor = 0.6f;
    material.ior = 1.33f;
    material.specular_factor = 0.9f;
    material.normal_scale = 1.5f;
    material.occlusion_strength = 0.8f;
    material.alpha_cutoff = 0.25f;
    material.alpha_mode = 2U;
    material.double_sided = CNA_TRUE;
    REQUIRE(cna_cnb_model_set_material(model, 0U, &material) == CNA_RESULT_SUCCESS);

    /* Eight named slots, each given its own value so a mix-up cannot round trip. */
    REQUIRE(cna_cnb_model_set_material_texture(
                model, 0U, CNA_CNB_MATERIAL_TEXTURE_BASE_COLOR, view("albedo")) ==
            CNA_RESULT_SUCCESS);
    REQUIRE(cna_cnb_model_set_material_texture(
                model, 0U, CNA_CNB_MATERIAL_TEXTURE_SECOND, view("overlay")) ==
            CNA_RESULT_SUCCESS);
    REQUIRE(cna_cnb_model_set_material_texture(
                model, 0U, CNA_CNB_MATERIAL_TEXTURE_NORMAL, view("bumps")) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_cnb_model_set_material_texture(
                model, 0U, CNA_CNB_MATERIAL_TEXTURE_METALLIC_ROUGHNESS, view("mr")) ==
            CNA_RESULT_SUCCESS);
    REQUIRE(cna_cnb_model_set_material_texture(
                model, 0U, CNA_CNB_MATERIAL_TEXTURE_EMISSIVE, view("glow")) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_cnb_model_set_material_texture(
                model, 0U, CNA_CNB_MATERIAL_TEXTURE_OCCLUSION, view("ao")) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_cnb_model_set_material_texture(
                model, 0U, CNA_CNB_MATERIAL_TEXTURE_SPECULAR, view("spec")) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_cnb_model_set_material_texture(
                model, 0U, CNA_CNB_MATERIAL_TEXTURE_SPECULAR_COLOR, view("spectint")) ==
            CNA_RESULT_SUCCESS);

    /* Seven per-slot entries, in the importer's own order and index space. */
    for (uint64_t slot = 0U; slot < CNA_CNB_TEXTURE_SLOT_COUNT; ++slot) {
        /* 0 or 1 only: CNA's vertex layouts carry two UV sets, and the decoder enforces it. */
        REQUIRE(cna_cnb_model_set_material_texture_coordinate_set(
                    model, 0U, slot, (uint8_t)(slot % 2U)) == CNA_RESULT_SUCCESS);
        CNA_CnbTextureTransform uv;
        uv.offset_x = (float)slot;
        uv.offset_y = (float)slot + 0.5f;
        uv.scale_x = 2.0f;
        uv.scale_y = 3.0f;
        uv.rotation = 0.25f;
        REQUIRE(cna_cnb_model_set_material_texture_transform(model, 0U, slot, &uv) ==
                CNA_RESULT_SUCCESS);
        CNA_CnbSamplerState sampler;
        memset(&sampler, 0, sizeof(sampler));
        sampler.filter = (uint32_t)slot;
        sampler.address_u = 1U;
        sampler.address_v = 2U;
        sampler.declared = CNA_TRUE;
        REQUIRE(cna_cnb_model_set_material_sampler(model, 0U, slot, &sampler) ==
                CNA_RESULT_SUCCESS);
    }

    CNA_CnbMorphInfo morph;
    memset(&morph, 0, sizeof(morph));
    morph.struct_size = (uint32_t)sizeof(morph);
    morph.struct_version = CNA_CNB_MORPH_INFO_STRUCT_VERSION;
    morph.vertex_count = 3U;
    morph.recompute_flat_normals = CNA_TRUE;
    morph.weight_track_step_interpolation = CNA_TRUE;
    REQUIRE(cna_cnb_model_set_morph(model, 0U, &morph) == CNA_RESULT_SUCCESS);
    uint64_t target = UINT64_MAX;
    REQUIRE(cna_cnb_model_add_morph_target(model, 0U, &target) == CNA_RESULT_SUCCESS);
    REQUIRE(target == 0U);
    static const float positionDeltas[9] = {
        0.1f, 0.0f, 0.0f, 0.0f, 0.2f, 0.0f, 0.0f, 0.0f, 0.3f};
    REQUIRE(cna_cnb_model_set_morph_target_deltas(
                model, 0U, 0U, CNA_CNB_MORPH_DELTA_POSITION, positionDeltas, 9U) ==
            CNA_RESULT_SUCCESS);
    static const float weights[1] = {0.5f};
    REQUIRE(cna_cnb_model_set_morph_weights(model, 0U, weights, 1U) == CNA_RESULT_SUCCESS);
    static const float keyWeights[1] = {0.75f};
    uint64_t key = UINT64_MAX;
    REQUIRE(cna_cnb_model_add_morph_weight_key(
                model, 0U, 1.5, keyWeights, 1U, NULL, 0U, NULL, 0U, &key) == CNA_RESULT_SUCCESS);
    REQUIRE(key == 0U);

    static const uint32_t meshParts[1] = {0U};
    uint64_t mesh = UINT64_MAX;
    REQUIRE(cna_cnb_model_add_mesh(model, view("body"), 1, meshParts, 1U, &mesh) ==
            CNA_RESULT_SUCCESS);
    REQUIRE(mesh == 0U);

    static const int32_t hierarchy[2] = {-1, 0};
    float bind[32];
    float inverse[32];
    float prefix[32];
    for (int joint = 0; joint < 2; ++joint) {
        identity(bind + (joint * 16), 1.0f + (float)joint);
        identity(inverse + (joint * 16), 3.0f + (float)joint);
        identity(prefix + (joint * 16), 5.0f + (float)joint);
    }
    REQUIRE(cna_cnb_model_set_skeleton(model, hierarchy, 2U, bind, inverse, prefix) ==
            CNA_RESULT_SUCCESS);

    CNA_KeyframeEXT keys[2];
    memset(keys, 0, sizeof(keys));
    keys[0].time_seconds = 0.0;
    keys[0].rotation.w = 1.0f;
    keys[0].scale.x = 1.0f;
    keys[0].scale.y = 1.0f;
    keys[0].scale.z = 1.0f;
    keys[1] = keys[0];
    keys[1].time_seconds = 2.0;
    keys[1].translation.x = 4.0f;
    CNA_BoneTrackEXTDescriptor track;
    memset(&track, 0, sizeof(track));
    track.bone_index = 1;
    track.keyframes = keys;
    track.keyframe_count = 2U;
    CNA_AnimationClipEXTDescriptor clip;
    memset(&clip, 0, sizeof(clip));
    clip.duration_seconds = 2.0;
    clip.tracks = &track;
    clip.track_count = 1U;
    uint64_t animation = UINT64_MAX;
    REQUIRE(cna_cnb_model_add_animation(
                model, view("walk"), &clip, CNA_CLIP_TARGET_SPACE_SCENE_NODE_EXT, &animation) ==
            CNA_RESULT_SUCCESS);
    REQUIRE(animation == 0U);

    CNA_CnbModelLight light;
    light.direction[0] = 0.0f;
    light.direction[1] = -1.0f;
    light.direction[2] = 0.0f;
    light.diffuse_color[0] = 1.0f;
    light.diffuse_color[1] = 0.5f;
    light.diffuse_color[2] = 0.25f;
    uint64_t lightIndex = UINT64_MAX;
    REQUIRE(cna_cnb_model_add_light(model, &light, &lightIndex) == CNA_RESULT_SUCCESS);
    REQUIRE(lightIndex == 0U);

    *outModel = model;
    return 1;
}

static int expect_reference_model(const CNA_CnbModelDataHandle model)
{
    CNA_CnbModelInfo info;
    memset(&info, 0, sizeof(info));
    info.struct_size = (uint32_t)sizeof(info);
    info.struct_version = CNA_CNB_MODEL_INFO_STRUCT_VERSION;
    REQUIRE(cna_cnb_model_get_info(model, &info) == CNA_RESULT_SUCCESS);
    REQUIRE(info.bone_count == 2U);
    REQUIRE(info.part_count == 1U);
    REQUIRE(info.mesh_count == 1U);
    REQUIRE(info.animation_count == 1U);
    REQUIRE(info.light_count == 1U);
    REQUIRE(info.has_skeleton == CNA_TRUE);
    REQUIRE(info.applies_gltf_lighting_policy == CNA_TRUE);
    REQUIRE(info.has_bone_hierarchy == CNA_TRUE);

    char text[64];
    uint64_t produced = 0U;
    CNA_CnbModelBone bone;
    memset(&bone, 0, sizeof(bone));
    bone.struct_size = (uint32_t)sizeof(bone);
    bone.struct_version = CNA_CNB_MODEL_BONE_STRUCT_VERSION;
    REQUIRE(cna_cnb_model_get_bone(model, 1U, &bone) == CNA_RESULT_SUCCESS);
    REQUIRE(bone.parent == 0);
    REQUIRE(near_enough(bone.transform[0], 2.0f));
    REQUIRE(cna_cnb_model_get_bone_name_size(model, 1U, &produced) == CNA_RESULT_SUCCESS);
    REQUIRE(produced == 5U);
    REQUIRE(cna_cnb_model_copy_bone_name(model, 1U, text, sizeof(text), &produced) ==
            CNA_RESULT_SUCCESS);
    REQUIRE(produced == 5U && memcmp(text, "child", 5U) == 0);

    CNA_CnbModelPartInfo part;
    memset(&part, 0, sizeof(part));
    part.struct_size = (uint32_t)sizeof(part);
    part.struct_version = CNA_CNB_MODEL_PART_INFO_STRUCT_VERSION;
    REQUIRE(cna_cnb_model_get_part(model, 0U, &part) == CNA_RESULT_SUCCESS);
    REQUIRE(part.vertex_stride == 12U && part.vertex_count == 3U);
    REQUIRE(part.index_count == 3U && part.index_element_size == 2U);
    REQUIRE(part.primitive_topology == 4U && part.primitive_count == 1U);
    REQUIRE(part.effect_kind == CNA_CNB_EFFECT_KIND_PBR);
    REQUIRE(part.vertex_color_enabled == CNA_TRUE && part.unlit == CNA_FALSE);
    REQUIRE(cna_cnb_model_copy_part_name(model, 0U, text, sizeof(text), &produced) ==
            CNA_RESULT_SUCCESS);
    REQUIRE(produced == 8U && memcmp(text, "triangle", 8U) == 0);
    REQUIRE(cna_cnb_model_get_part_external_effect_size(model, 0U, &produced) ==
            CNA_RESULT_SUCCESS);
    REQUIRE(produced == 0U);

    uint8_t bytes[64];
    REQUIRE(cna_cnb_model_copy_part_vertex_bytes(model, 0U, bytes, sizeof(bytes), &produced) ==
            CNA_RESULT_SUCCESS);
    REQUIRE(produced == 36U);
    REQUIRE(cna_cnb_model_copy_part_index_bytes(model, 0U, bytes, sizeof(bytes), &produced) ==
            CNA_RESULT_SUCCESS);
    REQUIRE(produced == 6U);
    {
        uint16_t readIndices[3];
        memcpy(readIndices, bytes, sizeof(readIndices));
        REQUIRE(readIndices[0] == 0U && readIndices[1] == 1U && readIndices[2] == 2U);
    }

    CNA_CnbMaterialInfo material;
    memset(&material, 0, sizeof(material));
    material.struct_size = (uint32_t)sizeof(material);
    material.struct_version = CNA_CNB_MATERIAL_INFO_STRUCT_VERSION;
    REQUIRE(cna_cnb_model_get_material(model, 0U, &material) == CNA_RESULT_SUCCESS);
    REQUIRE(near_enough(material.base_color_factor[2], 0.75f));
    REQUIRE(near_enough(material.emissive_factor[0], 0.125f));
    REQUIRE(near_enough(material.specular_color_factor[0], 0.875f));
    REQUIRE(near_enough(material.metallic_factor, 0.4f));
    REQUIRE(near_enough(material.roughness_factor, 0.6f));
    REQUIRE(near_enough(material.ior, 1.33f));
    REQUIRE(near_enough(material.specular_factor, 0.9f));
    REQUIRE(near_enough(material.normal_scale, 1.5f));
    REQUIRE(near_enough(material.occlusion_strength, 0.8f));
    REQUIRE(near_enough(material.alpha_cutoff, 0.25f));
    REQUIRE(material.alpha_mode == 2U && material.double_sided == CNA_TRUE);

    {
        static const char* const expected[8] = {
            "albedo", "overlay", "bumps", "mr", "glow", "ao", "spec", "spectint"};
        for (uint32_t slot = 0U; slot <= CNA_CNB_MATERIAL_TEXTURE_MAXIMUM; ++slot) {
            const uint64_t length = (uint64_t)strlen(expected[slot]);
            REQUIRE(cna_cnb_model_get_material_texture_size(model, 0U, slot, &produced) ==
                    CNA_RESULT_SUCCESS);
            REQUIRE(produced == length);
            REQUIRE(cna_cnb_model_copy_material_texture(
                        model, 0U, slot, text, sizeof(text), &produced) == CNA_RESULT_SUCCESS);
            REQUIRE(produced == length && memcmp(text, expected[slot], (size_t)length) == 0);
        }
    }

    for (uint64_t slot = 0U; slot < CNA_CNB_TEXTURE_SLOT_COUNT; ++slot) {
        uint8_t coordinateSet = 0U;
        REQUIRE(cna_cnb_model_get_material_texture_coordinate_set(
                    model, 0U, slot, &coordinateSet) == CNA_RESULT_SUCCESS);
        REQUIRE(coordinateSet == (uint8_t)(slot % 2U));
        CNA_CnbTextureTransform uv;
        REQUIRE(cna_cnb_model_get_material_texture_transform(model, 0U, slot, &uv) ==
                CNA_RESULT_SUCCESS);
        REQUIRE(near_enough(uv.offset_x, (float)slot));
        REQUIRE(near_enough(uv.offset_y, (float)slot + 0.5f));
        REQUIRE(near_enough(uv.scale_x, 2.0f) && near_enough(uv.scale_y, 3.0f));
        REQUIRE(near_enough(uv.rotation, 0.25f));
        CNA_CnbSamplerState sampler;
        REQUIRE(cna_cnb_model_get_material_sampler(model, 0U, slot, &sampler) ==
                CNA_RESULT_SUCCESS);
        REQUIRE(sampler.filter == (uint32_t)slot);
        REQUIRE(sampler.address_u == 1U && sampler.address_v == 2U);
        REQUIRE(sampler.declared == CNA_TRUE);
    }

    CNA_Bool hasMorph = CNA_FALSE;
    REQUIRE(cna_cnb_model_has_morph(model, 0U, &hasMorph) == CNA_RESULT_SUCCESS);
    REQUIRE(hasMorph == CNA_TRUE);
    CNA_CnbMorphInfo morph;
    memset(&morph, 0, sizeof(morph));
    morph.struct_size = (uint32_t)sizeof(morph);
    morph.struct_version = CNA_CNB_MORPH_INFO_STRUCT_VERSION;
    REQUIRE(cna_cnb_model_get_morph(model, 0U, &morph) == CNA_RESULT_SUCCESS);
    REQUIRE(morph.vertex_count == 3U && morph.target_count == 1U);
    REQUIRE(morph.weight_count == 1U && morph.weight_track_key_count == 1U);
    REQUIRE(morph.recompute_flat_normals == CNA_TRUE);
    REQUIRE(morph.weight_track_step_interpolation == CNA_TRUE);
    REQUIRE(morph.weight_track_cubic_spline == CNA_FALSE);

    float floats[64];
    REQUIRE(cna_cnb_model_copy_morph_target_deltas(
                model, 0U, 0U, CNA_CNB_MORPH_DELTA_POSITION, floats, 64U, &produced) ==
            CNA_RESULT_SUCCESS);
    REQUIRE(produced == 9U && near_enough(floats[0], 0.1f) && near_enough(floats[8], 0.3f));
    REQUIRE(cna_cnb_model_copy_morph_target_deltas(
                model, 0U, 0U, CNA_CNB_MORPH_DELTA_NORMAL, floats, 64U, &produced) ==
            CNA_RESULT_SUCCESS);
    REQUIRE(produced == 0U);
    REQUIRE(cna_cnb_model_copy_morph_weights(model, 0U, floats, 64U, &produced) ==
            CNA_RESULT_SUCCESS);
    REQUIRE(produced == 1U && near_enough(floats[0], 0.5f));
    CNA_CnbMorphWeightKeyInfo weightKey;
    memset(&weightKey, 0, sizeof(weightKey));
    weightKey.struct_size = (uint32_t)sizeof(weightKey);
    weightKey.struct_version = CNA_CNB_MORPH_WEIGHT_KEY_INFO_STRUCT_VERSION;
    REQUIRE(cna_cnb_model_get_morph_weight_key(model, 0U, 0U, &weightKey) == CNA_RESULT_SUCCESS);
    REQUIRE(weightKey.time_seconds == 1.5);
    REQUIRE(weightKey.weight_count == 1U);
    REQUIRE(weightKey.in_tangent_count == 0U && weightKey.out_tangent_count == 0U);
    REQUIRE(cna_cnb_model_copy_morph_weight_key_values(
                model, 0U, 0U, CNA_CNB_MORPH_KEY_WEIGHTS, floats, 64U, &produced) ==
            CNA_RESULT_SUCCESS);
    REQUIRE(produced == 1U && near_enough(floats[0], 0.75f));

    CNA_CnbMeshInfo meshInfo;
    memset(&meshInfo, 0, sizeof(meshInfo));
    meshInfo.struct_size = (uint32_t)sizeof(meshInfo);
    meshInfo.struct_version = CNA_CNB_MESH_INFO_STRUCT_VERSION;
    REQUIRE(cna_cnb_model_get_mesh(model, 0U, &meshInfo) == CNA_RESULT_SUCCESS);
    REQUIRE(meshInfo.parent_bone == 1 && meshInfo.part_index_count == 1U);
    REQUIRE(cna_cnb_model_get_mesh_name_size(model, 0U, &produced) == CNA_RESULT_SUCCESS);
    REQUIRE(produced == 4U);
    REQUIRE(cna_cnb_model_copy_mesh_name(model, 0U, text, sizeof(text), &produced) ==
            CNA_RESULT_SUCCESS);
    REQUIRE(produced == 4U && memcmp(text, "body", 4U) == 0);
    {
        uint32_t partIndices[4];
        REQUIRE(cna_cnb_model_copy_mesh_part_indices(model, 0U, partIndices, 4U, &produced) ==
                CNA_RESULT_SUCCESS);
        REQUIRE(produced == 1U && partIndices[0] == 0U);
    }

    CNA_CnbSkeletonInfo skeleton;
    memset(&skeleton, 0, sizeof(skeleton));
    skeleton.struct_size = (uint32_t)sizeof(skeleton);
    skeleton.struct_version = CNA_CNB_SKELETON_INFO_STRUCT_VERSION;
    REQUIRE(cna_cnb_model_get_skeleton(model, &skeleton) == CNA_RESULT_SUCCESS);
    REQUIRE(skeleton.joint_count == 2U && skeleton.has_root_prefix == CNA_TRUE);
    {
        int32_t parents[4];
        REQUIRE(cna_cnb_model_copy_skeleton_hierarchy(model, parents, 4U, &produced) ==
                CNA_RESULT_SUCCESS);
        REQUIRE(produced == 2U && parents[0] == -1 && parents[1] == 0);
    }
    REQUIRE(cna_cnb_model_copy_skeleton_matrices(
                model, CNA_CNB_SKELETON_MATRIX_BIND_POSE, floats, 64U, &produced) ==
            CNA_RESULT_SUCCESS);
    REQUIRE(produced == 32U && near_enough(floats[0], 1.0f) && near_enough(floats[16], 2.0f));
    REQUIRE(cna_cnb_model_copy_skeleton_matrices(
                model, CNA_CNB_SKELETON_MATRIX_INVERSE_BIND_POSE, floats, 64U, &produced) ==
            CNA_RESULT_SUCCESS);
    REQUIRE(produced == 32U && near_enough(floats[0], 3.0f) && near_enough(floats[16], 4.0f));
    REQUIRE(cna_cnb_model_copy_skeleton_matrices(
                model, CNA_CNB_SKELETON_MATRIX_ROOT_PREFIX, floats, 64U, &produced) ==
            CNA_RESULT_SUCCESS);
    REQUIRE(produced == 32U && near_enough(floats[0], 5.0f) && near_enough(floats[16], 6.0f));

    double duration = 0.0;
    uint64_t trackCount = 0U;
    CNA_ClipTargetSpaceEXT targetSpace = UINT32_MAX;
    REQUIRE(cna_cnb_model_get_animation(model, 0U, &duration, &trackCount, &targetSpace) ==
            CNA_RESULT_SUCCESS);
    REQUIRE(duration == 2.0 && trackCount == 1U);
    REQUIRE(targetSpace == CNA_CLIP_TARGET_SPACE_SCENE_NODE_EXT);
    REQUIRE(cna_cnb_model_get_animation_name_size(model, 0U, &produced) == CNA_RESULT_SUCCESS);
    REQUIRE(produced == 4U);
    REQUIRE(cna_cnb_model_copy_animation_name(model, 0U, text, sizeof(text), &produced) ==
            CNA_RESULT_SUCCESS);
    REQUIRE(produced == 4U && memcmp(text, "walk", 4U) == 0);
    {
        int32_t boneIndex = 0;
        uint64_t keyframeCount = 0U;
        REQUIRE(cna_cnb_model_get_animation_track(model, 0U, 0U, &boneIndex, &keyframeCount) ==
                CNA_RESULT_SUCCESS);
        REQUIRE(boneIndex == 1 && keyframeCount == 2U);
        CNA_KeyframeEXT readKeys[4];
        REQUIRE(cna_cnb_model_copy_animation_keyframes(model, 0U, 0U, readKeys, 4U, &produced) ==
                CNA_RESULT_SUCCESS);
        REQUIRE(produced == 2U);
        REQUIRE(readKeys[0].time_seconds == 0.0 && readKeys[1].time_seconds == 2.0);
        REQUIRE(near_enough(readKeys[1].translation.x, 4.0f));
        REQUIRE(near_enough(readKeys[1].rotation.w, 1.0f));
        REQUIRE(near_enough(readKeys[1].scale.y, 1.0f));
    }

    CNA_CnbModelLight light;
    memset(&light, 0, sizeof(light));
    REQUIRE(cna_cnb_model_get_light(model, 0U, &light) == CNA_RESULT_SUCCESS);
    REQUIRE(near_enough(light.direction[1], -1.0f));
    REQUIRE(near_enough(light.diffuse_color[1], 0.5f));
    return 1;
}

/* The whole graph, out through the encoder and back in through the decoder. */
static int validate_round_trip(void)
{
    CNA_CnbModelDataHandle model = CNA_INVALID_HANDLE;
    if (!build_reference_model(&model)) { return 0; }
    if (!expect_reference_model(model)) { return 0; }

    uint64_t required = 0U;
    REQUIRE(cna_cnb_encode_model(model, view("Models/Hero"), NULL, 0U, &required) ==
            CNA_RESULT_BUFFER_TOO_SMALL);
    REQUIRE(required > 0U);
    static uint8_t encoded[65536];
    REQUIRE(required <= sizeof(encoded));
    uint64_t produced = 0U;
    {
        const CNA_Result encoding =
            cna_cnb_encode_model(model, view("Models/Hero"), encoded, sizeof(encoded), &produced);
        if (encoding != CNA_RESULT_SUCCESS) { report_last_error("cna_cnb_encode_model"); }
        REQUIRE(encoding == CNA_RESULT_SUCCESS);
    }
    REQUIRE(produced == required);

    CNA_CnbDocumentHandle document = CNA_INVALID_HANDLE;
    REQUIRE(cna_cnb_document_parse(encoded, produced, view("model"), NULL, &document) ==
            CNA_RESULT_SUCCESS);
    uint32_t assetType = 0U;
    REQUIRE(cna_cnb_document_get_asset_type_id(document, &assetType) == CNA_RESULT_SUCCESS);
    REQUIRE(assetType == CNA_CNB_ASSET_TYPE_MODEL);
    uint32_t schema = 0U;
    REQUIRE(cna_cnb_document_get_asset_schema_version(document, &schema) == CNA_RESULT_SUCCESS);
    REQUIRE(schema == CNA_CNB_MODEL_SCHEMA_VERSION);
    /* The chunk ids this header names are the ones the encoder actually wrote. */
    {
        static const CNA_CnbChunkId written[3] = {
            CNA_CNB_MODEL_CHUNK_HEADER,
            CNA_CNB_MODEL_CHUNK_SKELETON,
            CNA_CNB_MODEL_CHUNK_LIGHTS};
        for (size_t which = 0U; which < 3U; ++which) {
            CNA_Bool found = CNA_FALSE;
            uint64_t at = 0U;
            REQUIRE(cna_cnb_document_find_single(document, written[which], &found, &at) ==
                    CNA_RESULT_SUCCESS);
            REQUIRE(found == CNA_TRUE);
        }
    }

    CNA_CnbModelDataHandle decoded = CNA_INVALID_HANDLE;
    {
        const CNA_Result decoding = cna_cnb_decode_model(document, &decoded);
        if (decoding != CNA_RESULT_SUCCESS) { report_last_error("cna_cnb_decode_model"); }
        REQUIRE(decoding == CNA_RESULT_SUCCESS);
    }
    REQUIRE(decoded != CNA_INVALID_HANDLE);
    REQUIRE(decoded != CNA_INVALID_HANDLE);
    if (!expect_reference_model(decoded)) { return 0; }

    REQUIRE(cna_cnb_model_destroy(decoded) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_cnb_document_destroy(document) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_cnb_model_destroy(model) == CNA_RESULT_SUCCESS);
    return 1;
}

static int validate_mutation(void)
{
    CNA_CnbModelDataHandle model = CNA_INVALID_HANDLE;
    REQUIRE(cna_cnb_model_create(&model) == CNA_RESULT_SUCCESS);

    CNA_CnbModelPartInfo part;
    memset(&part, 0, sizeof(part));
    part.struct_size = (uint32_t)sizeof(part);
    part.struct_version = CNA_CNB_MODEL_PART_INFO_STRUCT_VERSION;
    part.index_element_size = 2U;
    part.primitive_topology = 4U;
    REQUIRE(cna_cnb_model_add_part(model, &part, view("p"), view("Effects/Water"), NULL) ==
            CNA_RESULT_SUCCESS);

    /* set_part replaces the numbers and leaves the names, bytes and material where they were. */
    part.vertex_stride = 32U;
    part.effect_kind = CNA_CNB_EFFECT_KIND_EXTERNAL;
    REQUIRE(cna_cnb_model_set_part(model, 0U, &part) == CNA_RESULT_SUCCESS);
    memset(&part, 0, sizeof(part));
    part.struct_size = (uint32_t)sizeof(part);
    part.struct_version = CNA_CNB_MODEL_PART_INFO_STRUCT_VERSION;
    REQUIRE(cna_cnb_model_get_part(model, 0U, &part) == CNA_RESULT_SUCCESS);
    REQUIRE(part.vertex_stride == 32U && part.effect_kind == CNA_CNB_EFFECT_KIND_EXTERNAL);
    char text[32];
    uint64_t produced = 0U;
    REQUIRE(cna_cnb_model_copy_part_external_effect(model, 0U, text, sizeof(text), &produced) ==
            CNA_RESULT_SUCCESS);
    REQUIRE(produced == 13U && memcmp(text, "Effects/Water", 13U) == 0);

    /* Morph data comes and goes; clearing it makes every morph route refuse again. */
    CNA_Bool present = CNA_TRUE;
    REQUIRE(cna_cnb_model_has_morph(model, 0U, &present) == CNA_RESULT_SUCCESS);
    REQUIRE(present == CNA_FALSE);
    REQUIRE(cna_cnb_model_add_morph_target(model, 0U, NULL) == CNA_RESULT_INVALID_ARGUMENT);
    CNA_CnbMorphInfo morph;
    memset(&morph, 0, sizeof(morph));
    morph.struct_size = (uint32_t)sizeof(morph);
    morph.struct_version = CNA_CNB_MORPH_INFO_STRUCT_VERSION;
    REQUIRE(cna_cnb_model_set_morph(model, 0U, &morph) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_cnb_model_has_morph(model, 0U, &present) == CNA_RESULT_SUCCESS);
    REQUIRE(present == CNA_TRUE);
    REQUIRE(cna_cnb_model_add_morph_target(model, 0U, NULL) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_cnb_model_clear_morph(model, 0U) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_cnb_model_has_morph(model, 0U, &present) == CNA_RESULT_SUCCESS);
    REQUIRE(present == CNA_FALSE);
    /* Clearing what is already absent is still a success, not a second kind of answer. */
    REQUIRE(cna_cnb_model_clear_morph(model, 0U) == CNA_RESULT_SUCCESS);

    /* So does the skeleton, and its absence is what get_info reports rather than a failed read. */
    static const int32_t hierarchy[1] = {-1};
    float pose[16];
    identity(pose, 1.0f);
    REQUIRE(cna_cnb_model_set_skeleton(model, hierarchy, 1U, pose, pose, NULL) ==
            CNA_RESULT_SUCCESS);
    CNA_CnbSkeletonInfo skeleton;
    memset(&skeleton, 0, sizeof(skeleton));
    skeleton.struct_size = (uint32_t)sizeof(skeleton);
    skeleton.struct_version = CNA_CNB_SKELETON_INFO_STRUCT_VERSION;
    REQUIRE(cna_cnb_model_get_skeleton(model, &skeleton) == CNA_RESULT_SUCCESS);
    REQUIRE(skeleton.joint_count == 1U && skeleton.has_root_prefix == CNA_FALSE);
    REQUIRE(cna_cnb_model_clear_skeleton(model) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_cnb_model_get_skeleton(model, &skeleton) == CNA_RESULT_INVALID_ARGUMENT);
    REQUIRE(cna_cnb_model_clear_skeleton(model) == CNA_RESULT_SUCCESS);

    REQUIRE(cna_cnb_model_destroy(model) == CNA_RESULT_SUCCESS);
    return 1;
}

static int validate_refusals(void)
{
    CNA_CnbModelDataHandle model = CNA_INVALID_HANDLE;
    if (!build_reference_model(&model)) { return 0; }

    uint64_t produced = 0U;
    char text[16];
    /* An out-of-range index, at each level that has one. */
    CNA_CnbModelBone bone;
    memset(&bone, 0, sizeof(bone));
    bone.struct_size = (uint32_t)sizeof(bone);
    bone.struct_version = CNA_CNB_MODEL_BONE_STRUCT_VERSION;
    REQUIRE(cna_cnb_model_get_bone(model, 2U, &bone) == CNA_RESULT_INVALID_ARGUMENT);
    REQUIRE(cna_cnb_model_get_bone_name_size(model, 2U, &produced) == CNA_RESULT_INVALID_ARGUMENT);
    REQUIRE(cna_cnb_model_get_part_name_size(model, 1U, &produced) == CNA_RESULT_INVALID_ARGUMENT);
    REQUIRE(cna_cnb_model_get_mesh_name_size(model, 1U, &produced) == CNA_RESULT_INVALID_ARGUMENT);
    REQUIRE(cna_cnb_model_get_animation_name_size(model, 1U, &produced) ==
            CNA_RESULT_INVALID_ARGUMENT);
    REQUIRE(cna_cnb_model_get_animation_track(model, 0U, 1U, NULL, NULL) ==
            CNA_RESULT_INVALID_ARGUMENT);
    REQUIRE(cna_cnb_model_get_light(model, 1U, NULL) == CNA_RESULT_INVALID_ARGUMENT);
    REQUIRE(cna_cnb_model_set_material_texture_coordinate_set(
                model, 0U, CNA_CNB_TEXTURE_SLOT_COUNT, 0U) == CNA_RESULT_INVALID_ARGUMENT);

    /* A selector that names nothing, in each family that takes one. */
    REQUIRE(cna_cnb_model_get_material_texture_size(
                model, 0U, CNA_CNB_MATERIAL_TEXTURE_MAXIMUM + 1U, &produced) ==
            CNA_RESULT_INVALID_ARGUMENT);
    REQUIRE(cna_cnb_model_copy_morph_target_deltas(
                model, 0U, 0U, CNA_CNB_MORPH_DELTA_MAXIMUM + 1U, NULL, 0U, &produced) ==
            CNA_RESULT_INVALID_ARGUMENT);
    REQUIRE(cna_cnb_model_copy_morph_weight_key_values(
                model, 0U, 0U, CNA_CNB_MORPH_KEY_MAXIMUM + 1U, NULL, 0U, &produced) ==
            CNA_RESULT_INVALID_ARGUMENT);
    REQUIRE(cna_cnb_model_copy_skeleton_matrices(
                model, CNA_CNB_SKELETON_MATRIX_MAXIMUM + 1U, NULL, 0U, &produced) ==
            CNA_RESULT_INVALID_ARGUMENT);

    /* A structure whose version is not this one, in both directions. */
    {
        CNA_CnbModelInfo info;
        memset(&info, 0, sizeof(info));
        info.struct_size = (uint32_t)sizeof(info);
        info.struct_version = CNA_CNB_MODEL_INFO_STRUCT_VERSION + 1U;
        REQUIRE(cna_cnb_model_get_info(model, &info) == CNA_RESULT_INVALID_ARGUMENT);
        info.struct_version = CNA_CNB_MODEL_INFO_STRUCT_VERSION;
        info.struct_size = (uint32_t)sizeof(info) - 1U;
        REQUIRE(cna_cnb_model_get_info(model, &info) == CNA_RESULT_INVALID_ARGUMENT);
        info.struct_size = (uint32_t)sizeof(info);
        REQUIRE(cna_cnb_model_get_info(model, &info) == CNA_RESULT_SUCCESS);
    }
    {
        CNA_CnbModelPartInfo bad;
        memset(&bad, 0, sizeof(bad));
        bad.struct_size = (uint32_t)sizeof(bad);
        bad.struct_version = CNA_CNB_MODEL_PART_INFO_STRUCT_VERSION + 1U;
        REQUIRE(cna_cnb_model_add_part(model, &bad, view(""), view(""), NULL) ==
                CNA_RESULT_INVALID_ARGUMENT);
    }

    /* An effect kind above the highest this ABI names is refused, not stored. */
    {
        CNA_CnbModelPartInfo bad;
        memset(&bad, 0, sizeof(bad));
        bad.struct_size = (uint32_t)sizeof(bad);
        bad.struct_version = CNA_CNB_MODEL_PART_INFO_STRUCT_VERSION;
        bad.effect_kind = CNA_CNB_EFFECT_KIND_MAXIMUM + 1U;
        REQUIRE(cna_cnb_model_add_part(model, &bad, view(""), view(""), NULL) ==
                CNA_RESULT_INVALID_ARGUMENT);
        REQUIRE(cna_cnb_model_set_part(model, 0U, &bad) == CNA_RESULT_INVALID_ARGUMENT);
    }

    /* A clip target space that is not one of the two. */
    {
        CNA_AnimationClipEXTDescriptor clip;
        memset(&clip, 0, sizeof(clip));
        REQUIRE(cna_cnb_model_add_animation(
                    model, view("x"), &clip, CNA_CLIP_TARGET_SPACE_MAXIMUM_EXT + 1U, NULL) ==
                CNA_RESULT_INVALID_ARGUMENT);
        clip.duration_seconds = 1.0 / 0.0;
        REQUIRE(cna_cnb_model_add_animation(
                    model, view("x"), &clip, CNA_CLIP_TARGET_SPACE_JOINT_PALETTE_EXT, NULL) ==
                CNA_RESULT_INVALID_ARGUMENT);
        REQUIRE(cna_cnb_model_add_animation(
                    model, view("x"), NULL, CNA_CLIP_TARGET_SPACE_JOINT_PALETTE_EXT, NULL) ==
                CNA_RESULT_INVALID_ARGUMENT);
    }

    /* Pose arrays that disagree with the joint count. */
    {
        static const int32_t hierarchy[2] = {-1, 0};
        float pose[32];
        identity(pose, 1.0f);
        identity(pose + 16, 1.0f);
        REQUIRE(cna_cnb_model_set_skeleton(model, hierarchy, 2U, NULL, pose, NULL) ==
                CNA_RESULT_INVALID_ARGUMENT);
        REQUIRE(cna_cnb_model_set_skeleton(model, NULL, 2U, pose, pose, NULL) ==
                CNA_RESULT_INVALID_ARGUMENT);
    }

    /* A short destination writes nothing and still reports what was needed. */
    memset(text, 0x5A, sizeof(text));
    REQUIRE(cna_cnb_model_copy_bone_name(model, 1U, text, 2U, &produced) ==
            CNA_RESULT_BUFFER_TOO_SMALL);
    REQUIRE(produced == 5U);
    REQUIRE(text[0] == 0x5A && text[1] == 0x5A);

    /* An encode into a short destination behaves the same way. */
    {
        uint8_t small[4];
        memset(small, 0x5A, sizeof(small));
        REQUIRE(cna_cnb_encode_model(model, view(""), small, sizeof(small), &produced) ==
                CNA_RESULT_BUFFER_TOO_SMALL);
        REQUIRE(produced > sizeof(small));
        REQUIRE(small[0] == 0x5A);
    }

    /* A non-canonical CNA_Bool is refused before the handle is even looked at. */
    REQUIRE(cna_cnb_model_set_flags(model, (CNA_Bool)9, CNA_FALSE) ==
            CNA_RESULT_INVALID_ARGUMENT);
    REQUIRE(cna_cnb_model_set_flags(CNA_INVALID_HANDLE, (CNA_Bool)9, CNA_FALSE) ==
            CNA_RESULT_INVALID_ARGUMENT);

    /* An invalid handle is refused by every family, including after its own destroy. */
    REQUIRE(cna_cnb_model_destroy(model) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_cnb_model_get_bone_name_size(model, 0U, &produced) ==
            CNA_RESULT_INVALID_HANDLE);
    REQUIRE(cna_cnb_model_destroy(model) == CNA_RESULT_INVALID_HANDLE);
    REQUIRE(cna_cnb_model_create(NULL) == CNA_RESULT_INVALID_ARGUMENT);
    return 1;
}

/* A document that is not a model is refused by the model decoder, the way the textures are. */
static int validate_wrong_asset_type(void)
{
    CNA_CnbWriterHandle writer = CNA_INVALID_HANDLE;
    REQUIRE(cna_cnb_writer_create(CNA_CNB_ASSET_TYPE_SPRITE_FONT, 1U, &writer) ==
            CNA_RESULT_SUCCESS);
    static const uint8_t payload[4] = {1U, 2U, 3U, 4U};
    REQUIRE(cna_cnb_writer_add_chunk(
                writer, CNA_CNB_MODEL_CHUNK_HEADER, payload, sizeof(payload), CNA_CNB_CHUNK_FLAG_NONE, 4U) ==
            CNA_RESULT_SUCCESS);
    uint64_t produced = 0U;
    static uint8_t bytes[4096];
    REQUIRE(cna_cnb_writer_build(writer, bytes, sizeof(bytes), &produced) == CNA_RESULT_SUCCESS);

    CNA_CnbDocumentHandle document = CNA_INVALID_HANDLE;
    REQUIRE(cna_cnb_document_parse(bytes, produced, view("font"), NULL, &document) ==
            CNA_RESULT_SUCCESS);
    CNA_CnbModelDataHandle model = UINT64_MAX;
    REQUIRE(cna_cnb_decode_model(document, &model) == CNA_RESULT_IO);
    REQUIRE(model == CNA_INVALID_HANDLE);

    REQUIRE(cna_cnb_document_destroy(document) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_cnb_writer_destroy(writer) == CNA_RESULT_SUCCESS);
    return 1;
}

/* The `.cnj` compile: a missing document is an IO failure, not a crash or an empty success. */
static int validate_cnj_compile(void)
{
    CNA_CnbModelFromCnjHandle compiled = UINT64_MAX;
    REQUIRE(cna_cnb_build_model_from_cnj(
                view("does-not-exist.cnj"), view("."), &compiled) == CNA_RESULT_IO);
    REQUIRE(compiled == CNA_INVALID_HANDLE);
    REQUIRE(cna_cnb_build_model_from_cnj(view("x.cnj"), view("."), NULL) ==
            CNA_RESULT_INVALID_ARGUMENT);
    /* Every accessor refuses the handle the failed compile did not produce. */
    uint64_t count = 0U;
    REQUIRE(cna_cnb_model_from_cnj_get_absorbed_file_count(compiled, &count) ==
            CNA_RESULT_INVALID_HANDLE);
    REQUIRE(cna_cnb_model_from_cnj_get_absorbed_file_size(compiled, 0U, &count) ==
            CNA_RESULT_INVALID_HANDLE);
    REQUIRE(cna_cnb_model_from_cnj_copy_absorbed_file(compiled, 0U, NULL, 0U, &count) ==
            CNA_RESULT_INVALID_HANDLE);
    REQUIRE(cna_cnb_model_from_cnj_get_external_reference_count(compiled, &count) ==
            CNA_RESULT_INVALID_HANDLE);
    REQUIRE(cna_cnb_model_from_cnj_get_external_reference_size(compiled, 0U, &count) ==
            CNA_RESULT_INVALID_HANDLE);
    REQUIRE(cna_cnb_model_from_cnj_copy_external_reference(compiled, 0U, NULL, 0U, &count) ==
            CNA_RESULT_INVALID_HANDLE);
    CNA_CnbModelDataHandle taken = UINT64_MAX;
    REQUIRE(cna_cnb_model_from_cnj_take_model(compiled, &taken) == CNA_RESULT_INVALID_HANDLE);
    REQUIRE(cna_cnb_model_from_cnj_destroy(compiled) == CNA_RESULT_INVALID_HANDLE);
    return 1;
}

int main(void)
{
    if (!validate_identities()) { return 1; }
    if (!validate_round_trip()) { return 2; }
    if (!validate_mutation()) { return 3; }
    if (!validate_refusals()) { return 4; }
    if (!validate_wrong_asset_type()) { return 5; }
    if (!validate_cnj_compile()) { return 6; }
    return 0;
}
