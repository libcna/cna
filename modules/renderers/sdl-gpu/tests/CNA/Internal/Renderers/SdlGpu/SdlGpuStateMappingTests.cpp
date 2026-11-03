// SPDX-License-Identifier: MS-PL
// Renderer-native test (non-production SDL audit category renderer-native-runtime-test): the
// XNA -> SDL GPU state, format and vertex translation tables can only be checked against the
// native enum values they produce, so this file names those native types directly.
#if defined(CNA_RENDERER_SDLGPU) || defined(CNA_RENDERER_PRESENT_SDLGPU)
#include "CNA/Internal/Renderers/SdlGpu/SdlGpuStateMapping.hpp"
#include <gtest/gtest.h>
#include <functional>
#include <set>
#include <map>

using namespace CNA::Internal::Renderers::SdlGpu;
using namespace CNA::Internal::Renderers::SdlGpu::Detail;
using namespace Microsoft::Xna::Framework::Graphics;

// Oracles: FNA src/Graphics/States enum ordinals and SDL_gpu.h's named operations.
// Exception: Microsoft.Xna.Framework.Graphics.dll BlendFunction literal fields are
// Min=0x00000003, Max=0x00000004 (ikdasm metadata); FNA reverses their names.
// Explicit tables intentionally do not obtain their expected values from CNA translators.
TEST(SdlGpuStateMapping, BlendFactorsAndEquations)
{
    const SDL_GPUBlendFactor factors[] = {
        SDL_GPU_BLENDFACTOR_ONE, SDL_GPU_BLENDFACTOR_ZERO, SDL_GPU_BLENDFACTOR_SRC_COLOR,
        SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_COLOR, SDL_GPU_BLENDFACTOR_SRC_ALPHA,
        SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA, SDL_GPU_BLENDFACTOR_DST_COLOR,
        SDL_GPU_BLENDFACTOR_ONE_MINUS_DST_COLOR, SDL_GPU_BLENDFACTOR_DST_ALPHA,
        SDL_GPU_BLENDFACTOR_ONE_MINUS_DST_ALPHA, SDL_GPU_BLENDFACTOR_CONSTANT_COLOR,
        SDL_GPU_BLENDFACTOR_ONE_MINUS_CONSTANT_COLOR, SDL_GPU_BLENDFACTOR_SRC_ALPHA_SATURATE};
    for (int i = 0; i < 13; ++i) EXPECT_EQ(ToBlendFactor(i), factors[i]) << i;
    const SDL_GPUBlendOp ops[] = {SDL_GPU_BLENDOP_ADD, SDL_GPU_BLENDOP_SUBTRACT,
        SDL_GPU_BLENDOP_REVERSE_SUBTRACT, SDL_GPU_BLENDOP_MIN, SDL_GPU_BLENDOP_MAX};
    for (int i = 0; i < 5; ++i) EXPECT_EQ(ToBlendOp(i), ops[i]) << i;
    SdlGpuRenderer::RenderStateSnapshot rs;
    rs.blendEnabled = true;
    rs.blend = {4, 5, 0, 1, 3, 4};
    SDL_GPUColorTargetBlendState out{};
    FillBlendState(out, rs);
    EXPECT_TRUE(out.enable_blend);
    EXPECT_EQ(out.src_color_blendfactor, SDL_GPU_BLENDFACTOR_SRC_ALPHA);
    EXPECT_EQ(out.dst_color_blendfactor, SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA);
    EXPECT_EQ(out.src_alpha_blendfactor, SDL_GPU_BLENDFACTOR_ONE);
    EXPECT_EQ(out.dst_alpha_blendfactor, SDL_GPU_BLENDFACTOR_ZERO);
    EXPECT_EQ(out.color_blend_op, SDL_GPU_BLENDOP_MIN);
    EXPECT_EQ(out.alpha_blend_op, SDL_GPU_BLENDOP_MAX);
}

TEST(SdlGpuStateMapping, CompareAndStencilOperations)
{
    const SDL_GPUCompareOp compares[] = {SDL_GPU_COMPAREOP_ALWAYS, SDL_GPU_COMPAREOP_NEVER,
        SDL_GPU_COMPAREOP_LESS, SDL_GPU_COMPAREOP_LESS_OR_EQUAL, SDL_GPU_COMPAREOP_EQUAL,
        SDL_GPU_COMPAREOP_GREATER_OR_EQUAL, SDL_GPU_COMPAREOP_GREATER, SDL_GPU_COMPAREOP_NOT_EQUAL};
    const SDL_GPUStencilOp stencil[] = {SDL_GPU_STENCILOP_KEEP, SDL_GPU_STENCILOP_ZERO,
        SDL_GPU_STENCILOP_REPLACE, SDL_GPU_STENCILOP_INCREMENT_AND_WRAP,
        SDL_GPU_STENCILOP_DECREMENT_AND_WRAP, SDL_GPU_STENCILOP_INCREMENT_AND_CLAMP,
        SDL_GPU_STENCILOP_DECREMENT_AND_CLAMP, SDL_GPU_STENCILOP_INVERT};
    for (int i = 0; i < 8; ++i) {
        EXPECT_EQ(ToCompareOp(i), compares[i]) << i;
        EXPECT_EQ(ToStencilOp(i), stencil[i]) << i;
    }
}

TEST(SdlGpuStateMapping, ClockwiseOrdinaryStencilAndCounterclockwiseStateStayDistinct)
{
    // XNA DepthStencilState.Apply IL_01cf..0242 writes CounterClockwise* to
    // D3DRS_CCW_STENCIL* (0xba..0xbd). Ordinary stencil is CW; SDL front_face is CCW.
    SdlGpuRenderer::RenderStateSnapshot rs;
    rs.stencil = {true, 2, 1, 2, 3, true, 6, 4, 5, 7, 0x1234, 0x5678};
    SDL_GPUDepthStencilState out{};
    FillDepthStencilState(out, true, false, 3, rs);
    EXPECT_TRUE(out.enable_stencil_test);
    EXPECT_EQ(out.compare_mask, 0x34); EXPECT_EQ(out.write_mask, 0x78);
    EXPECT_EQ(out.back_stencil_state.compare_op, SDL_GPU_COMPAREOP_LESS);
    EXPECT_EQ(out.back_stencil_state.fail_op, SDL_GPU_STENCILOP_ZERO);
    EXPECT_EQ(out.back_stencil_state.depth_fail_op, SDL_GPU_STENCILOP_REPLACE);
    EXPECT_EQ(out.back_stencil_state.pass_op, SDL_GPU_STENCILOP_INCREMENT_AND_WRAP);
    EXPECT_EQ(out.front_stencil_state.compare_op, SDL_GPU_COMPAREOP_GREATER);
    EXPECT_EQ(out.front_stencil_state.fail_op, SDL_GPU_STENCILOP_DECREMENT_AND_WRAP);
    EXPECT_EQ(out.front_stencil_state.depth_fail_op, SDL_GPU_STENCILOP_INCREMENT_AND_CLAMP);
    EXPECT_EQ(out.front_stencil_state.pass_op, SDL_GPU_STENCILOP_INVERT);
    rs.stencil.twoSided = false;
    FillDepthStencilState(out, true, false, 3, rs);
    EXPECT_EQ(out.front_stencil_state.compare_op, SDL_GPU_COMPAREOP_LESS);
    EXPECT_EQ(out.front_stencil_state.fail_op, SDL_GPU_STENCILOP_ZERO);
    EXPECT_EQ(out.front_stencil_state.depth_fail_op, SDL_GPU_STENCILOP_REPLACE);
    EXPECT_EQ(out.front_stencil_state.pass_op, SDL_GPU_STENCILOP_INCREMENT_AND_WRAP);
}

TEST(SdlGpuStateMapping, DepthEnableAndWriteAreIndependentInputs)
{
    // SDL_gpu.h SDL_GPUDepthStencilState: writes are ALWAYS disabled if enable_depth_test=false.
    // Thus the write flag may stay true here; a GPU regression separately verifies the pixels.
    for (bool test : {false, true}) for (bool write : {false, true}) {
        SDL_GPUDepthStencilState out{};
        FillDepthStencilState(out, test, write, 6, {});
        EXPECT_EQ(out.enable_depth_test, test);
        if (test) EXPECT_EQ(out.enable_depth_write, write);
        EXPECT_EQ(out.compare_op, SDL_GPU_COMPAREOP_GREATER);
        EXPECT_FALSE(out.enable_stencil_test);
    }
}

TEST(SdlGpuStateMapping, RasterizerWindingFillAndDepthClip)
{
    const SDL_GPUCullMode culls[] = {SDL_GPU_CULLMODE_NONE, SDL_GPU_CULLMODE_BACK, SDL_GPU_CULLMODE_FRONT};
    for (int cull = 0; cull < 3; ++cull) for (bool wire : {false, true}) {
        SdlGpuRenderer::RenderStateSnapshot rs;
        rs.cullMode = cull; rs.wireframe = wire;
        SDL_GPURasterizerState out{};
        FillRasterizerState(out, rs, SDL_GPU_PRIMITIVETYPE_TRIANGLELIST, SDL_GPU_TEXTUREFORMAT_D16_UNORM);
        EXPECT_EQ(out.cull_mode, culls[cull]);
        EXPECT_EQ(out.front_face, SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE);
        EXPECT_EQ(out.fill_mode, wire ? SDL_GPU_FILLMODE_LINE : SDL_GPU_FILLMODE_FILL);
        EXPECT_TRUE(out.enable_depth_clip); EXPECT_FALSE(out.enable_depth_bias);
    }
}

TEST(SdlGpuStateMapping, EveryColorWriteMaskAndMrtSlot)
{
    // XNA ColorWriteChannels bits R/G/B/A = 1/2/4/8; disabled SDL mask means all four.
    for (int mask = 0; mask < 16; ++mask) {
        SdlGpuRenderer::RenderStateSnapshot rs;
        rs.colorWriteMasks = {mask, (mask+1)%16, (mask+2)%16, (mask+3)%16};
        std::array<SDL_GPUColorTargetDescription, 4> out{};
        const SdlGpuColorTargetFormatsEXT formats{SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
            SDL_GPU_TEXTUREFORMAT_R32_FLOAT, SDL_GPU_TEXTUREFORMAT_R16_FLOAT,
            SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT};
        FillColorTargetDescriptions(out, 4, formats, rs);
        for (int slot = 0; slot < 4; ++slot) {
            EXPECT_EQ(out[slot].format, formats[slot]);
            const auto& blend = out[slot].blend_state;
            EXPECT_FALSE(blend.enable_blend);
            EXPECT_EQ(blend.enable_color_write_mask ? blend.color_write_mask : 15, (mask+slot)%16);
        }
    }
}

TEST(SdlGpuStateMapping, EverySamplerFilterComponentAndAddressAxis)
{
    // TextureFilter names specify min/mag/mip independently. 0=nearest, 1=linear here.
    const int expected[9][3] = {{1,1,1},{0,0,0},{1,1,1},{1,1,0},{0,0,1},
        {1,0,1},{1,0,0},{0,1,1},{0,1,0}};
    const SDL_GPUSamplerAddressMode address[] = {SDL_GPU_SAMPLERADDRESSMODE_REPEAT,
        SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE, SDL_GPU_SAMPLERADDRESSMODE_MIRRORED_REPEAT};
    for (int filter = 0; filter < 9; ++filter) for (int u = 0; u < 3; ++u) for (int v = 0; v < 3; ++v) {
        SCOPED_TRACE(filter*100+u*10+v);
        SDL_GPUSamplerCreateInfo out{};
        FillSdlGpuSamplerCreateInfo(out, filter, u, v, 8);
        EXPECT_EQ(out.min_filter, expected[filter][0] ? SDL_GPU_FILTER_LINEAR : SDL_GPU_FILTER_NEAREST);
        EXPECT_EQ(out.mag_filter, expected[filter][1] ? SDL_GPU_FILTER_LINEAR : SDL_GPU_FILTER_NEAREST);
        EXPECT_EQ(out.mipmap_mode, expected[filter][2] ? SDL_GPU_SAMPLERMIPMAPMODE_LINEAR : SDL_GPU_SAMPLERMIPMAPMODE_NEAREST);
        EXPECT_EQ(out.address_mode_u, address[u]); EXPECT_EQ(out.address_mode_v, address[v]);
        EXPECT_EQ(ToAddressMode(u), address[u]); // also used for W in GetOrCreateSampler
        EXPECT_EQ(out.enable_anisotropy, filter == 2);
        EXPECT_FLOAT_EQ(out.max_anisotropy, filter == 2 ? 8.0f : 1.0f);
        EXPECT_FLOAT_EQ(out.min_lod, 0); EXPECT_FLOAT_EQ(out.max_lod, 32);
        EXPECT_FLOAT_EQ(out.mip_lod_bias, 0);
    }
    for (const auto [requested, expectedAniso] : {std::pair{-10,1}, {0,1}, {1,1}, {16,16}, {100,16}}) {
        SDL_GPUSamplerCreateInfo out{};
        FillSdlGpuSamplerCreateInfo(out, 2, 0, 0, requested);
        EXPECT_FLOAT_EQ(out.max_anisotropy, expectedAniso); // XNA supported range 1..16
    }
}

TEST(SdlGpuStateMapping, EveryVertexFormatAndUnknownRefusal)
{
    // FNA Graphics/Vertices/VertexElementFormat.cs plus SDL_gpu.h signed/normalized formats.
    const SDL_GPUVertexElementFormat formats[] = {SDL_GPU_VERTEXELEMENTFORMAT_FLOAT,
        SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3,
        SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM,
        SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4, SDL_GPU_VERTEXELEMENTFORMAT_SHORT2,
        SDL_GPU_VERTEXELEMENTFORMAT_SHORT4, SDL_GPU_VERTEXELEMENTFORMAT_SHORT2_NORM,
        SDL_GPU_VERTEXELEMENTFORMAT_SHORT4_NORM, SDL_GPU_VERTEXELEMENTFORMAT_HALF2,
        SDL_GPU_VERTEXELEMENTFORMAT_HALF4};
    for (int i=0; i<12; ++i) EXPECT_EQ(ToSdlGpuStockVertexFormat(static_cast<VertexElementFormat>(i)), formats[i]) << i;
    for (int i : {-1,12,1000}) EXPECT_THROW((void)ToSdlGpuStockVertexFormat(static_cast<VertexElementFormat>(i)), std::invalid_argument);
}

TEST(SdlGpuStateMapping, PolygonBiasUsesAttachmentUnitsAndLinesIgnoreBias)
{
    // D16 unit 1/(2^16-1); D24 unit 1/(2^24-1). Normalized bias times denominator.
    for (auto topology : {SDL_GPU_PRIMITIVETYPE_TRIANGLELIST, SDL_GPU_PRIMITIVETYPE_TRIANGLESTRIP}) {
        const auto bias = NormalizeDepthBias(topology, SDL_GPU_TEXTUREFORMAT_D16_UNORM, 0.25f, -2);
        EXPECT_TRUE(bias.enabled); EXPECT_FLOAT_EQ(bias.constantFactor, 16383.75f);
        EXPECT_FLOAT_EQ(bias.slopeFactor, -2);
        const auto d24 = NormalizeDepthBias(topology, SDL_GPU_TEXTUREFORMAT_D24_UNORM, 1, 0);
        EXPECT_FLOAT_EQ(d24.constantFactor, 16777215);
        const auto zero = NormalizeDepthBias(topology, SDL_GPU_TEXTUREFORMAT_D16_UNORM, -0.0f, -0.0f);
        EXPECT_FALSE(zero.enabled); EXPECT_EQ(std::bit_cast<std::uint32_t>(zero.constantFactor), 0u);
    }
    for (auto topology : {SDL_GPU_PRIMITIVETYPE_LINELIST, SDL_GPU_PRIMITIVETYPE_LINESTRIP, SDL_GPU_PRIMITIVETYPE_POINTLIST}) {
        const auto bias = NormalizeDepthBias(topology, SDL_GPU_TEXTUREFORMAT_D16_UNORM, 1, 2);
        EXPECT_FALSE(bias.enabled); EXPECT_FLOAT_EQ(bias.constantFactor, 0); EXPECT_FLOAT_EQ(bias.slopeFactor, 0);
    }
}

namespace {
struct Request {
    SDL_GPUPrimitiveType topology = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    bool test = true, write = true;
    int func = 3, count = 4;
    SdlGpuColorTargetFormatsEXT colors{SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
        SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
        SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM};
    SDL_GPUSampleCount samples = SDL_GPU_SAMPLECOUNT_1;
    SDL_GPUTextureFormat depth = SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT;
    SdlGpuRenderer::RenderStateSnapshot state;
    Request() { state.blendEnabled = true; state.stencil.enable = true; state.stencil.twoSided = true; }
    std::size_t key() const { return PipelineCacheKey(topology, test, write, func, colors, count, samples, depth, state); }
};
}

TEST(SdlGpuPipelineKeys, EveryBakedFieldDistinguishesRequests)
{
    // SDL immutable pipeline compatibility requires each of these dimensions; no golden hash.
    const Request base;
    const std::function<void(Request&)> variants[] = {
        [](auto& r){r.topology=SDL_GPU_PRIMITIVETYPE_TRIANGLESTRIP;},
        [](auto& r){r.test=false;}, [](auto& r){r.write=false;}, [](auto& r){r.func=6;},
        [](auto& r){r.count=3;}, [](auto& r){r.samples=SDL_GPU_SAMPLECOUNT_4;},
        [](auto& r){r.depth=SDL_GPU_TEXTUREFORMAT_INVALID;},
        [](auto& r){r.depth=SDL_GPU_TEXTUREFORMAT_D16_UNORM;},
        [](auto& r){r.state.blendEnabled=false;},
        [](auto& r){r.state.blend.colorSrc=4;}, [](auto& r){r.state.blend.colorDst=5;},
        [](auto& r){r.state.blend.alphaSrc=8;}, [](auto& r){r.state.blend.alphaDst=9;},
        [](auto& r){r.state.blend.colorFunc=3;}, [](auto& r){r.state.blend.alphaFunc=4;},
        [](auto& r){r.state.cullMode=1;}, [](auto& r){r.state.wireframe=true;},
        [](auto& r){r.state.depthBias=0.125f;}, [](auto& r){r.state.slopeScaleDepthBias=2;},
        [](auto& r){r.state.stencil.enable=false;}, [](auto& r){r.state.stencil.func=2;},
        [](auto& r){r.state.stencil.fail=1;}, [](auto& r){r.state.stencil.depthFail=2;},
        [](auto& r){r.state.stencil.pass=3;}, [](auto& r){r.state.stencil.readMask=0x3f;},
        [](auto& r){r.state.stencil.writeMask=0x7f;}, [](auto& r){r.state.stencil.twoSided=false;},
        [](auto& r){r.state.stencil.ccwFunc=6;}, [](auto& r){r.state.stencil.ccwFail=7;},
        [](auto& r){r.state.stencil.ccwDepthFail=5;}, [](auto& r){r.state.stencil.ccwPass=4;}
    };
    std::set<std::size_t> keys{base.key()};
    int index=0;
    for (const auto& change : variants) { Request r=base; change(r); EXPECT_TRUE(keys.insert(r.key()).second) << index++; EXPECT_EQ(r.key(), Request(r).key()); }
    for (int slot=0; slot<4; ++slot) {
        Request r=base; r.colors[slot]=SDL_GPU_TEXTUREFORMAT_R32_FLOAT;
        EXPECT_TRUE(keys.insert(r.key()).second) << "format slot " << slot;
        r=base; r.state.colorWriteMasks[slot]=7;
        EXPECT_TRUE(keys.insert(r.key()).second) << "mask slot " << slot;
    }
}

TEST(SdlGpuPipelineKeys, DisabledAndDynamicFieldsDoNotFragmentCache)
{
    Request a; a.state.blendEnabled=false; a.state.stencil.enable=false;
    Request b=a;
    b.state.blend={4,5,8,9,3,4}; b.state.stencil.func=6; b.state.stencil.ccwPass=7;
    b.state.stencil.readMask=0; b.state.stencil.twoSided=false; b.state.stencilReference=100;
    EXPECT_EQ(a.key(), b.key()); // stencil reference is set dynamically by SDL_SetGPUStencilReference
    a=Request{}; b=a; b.state.stencil.readMask=0x1ff; b.state.stencil.writeMask=0x7fffffff;
    EXPECT_EQ(a.key(), b.key()); // native masks are Uint8
    a.count=1; b=a; b.colors[3]=SDL_GPU_TEXTUREFORMAT_R32_FLOAT; b.state.colorWriteMasks[3]=0;
    EXPECT_EQ(a.key(), b.key()); // unbound MRT slots do not exist in the pipeline
    a.state.stencil.twoSided=false; b=a; b.state.stencil.ccwFunc=6;
    EXPECT_EQ(a.key(), b.key());
    a.topology=SDL_GPU_PRIMITIVETYPE_LINELIST; b=a; b.state.depthBias=1; b.state.slopeScaleDepthBias=2;
    EXPECT_EQ(a.key(), b.key()); // polygon bias cannot change a line pipeline
}

TEST(SdlGpuPipelineKeys, AttachmentCompatibilitySweepHasNoAliases)
{
    // Distinct active attachment tuples must never hit an incompatible cached pipeline.
    std::map<std::size_t,std::string> keys;
    for (int c0=0; c0<3; ++c0) for (int c1=0; c1<3; ++c1)
    for (int c2=0; c2<3; ++c2) for (int c3=0; c3<3; ++c3)
    for (auto depth : {SDL_GPU_TEXTUREFORMAT_INVALID, SDL_GPU_TEXTUREFORMAT_D16_UNORM,
         SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT, SDL_GPU_TEXTUREFORMAT_D32_FLOAT_S8_UINT})
    for (auto samples : {SDL_GPU_SAMPLECOUNT_1, SDL_GPU_SAMPLECOUNT_2, SDL_GPU_SAMPLECOUNT_4}) {
        const SDL_GPUTextureFormat choices[] = {SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
            SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT, SDL_GPU_TEXTUREFORMAT_R32_FLOAT};
        Request r; r.colors={choices[c0],choices[c1],choices[c2],choices[c3]}; r.depth=depth; r.samples=samples;
        // Keep every tuple legal even without a depth attachment; compatibility still matters
        // with both depth and stencil disabled, as in an ordinary SpriteBatch pass.
        r.test=false; r.write=false; r.state.stencil.enable=false;
        const auto label=std::to_string(c0)+std::to_string(c1)+std::to_string(c2)+std::to_string(c3)+
            "/"+std::to_string(depth)+"/"+std::to_string(samples);
        const auto [it,inserted]=keys.emplace(r.key(),label);
        ASSERT_TRUE(inserted) << "attachment collision: " << it->second << " versus " << label;
    }
    EXPECT_EQ(keys.size(), 972u); // 3^4 * 4 * 3
}

TEST(SdlGpuPipelineKeys, FixedInstanceLayoutChangesIdentityAndDescriptor)
{
    SDL_GPUVertexBufferDescription record{}; record.pitch=28;
    SDL_GPUVertexAttribute attr{}; attr.format=SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
    const auto plain=MakeFixedVertexInputEXT(record, &attr, 1, 0, {});
    EXPECT_EQ(plain.bufferCount, 1u); EXPECT_EQ(plain.attributeCount, 1u);
    const auto input=MakeFixedVertexInputEXT(record, &attr, 1, 80, {4,20,36,52});
    EXPECT_EQ(input.bufferCount, 2u); EXPECT_EQ(input.attributeCount, 5u);
    EXPECT_EQ(input.buffers[1].pitch, 80u); EXPECT_EQ(input.buffers[1].input_rate, SDL_GPU_VERTEXINPUTRATE_INSTANCE);
    for (int i=0; i<4; ++i) {
        EXPECT_EQ(input.attributes[i+1].location, 12u+i);
        EXPECT_EQ(input.attributes[i+1].buffer_slot, 1u);
        EXPECT_EQ(input.attributes[i+1].format, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4);
        EXPECT_EQ(input.attributes[i+1].offset, 4u+16u*i);
    }
    const auto base=FixedInstancePipelineKey(123,80,{4,20,36,52});
    EXPECT_EQ(FixedInstancePipelineKey(123,0,{}),123u);
    EXPECT_NE(base,FixedInstancePipelineKey(123,64,{4,20,36,52}));
    for (int i=0; i<4; ++i) { std::array<Uint32,4> offsets{4,20,36,52}; offsets[i]+=4; EXPECT_NE(base,FixedInstancePipelineKey(123,80,offsets)); }
}

TEST(SdlGpuStateMapping, TextureAndTargetFormatsUseExactNativeStorage)
{
    // SurfaceFormat.cs storage widths; SDL_gpu.h format channel widths/types. Sampled
    // short-channel formats are expanded to RGBA; render targets retain native channel counts.
    struct Row { SurfaceFormat format; SDL_GPUTextureFormat sampled; int bytes;
                 SDL_GPUTextureFormat target; int targetBytes; };
    const Row rows[] = {
        {SurfaceFormat::Color, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,4,SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,4},
        {SurfaceFormat::Bgr565, SDL_GPU_TEXTUREFORMAT_B5G6R5_UNORM,2,SDL_GPU_TEXTUREFORMAT_INVALID,0},
        {SurfaceFormat::Bgra5551, SDL_GPU_TEXTUREFORMAT_B5G5R5A1_UNORM,2,SDL_GPU_TEXTUREFORMAT_INVALID,0},
        {SurfaceFormat::Bgra4444, SDL_GPU_TEXTUREFORMAT_B4G4R4A4_UNORM,2,SDL_GPU_TEXTUREFORMAT_INVALID,0},
        {SurfaceFormat::Dxt1, SDL_GPU_TEXTUREFORMAT_BC1_RGBA_UNORM,8,SDL_GPU_TEXTUREFORMAT_INVALID,0},
        {SurfaceFormat::Dxt3, SDL_GPU_TEXTUREFORMAT_BC2_RGBA_UNORM,16,SDL_GPU_TEXTUREFORMAT_INVALID,0},
        {SurfaceFormat::Dxt5, SDL_GPU_TEXTUREFORMAT_BC3_RGBA_UNORM,16,SDL_GPU_TEXTUREFORMAT_INVALID,0},
        {SurfaceFormat::NormalizedByte2, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_SNORM,2,SDL_GPU_TEXTUREFORMAT_INVALID,0},
        {SurfaceFormat::NormalizedByte4, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_SNORM,4,SDL_GPU_TEXTUREFORMAT_INVALID,0},
        {SurfaceFormat::Rgba1010102, SDL_GPU_TEXTUREFORMAT_R10G10B10A2_UNORM,4,SDL_GPU_TEXTUREFORMAT_INVALID,0},
        {SurfaceFormat::Rg32, SDL_GPU_TEXTUREFORMAT_R16G16B16A16_UNORM,4,SDL_GPU_TEXTUREFORMAT_INVALID,0},
        {SurfaceFormat::Rgba64, SDL_GPU_TEXTUREFORMAT_R16G16B16A16_UNORM,8,SDL_GPU_TEXTUREFORMAT_R16G16B16A16_UNORM,8},
        {SurfaceFormat::Alpha8, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,1,SDL_GPU_TEXTUREFORMAT_INVALID,0},
        {SurfaceFormat::Single, SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT,4,SDL_GPU_TEXTUREFORMAT_R32_FLOAT,4},
        {SurfaceFormat::Vector2, SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT,8,SDL_GPU_TEXTUREFORMAT_R32G32_FLOAT,8},
        {SurfaceFormat::Vector4, SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT,16,SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT,16},
        {SurfaceFormat::HalfSingle, SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT,2,SDL_GPU_TEXTUREFORMAT_R16_FLOAT,2},
        {SurfaceFormat::HalfVector2, SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT,4,SDL_GPU_TEXTUREFORMAT_R16G16_FLOAT,4},
        {SurfaceFormat::HalfVector4, SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT,8,SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT,8},
        {SurfaceFormat::HdrBlendable, SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT,8,SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT,8}
    };
    for (const auto& row : rows) {
        SCOPED_TRACE(static_cast<int>(row.format));
        EXPECT_EQ(PreferredTextureFormat(row.format),row.sampled);
        EXPECT_EQ(LogicalTextureBlockBytes(row.format),row.bytes);
        RenderTargetFormatInfo info;
        EXPECT_EQ(TryGetRenderTargetFormatInfo(row.format,info),row.targetBytes != 0);
        if (row.targetBytes) { EXPECT_EQ(info.nativeFormat,row.target); EXPECT_EQ(info.bytesPerPixel,row.targetBytes); }
    }
    for (int ordinal : {-1,10000}) {
        auto format=static_cast<SurfaceFormat>(ordinal); RenderTargetFormatInfo info;
        EXPECT_EQ(PreferredTextureFormat(format),SDL_GPU_TEXTUREFORMAT_INVALID);
        EXPECT_EQ(LogicalTextureBlockBytes(format),0);
        EXPECT_FALSE(TryGetRenderTargetFormatInfo(format,info));
    }
}

TEST(SdlGpuPipelineKeys, StockVertexStreamsAndNeutralRecordAreBaked)
{
    // Vertex declarations feed byte offsets/strides and instance rates to SDL unchanged;
    // a missing color comes from the separate neutral record, never stream zero.
    using namespace CNA::Internal::Graphics;
    ResolvedStockVertexLayoutEXT layout{};
    layout.stride=28; layout.streamCount=2;
    layout.streams[0].stride=28; layout.streams[1].stride=16;
    layout.streams[1].instanceFrequency=1;
    layout.count=2; layout.usesNeutralRecord=true;
    layout.attributes[0].format=VertexElementFormat::Vector3;
    layout.attributes[0].shaderLocation=3; layout.attributes[0].streamIndex=1; layout.attributes[0].offset=4;
    layout.attributes[1].format=VertexElementFormat::Vector4;
    layout.attributes[1].shaderLocation=7; layout.attributes[1].defaulted=true; layout.attributes[1].offset=16;
    SdlGpuStockVertexStateEXT out;
    BuildSdlGpuStockVertexStateEXT(layout,out);
    ASSERT_EQ(out.bufferCount,3u); ASSERT_EQ(out.attributeCount,2u);
    EXPECT_EQ(out.buffers[0].pitch,28u); EXPECT_EQ(out.buffers[0].input_rate,SDL_GPU_VERTEXINPUTRATE_VERTEX);
    EXPECT_EQ(out.buffers[1].pitch,16u); EXPECT_EQ(out.buffers[1].input_rate,SDL_GPU_VERTEXINPUTRATE_INSTANCE);
    EXPECT_EQ(out.buffers[2].slot,2u); EXPECT_EQ(out.buffers[2].input_rate,SDL_GPU_VERTEXINPUTRATE_INSTANCE);
    EXPECT_EQ(out.attributes[0].location,3u); EXPECT_EQ(out.attributes[0].buffer_slot,1u);
    EXPECT_EQ(out.attributes[0].format,SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3); EXPECT_EQ(out.attributes[0].offset,4u);
    EXPECT_EQ(out.attributes[1].buffer_slot,2u); EXPECT_EQ(out.attributes[1].offset,16u);
    const auto key=StockPipelineKey(42,layout);
    const std::function<void(ResolvedStockVertexLayoutEXT&)> changes[] = {
        [](auto& l){l.streams[0].stride=32;}, [](auto& l){l.streams[1].instanceFrequency=0;},
        [](auto& l){l.attributes[0].offset=8;}, [](auto& l){l.attributes[0].shaderLocation=4;},
        [](auto& l){l.attributes[0].format=VertexElementFormat::Vector4;},
        [](auto& l){l.attributes[0].streamIndex=0;}, [](auto& l){l.attributes[1].defaulted=false;}
    };
    for (const auto& change : changes) { auto other=layout; change(other); EXPECT_NE(key,StockPipelineKey(42,other)); }
    EXPECT_EQ(key,StockPipelineKey(42,layout));
}
#endif
