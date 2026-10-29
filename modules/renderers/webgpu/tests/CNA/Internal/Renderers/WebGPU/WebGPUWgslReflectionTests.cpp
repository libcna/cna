// SPDX-License-Identifier: MS-PL
//
// WMG-0004: the WGSL reflection the WebGPU renderer builds its
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

    // What naga emits for a std140 block of narrow arrays after the generator's std430 rewrite
    // (tools/shader_package, `cna-webgpu-glsl/1`): a read-only storage block, tightly packed.
    constexpr const char* kGeneratedStorageArrays = R"WGSL(
struct Floats {
    uFloats: array<f32, 72>,
}
struct Weights {
    uWeights: array<f32>,
}
struct Params {
    uCount: i32,
    uScale: f32,
}

@group(1) @binding(12)
var<storage> unnamed: Floats;
@group(1) @binding(13)
var<storage, read> unnamed_1: Weights;
@group(3) @binding(0)
var<uniform> params: Params;

@fragment
fn main(@location(0) TexCoord: vec2<f32>) -> @location(0) vec4<f32> {
    return vec4<f32>(unnamed.uFloats[0] * unnamed_1.uWeights[0] * params.uScale);
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

TEST(WebGPUWgslReflectionTest, GeneratedStorageArraysReflectAsReadOnlyStorage)
{
    const WgslModuleReflection r = ReflectWgsl(kGeneratedStorageArrays);
    ASSERT_TRUE(r.ok) << r.error;

    const WgslResourceBinding* floats = r.FindResource(1, 12);
    ASSERT_NE(floats, nullptr);
    EXPECT_EQ(floats->kind, WgslResourceKind::ReadOnlyStorageBuffer);
    EXPECT_EQ(floats->minBindingSize, 72u * 4u);   // std430: a tightly packed array<f32, 72>

    const WgslResourceBinding* weights = r.FindResource(1, 13);
    ASSERT_NE(weights, nullptr);
    EXPECT_EQ(weights->kind, WgslResourceKind::ReadOnlyStorageBuffer);
    EXPECT_EQ(weights->minBindingSize, 4u);   // a runtime-sized array needs one element

    const auto& params = r.structs.at("Params");
    ASSERT_EQ(params.members.size(), 2u);
    EXPECT_EQ(params.members[0].name, "uCount");
    EXPECT_EQ(params.members[0].offset, 0u);
    EXPECT_EQ(params.members[1].name, "uScale");
    EXPECT_EQ(params.members[1].offset, 4u);

    ASSERT_NE(r.FindEntryPoint(WGPUShaderStage_Fragment), nullptr);
}

TEST(WebGPUWgslReflectionTest, WritableStorageAndStorageImagesAreRefusedByName)
{
    // Nothing in CNA binds a writable block or a storage image to a graphics stage, so both are
    // refused where the shader is read rather than failing later inside wgpu-native.
    const WgslModuleReflection writable = ReflectWgsl(
        "struct B { v: array<u32>, }\n@group(0) @binding(0) var<storage, read_write> b: B;");
    EXPECT_FALSE(writable.ok);
    EXPECT_NE(writable.error.find("read_write"), std::string::npos) << writable.error;

    const WgslModuleReflection image = ReflectWgsl(
        "@group(0) @binding(0) var i: texture_storage_2d<rgba8unorm, write>;");
    EXPECT_FALSE(image.ok);
    EXPECT_NE(image.error.find("texture_storage_2d"), std::string::npos) << image.error;
}

TEST(WebGPUWgslReflectionTest, ComputeEntryPointsAreOrdinaryFunctions)
{
    // A module may carry one; this renderer builds no compute pipeline, so it is not reported as
    // an entry point and its @workgroup_size is never evaluated.
    const WgslModuleReflection r = ReflectWgsl(
        "const WIDTH: u32 = 8u;\n"
        "var<workgroup> shared: array<f32, 64>;\n"
        "@compute @workgroup_size(WIDTH, WIDTH) fn main() {}\n"
        "@fragment fn fs_main() -> @location(0) vec4<f32> { return vec4<f32>(1.0); }");
    ASSERT_TRUE(r.ok) << r.error;
    ASSERT_EQ(r.entryPoints.size(), 1u);
    EXPECT_EQ(r.entryPoints[0].stage, WGPUShaderStage_Fragment);
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
)WGSL");
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_EQ(r.FindResource(1, 0)->viewDimension, WGPUTextureViewDimension_2DArray);
    EXPECT_EQ(r.FindResource(1, 1)->viewDimension, WGPUTextureViewDimension_Cube);
    EXPECT_EQ(r.FindResource(1, 2)->viewDimension, WGPUTextureViewDimension_3D);
    EXPECT_EQ(r.FindResource(1, 3)->sampleType, WGPUTextureSampleType_Depth);
    EXPECT_EQ(r.FindResource(1, 4)->sampleType, WGPUTextureSampleType_Uint);
    EXPECT_TRUE(r.FindResource(1, 5)->multisampled);
    EXPECT_EQ(r.FindResource(1, 6)->kind, WgslResourceKind::ComparisonSampler);
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
