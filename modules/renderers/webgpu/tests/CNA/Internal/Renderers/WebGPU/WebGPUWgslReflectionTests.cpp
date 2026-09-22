// SPDX-License-Identifier: MS-PL
//
// plans/plan_webgpu_modern_graphics.md WMG-0004: the WGSL reflection the WebGPU renderer builds its
// explicit bind-group and pipeline layouts from. CPU-only: no adapter, no device.
//
// The first fixtures are verbatim shapes of what naga 28 emits for the engine layer's Vulkan GLSL
// (tools/shader_package), because that is the WGSL the renderer will see most; the rest pin the
// WGSL memory-layout rules and the refusals.

#include "CNA/Internal/Renderers/WebGPU/WebGPUWgslReflection.hpp"

#include <gtest/gtest.h>

#include <string>

using namespace CNA::Internal::Renderers::WebGPU;

namespace
{
    constexpr const char* kGeneratedFragment = R"WGSL(
struct PushConstants {
    viewportSize: vec2<f32>,
    uMatrix: mat4x4<f32>,
    uBloomParams: vec4<f32>,
}

@group(0) @binding(0)
var texture1_cnaTexture: texture_2d<f32>;
@group(0) @binding(32)
var texture1_cnaSampler: sampler;
var<private> TexCoord_1: vec2<f32>;
@group(3) @binding(0)
var<uniform> pc: PushConstants;
var<private> FragColor: vec4<f32>;

fn main_1() {
    let _e22 = TexCoord_1;
    let _e23 = textureSample(texture1_cnaTexture, texture1_cnaSampler, _e22);
    FragColor = vec4<f32>(_e23.xyz * pc.uBloomParams.x, 1f);
    return;
}

@fragment
fn main(@location(0) TexCoord: vec2<f32>, @location(1) SpriteColor: vec4<f32>) -> @location(0) vec4<f32> {
    TexCoord_1 = TexCoord;
    main_1();
    let _e5 = FragColor;
    return _e5;
}
)WGSL";

    constexpr const char* kGeneratedVertex = R"WGSL(
struct gl_PerVertex {
    @builtin(position) gl_Position: vec4<f32>,
    gl_PointSize: f32,
    gl_ClipDistance: array<f32, 1>,
}

struct VertexOutput {
    @builtin(position) gl_Position: vec4<f32>,
    @location(0) member: vec2<f32>,
    @location(1) member_1: vec4<f32>,
}

@vertex
fn main(@location(0) aPos: vec2<f32>, @location(1) aTexCoord: vec2<f32>, @location(4) aBones: vec4<u32>) -> VertexOutput {
    return VertexOutput(vec4<f32>(aPos, 0f, 1f), aTexCoord, vec4<f32>(aBones));
}
)WGSL";

    constexpr const char* kGeneratedCompute = R"WGSL(
struct Output {
    uValues: array<u32>,
}
struct CnaCommand {
    cnaCommand: array<atomic<u32>>,
}
struct Params {
    uCount: i32,
    uScale: f32,
}

@group(0) @binding(0)
var<storage, read_write> unnamed: Output;
@group(0) @binding(1)
var<storage> unnamed_1: Output;
@group(0) @binding(2)
var<storage, read_write> counts: CnaCommand;
@group(0) @binding(3)
var uImage: texture_storage_2d<rgba8unorm,write>;
@group(3) @binding(0)
var<uniform> params: Params;
var<workgroup> sharedSums: array<f32, 64>;

@compute @workgroup_size(4, 2, 2)
fn main(@builtin(global_invocation_id) id: vec3<u32>) {
    unnamed.uValues[id.x] = unnamed_1.uValues[id.x];
}
)WGSL";
}

TEST(WebGPUWgslReflectionTest, GeneratedFragmentReflectsSplitSamplerAndScalarBlock)
{
    const WgslModuleReflection r = ReflectWgsl(kGeneratedFragment);
    ASSERT_TRUE(r.ok) << r.error;
    ASSERT_EQ(r.resources.size(), 3u);

    const WgslResourceBinding* texture = r.FindResource(0, 0);
    ASSERT_NE(texture, nullptr);
    EXPECT_EQ(texture->kind, WgslResourceKind::SampledTexture);
    EXPECT_EQ(texture->viewDimension, WGPUTextureViewDimension_2D);
    EXPECT_EQ(texture->sampleType, WGPUTextureSampleType_Float);
    EXPECT_FALSE(texture->multisampled);

    const WgslResourceBinding* sampler = r.FindResource(0, 32);
    ASSERT_NE(sampler, nullptr);
    EXPECT_EQ(sampler->kind, WgslResourceKind::Sampler);

    const WgslResourceBinding* block = r.FindResource(3, 0);
    ASSERT_NE(block, nullptr);
    EXPECT_EQ(block->kind, WgslResourceKind::UniformBuffer);
    EXPECT_EQ(block->type, "PushConstants");
    // vec2 (8) padded to mat4's 16-byte alignment, mat4 (64), vec4 (16): the std140 offsets the
    // Vulkan push-constant block has, which is what makes the generated WGSL a faithful copy.
    EXPECT_EQ(block->minBindingSize, 96u);

    const auto& pc = r.structs.at("PushConstants");
    ASSERT_EQ(pc.members.size(), 3u);
    EXPECT_EQ(pc.members[0].offset, 0u);
    EXPECT_EQ(pc.members[1].offset, 16u);
    EXPECT_EQ(pc.members[2].offset, 80u);
    EXPECT_EQ(pc.layout.align, 16u);

    const WgslEntryPoint* fragment = r.FindEntryPoint(WGPUShaderStage_Fragment);
    ASSERT_NE(fragment, nullptr);
    EXPECT_EQ(fragment->name, "main");
    EXPECT_EQ(fragment->colorOutputCount, 1u);
    EXPECT_EQ(r.FindEntryPoint(WGPUShaderStage_Vertex), nullptr);
}

TEST(WebGPUWgslReflectionTest, VertexInputsComeFromParametersInLocationOrder)
{
    const WgslModuleReflection r = ReflectWgsl(kGeneratedVertex);
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_TRUE(r.resources.empty());
    const WgslEntryPoint* vertex = r.FindEntryPoint(WGPUShaderStage_Vertex);
    ASSERT_NE(vertex, nullptr);
    ASSERT_EQ(vertex->vertexInputs.size(), 3u);
    EXPECT_EQ(vertex->vertexInputs[0].location, 0u);
    EXPECT_EQ(vertex->vertexInputs[0].type, "vec2<f32>");
    EXPECT_EQ(vertex->vertexInputs[1].location, 1u);
    EXPECT_EQ(vertex->vertexInputs[2].location, 4u);
    EXPECT_EQ(vertex->vertexInputs[2].type, "vec4<u32>");
}

TEST(WebGPUWgslReflectionTest, VertexInputsInsideAnInputStructAreFound)
{
    const WgslModuleReflection r = ReflectWgsl(R"WGSL(
struct In {
    @location(1) normal: vec3f,
    @location(0) position: vec3f,
    @builtin(instance_index) instance: u32,
}
@vertex fn vs_main(input: In) -> @builtin(position) vec4f { return vec4f(input.position, 1.0); }
)WGSL");
    ASSERT_TRUE(r.ok) << r.error;
    const WgslEntryPoint* vertex = r.FindEntryPoint(WGPUShaderStage_Vertex);
    ASSERT_NE(vertex, nullptr);
    EXPECT_EQ(vertex->name, "vs_main");
    ASSERT_EQ(vertex->vertexInputs.size(), 2u);
    EXPECT_EQ(vertex->vertexInputs[0].location, 0u);
    EXPECT_EQ(vertex->vertexInputs[1].location, 1u);
}

TEST(WebGPUWgslReflectionTest, ComputeResourcesAccessAndWorkgroupSize)
{
    const WgslModuleReflection r = ReflectWgsl(kGeneratedCompute);
    ASSERT_TRUE(r.ok) << r.error;

    const WgslResourceBinding* output = r.FindResource(0, 0);
    ASSERT_NE(output, nullptr);
    EXPECT_EQ(output->kind, WgslResourceKind::StorageBuffer);
    EXPECT_EQ(output->minBindingSize, 4u);   // a runtime-sized array needs one element

    const WgslResourceBinding* input = r.FindResource(0, 1);
    ASSERT_NE(input, nullptr);
    EXPECT_EQ(input->kind, WgslResourceKind::ReadOnlyStorageBuffer);

    const WgslResourceBinding* counts = r.FindResource(0, 2);
    ASSERT_NE(counts, nullptr);
    EXPECT_EQ(counts->kind, WgslResourceKind::StorageBuffer);

    const WgslResourceBinding* image = r.FindResource(0, 3);
    ASSERT_NE(image, nullptr);
    EXPECT_EQ(image->kind, WgslResourceKind::StorageTexture);
    EXPECT_EQ(image->storageFormat, WGPUTextureFormat_RGBA8Unorm);
    EXPECT_EQ(image->storageAccess, WGPUStorageTextureAccess_WriteOnly);
    EXPECT_EQ(image->viewDimension, WGPUTextureViewDimension_2D);

    const auto& params = r.structs.at("Params");
    ASSERT_EQ(params.members.size(), 2u);
    EXPECT_EQ(params.members[0].name, "uCount");
    EXPECT_EQ(params.members[0].offset, 0u);
    EXPECT_EQ(params.members[1].name, "uScale");
    EXPECT_EQ(params.members[1].offset, 4u);

    const WgslEntryPoint* compute = r.FindEntryPoint(WGPUShaderStage_Compute);
    ASSERT_NE(compute, nullptr);
    EXPECT_EQ(compute->workgroupSize[0], 4u);
    EXPECT_EQ(compute->workgroupSize[1], 2u);
    EXPECT_EQ(compute->workgroupSize[2], 2u);
}

TEST(WebGPUWgslReflectionTest, WorkgroupSizeMayOmitAxesOrNameAConstant)
{
    const WgslModuleReflection one = ReflectWgsl(
        "@compute @workgroup_size(64) fn main() {}");
    ASSERT_TRUE(one.ok) << one.error;
    EXPECT_EQ(one.entryPoints[0].workgroupSize[0], 64u);
    EXPECT_EQ(one.entryPoints[0].workgroupSize[1], 1u);
    EXPECT_EQ(one.entryPoints[0].workgroupSize[2], 1u);

    const WgslModuleReflection named = ReflectWgsl(
        "const WIDTH: u32 = 8u;\n@compute @workgroup_size(WIDTH, WIDTH) fn main() {}");
    ASSERT_TRUE(named.ok) << named.error;
    EXPECT_EQ(named.entryPoints[0].workgroupSize[0], 8u);
    EXPECT_EQ(named.entryPoints[0].workgroupSize[1], 8u);
}

TEST(WebGPUWgslReflectionTest, LayoutFollowsTheWgslMemoryRules)
{
    const WgslModuleReflection r = ReflectWgsl(R"WGSL(
struct Inner { a: f32, }
struct S {
    v3: vec3<f32>,
    f: f32,
    v2: vec2<f32>,
    m3: mat3x3<f32>,
    arr: array<vec3<f32>, 4>,
    @align(16) inner: Inner,
    @size(32) padded: f32,
    tail: vec4f,
}
@group(1) @binding(19) var<uniform> s: S;
)WGSL");
    ASSERT_TRUE(r.ok) << r.error;
    const auto& s = r.structs.at("S");
    ASSERT_EQ(s.members.size(), 8u);
    EXPECT_EQ(s.members[0].offset, 0u);    // vec3: align 16, size 12
    EXPECT_EQ(s.members[1].offset, 12u);   // f32 packs into the vec3's tail
    EXPECT_EQ(s.members[2].offset, 16u);   // vec2: align 8
    EXPECT_EQ(s.members[3].offset, 32u);   // mat3x3: align 16, size 48
    EXPECT_EQ(s.members[4].offset, 80u);   // array<vec3, 4>: stride 16, size 64
    EXPECT_EQ(s.members[5].offset, 144u);  // @align(16) struct
    EXPECT_EQ(s.members[6].offset, 148u);  // Inner is 4 bytes
    EXPECT_EQ(s.members[6].size, 32u);     // @size(32)
    EXPECT_EQ(s.members[7].offset, 192u);  // vec4f: 180 rounded up to 16
    EXPECT_EQ(s.layout.size, 208u);
    EXPECT_EQ(r.FindResource(1, 19)->minBindingSize, 208u);

    const std::unordered_map<std::string, WgslStruct> none;
    EXPECT_EQ(WgslLayoutOf("array<f32, 72>", none)->size, 288u);
    EXPECT_EQ(WgslLayoutOf("array<vec2<f32>, 72>", none)->size, 576u);
    EXPECT_EQ(WgslLayoutOf("array<mat4x4<f32>, 72>", none)->size, 4608u);
    EXPECT_EQ(WgslLayoutOf("mat2x2<f32>", none)->size, 16u);
    EXPECT_EQ(WgslLayoutOf("mat4x4f", none)->align, 16u);
    EXPECT_FALSE(WgslLayoutOf("bool", none).has_value());
}

TEST(WebGPUWgslReflectionTest, EveryHandleTypeIsClassified)
{
    const WgslModuleReflection r = ReflectWgsl(R"WGSL(
@group(1) @binding(0) var a: texture_2d_array<f32>;
@group(1) @binding(1) var b: texture_cube<f32>;
@group(1) @binding(2) var c: texture_3d<f32>;
@group(1) @binding(3) var d: texture_depth_2d;
@group(1) @binding(4) var e: texture_2d<u32>;
@group(1) @binding(5) var f: texture_multisampled_2d<f32>;
@group(1) @binding(6) var g: sampler_comparison;
@group(1) @binding(7) var h: texture_storage_2d<r32float, read_write>;
@group(1) @binding(8) var i: texture_storage_2d<rgba16float, read>;
)WGSL");
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_EQ(r.FindResource(1, 0)->viewDimension, WGPUTextureViewDimension_2DArray);
    EXPECT_EQ(r.FindResource(1, 1)->viewDimension, WGPUTextureViewDimension_Cube);
    EXPECT_EQ(r.FindResource(1, 2)->viewDimension, WGPUTextureViewDimension_3D);
    EXPECT_EQ(r.FindResource(1, 3)->sampleType, WGPUTextureSampleType_Depth);
    EXPECT_EQ(r.FindResource(1, 4)->sampleType, WGPUTextureSampleType_Uint);
    EXPECT_TRUE(r.FindResource(1, 5)->multisampled);
    EXPECT_EQ(r.FindResource(1, 6)->kind, WgslResourceKind::ComparisonSampler);
    EXPECT_EQ(r.FindResource(1, 7)->storageAccess, WGPUStorageTextureAccess_ReadWrite);
    EXPECT_EQ(r.FindResource(1, 7)->storageFormat, WGPUTextureFormat_R32Float);
    EXPECT_EQ(r.FindResource(1, 8)->storageAccess, WGPUStorageTextureAccess_ReadOnly);
}

TEST(WebGPUWgslReflectionTest, LegacyShaderEffectConventionStillReflects)
{
    const WgslModuleReflection r = ReflectWgsl(R"WGSL(
// A WEBGPU-76 custom ShaderEffect: the uniform block at 0, sampler at 1, texture at 2.
/* nested /* comment */ still a comment */
struct U { World: mat4x4<f32>, View: mat4x4<f32>, Projection: mat4x4<f32>, tint: vec4<f32>, };
@group(0) @binding(0) var<uniform> u: U;
@group(0) @binding(1) var s: sampler;
@group(0) @binding(2) var t: texture_2d<f32>;
struct VOut { @builtin(position) pos: vec4<f32>, @location(0) uv: vec2<f32>, };
@vertex fn vs_main(@location(0) p: vec3<f32>, @location(1) uv: vec2<f32>) -> VOut {
    var o: VOut; o.pos = u.Projection * u.View * u.World * vec4<f32>(p, 1.0); o.uv = uv; return o;
}
@fragment fn fs_main(i: VOut) -> @location(0) vec4<f32> { return textureSample(t, s, i.uv) * u.tint; }
)WGSL");
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_EQ(r.resources.size(), 3u);
    EXPECT_EQ(r.FindResource(0, 0)->minBindingSize, 208u);
    EXPECT_EQ(r.FindEntryPoint(WGPUShaderStage_Vertex)->name, "vs_main");
    EXPECT_EQ(r.FindEntryPoint(WGPUShaderStage_Fragment)->name, "fs_main");
}

TEST(WebGPUWgslReflectionTest, UnsupportedDeclarationsAreRefusedByName)
{
    const WgslModuleReflection immediate = ReflectWgsl(
        "struct P { a: f32, }\nvar<immediate> pc: P;\n@fragment fn main() {}");
    EXPECT_FALSE(immediate.ok);
    EXPECT_NE(immediate.error.find("immediate"), std::string::npos) << immediate.error;

    const WgslModuleReflection external = ReflectWgsl(
        "@group(0) @binding(0) var video: texture_external;");
    EXPECT_FALSE(external.ok);
    EXPECT_NE(external.error.find("texture_external"), std::string::npos) << external.error;

    const WgslModuleReflection unbound = ReflectWgsl("var t: texture_2d<f32>;");
    EXPECT_FALSE(unbound.ok);
    EXPECT_NE(unbound.error.find("@group"), std::string::npos) << unbound.error;
}
