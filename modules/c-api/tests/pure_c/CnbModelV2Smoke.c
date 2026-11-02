// SPDX-License-Identifier: MS-PL
#include <CNA/C/cna.h>
#include "CnaRuntimeSmoke.h"
#include <math.h>
#define SV(s) (CNA_StringView){s, sizeof(s)-1}
#define PREFIX(v) do { (v).struct_size = sizeof(v); (v).struct_version = 1; } while (0)
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __func__, __LINE__, #x); cna_smoke_error(); failed = 1; goto cleanup; } } while (0)
#define EQ(a,b) CHECK(cna_smoke_operand(&smoke,#a,(uint64_t)(a)) == cna_smoke_operand(&smoke,#b,(uint64_t)(b)))
#define OWN(call, h, destroy) EQ(cna_smoke_publish(&smoke,#call,(call),&(h),destroy,NULL,NULL),CNA_RESULT_SUCCESS)
#define DROP(call,h) EQ(cna_smoke_drop(&smoke,#call,(call),(h)),CNA_RESULT_SUCCESS)
uint64_t cna_test_model_v2_invalid(unsigned, uint8_t*, uint64_t);
uint64_t cna_test_model_v2_bytes(unsigned, uint8_t*, uint64_t);
int cna_test_model_v2_worker(int (*)(void));
uint32_t cna_test_sound_schema(uint32_t);
uint64_t cna_test_pcm16(uint32_t, const uint8_t*, uint64_t, uint8_t*);
uint64_t cna_test_mips(uint32_t, const uint8_t*, uint32_t, uint8_t*);
static CNA_CnbModelV2Handle thread_model;
static int foreign_thread(void)
{
    CNA_CnbModelV2DocumentInfo info = {0}; PREFIX(info); info.root_bone = 99;
    return cna_cnb_model_v2_get_info(thread_model, &info) != CNA_RESULT_THREAD || info.root_bone != 99 ||
        cna_cnb_model_v2_destroy(thread_model) != CNA_RESULT_THREAD;
}
static int roundtrip(unsigned variant)
{
    int failed = 0;
    CnaRuntimeSmoke smoke = {0};
    CNA_CnbModelV2Handle model = 0, other = 0, decoded = 0;
    CNA_CnbDocumentHandle container = 0;
    CNA_Bool equal = 99;
    uint64_t count = 0;
    uint8_t bytes[32768], expected[32768], again[32768], vertex[120], indices[24] = {0};
    CNA_CnbModelV2Element elements[2] = {{0}};
    for (unsigned i = 0; i < 2; ++i) { PREFIX(elements[i]); elements[i].offset = i*12; elements[i].format = CNA_VERTEX_ELEMENT_FORMAT_VECTOR3; elements[i].usage = i ? CNA_VERTEX_ELEMENT_USAGE_NORMAL : CNA_VERTEX_ELEMENT_USAGE_POSITION; }
    CNA_CnbModelV2DeclarationDesc declarations[2] = {0};
    for (unsigned i = 0; i < 2; ++i) { PREFIX(declarations[i]); PREFIX(declarations[i].info); declarations[i].info.vertex_stride = 24; declarations[i].info.element_count = 2; declarations[i].elements = elements; }
    for (unsigned i = 0; i < sizeof(vertex); ++i) vertex[i] = (uint8_t)i;
    const unsigned width = variant == 3 ? 4 : 2;
    const unsigned ids[6] = {0,1,2,1,3,2};
    for (unsigned i = 0; i < 6; ++i) indices[i*width] = (uint8_t)ids[i];
    CNA_CnbModelV2VertexBufferDesc vb = {0}; PREFIX(vb); PREFIX(vb.info);
    vb.info.declaration = variant == 3 ? 1 : 0; vb.info.vertex_count = 5; vb.info.byte_count = sizeof(vertex); vb.bytes = vertex;
    CNA_CnbModelV2IndexBufferDesc ib = {0}; PREFIX(ib); PREFIX(ib.info);
    ib.info.index_element_size = width; ib.info.index_count = 6; ib.info.byte_count = 6*width; ib.bytes = indices;
    CNA_CnbModelV2BoneDesc bones[2] = {0};
    for (unsigned i = 0; i < 2; ++i) { PREFIX(bones[i]); PREFIX(bones[i].info); bones[i].info.parent = -1; for (unsigned j = 0; j < 16; j += 5) bones[i].info.transform[j] = 1; }
    bones[0].name = variant == 2 ? SV("") : SV("Detached"); bones[0].info.name_is_null = variant == 2;
    bones[1].name = SV("Root"); bones[1].info.transform[12] = 4;
    CNA_CnbModelV2Part parts[2] = {{0}};
    for (unsigned i = 0; i < 2; ++i) { PREFIX(parts[i]); parts[i].vertex_offset = 1; parts[i].num_vertices = 4; parts[i].start_index = i*3; parts[i].primitive_count = 1; }
    CNA_CnbModelV2MeshDesc mesh = {0}; PREFIX(mesh); PREFIX(mesh.info);
    mesh.name = SV("SharedGeometry"); mesh.info.parent_bone = 1; mesh.info.part_count = 2; mesh.parts = parts;
    for (unsigned i = 0; i < 4; ++i) mesh.info.bounding_sphere[i] = (float)i+1;
    CNA_CnbModelV2EffectDesc effects[5] = {0};
    for (unsigned i = 0; i < 5; ++i) { PREFIX(effects[i]); PREFIX(effects[i].info); effects[i].info.kind = i; }
    effects[0].primary_texture = SV("Textures/checker");
    effects[0].info.diffuse[0]=.25f; effects[0].info.diffuse[1]=.5f; effects[0].info.diffuse[2]=.75f;
    effects[0].info.emissive[0]=.1f; effects[0].info.emissive[1]=.2f; effects[0].info.emissive[2]=.3f;
    effects[0].info.specular[0]=.8f; effects[0].info.specular[1]=.7f; effects[0].info.specular[2]=.6f;
    effects[0].info.specular_power=9.5f; effects[0].info.alpha=.75f; effects[0].info.vertex_color_enabled=CNA_TRUE;
    CNA_CnbModelV2DocumentDesc desc = {0}; PREFIX(desc); PREFIX(desc.info);
    desc.info.root_bone=1; desc.info.bone_count=2; desc.info.mesh_count=1; desc.info.declaration_count=variant == 3 ? 2 : 1;
    desc.info.vertex_buffer_count=1; desc.info.index_buffer_count=1; desc.info.effect_count=1;
    desc.bones=bones; desc.meshes=&mesh; desc.declarations=declarations; desc.vertex_buffers=&vb; desc.index_buffers=&ib; desc.effects=effects;
    if (variant == 1) {
        memset(effects, 0, sizeof(effects));
        for (unsigned i=0; i<5; ++i) { PREFIX(effects[i]); PREFIX(effects[i].info); effects[i].info.kind=i; }
        effects[0].primary_texture = SV("Textures/basic");
        effects[0].info.diffuse[0] = 1.0f;
        effects[0].info.diffuse[1] = 0.5f;
        effects[0].info.diffuse[2] = 0.25f;
        effects[0].info.emissive[0] = 0.1f;
        effects[0].info.emissive[1] = 0.2f;
        effects[0].info.emissive[2] = 0.3f;
        effects[0].info.specular[0] = 0.4f;
        effects[0].info.specular[1] = 0.5f;
        effects[0].info.specular[2] = 0.6f;
        effects[0].info.specular_power = 12.0f;
        effects[0].info.alpha = 0.9f;
        effects[0].info.vertex_color_enabled = CNA_TRUE;
        effects[1].primary_texture = SV("Textures/skinned");
        effects[1].info.diffuse[0] = 0.9f;
        effects[1].info.diffuse[1] = 0.8f;
        effects[1].info.diffuse[2] = 0.7f;
        effects[1].info.emissive[0] = 0.0f;
        effects[1].info.emissive[1] = 0.1f;
        effects[1].info.emissive[2] = 0.2f;
        effects[1].info.specular[0] = 0.3f;
        effects[1].info.specular[1] = 0.4f;
        effects[1].info.specular[2] = 0.5f;
        effects[1].info.specular_power = 32.0f;
        effects[1].info.alpha = 0.8f;
        effects[1].info.weights_per_vertex = 4u;
        effects[2].primary_texture = SV("Textures/dual-a");
        effects[2].secondary_texture = SV("Textures/dual-b");
        effects[2].info.diffuse[0] = 0.7f;
        effects[2].info.diffuse[1] = 0.6f;
        effects[2].info.diffuse[2] = 0.5f;
        effects[2].info.alpha = 0.7f;
        effects[2].info.vertex_color_enabled = CNA_TRUE;
        effects[3].primary_texture = SV("Textures/cutout");
        effects[3].info.diffuse[0] = 0.6f;
        effects[3].info.diffuse[1] = 0.5f;
        effects[3].info.diffuse[2] = 0.4f;
        effects[3].info.alpha = 0.6f;
        effects[3].info.alpha_function = 6u;
        effects[3].info.reference_alpha = 0xF0000040u;
        effects[3].info.vertex_color_enabled = CNA_TRUE;
        effects[4].primary_texture = SV("Textures/env-base");
        effects[4].cube_texture = SV("Textures/sky");
        effects[4].info.diffuse[0] = 0.5f;
        effects[4].info.diffuse[1] = 0.4f;
        effects[4].info.diffuse[2] = 0.3f;
        effects[4].info.emissive[0] = 0.2f;
        effects[4].info.emissive[1] = 0.1f;
        effects[4].info.emissive[2] = 0.0f;
        effects[4].info.specular[0] = 0.8f;
        effects[4].info.specular[1] = 0.9f;
        effects[4].info.specular[2] = 1.0f;
        effects[4].info.environment_map_amount = 0.75f;
        effects[4].info.fresnel_factor = 0.25f;
        effects[4].info.alpha = 0.5f;
        desc.info.root_bone=0; desc.info.bone_count=1; bones[0].name=SV("Root");
        desc.info.mesh_count=0; desc.info.declaration_count=0; desc.info.vertex_buffer_count=0; desc.info.index_buffer_count=0; desc.info.effect_count=5;
    }
    {
        CNA_CnbModelV2Element changed = elements[0];
        EQ(cna_cnb_model_v2_element_equals(&elements[0],&changed,&equal),CNA_RESULT_SUCCESS); EQ(equal,CNA_TRUE);
        changed.offset = changed.offset + 1;
        EQ(cna_cnb_model_v2_element_equals(&elements[0],&changed,&equal),CNA_RESULT_SUCCESS); EQ(equal,CNA_FALSE); changed=elements[0];
        changed.format = changed.format + 1;
        EQ(cna_cnb_model_v2_element_equals(&elements[0],&changed,&equal),CNA_RESULT_SUCCESS); EQ(equal,CNA_FALSE); changed=elements[0];
        changed.usage = changed.usage + 1;
        EQ(cna_cnb_model_v2_element_equals(&elements[0],&changed,&equal),CNA_RESULT_SUCCESS); EQ(equal,CNA_FALSE); changed=elements[0];
        changed.usage_index = changed.usage_index + 1;
        EQ(cna_cnb_model_v2_element_equals(&elements[0],&changed,&equal),CNA_RESULT_SUCCESS); EQ(equal,CNA_FALSE); changed=elements[0];
        changed.struct_version=99; equal=99; EQ(cna_cnb_model_v2_element_equals(&elements[0],&changed,&equal),CNA_RESULT_INVALID_ARGUMENT); EQ(equal,99);
    }
    {
        CNA_CnbModelV2DeclarationDesc changed = declarations[0];
        EQ(cna_cnb_model_v2_declaration_equals(&declarations[0],&changed,&equal),CNA_RESULT_SUCCESS); EQ(equal,CNA_TRUE);
        changed.info.vertex_stride = changed.info.vertex_stride + 1;
        EQ(cna_cnb_model_v2_declaration_equals(&declarations[0],&changed,&equal),CNA_RESULT_SUCCESS); EQ(equal,CNA_FALSE); changed=declarations[0];
        changed.info.struct_version=99; equal=99; EQ(cna_cnb_model_v2_declaration_equals(&declarations[0],&changed,&equal),CNA_RESULT_INVALID_ARGUMENT); EQ(equal,99);
    }
    {
        CNA_CnbModelV2VertexBufferDesc changed = vb;
        EQ(cna_cnb_model_v2_vertex_buffer_equals(&vb,&changed,&equal),CNA_RESULT_SUCCESS); EQ(equal,CNA_TRUE);
        changed.info.declaration = changed.info.declaration + 1;
        EQ(cna_cnb_model_v2_vertex_buffer_equals(&vb,&changed,&equal),CNA_RESULT_SUCCESS); EQ(equal,CNA_FALSE); changed=vb;
        changed.info.struct_version=99; equal=99; EQ(cna_cnb_model_v2_vertex_buffer_equals(&vb,&changed,&equal),CNA_RESULT_INVALID_ARGUMENT); EQ(equal,99);
    }
    {
        CNA_CnbModelV2IndexBufferDesc changed = ib;
        EQ(cna_cnb_model_v2_index_buffer_equals(&ib,&changed,&equal),CNA_RESULT_SUCCESS); EQ(equal,CNA_TRUE);
        changed.info.index_element_size = changed.info.index_element_size == 2 ? 4 : 2;
        EQ(cna_cnb_model_v2_index_buffer_equals(&ib,&changed,&equal),CNA_RESULT_SUCCESS); EQ(equal,CNA_FALSE); changed=ib;
        changed.info.struct_version=99; equal=99; EQ(cna_cnb_model_v2_index_buffer_equals(&ib,&changed,&equal),CNA_RESULT_INVALID_ARGUMENT); EQ(equal,99);
    }
    {
        CNA_CnbModelV2EffectDesc changed = effects[0];
        EQ(cna_cnb_model_v2_effect_equals(&effects[0],&changed,&equal),CNA_RESULT_SUCCESS); EQ(equal,CNA_TRUE);
        changed.info.kind = changed.info.kind + 1;
        EQ(cna_cnb_model_v2_effect_equals(&effects[0],&changed,&equal),CNA_RESULT_SUCCESS); EQ(equal,CNA_FALSE); changed=effects[0];
        changed.info.diffuse[0] = changed.info.diffuse[0] + 1;
        EQ(cna_cnb_model_v2_effect_equals(&effects[0],&changed,&equal),CNA_RESULT_SUCCESS); EQ(equal,CNA_FALSE); changed=effects[0];
        changed.info.emissive[0] = changed.info.emissive[0] + 1;
        EQ(cna_cnb_model_v2_effect_equals(&effects[0],&changed,&equal),CNA_RESULT_SUCCESS); EQ(equal,CNA_FALSE); changed=effects[0];
        changed.info.specular[0] = changed.info.specular[0] + 1;
        EQ(cna_cnb_model_v2_effect_equals(&effects[0],&changed,&equal),CNA_RESULT_SUCCESS); EQ(equal,CNA_FALSE); changed=effects[0];
        changed.info.specular_power = changed.info.specular_power + 1;
        EQ(cna_cnb_model_v2_effect_equals(&effects[0],&changed,&equal),CNA_RESULT_SUCCESS); EQ(equal,CNA_FALSE); changed=effects[0];
        changed.info.alpha = changed.info.alpha + 1;
        EQ(cna_cnb_model_v2_effect_equals(&effects[0],&changed,&equal),CNA_RESULT_SUCCESS); EQ(equal,CNA_FALSE); changed=effects[0];
        changed.info.environment_map_amount = changed.info.environment_map_amount + 1;
        EQ(cna_cnb_model_v2_effect_equals(&effects[0],&changed,&equal),CNA_RESULT_SUCCESS); EQ(equal,CNA_FALSE); changed=effects[0];
        changed.info.fresnel_factor = changed.info.fresnel_factor + 1;
        EQ(cna_cnb_model_v2_effect_equals(&effects[0],&changed,&equal),CNA_RESULT_SUCCESS); EQ(equal,CNA_FALSE); changed=effects[0];
        changed.info.weights_per_vertex = changed.info.weights_per_vertex + 1;
        EQ(cna_cnb_model_v2_effect_equals(&effects[0],&changed,&equal),CNA_RESULT_SUCCESS); EQ(equal,CNA_FALSE); changed=effects[0];
        changed.info.alpha_function = changed.info.alpha_function + 1;
        EQ(cna_cnb_model_v2_effect_equals(&effects[0],&changed,&equal),CNA_RESULT_SUCCESS); EQ(equal,CNA_FALSE); changed=effects[0];
        changed.info.reference_alpha = changed.info.reference_alpha + 1;
        EQ(cna_cnb_model_v2_effect_equals(&effects[0],&changed,&equal),CNA_RESULT_SUCCESS); EQ(equal,CNA_FALSE); changed=effects[0];
        changed.info.vertex_color_enabled = !changed.info.vertex_color_enabled;
        EQ(cna_cnb_model_v2_effect_equals(&effects[0],&changed,&equal),CNA_RESULT_SUCCESS); EQ(equal,CNA_FALSE); changed=effects[0];
        changed.primary_texture=SV("different"); EQ(cna_cnb_model_v2_effect_equals(&effects[0],&changed,&equal),CNA_RESULT_SUCCESS); EQ(equal,CNA_FALSE); changed=effects[0];
        changed.secondary_texture=SV("different"); EQ(cna_cnb_model_v2_effect_equals(&effects[0],&changed,&equal),CNA_RESULT_SUCCESS); EQ(equal,CNA_FALSE); changed=effects[0];
        changed.cube_texture=SV("different"); EQ(cna_cnb_model_v2_effect_equals(&effects[0],&changed,&equal),CNA_RESULT_SUCCESS); EQ(equal,CNA_FALSE); changed=effects[0];
        changed.info.struct_version=99; equal=99; EQ(cna_cnb_model_v2_effect_equals(&effects[0],&changed,&equal),CNA_RESULT_INVALID_ARGUMENT); EQ(equal,99);
    }
    {
        CNA_CnbModelV2BoneDesc changed = bones[0];
        EQ(cna_cnb_model_v2_bone_equals(&bones[0],&changed,&equal),CNA_RESULT_SUCCESS); EQ(equal,CNA_TRUE);
        changed.info.name_is_null = !changed.info.name_is_null;
        EQ(cna_cnb_model_v2_bone_equals(&bones[0],&changed,&equal),CNA_RESULT_SUCCESS); EQ(equal,CNA_FALSE); changed=bones[0];
        changed.info.parent = 0;
        EQ(cna_cnb_model_v2_bone_equals(&bones[0],&changed,&equal),CNA_RESULT_SUCCESS); EQ(equal,CNA_FALSE); changed=bones[0];
        changed.info.transform[0] = changed.info.transform[0] + 1;
        EQ(cna_cnb_model_v2_bone_equals(&bones[0],&changed,&equal),CNA_RESULT_SUCCESS); EQ(equal,CNA_FALSE); changed=bones[0];
        changed.name=SV("different"); EQ(cna_cnb_model_v2_bone_equals(&bones[0],&changed,&equal),CNA_RESULT_SUCCESS); EQ(equal,CNA_FALSE); changed=bones[0];
        changed.info.struct_version=99; equal=99; EQ(cna_cnb_model_v2_bone_equals(&bones[0],&changed,&equal),CNA_RESULT_INVALID_ARGUMENT); EQ(equal,99);
    }
    {
        CNA_CnbModelV2Part changed = parts[0];
        EQ(cna_cnb_model_v2_part_equals(&parts[0],&changed,&equal),CNA_RESULT_SUCCESS); EQ(equal,CNA_TRUE);
        changed.vertex_offset = changed.vertex_offset + 1;
        EQ(cna_cnb_model_v2_part_equals(&parts[0],&changed,&equal),CNA_RESULT_SUCCESS); EQ(equal,CNA_FALSE); changed=parts[0];
        changed.num_vertices = changed.num_vertices + 1;
        EQ(cna_cnb_model_v2_part_equals(&parts[0],&changed,&equal),CNA_RESULT_SUCCESS); EQ(equal,CNA_FALSE); changed=parts[0];
        changed.start_index = changed.start_index + 1;
        EQ(cna_cnb_model_v2_part_equals(&parts[0],&changed,&equal),CNA_RESULT_SUCCESS); EQ(equal,CNA_FALSE); changed=parts[0];
        changed.vertex_buffer = changed.vertex_buffer + 1;
        EQ(cna_cnb_model_v2_part_equals(&parts[0],&changed,&equal),CNA_RESULT_SUCCESS); EQ(equal,CNA_FALSE); changed=parts[0];
        changed.index_buffer = changed.index_buffer + 1;
        EQ(cna_cnb_model_v2_part_equals(&parts[0],&changed,&equal),CNA_RESULT_SUCCESS); EQ(equal,CNA_FALSE); changed=parts[0];
        changed.effect = changed.effect + 1;
        EQ(cna_cnb_model_v2_part_equals(&parts[0],&changed,&equal),CNA_RESULT_SUCCESS); EQ(equal,CNA_FALSE); changed=parts[0];
        changed.struct_version=99; equal=99; EQ(cna_cnb_model_v2_part_equals(&parts[0],&changed,&equal),CNA_RESULT_INVALID_ARGUMENT); EQ(equal,99);
    }
    {
        CNA_CnbModelV2MeshDesc changed = mesh;
        EQ(cna_cnb_model_v2_mesh_equals(&mesh,&changed,&equal),CNA_RESULT_SUCCESS); EQ(equal,CNA_TRUE);
        changed.info.parent_bone = 0;
        EQ(cna_cnb_model_v2_mesh_equals(&mesh,&changed,&equal),CNA_RESULT_SUCCESS); EQ(equal,CNA_FALSE); changed=mesh;
        changed.info.bounding_sphere[0] = changed.info.bounding_sphere[0] + 1;
        EQ(cna_cnb_model_v2_mesh_equals(&mesh,&changed,&equal),CNA_RESULT_SUCCESS); EQ(equal,CNA_FALSE); changed=mesh;
        changed.name=SV("different"); EQ(cna_cnb_model_v2_mesh_equals(&mesh,&changed,&equal),CNA_RESULT_SUCCESS); EQ(equal,CNA_FALSE); changed=mesh;
        changed.info.struct_version=99; equal=99; EQ(cna_cnb_model_v2_mesh_equals(&mesh,&changed,&equal),CNA_RESULT_INVALID_ARGUMENT); EQ(equal,99);
    }
    {
        CNA_CnbModelV2DeclarationDesc changed=declarations[0];
        CNA_CnbModelV2Element reversed[2]={elements[1],elements[0]}; changed.elements=reversed;
        EQ(cna_cnb_model_v2_declaration_equals(&declarations[0],&changed,&equal),CNA_RESULT_SUCCESS); EQ(equal,CNA_FALSE);
        changed=declarations[0]; changed.info.element_count=1;
        EQ(cna_cnb_model_v2_declaration_equals(&declarations[0],&changed,&equal),CNA_RESULT_SUCCESS); EQ(equal,CNA_FALSE);
        CNA_CnbModelV2VertexBufferDesc vertex_changed=vb; vertex_changed.info.vertex_count++;
        EQ(cna_cnb_model_v2_vertex_buffer_equals(&vb,&vertex_changed,&equal),CNA_RESULT_SUCCESS); EQ(equal,CNA_FALSE);
        uint8_t different[120]; memcpy(different,vertex,sizeof(vertex)); different[119]^=1; vertex_changed=vb; vertex_changed.bytes=different;
        EQ(cna_cnb_model_v2_vertex_buffer_equals(&vb,&vertex_changed,&equal),CNA_RESULT_SUCCESS); EQ(equal,CNA_FALSE);
        CNA_CnbModelV2IndexBufferDesc index_changed=ib; index_changed.info.index_count++;
        EQ(cna_cnb_model_v2_index_buffer_equals(&ib,&index_changed,&equal),CNA_RESULT_SUCCESS); EQ(equal,CNA_FALSE);
        memcpy(different,indices,24); different[0]=1; index_changed=ib; index_changed.bytes=different;
        EQ(cna_cnb_model_v2_index_buffer_equals(&ib,&index_changed,&equal),CNA_RESULT_SUCCESS); EQ(equal,CNA_FALSE);
        CNA_CnbModelV2Part changed_part=parts[0]; changed_part.primitive_count++;
        EQ(cna_cnb_model_v2_part_equals(&parts[0],&changed_part,&equal),CNA_RESULT_SUCCESS); EQ(equal,CNA_FALSE);
        CNA_CnbModelV2MeshDesc changed_mesh=mesh; CNA_CnbModelV2Part swapped[2]={parts[1],parts[0]}; changed_mesh.parts=swapped;
        EQ(cna_cnb_model_v2_mesh_equals(&mesh,&changed_mesh,&equal),CNA_RESULT_SUCCESS); EQ(equal,CNA_FALSE);
    }
    OWN(cna_cnb_model_v2_create(&desc,&model),model,cna_cnb_model_v2_destroy);
    OWN(cna_cnb_model_v2_create(&desc,&other),other,cna_cnb_model_v2_destroy);
    EQ(cna_cnb_model_v2_equals(model,other,&equal),CNA_RESULT_SUCCESS); EQ(equal,CNA_TRUE);
    DROP(cna_cnb_model_v2_destroy(other),other); other=0;
    thread_model=model; EQ(cna_test_model_v2_worker(foreign_thread),0);
    CNA_CnbModelV2DocumentInfo info = {0}; PREFIX(info);
    EQ(cna_cnb_model_v2_get_info(model,&info),CNA_RESULT_SUCCESS);
    EQ(info.root_bone,desc.info.root_bone); EQ(info.bone_count,desc.info.bone_count); EQ(info.mesh_count,desc.info.mesh_count);
    EQ(info.declaration_count,desc.info.declaration_count); EQ(info.vertex_buffer_count,desc.info.vertex_buffer_count); EQ(info.index_buffer_count,desc.info.index_buffer_count); EQ(info.effect_count,desc.info.effect_count);
    for (uint64_t i=0; i<desc.info.bone_count; ++i) {
        CNA_CnbModelV2BoneInfo actual={0}; PREFIX(actual);
        EQ(cna_cnb_model_v2_get_bone(model,i,&actual),CNA_RESULT_SUCCESS);
        CHECK(actual.name_is_null==bones[i].info.name_is_null && actual.parent==bones[i].info.parent && memcmp(actual.transform,bones[i].info.transform,sizeof(actual.transform))==0);
        EQ(cna_cnb_model_v2_copy_bone_name(model,i,(char*)again,sizeof(again),&count),CNA_RESULT_SUCCESS);
        EQ(count,bones[i].name.byte_length); CHECK(memcmp(again,bones[i].name.data,(size_t)count)==0);
    }
    for (uint64_t i=0; i<desc.info.effect_count; ++i) {
        CNA_CnbModelV2EffectInfo actual={0}; PREFIX(actual);
        EQ(cna_cnb_model_v2_get_effect(model,i,&actual),CNA_RESULT_SUCCESS);
        CHECK(actual.kind==effects[i].info.kind && actual.alpha==effects[i].info.alpha && actual.reference_alpha==effects[i].info.reference_alpha);
        EQ(cna_cnb_model_v2_copy_effect_primary_texture(model,i,(char*)again,sizeof(again),&count),CNA_RESULT_SUCCESS);
        EQ(count,effects[i].primary_texture.byte_length); CHECK(count==0 || memcmp(again,effects[i].primary_texture.data,(size_t)count)==0);
        EQ(cna_cnb_model_v2_copy_effect_secondary_texture(model,i,(char*)again,sizeof(again),&count),CNA_RESULT_SUCCESS);
        EQ(count,effects[i].secondary_texture.byte_length); CHECK(count==0 || memcmp(again,effects[i].secondary_texture.data,(size_t)count)==0);
        EQ(cna_cnb_model_v2_copy_effect_cube_texture(model,i,(char*)again,sizeof(again),&count),CNA_RESULT_SUCCESS);
        EQ(count,effects[i].cube_texture.byte_length); CHECK(count==0 || memcmp(again,effects[i].cube_texture.data,(size_t)count)==0);
    }
    if (variant != 1) {
        CNA_CnbModelV2DeclarationInfo di={0}; PREFIX(di);
        EQ(cna_cnb_model_v2_get_declaration(model,0,&di),CNA_RESULT_SUCCESS); EQ(di.vertex_stride,24); EQ(di.element_count,2);
        CNA_CnbModelV2Element copy[2];
        EQ(cna_cnb_model_v2_copy_declaration_elements(model,0,copy,2,&count),CNA_RESULT_SUCCESS); EQ(count,2);
        for (unsigned i=0;i<2;++i) { EQ(cna_cnb_model_v2_element_equals(&elements[i],&copy[i],&equal),CNA_RESULT_SUCCESS); EQ(equal,CNA_TRUE); }
        memset(copy,0x5a,sizeof(copy)); EQ(cna_cnb_model_v2_copy_declaration_elements(model,0,copy,1,&count),CNA_RESULT_BUFFER_TOO_SMALL); EQ(count,2); EQ(copy[0].struct_size,0x5a5a5a5a);
        CNA_CnbModelV2VertexBufferInfo vi={0}; PREFIX(vi);
        CNA_CnbModelV2IndexBufferInfo ii={0}; PREFIX(ii);
        EQ(cna_cnb_model_v2_get_vertex_buffer(model,0,&vi),CNA_RESULT_SUCCESS); EQ(vi.declaration,vb.info.declaration); EQ(vi.vertex_count,5); EQ(vi.byte_count,120);
        EQ(cna_cnb_model_v2_get_index_buffer(model,0,&ii),CNA_RESULT_SUCCESS); EQ(ii.index_element_size,width); EQ(ii.index_count,6); EQ(ii.byte_count,6*width);
        EQ(cna_cnb_model_v2_copy_vertex_buffer_bytes(model,0,again,sizeof(again),&count),CNA_RESULT_SUCCESS); CHECK(count==120 && memcmp(again,vertex,120)==0);
        EQ(cna_cnb_model_v2_copy_index_buffer_bytes(model,0,again,sizeof(again),&count),CNA_RESULT_SUCCESS); CHECK(count==6*width && memcmp(again,indices,(size_t)count)==0);
        CNA_CnbModelV2MeshInfo mi={0}; PREFIX(mi);
        EQ(cna_cnb_model_v2_get_mesh(model,0,&mi),CNA_RESULT_SUCCESS); CHECK(mi.parent_bone==1 && mi.part_count==2 && memcmp(mi.bounding_sphere,mesh.info.bounding_sphere,sizeof(mi.bounding_sphere))==0);
        EQ(cna_cnb_model_v2_copy_mesh_name(model,0,(char*)again,sizeof(again),&count),CNA_RESULT_SUCCESS); CHECK(count==14 && memcmp(again,"SharedGeometry",14)==0);
        CNA_CnbModelV2Part part_copy[2];
        EQ(cna_cnb_model_v2_copy_mesh_parts(model,0,part_copy,2,&count),CNA_RESULT_SUCCESS); EQ(count,2);
        for (unsigned i=0;i<2;++i) { EQ(cna_cnb_model_v2_part_equals(&parts[i],&part_copy[i],&equal),CNA_RESULT_SUCCESS); EQ(equal,CNA_TRUE); }
    }
    if (variant==0) {
        for (unsigned bad=0;bad<5;++bad) {
            CNA_CnbModelV2DocumentDesc invalid=desc;
            CNA_CnbModelV2MeshDesc bad_mesh=mesh; CNA_CnbModelV2Part bad_parts[2]={parts[0],parts[1]};
            CNA_CnbModelV2VertexBufferDesc bad_vb=vb;
            if (bad==0) invalid.info.root_bone=99;
            if (bad==1) { bad_parts[0].primitive_count=UINT32_MAX; bad_mesh.parts=bad_parts; invalid.meshes=&bad_mesh; }
            if (bad==2) { bad_parts[0].effect=99; bad_mesh.parts=bad_parts; invalid.meshes=&bad_mesh; }
            if (bad==3) { bad_vb.info.byte_count--; invalid.vertex_buffers=&bad_vb; }
            if (bad==4) { bad_mesh.info.bounding_sphere[3]=-1; invalid.meshes=&bad_mesh; }
            CNA_CnbModelV2Handle bad_model=0;
            OWN(cna_cnb_model_v2_create(&invalid,&bad_model),bad_model,cna_cnb_model_v2_destroy);
            memset(again,0x5a,sizeof(again)); count=99;
            EQ(cna_cnb_model_v2_encode(bad_model,SV(""),again,sizeof(again),&count),CNA_RESULT_IO);
            EQ(count,99); EQ(again[0],0x5a);
            DROP(cna_cnb_model_v2_destroy(bad_model),bad_model);
        }
    }
    const uint64_t native_count=cna_test_model_v2_bytes(variant,expected,sizeof(expected)); CHECK(native_count>0);
    EQ(cna_cnb_model_v2_encode(model,SV("fixture"),NULL,0,&count),CNA_RESULT_BUFFER_TOO_SMALL); EQ(count,native_count);
    memset(bytes,0x5a,sizeof(bytes)); EQ(cna_cnb_model_v2_encode(model,SV("fixture"),bytes,count-1,&count),CNA_RESULT_BUFFER_TOO_SMALL); EQ(bytes[0],0x5a);
    EQ(cna_cnb_model_v2_encode(model,SV("fixture"),bytes,sizeof(bytes),&count),CNA_RESULT_SUCCESS); CHECK(memcmp(bytes,expected,(size_t)count)==0);
    OWN(cna_cnb_document_parse(bytes,count,SV("test"),NULL,&container),container,cna_cnb_document_destroy);
    OWN(cna_cnb_model_v2_decode(container,&decoded),decoded,cna_cnb_model_v2_destroy);
    DROP(cna_cnb_document_destroy(container),container); container=0;
    EQ(cna_cnb_model_v2_equals(model,decoded,&equal),CNA_RESULT_SUCCESS); EQ(equal,variant==2 ? CNA_FALSE : CNA_TRUE);
    EQ(cna_cnb_model_v2_encode(decoded,SV("fixture"),again,sizeof(again),&count),CNA_RESULT_SUCCESS); EQ(count,native_count); CHECK(memcmp(again,expected,(size_t)count)==0);
    if (variant==2) { CNA_CnbModelV2BoneInfo bi={0}; PREFIX(bi); EQ(cna_cnb_model_v2_get_bone(decoded,0,&bi),CNA_RESULT_SUCCESS); EQ(bi.name_is_null,CNA_FALSE); }
    /* Inputs are copies; destroying a separate snapshot and mutating descriptors changes nothing. */
    bones[0].info.parent=99; vertex[0]^=0xff;
    EQ(cna_cnb_model_v2_encode(model,SV("fixture"),again,sizeof(again),&count),CNA_RESULT_SUCCESS); CHECK(memcmp(again,expected,(size_t)count)==0);
    info.struct_version=9; info.root_bone=99; EQ(cna_cnb_model_v2_get_info(model,&info),CNA_RESULT_INVALID_ARGUMENT); EQ(info.root_bone,99);
    EQ(cna_cnb_model_v2_get_info(0,NULL),CNA_RESULT_INVALID_ARGUMENT);
    EQ(cna_cnb_model_v2_copy_bone_name(model,UINT64_MAX,NULL,0,&count),CNA_RESULT_INVALID_ARGUMENT);
    EQ(cna_cnb_model_v2_decode(model,&other),CNA_RESULT_INVALID_HANDLE); EQ(other,0);
    DROP(cna_cnb_model_v2_destroy(model),model); EQ(cna_cnb_model_v2_destroy(model),CNA_RESULT_INVALID_HANDLE); model=0;
cleanup:
    if (failed) fprintf(stderr,"variant=%u count=%" PRIu64 " equal=%u\n%s",variant,count,equal,smoke.trace);
    return cna_smoke_cleanup(&smoke) || failed;
}

static int failures(void)
{
    int failed=0;
    CnaRuntimeSmoke smoke={0};
    CNA_CnbModelV2Handle model=0;
    CNA_CnbModelV2DocumentDesc desc={0}; PREFIX(desc); PREFIX(desc.info);
    CNA_Bool equal=99;
    uint64_t count=99;
    uint8_t bytes[128]; memset(bytes,0x5a,sizeof(bytes));
    OWN(cna_cnb_model_v2_create(&desc,&model),model,cna_cnb_model_v2_destroy);
    EQ(cna_cnb_model_v2_encode(model,SV(""),bytes,sizeof(bytes),&count),CNA_RESULT_IO); EQ(count,99); EQ(bytes[0],0x5a);
    for (unsigned variant=0; variant<3; ++variant) {
        uint8_t malformed[32768]; const uint64_t size=cna_test_model_v2_invalid(variant,malformed,sizeof(malformed)); CHECK(size>0);
        CNA_CnbDocumentHandle document=0; CNA_CnbModelV2Handle bad_model=99;
        OWN(cna_cnb_document_parse(malformed,size,SV("bad"),NULL,&document),document,cna_cnb_document_destroy);
        EQ(cna_smoke_publish(&smoke,"decode invalid",cna_cnb_model_v2_decode(document,&bad_model),&bad_model,cna_cnb_model_v2_destroy,NULL,NULL),CNA_RESULT_IO); EQ(bad_model,0);
        DROP(cna_cnb_document_destroy(document),document);
    }
    CNA_CnbModelV2Handle rejected=99;
    desc.info.bone_count=1; EQ(cna_smoke_publish(&smoke,"create refusal",cna_cnb_model_v2_create(&desc,&rejected),&rejected,cna_cnb_model_v2_destroy,NULL,NULL),CNA_RESULT_INVALID_ARGUMENT); EQ(rejected,0);
    desc.info.bone_count=UINT64_MAX; EQ(cna_smoke_publish(&smoke,"create refusal",cna_cnb_model_v2_create(&desc,&rejected),&rejected,cna_cnb_model_v2_destroy,NULL,NULL),CNA_RESULT_OVERFLOW); EQ(rejected,0);
    CNA_CnbModelV2BoneDesc bone={0}; PREFIX(bone); PREFIX(bone.info);
    desc.bones=&bone; desc.info.bone_count=1;
    bone.struct_size=sizeof(bone)-1; EQ(cna_smoke_publish(&smoke,"descriptor prefix",cna_cnb_model_v2_create(&desc,&rejected),&rejected,cna_cnb_model_v2_destroy,NULL,NULL),CNA_RESULT_INVALID_ARGUMENT); EQ(rejected,0); PREFIX(bone);
    bone.info.name_is_null=2; EQ(cna_smoke_publish(&smoke,"create refusal",cna_cnb_model_v2_create(&desc,&rejected),&rejected,cna_cnb_model_v2_destroy,NULL,NULL),CNA_RESULT_INVALID_ARGUMENT); EQ(rejected,0);
    bone.info.name_is_null=0; bone.name=SV("a\0b"); EQ(cna_smoke_publish(&smoke,"create refusal",cna_cnb_model_v2_create(&desc,&rejected),&rejected,cna_cnb_model_v2_destroy,NULL,NULL),CNA_RESULT_ENCODING);
    bone.name=SV("\xff"); EQ(cna_smoke_publish(&smoke,"create refusal",cna_cnb_model_v2_create(&desc,&rejected),&rejected,cna_cnb_model_v2_destroy,NULL,NULL),CNA_RESULT_ENCODING);
    CNA_CnbModelV2Element element={0}; PREFIX(element); element.format=99;
    EQ(cna_cnb_model_v2_element_equals(&element,&element,&equal),CNA_RESULT_INVALID_ARGUMENT); EQ(equal,99);
    element.format=0; element.usage=99; EQ(cna_cnb_model_v2_element_equals(&element,&element,&equal),CNA_RESULT_INVALID_ARGUMENT);
    CNA_CnbModelV2IndexBufferDesc ib={0}; PREFIX(ib); PREFIX(ib.info); ib.info.index_element_size=3;
    EQ(cna_cnb_model_v2_index_buffer_equals(&ib,&ib,&equal),CNA_RESULT_INVALID_ARGUMENT);
    CNA_CnbModelV2EffectDesc effect={0}; PREFIX(effect); PREFIX(effect.info); effect.info.kind=5;
    EQ(cna_cnb_model_v2_effect_equals(&effect,&effect,&equal),CNA_RESULT_INVALID_ARGUMENT);
    effect.info.kind=0; effect.info.vertex_color_enabled=2; EQ(cna_cnb_model_v2_effect_equals(&effect,&effect,&equal),CNA_RESULT_INVALID_ARGUMENT);
    CNA_CnbModelV2DeclarationDesc declaration={0}; PREFIX(declaration); PREFIX(declaration.info); declaration.info.element_count=1;
    EQ(cna_cnb_model_v2_declaration_equals(&declaration,&declaration,&equal),CNA_RESULT_INVALID_ARGUMENT);
    declaration.info.element_count=UINT64_MAX; EQ(cna_cnb_model_v2_declaration_equals(&declaration,&declaration,&equal),CNA_RESULT_OVERFLOW);
    EQ(cna_cnb_model_v2_equals(0,0,&equal),CNA_RESULT_INVALID_HANDLE); EQ(equal,99);
    EQ(cna_cnb_model_v2_encode(model,SV(""),NULL,1,&count),CNA_RESULT_INVALID_ARGUMENT);
    EQ(cna_cnb_model_v2_copy_mesh_parts(model,0,NULL,0,&count),CNA_RESULT_INVALID_ARGUMENT);
    EQ(cna_cnb_model_v2_copy_declaration_elements(model,0,NULL,1,&count),CNA_RESULT_INVALID_ARGUMENT);
cleanup:
    if (failed) fputs(smoke.trace,stderr);
    return cna_smoke_cleanup(&smoke) || failed;
}
static int helpers(void)
{
    int failed=0;
    CnaRuntimeSmoke smoke={0};
    uint8_t samples[4]={0,127,128,255}, actual[256], expected[256];
    uint64_t count=99;
    uint32_t schema=99;
    for (uint32_t format=0;format<8;++format) { EQ(cna_cnb_sound_effect_schema_version_for_format(format,&schema),CNA_RESULT_SUCCESS); EQ(schema,cna_test_sound_schema(format)); }
    EQ(cna_cnb_sound_effect_schema_version_for_format(UINT32_MAX,&schema),CNA_RESULT_SUCCESS); EQ(schema,0);
    EQ(cna_cnb_sound_effect_schema_version_for_format(0,NULL),CNA_RESULT_INVALID_ARGUMENT);
    for (uint32_t format=1;format<=2;++format) {
        CNA_CnbSoundEffectInfo info={sizeof(info),1,format,8000,1,format==1?2:4,0,0};
        CNA_CnbSoundEffectDataHandle sound=0;
        OWN(cna_cnb_sound_effect_data_create(&info,samples,4,&sound),sound,cna_cnb_sound_effect_data_destroy);
        const uint64_t expected_count=cna_test_pcm16(format,samples,4,expected);
        EQ(cna_cnb_sound_effect_data_copy_pcm16(sound,NULL,0,&count),CNA_RESULT_BUFFER_TOO_SMALL); EQ(count,expected_count);
        memset(actual,0x5a,sizeof(actual)); EQ(cna_cnb_sound_effect_data_copy_pcm16(sound,actual,count-1,&count),CNA_RESULT_BUFFER_TOO_SMALL); EQ(actual[0],0x5a);
        EQ(cna_cnb_sound_effect_data_copy_pcm16(sound,actual,sizeof(actual),&count),CNA_RESULT_SUCCESS); CHECK(count==expected_count && memcmp(actual,expected,(size_t)count)==0);
        EQ(cna_cnb_sound_effect_data_copy_samples(sound,actual,sizeof(actual),&count),CNA_RESULT_SUCCESS); CHECK(count==4 && memcmp(actual,samples,4)==0);
        DROP(cna_cnb_sound_effect_data_destroy(sound),sound);
        EQ(cna_cnb_sound_effect_data_copy_pcm16(sound,actual,sizeof(actual),&count),CNA_RESULT_INVALID_HANDLE);
    }
    {
        CNA_CnbSoundEffectInfo info={sizeof(info),1,CNA_CNB_AUDIO_FORMAT_PCM_FLOAT32,8000,1,1,0,0};
        CNA_CnbSoundEffectDataHandle sound=0;
        OWN(cna_cnb_sound_effect_data_create(&info,samples,4,&sound),sound,cna_cnb_sound_effect_data_destroy);
        memset(actual,0x5a,sizeof(actual)); count=99;
        EQ(cna_cnb_sound_effect_data_copy_pcm16(sound,actual,sizeof(actual),&count),CNA_RESULT_IO); EQ(count,99); EQ(actual[0],0x5a);
        EQ(cna_cnb_sound_effect_data_copy_pcm16(sound,NULL,1,&count),CNA_RESULT_INVALID_ARGUMENT);
        EQ(cna_cnb_sound_effect_data_copy_samples(sound,actual,sizeof(actual),&count),CNA_RESULT_SUCCESS); CHECK(count==4 && memcmp(actual,samples,4)==0);
        DROP(cna_cnb_sound_effect_data_destroy(sound),sound);
    }
    uint8_t rgba[24]; for (unsigned i=0;i<24;++i) rgba[i]=(uint8_t)(i*11);
    CNA_CnbTextureDataHandle texture=0, mips=0, rejected=99;
    OWN(cna_cnb_texture_data_create_rgba8(3,2,rgba,24,&texture),texture,cna_cnb_texture_data_destroy);
    for (uint32_t color=0;color<2;++color) {
        OWN(cna_cnb_texture_data_generate_rgba8_mips(texture,color,&mips),mips,cna_cnb_texture_data_destroy);
        CNA_CnbTextureInfo info={0}; PREFIX(info);
        EQ(cna_cnb_texture_data_get_info(mips,&info),CNA_RESULT_SUCCESS); EQ(info.mip_count,2);
        for (uint32_t level=0;level<2;++level) {
            const uint64_t expected_count=cna_test_mips(color,rgba,level,expected);
            EQ(cna_cnb_texture_data_copy_level(mips,0,level,actual,sizeof(actual),&count),CNA_RESULT_SUCCESS);
            CHECK(count==expected_count && memcmp(actual,expected,(size_t)count)==0);
        }
        EQ(cna_cnb_texture_data_generate_rgba8_mips(mips,color,&rejected),CNA_RESULT_IO); EQ(rejected,0);
        DROP(cna_cnb_texture_data_destroy(mips),mips); mips=0;
    }
    CNA_CnbTextureInfo info={0}; PREFIX(info);
    EQ(cna_cnb_texture_data_get_info(texture,&info),CNA_RESULT_SUCCESS); EQ(info.mip_count,1);
    EQ(cna_cnb_texture_data_copy_level(texture,0,0,actual,sizeof(actual),&count),CNA_RESULT_SUCCESS); CHECK(count==24 && memcmp(actual,rgba,24)==0);
    EQ(cna_cnb_texture_data_generate_rgba8_mips(texture,2,&rejected),CNA_RESULT_INVALID_ARGUMENT); EQ(rejected,0);
    EQ(cna_cnb_texture_data_generate_rgba8_mips(0,0,NULL),CNA_RESULT_INVALID_ARGUMENT);
    EQ(cna_cnb_texture_data_generate_rgba8_mips(0,0,&rejected),CNA_RESULT_INVALID_HANDLE); EQ(rejected,0);
cleanup:
    if (failed) fputs(smoke.trace,stderr);
    return cna_smoke_cleanup(&smoke) || failed;
}
int main(void)
{
    for (unsigned variant=0;variant<4;++variant) if (roundtrip(variant)) return 1;
    if (failures() || helpers()) return 1;
    puts("CnbModelV2Smoke: 4 native byte-exact fixtures, geometry/graph equality, boundaries, thread/ownership, PCM/mip helpers PASS");
    return 0;
}
