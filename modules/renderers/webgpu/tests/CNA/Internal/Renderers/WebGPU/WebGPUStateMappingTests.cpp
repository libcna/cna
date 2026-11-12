// SPDX-License-Identifier: MS-PL
//
// TQ-012: the WebGPU renderer's pure XNA -> WebGPU translations (WebGPUStateMapping.hpp). CPU-only:
// no adapter, no device, no window.
//
// Every expectation here comes from outside this renderer:
//  - XNA's enum ordinals and meanings, as the public CNA headers spell them (they match FNA's
//    src/Graphics/States/*.cs, except BlendFunction, where CNA follows the Microsoft ordinals
//    Min = 3, Max = 4 that FNA swaps -- see BlendFunction.hpp);
//  - the WebGPU specification (GPUCompareFunction, GPUStencilOperation, GPUBlendFactor, texel block
//    copy footprints, vertex format byte sizes, the 256-byte bytesPerRow rule, the
//    maxAnisotropy/filter validation rule, the stripIndexFormat rule);
//  - FNA3D's GPU-API driver (FNA3D_Driver_SDL.c and its XNA-to-native state tables), the
//    reference implementation that maps the same XNA state onto a modern explicit API;
//  - hand-derived arithmetic, shown next to each value.

#include "CNA/Internal/Renderers/WebGPU/WebGPUStateMapping.hpp"

#include "Microsoft/Xna/Framework/Graphics/Blend.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendFunction.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/CompareFunction.hpp"
#include "Microsoft/Xna/Framework/Graphics/CullMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/StencilOperation.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureAddressMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureFilter.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"

#include <gtest/gtest.h>

#include <array>
#include <limits>
#include <cstdint>
#include <set>
#include <stdexcept>
#include <string>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

using namespace CNA::Internal::Renderers::WebGPU;
using Microsoft::Xna::Framework::Graphics::Blend;
using Microsoft::Xna::Framework::Graphics::BlendFunction;
using Microsoft::Xna::Framework::Graphics::BlendState;
using Microsoft::Xna::Framework::Graphics::CompareFunction;
using Microsoft::Xna::Framework::Graphics::CullMode;
using Microsoft::Xna::Framework::Graphics::DepthFormat;
using Microsoft::Xna::Framework::Graphics::DepthStencilState;
using Microsoft::Xna::Framework::Graphics::StencilOperation;
using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
using Microsoft::Xna::Framework::Graphics::TextureAddressMode;
using Microsoft::Xna::Framework::Graphics::TextureFilter;
using Microsoft::Xna::Framework::Graphics::VertexElementFormat;

namespace
{
    template <typename E>
    constexpr int Ord(E value) { return static_cast<int>(value); }

    using StencilKey = WebGPURenderer::StencilKeyParams;
    using BlendKey = WebGPURenderer::BlendKeyParams;

    /// The fields of one WGPUStencilFaceState, comparable as a tuple.
    std::tuple<int, int, int, int> FaceOf(const WGPUStencilFaceState& face)
    {
        return {static_cast<int>(face.compare), static_cast<int>(face.failOp),
                static_cast<int>(face.depthFailOp), static_cast<int>(face.passOp)};
    }

    /// Everything FillWGPUStencilState writes, so two states can be compared as pipeline state.
    std::tuple<std::tuple<int, int, int, int>, std::tuple<int, int, int, int>, std::uint32_t, std::uint32_t>
    FilledStencil(const StencilKey& s)
    {
        WGPUDepthStencilState ds = WGPU_DEPTH_STENCIL_STATE_INIT;
        FillWGPUStencilState(ds, s, true);
        return {FaceOf(ds.stencilFront), FaceOf(ds.stencilBack), ds.stencilReadMask, ds.stencilWriteMask};
    }
}

// ---------------------------------------------------------------------------------------------
// 1. Comparison, stencil, blend and cull state.
// ---------------------------------------------------------------------------------------------

TEST(WebGPUStateMapping, CompareFunctionKeepsTheXnaPredicateForEveryOrdinal)
{
    // XNA CompareFunction names the predicate "new value OP stored value"; GPUCompareFunction names
    // the same predicate "provided value OP sampled/stored value". FNA3D's CompareOp table pairs
    // the same eight names in the same order.
    const std::array<std::pair<CompareFunction, WGPUCompareFunction>, 8> table{{
        {CompareFunction::Always,       WGPUCompareFunction_Always},
        {CompareFunction::Never,        WGPUCompareFunction_Never},
        {CompareFunction::Less,         WGPUCompareFunction_Less},
        {CompareFunction::LessEqual,    WGPUCompareFunction_LessEqual},
        {CompareFunction::Equal,        WGPUCompareFunction_Equal},
        {CompareFunction::GreaterEqual, WGPUCompareFunction_GreaterEqual},
        {CompareFunction::Greater,      WGPUCompareFunction_Greater},
        {CompareFunction::NotEqual,     WGPUCompareFunction_NotEqual},
    }};
    for (const auto& [xna, expected] : table)
        EXPECT_EQ(ToWGPUCompareFunction(Ord(xna)), expected) << "XNA CompareFunction ordinal " << Ord(xna);
}

TEST(WebGPUStateMapping, StencilOperationWrapsOnlyTheUnsaturatedIncrementAndDecrement)
{
    // XNA: Increment/Decrement wrap ("wrapping to 0 if the new value exceeds the maximum value",
    // D3DSTENCILOP_INCR/DECR); IncrementSaturation/DecrementSaturation clamp (INCRSAT/DECRSAT).
    // FNA3D's StencilOp table: INCREMENT_AND_WRAP, DECREMENT_AND_WRAP, INCREMENT_AND_CLAMP,
    // DECREMENT_AND_CLAMP in that order.
    const std::array<std::pair<StencilOperation, WGPUStencilOperation>, 8> table{{
        {StencilOperation::Keep,                WGPUStencilOperation_Keep},
        {StencilOperation::Zero,                WGPUStencilOperation_Zero},
        {StencilOperation::Replace,             WGPUStencilOperation_Replace},
        {StencilOperation::Increment,           WGPUStencilOperation_IncrementWrap},
        {StencilOperation::Decrement,           WGPUStencilOperation_DecrementWrap},
        {StencilOperation::IncrementSaturation, WGPUStencilOperation_IncrementClamp},
        {StencilOperation::DecrementSaturation, WGPUStencilOperation_DecrementClamp},
        {StencilOperation::Invert,              WGPUStencilOperation_Invert},
    }};
    for (const auto& [xna, expected] : table)
        EXPECT_EQ(ToWGPUStencilOperation(Ord(xna)), expected) << "XNA StencilOperation ordinal " << Ord(xna);
}

TEST(WebGPUStateMapping, BlendFactorKeepsEveryXnaBlendMeaning)
{
    // XNA Blend -> GPUBlendFactor of the same term; FNA3D's BlendFactor table uses the same
    // order. BlendFactor/InverseBlendFactor are the device's BlendFactor colour, which WebGPU calls
    // the blend "constant".
    const std::array<std::pair<Blend, WGPUBlendFactor>, 13> table{{
        {Blend::One,                     WGPUBlendFactor_One},
        {Blend::Zero,                    WGPUBlendFactor_Zero},
        {Blend::SourceColor,             WGPUBlendFactor_Src},
        {Blend::InverseSourceColor,      WGPUBlendFactor_OneMinusSrc},
        {Blend::SourceAlpha,             WGPUBlendFactor_SrcAlpha},
        {Blend::InverseSourceAlpha,      WGPUBlendFactor_OneMinusSrcAlpha},
        {Blend::DestinationColor,        WGPUBlendFactor_Dst},
        {Blend::InverseDestinationColor, WGPUBlendFactor_OneMinusDst},
        {Blend::DestinationAlpha,        WGPUBlendFactor_DstAlpha},
        {Blend::InverseDestinationAlpha, WGPUBlendFactor_OneMinusDstAlpha},
        {Blend::BlendFactor,             WGPUBlendFactor_Constant},
        {Blend::InverseBlendFactor,      WGPUBlendFactor_OneMinusConstant},
        {Blend::SourceAlphaSaturation,   WGPUBlendFactor_SrcAlphaSaturated},
    }};
    for (const auto& [xna, expected] : table)
        EXPECT_EQ(ToWGPUBlendFactor(Ord(xna)), expected) << "XNA Blend ordinal " << Ord(xna);
}

TEST(WebGPUStateMapping, BlendFunctionUsesTheMicrosoftMinMaxOrdinalsNotFnas)
{
    // The XNA assemblies number Min = 3 and Max = 4; FNA's BlendFunction.cs declares Max before
    // Min, and FNA3D's BlendOp table follows FNA. A table copied from FNA3D would therefore
    // swap the two -- which is exactly what the explicit ordinals below would catch.
    ASSERT_EQ(Ord(BlendFunction::Min), 3);
    ASSERT_EQ(Ord(BlendFunction::Max), 4);
    EXPECT_EQ(ToWGPUBlendOperation(Ord(BlendFunction::Add)), WGPUBlendOperation_Add);
    EXPECT_EQ(ToWGPUBlendOperation(Ord(BlendFunction::Subtract)), WGPUBlendOperation_Subtract);
    EXPECT_EQ(ToWGPUBlendOperation(Ord(BlendFunction::ReverseSubtract)), WGPUBlendOperation_ReverseSubtract);
    EXPECT_EQ(ToWGPUBlendOperation(Ord(BlendFunction::Min)), WGPUBlendOperation_Min);
    EXPECT_EQ(ToWGPUBlendOperation(Ord(BlendFunction::Max)), WGPUBlendOperation_Max);
}

TEST(WebGPUStateMapping, BlendStateRoutesColourAndAlphaTermsIndependently)
{
    // Six distinct inputs, so a colour/alpha or source/destination cross-wiring changes a result.
    BlendKey bp;
    bp.colorSrc = Ord(Blend::SourceColor);
    bp.colorDst = Ord(Blend::InverseDestinationColor);
    bp.colorFunc = Ord(BlendFunction::Subtract);
    bp.alphaSrc = Ord(Blend::DestinationAlpha);
    bp.alphaDst = Ord(Blend::InverseBlendFactor);
    bp.alphaFunc = Ord(BlendFunction::ReverseSubtract);
    WGPUBlendState state = WGPU_BLEND_STATE_INIT;
    FillWGPUBlendState(state, bp);
    EXPECT_EQ(state.color.srcFactor, WGPUBlendFactor_Src);
    EXPECT_EQ(state.color.dstFactor, WGPUBlendFactor_OneMinusDst);
    EXPECT_EQ(state.color.operation, WGPUBlendOperation_Subtract);
    EXPECT_EQ(state.alpha.srcFactor, WGPUBlendFactor_DstAlpha);
    EXPECT_EQ(state.alpha.dstFactor, WGPUBlendFactor_OneMinusConstant);
    EXPECT_EQ(state.alpha.operation, WGPUBlendOperation_ReverseSubtract);
}

TEST(WebGPUStateMapping, StockBlendStatesTranslateToTheirDocumentedEquations)
{
    // XNA 4.0 documents the four stock states: Opaque = One/Zero, AlphaBlend = One/InverseSourceAlpha
    // (premultiplied), Additive = SourceAlpha/One, NonPremultiplied = SourceAlpha/InverseSourceAlpha,
    // each with Add for colour and alpha alike.
    struct Row { const BlendState* state; WGPUBlendFactor src; WGPUBlendFactor dst; const char* name; };
    const std::array<Row, 4> rows{{
        {&BlendState::Opaque, WGPUBlendFactor_One, WGPUBlendFactor_Zero, "Opaque"},
        {&BlendState::AlphaBlend, WGPUBlendFactor_One, WGPUBlendFactor_OneMinusSrcAlpha, "AlphaBlend"},
        {&BlendState::Additive, WGPUBlendFactor_SrcAlpha, WGPUBlendFactor_One, "Additive"},
        {&BlendState::NonPremultiplied, WGPUBlendFactor_SrcAlpha, WGPUBlendFactor_OneMinusSrcAlpha,
         "NonPremultiplied"},
    }};
    for (const Row& row : rows)
    {
        BlendKey bp;
        bp.colorSrc = Ord(row.state->getColorSourceBlendProperty());
        bp.colorDst = Ord(row.state->getColorDestinationBlendProperty());
        bp.colorFunc = Ord(row.state->getColorBlendFunctionProperty());
        bp.alphaSrc = Ord(row.state->getAlphaSourceBlendProperty());
        bp.alphaDst = Ord(row.state->getAlphaDestinationBlendProperty());
        bp.alphaFunc = Ord(row.state->getAlphaBlendFunctionProperty());
        WGPUBlendState state = WGPU_BLEND_STATE_INIT;
        FillWGPUBlendState(state, bp);
        EXPECT_EQ(state.color.srcFactor, row.src) << row.name;
        EXPECT_EQ(state.color.dstFactor, row.dst) << row.name;
        EXPECT_EQ(state.alpha.srcFactor, row.src) << row.name;
        EXPECT_EQ(state.alpha.dstFactor, row.dst) << row.name;
        EXPECT_EQ(state.color.operation, WGPUBlendOperation_Add) << row.name;
        EXPECT_EQ(state.alpha.operation, WGPUBlendOperation_Add) << row.name;
    }
}

TEST(WebGPUStateMapping, CullModeRemovesTheFaceXnaNames)
{
    // Every WebGPU pipeline here is built with frontFace = CCW. FNA3D's GPU-API driver
    // (FNA3D_Driver_SDL.c) builds its pipelines with front_face = CLOCKWISE and maps
    // CullClockwiseFace -> CULLMODE_FRONT,
    // CullCounterClockwiseFace -> CULLMODE_BACK. With the front face declared the other way round,
    // the same windings are WebGPU's BACK and FRONT faces respectively.
    EXPECT_EQ(ToWGPUCullMode(Ord(CullMode::None)), WGPUCullMode_None);
    EXPECT_EQ(ToWGPUCullMode(Ord(CullMode::CullClockwiseFace)), WGPUCullMode_Back);
    EXPECT_EQ(ToWGPUCullMode(Ord(CullMode::CullCounterClockwiseFace)), WGPUCullMode_Front);
}

TEST(WebGPUStateMapping, TwoSidedStencilGivesEachWindingTheStateItsCullModeNames)
{
    // XNA applies DepthStencilState's ordinary stencil state to CLOCKWISE triangles and the
    // CounterClockwise* state to COUNTER-CLOCKWISE ones (D3DRS_CCW_STENCIL*; the shared
    // TwoSidedStencilTest pins it on Software/EasyGL, and FNA3D's GPU-API driver
    // (FNA3D_Driver_SDL.c) puts the ordinary state on its front = clockwise face). CullMode names the same two windings, so the WebGPU face
    // slot that CullClockwiseFace removes must carry the ordinary state, and the slot
    // CullCounterClockwiseFace removes the counter-clockwise state -- whatever the pipeline's
    // frontFace happens to be.
    StencilKey s;
    s.enable = true;
    s.twoSided = true;
    s.func = Ord(CompareFunction::Equal);
    s.stencilPass = Ord(StencilOperation::Replace);
    s.stencilFail = Ord(StencilOperation::Zero);
    s.depthFail = Ord(StencilOperation::Invert);
    s.ccwFunc = Ord(CompareFunction::NotEqual);
    s.ccwPass = Ord(StencilOperation::IncrementSaturation);
    s.ccwFail = Ord(StencilOperation::DecrementSaturation);
    s.ccwDepthFail = Ord(StencilOperation::Increment);
    WGPUDepthStencilState ds = WGPU_DEPTH_STENCIL_STATE_INIT;
    FillWGPUStencilState(ds, s, true);

    const auto slotCulledBy = [&ds](CullMode mode) -> const WGPUStencilFaceState& {
        const WGPUCullMode culled = ToWGPUCullMode(Ord(mode));
        return culled == WGPUCullMode_Front ? ds.stencilFront : ds.stencilBack;
    };
    const WGPUStencilFaceState& clockwise = slotCulledBy(CullMode::CullClockwiseFace);
    const WGPUStencilFaceState& counterClockwise = slotCulledBy(CullMode::CullCounterClockwiseFace);
    ASSERT_NE(&clockwise, &counterClockwise) << "the two cull modes must name different faces";

    EXPECT_EQ(clockwise.compare, WGPUCompareFunction_Equal) << "ordinary StencilFunction";
    EXPECT_EQ(clockwise.passOp, WGPUStencilOperation_Replace) << "ordinary StencilPass";
    EXPECT_EQ(clockwise.failOp, WGPUStencilOperation_Zero) << "ordinary StencilFail";
    EXPECT_EQ(clockwise.depthFailOp, WGPUStencilOperation_Invert) << "ordinary StencilDepthBufferFail";
    EXPECT_EQ(counterClockwise.compare, WGPUCompareFunction_NotEqual) << "CounterClockwiseStencilFunction";
    EXPECT_EQ(counterClockwise.passOp, WGPUStencilOperation_IncrementClamp) << "CounterClockwiseStencilPass";
    EXPECT_EQ(counterClockwise.failOp, WGPUStencilOperation_DecrementClamp) << "CounterClockwiseStencilFail";
    EXPECT_EQ(counterClockwise.depthFailOp, WGPUStencilOperation_IncrementWrap)
        << "CounterClockwiseStencilDepthBufferFail";
}

TEST(WebGPUStateMapping, TwoSidedStencilLeavesLinesAndPointsOnTheOrdinaryState)
{
    // A line or point has no winding. XNA's two-sided mode keeps such a primitive on the ordinary
    // stencil state (the shared stencil_matrix_contract_test: "two-sided mode ignores the CCW tuple
    // for line primitives", passing on EasyGL and Software), while WebGPU counts every non-polygon
    // primitive as front-facing. Whatever slot that is, both must carry the ordinary state.
    StencilKey s;
    s.enable = true;
    s.twoSided = true;
    s.func = Ord(CompareFunction::Equal);
    s.stencilPass = Ord(StencilOperation::Replace);
    s.stencilFail = Ord(StencilOperation::Zero);
    s.depthFail = Ord(StencilOperation::Invert);
    s.ccwFunc = Ord(CompareFunction::NotEqual);
    s.ccwPass = Ord(StencilOperation::IncrementSaturation);
    s.ccwFail = Ord(StencilOperation::DecrementSaturation);
    s.ccwDepthFail = Ord(StencilOperation::Increment);
    WGPUDepthStencilState ds = WGPU_DEPTH_STENCIL_STATE_INIT;
    FillWGPUStencilState(ds, s, false);
    const std::tuple<int, int, int, int> ordinary{
        WGPUCompareFunction_Equal, WGPUStencilOperation_Zero, WGPUStencilOperation_Invert,
        WGPUStencilOperation_Replace};
    EXPECT_EQ(FaceOf(ds.stencilFront), ordinary) << "front (where WebGPU puts lines and points)";
    EXPECT_EQ(FaceOf(ds.stencilBack), ordinary);
}

TEST(WebGPUStateMapping, OneSidedStencilAppliesTheOrdinaryStateToBothFacesAndIgnoresTheCcwFields)
{
    StencilKey s;
    s.enable = true;
    s.twoSided = false;
    s.func = Ord(CompareFunction::Less);
    s.stencilPass = Ord(StencilOperation::Increment);
    s.stencilFail = Ord(StencilOperation::Decrement);
    s.depthFail = Ord(StencilOperation::Replace);
    // Different values in every counter-clockwise field: with TwoSidedStencilMode off they must not
    // reach either face.
    s.ccwFunc = Ord(CompareFunction::Never);
    s.ccwPass = Ord(StencilOperation::Zero);
    s.ccwFail = Ord(StencilOperation::Zero);
    s.ccwDepthFail = Ord(StencilOperation::Zero);
    WGPUDepthStencilState ds = WGPU_DEPTH_STENCIL_STATE_INIT;
    FillWGPUStencilState(ds, s, true);
    const std::tuple<int, int, int, int> expected{
        WGPUCompareFunction_Less, WGPUStencilOperation_DecrementWrap, WGPUStencilOperation_Replace,
        WGPUStencilOperation_IncrementWrap};
    EXPECT_EQ(FaceOf(ds.stencilFront), expected);
    EXPECT_EQ(FaceOf(ds.stencilBack), expected);
}

TEST(WebGPUStateMapping, StencilMasksKeepTheEightBitsAStencil8AspectHas)
{
    // XNA's DepthStencilState constructor stores -1 ("all bits") in both masks
    // (Microsoft.Xna.Framework.Graphics.dll, DepthStencilState::.ctor, IL_00b0 and IL_00bd:
    // ldc.i4.m1 -> cachedStencilMask / cachedStencilWriteMask); FNA's constructor uses
    // Int32.MaxValue. A depth24plus-stencil8 attachment has eight stencil bits, so either default
    // must keep all eight.
    const DepthStencilState defaults;
    for (const int allBits : {defaults.getStencilMaskProperty(), defaults.getStencilWriteMaskProperty(),
                              -1, 0x7FFFFFFF})
    {
        StencilKey s;
        s.enable = true;
        s.readMask = allBits;
        s.writeMask = allBits;
        WGPUDepthStencilState ds = WGPU_DEPTH_STENCIL_STATE_INIT;
        FillWGPUStencilState(ds, s, true);
        EXPECT_EQ(ds.stencilReadMask, 0xFFu) << allBits;
        EXPECT_EQ(ds.stencilWriteMask, 0xFFu) << allBits;
    }

    // A partial mask keeps exactly its low byte: 0x1F0 & 0xFF = 0xF0, 0x0F stays 0x0F.
    StencilKey s;
    s.enable = true;
    s.readMask = 0x1F0;
    s.writeMask = 0x0F;
    WGPUDepthStencilState ds = WGPU_DEPTH_STENCIL_STATE_INIT;
    FillWGPUStencilState(ds, s, true);
    EXPECT_EQ(ds.stencilReadMask, 0xF0u);
    EXPECT_EQ(ds.stencilWriteMask, 0x0Fu);
}

TEST(WebGPUStateMapping, DisabledStencilLeavesTheDepthStencilStateUntouched)
{
    StencilKey s;
    s.enable = false;
    s.func = Ord(CompareFunction::Never);
    s.stencilPass = Ord(StencilOperation::Zero);
    s.readMask = 0x0F;
    WGPUDepthStencilState ds = WGPU_DEPTH_STENCIL_STATE_INIT;
    const WGPUDepthStencilState pristine = WGPU_DEPTH_STENCIL_STATE_INIT;
    FillWGPUStencilState(ds, s, true);
    EXPECT_EQ(FaceOf(ds.stencilFront), FaceOf(pristine.stencilFront));
    EXPECT_EQ(FaceOf(ds.stencilBack), FaceOf(pristine.stencilBack));
    EXPECT_EQ(ds.stencilReadMask, pristine.stencilReadMask);
    EXPECT_EQ(ds.stencilWriteMask, pristine.stencilWriteMask);
}

TEST(WebGPUStateMapping, DisabledDepthBufferNeitherTestsNorWrites)
{
    // XNA DepthBufferEnable = false is D3DRS_ZENABLE = FALSE: no depth test and no depth write,
    // whatever DepthBufferWriteEnable says (it defaults to true). GL (GL_DEPTH_TEST off) and Vulkan
    // (depthWriteEnable only counts while depthTestEnable is set) behave the same. WebGPU has no
    // enable flag: compare = always with depthWriteEnabled = true WOULD write, so the write must go.
    struct Row { bool test; bool write; CompareFunction function; WGPUOptionalBool write_; WGPUCompareFunction compare; };
    const std::array<Row, 6> rows{{
        {false, true,  CompareFunction::Less,      WGPUOptionalBool_False, WGPUCompareFunction_Always},
        {false, false, CompareFunction::Less,      WGPUOptionalBool_False, WGPUCompareFunction_Always},
        {false, true,  CompareFunction::Never,     WGPUOptionalBool_False, WGPUCompareFunction_Always},
        {true,  true,  CompareFunction::LessEqual, WGPUOptionalBool_True,  WGPUCompareFunction_LessEqual},
        {true,  false, CompareFunction::LessEqual, WGPUOptionalBool_False, WGPUCompareFunction_LessEqual},
        {true,  true,  CompareFunction::Greater,   WGPUOptionalBool_True,  WGPUCompareFunction_Greater},
    }};
    for (const Row& row : rows)
    {
        WGPUDepthStencilState ds = WGPU_DEPTH_STENCIL_STATE_INIT;
        FillWGPUDepthState(ds, row.test, row.write, Ord(row.function));
        EXPECT_EQ(ds.depthWriteEnabled, row.write_) << "enable=" << row.test << " write=" << row.write;
        EXPECT_EQ(ds.depthCompare, row.compare) << "enable=" << row.test << " write=" << row.write;
    }
}

TEST(WebGPUStateMapping, StencilCacheHashIsEqualExactlyWhenThePipelineStencilStateIsEqual)
{
    // The 3D pipeline caches fold HashStencilState into their key. Two states whose baked
    // WGPUDepthStencilState differs must hash differently (or one pipeline serves both), and two
    // states that bake identically should hash identically (or the cache fragments). A disabled
    // stencil bakes nothing at all, so it is its own class.
    std::vector<StencilKey> states;
    StencilKey base;
    base.enable = true;
    base.func = Ord(CompareFunction::Equal);
    base.stencilPass = Ord(StencilOperation::Replace);
    states.push_back(base);
    const auto vary = [&](auto mutate) { StencilKey v = base; mutate(v); states.push_back(v); };
    vary([](StencilKey& v) { v.func = Ord(CompareFunction::Greater); });
    vary([](StencilKey& v) { v.stencilPass = Ord(StencilOperation::Invert); });
    vary([](StencilKey& v) { v.stencilFail = Ord(StencilOperation::Zero); });
    vary([](StencilKey& v) { v.depthFail = Ord(StencilOperation::Zero); });
    vary([](StencilKey& v) { v.readMask = 0x0F; });
    vary([](StencilKey& v) { v.writeMask = 0xF0; });
    vary([](StencilKey& v) { v.readMask = 0x1FF; });               // same low byte as 0xFF
    vary([](StencilKey& v) { v.ccwFunc = Ord(CompareFunction::Never); });  // ignored: one-sided
    vary([](StencilKey& v) { v.twoSided = true; });               // ccw defaults differ from base
    vary([](StencilKey& v) { v.twoSided = true; v.ccwFunc = Ord(CompareFunction::Equal);
                             v.ccwPass = Ord(StencilOperation::Replace); });  // ccw == ordinary
    vary([](StencilKey& v) { v.twoSided = true; v.ccwFunc = Ord(CompareFunction::Less); });
    vary([](StencilKey& v) { v.twoSided = true; v.ccwPass = Ord(StencilOperation::Decrement); });
    vary([](StencilKey& v) { v.twoSided = true; v.ccwFail = Ord(StencilOperation::Increment); });
    vary([](StencilKey& v) { v.twoSided = true; v.ccwDepthFail = Ord(StencilOperation::Zero); });
    StencilKey off;
    states.push_back(off);
    StencilKey offOther = base;
    offOther.enable = false;
    states.push_back(offOther);

    for (std::size_t i = 0; i < states.size(); ++i)
    {
        if (!states[i].enable)
            EXPECT_EQ(HashStencilState(states[i]), 0u) << "state " << i;
        else
            EXPECT_NE(HashStencilState(states[i]), 0u) << "an enabled stencil collides with a disabled one: " << i;
        for (std::size_t j = i + 1; j < states.size(); ++j)
        {
            const bool sameClass = states[i].enable == states[j].enable;
            const bool samePipeline = sameClass &&
                (!states[i].enable || FilledStencil(states[i]) == FilledStencil(states[j]));
            const bool sameHash = HashStencilState(states[i]) == HashStencilState(states[j]);
            // One-sided and two-sided-with-identical-faces bake the same state; the hash is allowed
            // to keep them apart (it folds the flag), so only the direction that causes wrong reuse
            // is unconditional.
            if (!samePipeline)
                EXPECT_FALSE(sameHash) << "states " << i << " and " << j
                                       << " bake different stencil state but share a cache hash";
            else if (states[i].twoSided == states[j].twoSided)
                EXPECT_TRUE(sameHash) << "states " << i << " and " << j
                                      << " bake identical stencil state but fragment the cache";
        }
    }
}

// ---------------------------------------------------------------------------------------------
// 2. Samplers.
// ---------------------------------------------------------------------------------------------

TEST(WebGPUStateMapping, TextureAddressModeKeepsEachAxisItsOwnMode)
{
    // XNA Wrap/Clamp/Mirror = GPUAddressMode repeat/clamp-to-edge/mirror-repeat (FNA3D's
    // SamplerAddressMode table: REPEAT, CLAMP_TO_EDGE, MIRRORED_REPEAT).
    EXPECT_EQ(ToAddressMode(Ord(TextureAddressMode::Wrap)), WGPUAddressMode_Repeat);
    EXPECT_EQ(ToAddressMode(Ord(TextureAddressMode::Clamp)), WGPUAddressMode_ClampToEdge);
    EXPECT_EQ(ToAddressMode(Ord(TextureAddressMode::Mirror)), WGPUAddressMode_MirrorRepeat);

    // Three different modes on the three axes, so a U/V/W cross-wiring changes the answer.
    WGPUSamplerDescriptor d{};
    FillWGPUSamplerDescriptor(d, Ord(TextureFilter::Linear), Ord(TextureAddressMode::Wrap),
                              Ord(TextureAddressMode::Mirror), Ord(TextureAddressMode::Clamp), 0, 4);
    EXPECT_EQ(d.addressModeU, WGPUAddressMode_Repeat);
    EXPECT_EQ(d.addressModeV, WGPUAddressMode_MirrorRepeat);
    EXPECT_EQ(d.addressModeW, WGPUAddressMode_ClampToEdge);
    FillWGPUSamplerDescriptor(d, Ord(TextureFilter::Linear), Ord(TextureAddressMode::Clamp),
                              Ord(TextureAddressMode::Wrap), Ord(TextureAddressMode::Mirror), 0, 4);
    EXPECT_EQ(d.addressModeU, WGPUAddressMode_ClampToEdge);
    EXPECT_EQ(d.addressModeV, WGPUAddressMode_Repeat);
    EXPECT_EQ(d.addressModeW, WGPUAddressMode_MirrorRepeat);
}

TEST(WebGPUStateMapping, TextureFilterSplitsIntoMagMinAndMipExactlyAsXnaNamesThem)
{
    // FNA3D's MagFilter table / MinFilter table / MipFilter table, row by row; the XNA
    // names spell the same triple (e.g. MinLinearMagPointMipPoint = min Linear, mag Point, mip Point).
    constexpr WGPUFilterMode L = WGPUFilterMode_Linear;
    constexpr WGPUFilterMode P = WGPUFilterMode_Nearest;
    constexpr WGPUMipmapFilterMode ML = WGPUMipmapFilterMode_Linear;
    constexpr WGPUMipmapFilterMode MP = WGPUMipmapFilterMode_Nearest;
    struct Row { TextureFilter filter; WGPUFilterMode mag; WGPUFilterMode min; WGPUMipmapFilterMode mip; };
    const std::array<Row, 9> rows{{
        {TextureFilter::Linear,                     L, L, ML},
        {TextureFilter::Point,                      P, P, MP},
        {TextureFilter::Anisotropic,                L, L, ML},
        {TextureFilter::LinearMipPoint,             L, L, MP},
        {TextureFilter::PointMipLinear,             P, P, ML},
        {TextureFilter::MinLinearMagPointMipLinear, P, L, ML},
        {TextureFilter::MinLinearMagPointMipPoint,  P, L, MP},
        {TextureFilter::MinPointMagLinearMipLinear, L, P, ML},
        {TextureFilter::MinPointMagLinearMipPoint,  L, P, MP},
    }};
    for (const Row& row : rows)
    {
        WGPUSamplerDescriptor d{};
        FillWGPUSamplerDescriptor(d, Ord(row.filter), 0, 0, 0, 0, 1);
        EXPECT_EQ(d.magFilter, row.mag) << "TextureFilter ordinal " << Ord(row.filter);
        EXPECT_EQ(d.minFilter, row.min) << "TextureFilter ordinal " << Ord(row.filter);
        EXPECT_EQ(d.mipmapFilter, row.mip) << "TextureFilter ordinal " << Ord(row.filter);
    }
}

TEST(WebGPUStateMapping, AnisotropyIsUsedOnlyByTheAnisotropicFilterAndStaysWithinWebGpuRules)
{
    // WebGPU: maxAnisotropy >= 1, and a value above 1 is a validation error unless mag, min and
    // mipmap filters are all linear; implementations support at most 16. XNA applies MaxAnisotropy
    // only to TextureFilter.Anisotropic (D3DTEXF_ANISOTROPIC); every other filter -- including the
    // all-linear TextureFilter.Linear with XNA's default MaxAnisotropy of 4 -- samples without it.
    const std::array<int, 8> requested{0, 1, 2, 4, 8, 16, 17, 64};
    const std::array<int, 8> expectedAnisotropic{1, 1, 2, 4, 8, 16, 16, 16};
    for (int filter = 0; filter <= Ord(TextureFilter::MinPointMagLinearMipPoint); ++filter)
    {
        for (std::size_t i = 0; i < requested.size(); ++i)
        {
            WGPUSamplerDescriptor d{};
            FillWGPUSamplerDescriptor(d, filter, 0, 0, 0, 0, requested[i]);
            const int expected = filter == Ord(TextureFilter::Anisotropic) ? expectedAnisotropic[i] : 1;
            EXPECT_EQ(d.maxAnisotropy, expected) << "filter " << filter << ", MaxAnisotropy " << requested[i];
            EXPECT_GE(d.maxAnisotropy, 1) << "filter " << filter;
            if (d.maxAnisotropy > 1)
            {
                EXPECT_EQ(d.magFilter, WGPUFilterMode_Linear) << "filter " << filter;
                EXPECT_EQ(d.minFilter, WGPUFilterMode_Linear) << "filter " << filter;
                EXPECT_EQ(d.mipmapFilter, WGPUMipmapFilterMode_Linear) << "filter " << filter;
            }
        }
    }
}

TEST(WebGPUStateMapping, MaxMipLevelIsALowerLodClampThatNeverExceedsTheUpperOne)
{
    // XNA SamplerState.MaxMipLevel = D3DSAMP_MAXMIPLEVEL, "the LOD index of the largest map to use":
    // 0 (the default) = no restriction, n = never sample a level more detailed than n. That is
    // WebGPU's lodMinClamp. D3D9 reads the field as unsigned, so a negative value names the last
    // level, never level 0. WebGPU rejects lodMinClamp > lodMaxClamp or a negative lodMinClamp.
    const std::array<std::pair<int, float>, 6> rows{{
        {0, 0.0f}, {1, 1.0f}, {5, 5.0f}, {32, 32.0f}, {99, 32.0f}, {-1, 32.0f}}};
    for (const auto& [maxMipLevel, expectedMin] : rows)
    {
        WGPUSamplerDescriptor d{};
        FillWGPUSamplerDescriptor(d, 0, 0, 0, 0, maxMipLevel, 4);
        EXPECT_FLOAT_EQ(d.lodMinClamp, expectedMin) << "MaxMipLevel " << maxMipLevel;
        EXPECT_FLOAT_EQ(d.lodMaxClamp, 32.0f) << "MaxMipLevel " << maxMipLevel;
    }
    for (int maxMipLevel = -64; maxMipLevel <= 128; ++maxMipLevel)
    {
        WGPUSamplerDescriptor d{};
        FillWGPUSamplerDescriptor(d, 1, 1, 1, 1, maxMipLevel, 1);
        EXPECT_GE(d.lodMinClamp, 0.0f) << maxMipLevel;
        EXPECT_LE(d.lodMinClamp, d.lodMaxClamp) << maxMipLevel;
    }
}

// RV-MIX3 review #3: the slot-sampler cache key is the descriptor's, so MaxMipLevel 256 does not
// share level 0's sampler (a byte mask of the raw value did) while levels past the last one, which
// build the same descriptor, may share. Equal keys exactly when the lower clamps are equal.
TEST(WebGPUStateMapping, TheSlotSamplerKeyDistinguishesExactlyTheDescriptorsMaxMipLevelProduces)
{
    EXPECT_NE(WGPUSlotSamplerKey(1, 0, 0, 0, 0, 1), WGPUSlotSamplerKey(1, 0, 0, 0, 256, 1));
    EXPECT_NE(WGPUSlotSamplerKey(1, 0, 0, 0, 1, 1), WGPUSlotSamplerKey(1, 0, 0, 0, 257, 1));
    const std::array<int, 12> levels{{std::numeric_limits<int>::min(), -1, 0, 1, 31, 32, 33, 255, 256,
                                      257, 1 << 20, std::numeric_limits<int>::max()}};
    for (const int a : levels)
        for (const int b : levels)
        {
            WGPUSamplerDescriptor da{};
            WGPUSamplerDescriptor db{};
            FillWGPUSamplerDescriptor(da, 1, 0, 0, 0, a, 1);
            FillWGPUSamplerDescriptor(db, 1, 0, 0, 0, b, 1);
            EXPECT_EQ(WGPUSlotSamplerKey(1, 0, 0, 0, a, 1) == WGPUSlotSamplerKey(1, 0, 0, 0, b, 1),
                      da.lodMinClamp == db.lodMinClamp) << a << " vs " << b;
        }
}

// ---------------------------------------------------------------------------------------------
// 3. Texture, render-target and depth formats.
// ---------------------------------------------------------------------------------------------

TEST(WebGPUStateMapping, NativeTextureStorageKeepsXnaTexelAndBlockSizes)
{
    // Every SurfaceFormat ClassifySurfaceFormatEXT reports Supported, plus Color. The size column is
    // XNA's own (Dxt1 = 8 bytes per 4x4 block, Dxt3/Dxt5/BC7 = 16, NormalizedByte2 = 2 bytes per
    // texel, NormalizedByte4 = 4, HalfVector4/HdrBlendable = 8, Single = 4, Vector4 = 16), and it
    // must equal the WebGPU format's texel block copy footprint (bc1 8, bc2/bc3/bc7 16, rg8snorm 2,
    // rgba8snorm 4, rgba16float 8, r32float 4, rgba32float 16, rgba8unorm 4).
    struct Row { SurfaceFormat format; WGPUTextureFormat native; bool compressed; int bytes; };
    const std::array<Row, 13> rows{{
        {SurfaceFormat::Color,           WGPUTextureFormat_RGBA8Unorm,       false, 4},
        {SurfaceFormat::Dxt1,            WGPUTextureFormat_BC1RGBAUnorm,     true,  8},
        {SurfaceFormat::Dxt3,            WGPUTextureFormat_BC2RGBAUnorm,     true,  16},
        {SurfaceFormat::Dxt5,            WGPUTextureFormat_BC3RGBAUnorm,     true,  16},
        {SurfaceFormat::Dxt5SrgbEXT,     WGPUTextureFormat_BC3RGBAUnormSrgb, true,  16},
        {SurfaceFormat::Bc7EXT,          WGPUTextureFormat_BC7RGBAUnorm,     true,  16},
        {SurfaceFormat::Bc7SrgbEXT,      WGPUTextureFormat_BC7RGBAUnormSrgb, true,  16},
        {SurfaceFormat::NormalizedByte2, WGPUTextureFormat_RG8Snorm,         false, 2},
        {SurfaceFormat::NormalizedByte4, WGPUTextureFormat_RGBA8Snorm,       false, 4},
        {SurfaceFormat::HalfVector4,     WGPUTextureFormat_RGBA16Float,      false, 8},
        {SurfaceFormat::HdrBlendable,    WGPUTextureFormat_RGBA16Float,      false, 8},
        {SurfaceFormat::Single,          WGPUTextureFormat_R32Float,         false, 4},
        {SurfaceFormat::Vector4,         WGPUTextureFormat_RGBA32Float,      false, 16},
    }};
    for (const Row& row : rows)
    {
        const WebGPUTexFormatInfo info = ClassifyWebGPUTextureFormat(Ord(row.format));
        EXPECT_EQ(info.format, row.native) << "SurfaceFormat " << Ord(row.format);
        EXPECT_EQ(info.compressed, row.compressed) << "SurfaceFormat " << Ord(row.format);
        EXPECT_EQ(info.blockBytes, row.bytes) << "SurfaceFormat " << Ord(row.format);
    }
}

TEST(WebGPUStateMapping, OnlySignedNormalizedStorageLosesTheRenderAttachmentUsage)
{
    // WebGPU core: rg8snorm and rgba8snorm are sampleable but not RENDER_ATTACHMENT formats;
    // rgba8unorm, rgba16float, r32float and rgba32float are. Asking for RENDER_ATTACHMENT on a
    // snorm texture invalidates it.
    EXPECT_TRUE(IsSignedNormalizedTextureFormatEXT(WGPUTextureFormat_RG8Snorm));
    EXPECT_TRUE(IsSignedNormalizedTextureFormatEXT(WGPUTextureFormat_RGBA8Snorm));
    for (const WGPUTextureFormat renderable : {WGPUTextureFormat_RGBA8Unorm, WGPUTextureFormat_BGRA8Unorm,
                                               WGPUTextureFormat_RGBA16Float, WGPUTextureFormat_R32Float,
                                               WGPUTextureFormat_RGBA32Float, WGPUTextureFormat_RG8Unorm})
        EXPECT_FALSE(IsSignedNormalizedTextureFormatEXT(renderable)) << static_cast<int>(renderable);
}

TEST(WebGPUStateMapping, RenderTargetFormatsMapToTheirXnaWidthsAndNothingElseIsClaimed)
{
    // XNA texel widths: Single 4, Vector2 8, Vector4 16, HalfSingle 2, HalfVector2 4,
    // HalfVector4 8, HdrBlendable 8 (RGBA16F, as on Windows), Rgba64 8 (four 16-bit UNORM). The
    // readback slices rows by BytesPerTexelEXT, so it must agree with the XNA width the shared layer
    // asks for -- for every mapped format.
    struct Row { SurfaceFormat format; WGPUTextureFormat native; int xnaBytes; };
    const std::array<Row, 8> rows{{
        {SurfaceFormat::Single,       WGPUTextureFormat_R32Float,    4},
        {SurfaceFormat::Vector2,      WGPUTextureFormat_RG32Float,   8},
        {SurfaceFormat::Vector4,      WGPUTextureFormat_RGBA32Float, 16},
        {SurfaceFormat::HalfSingle,   WGPUTextureFormat_R16Float,    2},
        {SurfaceFormat::HalfVector2,  WGPUTextureFormat_RG16Float,   4},
        {SurfaceFormat::HalfVector4,  WGPUTextureFormat_RGBA16Float, 8},
        {SurfaceFormat::HdrBlendable, WGPUTextureFormat_RGBA16Float, 8},
        {SurfaceFormat::Rgba64,       WGPUTextureFormat_RGBA16Unorm, 8},
    }};
    std::set<int> mapped;
    for (const Row& row : rows)
    {
        WGPUTextureFormat native = WGPUTextureFormat_Undefined;
        ASSERT_TRUE(MapRenderTargetColorFormatEXT(Ord(row.format), native)) << Ord(row.format);
        EXPECT_EQ(native, row.native) << "SurfaceFormat " << Ord(row.format);
        EXPECT_EQ(BytesPerTexelEXT(native), row.xnaBytes) << "SurfaceFormat " << Ord(row.format);
        mapped.insert(Ord(row.format));
    }
    // Color is deliberately absent (it follows the live swap-chain format), and no other format
    // has a render-target representation here: claiming one would promise a target the probe
    // never built.
    for (int format = Ord(SurfaceFormat::Color); format <= Ord(SurfaceFormat::UShortEXT); ++format)
    {
        if (mapped.count(format) != 0) continue;
        WGPUTextureFormat native = WGPUTextureFormat_Undefined;
        EXPECT_FALSE(MapRenderTargetColorFormatEXT(format, native)) << "SurfaceFormat " << format;
    }
    // The swap-chain colour formats the backbuffer and Color targets use are four bytes per texel.
    for (const WGPUTextureFormat swap : {WGPUTextureFormat_RGBA8Unorm, WGPUTextureFormat_RGBA8UnormSrgb,
                                         WGPUTextureFormat_BGRA8Unorm, WGPUTextureFormat_BGRA8UnormSrgb})
        EXPECT_EQ(BytesPerTexelEXT(swap), 4) << static_cast<int>(swap);
}

TEST(WebGPUStateMapping, OnlyThirtyTwoBitFloatTargetsNeedTheOptionalFilterAndBlendFeatures)
{
    // WebGPU's float32-filterable and float32-blendable features cover exactly r32float, rg32float
    // and rgba32float; the 16-bit float formats filter and blend in core.
    EXPECT_TRUE(IsThirtyTwoBitFloatFormatEXT(WGPUTextureFormat_R32Float));
    EXPECT_TRUE(IsThirtyTwoBitFloatFormatEXT(WGPUTextureFormat_RG32Float));
    EXPECT_TRUE(IsThirtyTwoBitFloatFormatEXT(WGPUTextureFormat_RGBA32Float));
    for (const WGPUTextureFormat core : {WGPUTextureFormat_R16Float, WGPUTextureFormat_RG16Float,
                                         WGPUTextureFormat_RGBA16Float, WGPUTextureFormat_RGBA16Unorm,
                                         WGPUTextureFormat_RGBA8Unorm, WGPUTextureFormat_BGRA8Unorm})
        EXPECT_FALSE(IsThirtyTwoBitFloatFormatEXT(core)) << static_cast<int>(core);
}

TEST(WebGPUStateMapping, SrgbSwapChainFormatsDropOnlyTheTransferFunction)
{
    // SurfaceFormat.Color is plain UNORM bytes, so an sRGB swap chain is rendered through its linear
    // twin; the channel order (which the readback swizzle keys on) must survive.
    EXPECT_EQ(NonSrgbColorFormat(WGPUTextureFormat_BGRA8UnormSrgb), WGPUTextureFormat_BGRA8Unorm);
    EXPECT_EQ(NonSrgbColorFormat(WGPUTextureFormat_RGBA8UnormSrgb), WGPUTextureFormat_RGBA8Unorm);
    EXPECT_EQ(NonSrgbColorFormat(WGPUTextureFormat_BGRA8Unorm), WGPUTextureFormat_BGRA8Unorm);
    EXPECT_EQ(NonSrgbColorFormat(WGPUTextureFormat_RGBA8Unorm), WGPUTextureFormat_RGBA8Unorm);
    EXPECT_EQ(NonSrgbColorFormat(WGPUTextureFormat_RGBA16Float), WGPUTextureFormat_RGBA16Float);
}

TEST(WebGPUStateMapping, DepthFormatKeepsDepthAndStencilAspectsAsXnaDeclaresThem)
{
    // XNA DepthFormat: None (no buffer), Depth16 (16-bit depth), Depth24 (24-bit depth, no
    // stencil), Depth24Stencil8. WebGPU: depth16unorm, depth24plus, depth24plus-stencil8 -- all core.
    const DepthAttachmentEXT none = MapDepthFormatEXT(Ord(DepthFormat::None));
    EXPECT_EQ(none.format, WGPUTextureFormat_Undefined);
    EXPECT_FALSE(none.hasDepth);
    EXPECT_FALSE(none.hasStencil);
    const DepthAttachmentEXT d16 = MapDepthFormatEXT(Ord(DepthFormat::Depth16));
    EXPECT_EQ(d16.format, WGPUTextureFormat_Depth16Unorm);
    EXPECT_TRUE(d16.hasDepth);
    EXPECT_FALSE(d16.hasStencil);
    const DepthAttachmentEXT d24 = MapDepthFormatEXT(Ord(DepthFormat::Depth24));
    EXPECT_EQ(d24.format, WGPUTextureFormat_Depth24Plus);
    EXPECT_TRUE(d24.hasDepth);
    EXPECT_FALSE(d24.hasStencil);
    const DepthAttachmentEXT d24s8 = MapDepthFormatEXT(Ord(DepthFormat::Depth24Stencil8));
    EXPECT_EQ(d24s8.format, WGPUTextureFormat_Depth24PlusStencil8);
    EXPECT_TRUE(d24s8.hasDepth);
    EXPECT_TRUE(d24s8.hasStencil);

    // The format-side answer agrees with the XNA-side one for every format CNA creates, and
    // depth32float-stencil8 (the other combined WebGPU format) also has a stencil aspect.
    for (const DepthFormat format : {DepthFormat::Depth16, DepthFormat::Depth24, DepthFormat::Depth24Stencil8})
    {
        const DepthAttachmentEXT mapped = MapDepthFormatEXT(Ord(format));
        EXPECT_EQ(DepthFormatHasStencilEXT(mapped.format), mapped.hasStencil) << Ord(format);
    }
    EXPECT_TRUE(DepthFormatHasStencilEXT(WGPUTextureFormat_Depth32FloatStencil8));
    EXPECT_FALSE(DepthFormatHasStencilEXT(WGPUTextureFormat_Depth32Float));
}

// ---------------------------------------------------------------------------------------------
// 4. Vertex input and primitive state.
// ---------------------------------------------------------------------------------------------

namespace
{
    /// Byte size of a WebGPU vertex format, from the WebGPU specification's vertex format table;
    /// 0 for a format this table does not list.
    int WebGpuVertexFormatBytes(WGPUVertexFormat format)
    {
        switch (format)
        {
            case WGPUVertexFormat_Float32:   return 4;
            case WGPUVertexFormat_Float32x2: return 8;
            case WGPUVertexFormat_Float32x3: return 12;
            case WGPUVertexFormat_Float32x4: return 16;
            case WGPUVertexFormat_Unorm8x4:
            case WGPUVertexFormat_Uint8x4:
            case WGPUVertexFormat_Snorm8x4:
            case WGPUVertexFormat_Sint8x4:   return 4;
            case WGPUVertexFormat_Sint16x2:
            case WGPUVertexFormat_Snorm16x2:
            case WGPUVertexFormat_Uint16x2:
            case WGPUVertexFormat_Unorm16x2:
            case WGPUVertexFormat_Float16x2: return 4;
            case WGPUVertexFormat_Sint16x4:
            case WGPUVertexFormat_Snorm16x4:
            case WGPUVertexFormat_Uint16x4:
            case WGPUVertexFormat_Unorm16x4:
            case WGPUVertexFormat_Float16x4: return 8;
            default:                         return 0;
        }
    }
}

TEST(WebGPUStateMapping, VertexElementFormatKeepsXnaSizeSignednessAndNormalization)
{
    // XNA VertexElementFormat sizes: Single 4, Vector2 8, Vector3 12, Vector4 16, Color 4 (four
    // normalized unsigned bytes), Byte4 4 (four unsigned bytes, NOT normalized), Short2 4 / Short4 8
    // (signed, not normalized), NormalizedShort2 4 / NormalizedShort4 8 (signed normalized),
    // HalfVector2 4 / HalfVector4 8. FNA3D's VertexAttribType table makes the same choices
    // (UBYTE4_NORM, UBYTE4, SHORT2, SHORT4, SHORT2_NORM, SHORT4_NORM, HALF2, HALF4). The format CNA
    // returns must be that one AND read exactly the XNA element's bytes (a wrong width shifts every
    // later attribute of the vertex).
    struct Row { VertexElementFormat format; WGPUVertexFormat native; int xnaBytes; };
    const std::array<Row, 12> rows{{
        {VertexElementFormat::Single,           WGPUVertexFormat_Float32,   4},
        {VertexElementFormat::Vector2,          WGPUVertexFormat_Float32x2, 8},
        {VertexElementFormat::Vector3,          WGPUVertexFormat_Float32x3, 12},
        {VertexElementFormat::Vector4,          WGPUVertexFormat_Float32x4, 16},
        {VertexElementFormat::Color,            WGPUVertexFormat_Unorm8x4,  4},
        {VertexElementFormat::Byte4,            WGPUVertexFormat_Uint8x4,   4},
        {VertexElementFormat::Short2,           WGPUVertexFormat_Sint16x2,  4},
        {VertexElementFormat::Short4,           WGPUVertexFormat_Sint16x4,  8},
        {VertexElementFormat::NormalizedShort2, WGPUVertexFormat_Snorm16x2, 4},
        {VertexElementFormat::NormalizedShort4, WGPUVertexFormat_Snorm16x4, 8},
        {VertexElementFormat::HalfVector2,      WGPUVertexFormat_Float16x2, 4},
        {VertexElementFormat::HalfVector4,      WGPUVertexFormat_Float16x4, 8},
    }};
    for (const Row& row : rows)
    {
        const WGPUVertexFormat mapped = WebGPUVertexFormatFromVEF(row.format);
        EXPECT_EQ(mapped, row.native) << "VertexElementFormat " << Ord(row.format);
        EXPECT_EQ(WebGpuVertexFormatBytes(mapped), row.xnaBytes)
            << "VertexElementFormat " << Ord(row.format) << " reads a different byte width";
    }
    EXPECT_THROW((void)WebGPUVertexFormatFromVEF(static_cast<VertexElementFormat>(12)), std::invalid_argument);
    EXPECT_THROW((void)WebGPUVertexFormatFromVEF(static_cast<VertexElementFormat>(-1)), std::invalid_argument);
}

TEST(WebGPUStateMapping, StripIndexFormatIsSetOnlyForIndexedStripsAndMatchesTheIndexWidth)
{
    // WebGPU: stripIndexFormat must be undefined for list and point topologies, and an indexed
    // strip draw requires it to equal the bound index format. XNA IndexElementSize.SixteenBits is
    // uint16, ThirtyTwoBits uint32.
    const std::array<WGPUPrimitiveTopology, 5> topologies{
        WGPUPrimitiveTopology_PointList, WGPUPrimitiveTopology_LineList, WGPUPrimitiveTopology_LineStrip,
        WGPUPrimitiveTopology_TriangleList, WGPUPrimitiveTopology_TriangleStrip};
    for (const WGPUPrimitiveTopology topology : topologies)
    {
        const bool strip = topology == WGPUPrimitiveTopology_LineStrip ||
                           topology == WGPUPrimitiveTopology_TriangleStrip;
        for (const bool index32 : {false, true})
        {
            EXPECT_EQ(RequiredStripIndexFormat(topology, false, index32), WGPUIndexFormat_Undefined)
                << "non-indexed topology " << static_cast<int>(topology);
            const WGPUIndexFormat expected = !strip ? WGPUIndexFormat_Undefined
                                           : index32 ? WGPUIndexFormat_Uint32 : WGPUIndexFormat_Uint16;
            EXPECT_EQ(RequiredStripIndexFormat(topology, true, index32), expected)
                << "indexed topology " << static_cast<int>(topology) << " index32=" << index32;
        }
    }

    // The command overload: a draw only counts as indexed when it actually carries indices.
    struct FakeResident { std::shared_ptr<int> indexStorage; };
    struct FakeCommand
    {
        WGPUPrimitiveTopology topology = WGPUPrimitiveTopology_TriangleStrip;
        bool indexed = true;
        bool index32 = true;
        std::vector<std::uint8_t> indexData;
        FakeResident resident;
    };
    FakeCommand command;
    EXPECT_EQ(RequiredStripIndexFormat(command), WGPUIndexFormat_Undefined) << "indexed flag but no indices";
    command.indexData = {0, 0, 0, 0};
    EXPECT_EQ(RequiredStripIndexFormat(command), WGPUIndexFormat_Uint32);
    command.index32 = false;
    EXPECT_EQ(RequiredStripIndexFormat(command), WGPUIndexFormat_Uint16);
    command.indexData.clear();
    command.resident.indexStorage = std::make_shared<int>(1);
    EXPECT_EQ(RequiredStripIndexFormat(command), WGPUIndexFormat_Uint16) << "resident index storage";
    command.indexed = false;
    EXPECT_EQ(RequiredStripIndexFormat(command), WGPUIndexFormat_Undefined);
}

TEST(WebGPUStateMapping, OnlyTriangleTopologiesHaveAPolygonInterior)
{
    // WebGPU forbids a non-zero depth bias on line and point topologies.
    EXPECT_TRUE(TopologyHasPolygonInterior(WGPUPrimitiveTopology_TriangleList));
    EXPECT_TRUE(TopologyHasPolygonInterior(WGPUPrimitiveTopology_TriangleStrip));
    EXPECT_FALSE(TopologyHasPolygonInterior(WGPUPrimitiveTopology_LineList));
    EXPECT_FALSE(TopologyHasPolygonInterior(WGPUPrimitiveTopology_LineStrip));
    EXPECT_FALSE(TopologyHasPolygonInterior(WGPUPrimitiveTopology_PointList));
}

TEST(WebGPUStateMapping, InstancedColourOffsetIsKnownOnlyForTheTwoColouredStockLayouts)
{
    // VertexPositionColor (stride 16) and VertexPositionColorTexture (stride 24) both put COLOR0
    // after a 12-byte Vector3 position. VertexPositionTexture (20) and VertexPositionNormalTexture
    // (32) carry no colour at all.
    std::uint64_t offset = 99;
    EXPECT_TRUE(InstancedPackedColorOffsetForStride(16, offset));
    EXPECT_EQ(offset, 12u);
    offset = 99;
    EXPECT_TRUE(InstancedPackedColorOffsetForStride(24, offset));
    EXPECT_EQ(offset, 12u);
    for (const std::size_t stride : {std::size_t{12}, std::size_t{20}, std::size_t{32}, std::size_t{36}})
        EXPECT_FALSE(InstancedPackedColorOffsetForStride(stride, offset)) << stride;
}

// ---------------------------------------------------------------------------------------------
// 5. Copy-layout arithmetic.
// ---------------------------------------------------------------------------------------------

TEST(WebGPUStateMapping, ReadbackRowsAreAlignedToTheWebGpu256ByteRule)
{
    // WebGPU: a texture<->buffer copy's bytesPerRow must be a multiple of 256
    // (COPY_BYTES_PER_ROW_ALIGNMENT). Hand values: 1 -> 256, 255 -> 256, 256 -> 256, 257 -> 512,
    // 64 texels * 4 = 256 -> 256, 65 * 4 = 260 -> 512, 100 * 16 = 1600 -> 1792 (7 * 256).
    const std::array<std::pair<std::uint32_t, std::uint32_t>, 8> rows{{
        {1u, 256u}, {4u, 256u}, {255u, 256u}, {256u, 256u}, {257u, 512u}, {260u, 512u},
        {1600u, 1792u}, {4096u, 4096u}}};
    for (const auto& [bytes, aligned] : rows)
        EXPECT_EQ(AlignBytesPerRow(bytes), aligned) << bytes;
    for (std::uint32_t bytes = 1; bytes <= 5000; ++bytes)
    {
        const std::uint32_t aligned = AlignBytesPerRow(bytes);
        ASSERT_EQ(aligned % 256u, 0u) << bytes;
        ASSERT_GE(aligned, bytes) << bytes;
        ASSERT_LT(aligned - bytes, 256u) << bytes;
    }
}

TEST(WebGPUStateMapping, MipDimensionsHalveWithFloorAndNeverReachZero)
{
    // XNA/D3D mip chains: level n of an edge e is max(1, floor(e / 2^n)). 256 -> 256,128,...,1;
    // 5 -> 5, 2, 1, 1; 7 -> 7, 3, 1; 1 stays 1.
    const std::array<int, 10> chain256{256, 128, 64, 32, 16, 8, 4, 2, 1, 1};
    for (int level = 0; level < static_cast<int>(chain256.size()); ++level)
        EXPECT_EQ(MipDim(256, level), chain256[static_cast<std::size_t>(level)]) << level;
    EXPECT_EQ(MipDim(5, 1), 2);
    EXPECT_EQ(MipDim(5, 2), 1);
    EXPECT_EQ(MipDim(5, 3), 1);
    EXPECT_EQ(MipDim(7, 1), 3);
    EXPECT_EQ(MipDim(7, 2), 1);
    EXPECT_EQ(MipDim(1, 0), 1);
    EXPECT_EQ(MipDim(1, 6), 1);
}

TEST(WebGPUStateMapping, BufferUploadsArePaddedToFourBytesWithAFourByteMinimum)
{
    // wgpuQueueWriteBuffer's size must be a multiple of 4, and a zero-sized buffer is not useful:
    // three 16-bit indices are 6 bytes -> 8; one Color vertex element is 4 -> 4; 0 -> 4.
    EXPECT_EQ(Align4(0), 4u);
    EXPECT_EQ(Align4(1), 4u);
    EXPECT_EQ(Align4(4), 4u);
    EXPECT_EQ(Align4(5), 8u);
    EXPECT_EQ(Align4(6), 8u);
    EXPECT_EQ(Align4(8), 8u);
    EXPECT_EQ(Align4(1001), 1004u);
}

// ---------------------------------------------------------------------------------------------
// 6. Pipeline cache keys.
// ---------------------------------------------------------------------------------------------

namespace
{
    /// Every input of Make3DPipelineKey, so a test can vary exactly one.
    struct PipelineInputs
    {
        WGPUPrimitiveTopology topology = WGPUPrimitiveTopology_TriangleList;
        WGPUIndexFormat strip = WGPUIndexFormat_Undefined;
        bool depthTest = true;
        bool depthWrite = true;
        int depthFunc = Ord(CompareFunction::LessEqual);
        bool blend = true;
        BlendKey bp{};
        int cullMode = Ord(CullMode::CullCounterClockwiseFace);
        bool wireframe = false;
        float depthBias = 0.0f;
        float slopeScaleDepthBias = 0.0f;
        std::uint64_t salt = 20;
        int colorWriteMask = 15;
        std::uint32_t sampleMask = 0xFFFFFFFFu;
        int colorAttachmentCount = 1;
        WGPUTextureFormat depthFormat = WGPUTextureFormat_Depth24PlusStencil8;
        WGPUTextureFormat colorFormat = WGPUTextureFormat_BGRA8Unorm;
        std::uint32_t sampleCount = 1;
        std::array<WGPUTextureFormat, 4> mrt{};

        [[nodiscard]] std::uint64_t Key() const
        {
            return Make3DPipelineKey(topology, strip, depthTest, depthWrite, depthFunc, blend, bp,
                                     cullMode, wireframe, depthBias, slopeScaleDepthBias, salt,
                                     colorWriteMask, sampleMask, colorAttachmentCount, depthFormat,
                                     colorFormat, sampleCount, mrt);
        }
    };
}

TEST(WebGPUStateMapping, PipelineKeySeparatesEveryStateAWebGpuPipelineBakes)
{
    // A WebGPU render pipeline is immutable: topology, strip format, depth test/write/compare,
    // depth bias, blend factors and equations, cull mode, colour write mask, sample mask, colour
    // attachment count and formats, depth attachment format and sample count are all baked into it.
    // Changing any ONE of them must therefore change the cache key; otherwise the second draw
    // silently reuses the first draw's pipeline (or fails validation against its pass).
    const PipelineInputs base;
    const std::uint64_t baseKey = base.Key();
    EXPECT_EQ(baseKey, PipelineInputs{}.Key()) << "the key must be a pure function of its inputs";

    std::vector<std::pair<const char*, PipelineInputs>> variants;
    const auto vary = [&](const char* name, auto mutate) {
        PipelineInputs v = base;
        mutate(v);
        variants.emplace_back(name, v);
    };
    vary("topology", [](PipelineInputs& v) { v.topology = WGPUPrimitiveTopology_LineList; });
    vary("strip index format", [](PipelineInputs& v) {
        v.topology = WGPUPrimitiveTopology_TriangleStrip; v.strip = WGPUIndexFormat_Uint16; });
    vary("depth test", [](PipelineInputs& v) { v.depthTest = false; });
    vary("depth write", [](PipelineInputs& v) { v.depthWrite = false; });
    vary("depth function", [](PipelineInputs& v) { v.depthFunc = Ord(CompareFunction::Greater); });
    vary("blend enable", [](PipelineInputs& v) { v.blend = false; });
    vary("colour source", [](PipelineInputs& v) { v.bp.colorSrc = Ord(Blend::SourceAlpha); });
    vary("colour destination", [](PipelineInputs& v) { v.bp.colorDst = Ord(Blend::InverseSourceAlpha); });
    vary("alpha source", [](PipelineInputs& v) { v.bp.alphaSrc = Ord(Blend::SourceAlpha); });
    vary("alpha destination", [](PipelineInputs& v) { v.bp.alphaDst = Ord(Blend::InverseSourceAlpha); });
    vary("colour function", [](PipelineInputs& v) { v.bp.colorFunc = Ord(BlendFunction::Subtract); });
    vary("alpha function", [](PipelineInputs& v) { v.bp.alphaFunc = Ord(BlendFunction::Subtract); });
    vary("cull mode", [](PipelineInputs& v) { v.cullMode = Ord(CullMode::None); });
    vary("wireframe", [](PipelineInputs& v) { v.wireframe = true; });
    vary("depth bias", [](PipelineInputs& v) { v.depthBias = -0.0001f; });
    vary("slope-scale depth bias", [](PipelineInputs& v) { v.slopeScaleDepthBias = 1.0f; });
    vary("salt", [](PipelineInputs& v) { v.salt = 32; });
    vary("colour write mask", [](PipelineInputs& v) { v.colorWriteMask = 0x7; });
    vary("sample mask", [](PipelineInputs& v) { v.sampleMask = 0x1u; });
    vary("colour attachment count", [](PipelineInputs& v) {
        v.colorAttachmentCount = 2; v.mrt[1] = WGPUTextureFormat_BGRA8Unorm; });
    vary("depth format Depth24Plus", [](PipelineInputs& v) { v.depthFormat = WGPUTextureFormat_Depth24Plus; });
    vary("depth format Depth16", [](PipelineInputs& v) { v.depthFormat = WGPUTextureFormat_Depth16Unorm; });
    vary("no depth attachment", [](PipelineInputs& v) { v.depthFormat = WGPUTextureFormat_Undefined; });
    vary("colour format", [](PipelineInputs& v) { v.colorFormat = WGPUTextureFormat_RGBA16Float; });
    vary("sample count", [](PipelineInputs& v) { v.sampleCount = 4; });

    std::unordered_set<std::uint64_t> seen{baseKey};
    for (const auto& [name, variant] : variants)
    {
        const std::uint64_t key = variant.Key();
        EXPECT_NE(key, baseKey) << name << " does not reach the pipeline cache key";
        EXPECT_TRUE(seen.insert(key).second) << name << " collides with another single-field variant";
        EXPECT_EQ(key, variant.Key()) << name << " is not stable";
    }
}

TEST(WebGPUStateMapping, PipelineKeyFoldsOnlyTheMrtSlotsThePassActuallyHas)
{
    PipelineInputs two;
    two.colorAttachmentCount = 2;
    two.mrt[1] = WGPUTextureFormat_RGBA8Unorm;
    // Slot 1's own format is baked into a two-target pipeline: a set that differs only there needs
    // its own pipeline.
    PipelineInputs twoOther = two;
    twoOther.mrt[1] = WGPUTextureFormat_R32Float;
    EXPECT_NE(two.Key(), twoOther.Key());
    // Slots beyond the attachment count are not part of the pass and must not split the cache.
    PipelineInputs twoWithStaleSlot = two;
    twoWithStaleSlot.mrt[3] = WGPUTextureFormat_R32Float;
    EXPECT_EQ(two.Key(), twoWithStaleSlot.Key());
    PipelineInputs single;
    single.mrt[1] = WGPUTextureFormat_R32Float;
    EXPECT_EQ(single.Key(), PipelineInputs{}.Key());
}

TEST(WebGPUStateMapping, PipelineKeyIgnoresBlendTermsWhileBlendingIsOff)
{
    // A disabled WGPUColorTargetState::blend is null, so the factors are not pipeline state at all.
    PipelineInputs off;
    off.blend = false;
    PipelineInputs offOther = off;
    offOther.bp.colorSrc = Ord(Blend::SourceAlpha);
    offOther.bp.alphaDst = Ord(Blend::InverseDestinationAlpha);
    offOther.bp.colorFunc = Ord(BlendFunction::Max);
    EXPECT_EQ(off.Key(), offOther.Key());
}

TEST(WebGPUStateMapping, PipelineKeysOfDistinctRealisticStatesNeverCollide)
{
    // A product of the states a game really varies between draws. Every combination is a distinct
    // pipeline, so every key must be distinct -- a structural collision in the hash (for example a
    // field multiplied into a position another field already occupies) shows up here.
    const std::array<BlendKey, 4> blends = [] {
        std::array<BlendKey, 4> b{};
        b[1].colorSrc = b[1].alphaSrc = Ord(Blend::One);
        b[1].colorDst = b[1].alphaDst = Ord(Blend::InverseSourceAlpha);
        b[2].colorSrc = b[2].alphaSrc = Ord(Blend::SourceAlpha);
        b[2].colorDst = b[2].alphaDst = Ord(Blend::One);
        b[3].colorSrc = b[3].alphaSrc = Ord(Blend::SourceAlpha);
        b[3].colorDst = b[3].alphaDst = Ord(Blend::InverseSourceAlpha);
        return b;
    }();
    const std::array<WGPUTextureFormat, 4> depthFormats{
        WGPUTextureFormat_Undefined, WGPUTextureFormat_Depth16Unorm, WGPUTextureFormat_Depth24Plus,
        WGPUTextureFormat_Depth24PlusStencil8};
    const std::array<WGPUTextureFormat, 3> colorFormats{
        WGPUTextureFormat_BGRA8Unorm, WGPUTextureFormat_RGBA8Unorm, WGPUTextureFormat_RGBA16Float};
    std::unordered_map<std::uint64_t, std::string> keys;
    std::size_t count = 0;
    std::size_t reported = 0;
    for (const WGPUPrimitiveTopology topology : {WGPUPrimitiveTopology_TriangleList,
                                                 WGPUPrimitiveTopology_TriangleStrip,
                                                 WGPUPrimitiveTopology_LineList})
    for (const bool depthTest : {false, true})
    for (const bool depthWrite : {false, true})
    for (int depthFunc = 0; depthFunc < 8; ++depthFunc)
    for (std::size_t blendIndex = 0; blendIndex < blends.size() + 1; ++blendIndex)
    for (int cullMode = 0; cullMode < 3; ++cullMode)
    for (const int writeMask : {15, 7, 0})
    for (const WGPUTextureFormat depthFormat : depthFormats)
    for (const WGPUTextureFormat colorFormat : colorFormats)
    for (const std::uint32_t sampleCount : {1u, 4u})
    {
        // With the depth test off, the compare function and the write flag bake nothing
        // (DepthBufferEnable = false disables depth buffering outright), so only one such state is
        // a pipeline of its own.
        if (!depthTest && (depthWrite || depthFunc != Ord(CompareFunction::LessEqual)))
            continue;
        PipelineInputs v;
        v.topology = topology;
        v.depthTest = depthTest;
        v.depthWrite = depthWrite;
        v.depthFunc = depthFunc;
        v.blend = blendIndex != 0;
        v.bp = blends[blendIndex == 0 ? 0 : blendIndex - 1];
        v.cullMode = cullMode;
        v.colorWriteMask = writeMask;
        v.depthFormat = depthFormat;
        v.colorFormat = colorFormat;
        v.sampleCount = sampleCount;
        const std::string name =
            "topology=" + std::to_string(static_cast<int>(topology)) + " depthTest=" + std::to_string(depthTest) +
            " depthWrite=" + std::to_string(depthWrite) + " depthFunc=" + std::to_string(depthFunc) +
            " blend=" + std::to_string(blendIndex) + " cull=" + std::to_string(cullMode) +
            " writeMask=" + std::to_string(writeMask) + " depthFormat=" + std::to_string(static_cast<int>(depthFormat)) +
            " colorFormat=" + std::to_string(static_cast<int>(colorFormat)) +
            " samples=" + std::to_string(sampleCount);
        const auto [it, inserted] = keys.emplace(v.Key(), name);
        if (!inserted && reported++ < 3)
            ADD_FAILURE() << "pipeline key collision:\n  " << it->second << "\n  " << name;
        ++count;
    }
    // 3 topologies * (2 writes * 8 functions + 1 depth-off) * 5 blends * 3 cull modes
    // * 3 write masks * 4 depth formats * 3 colour formats * 2 sample counts = 55,080 states.
    ASSERT_EQ(count, 55080u);
    EXPECT_EQ(keys.size(), count) << (count - keys.size()) << " distinct pipeline states share a key";
}

TEST(WebGPUStateMapping, SpritePipelineKeyDistinguishesEveryBakedField)
{
    // The stock SpriteBatch pipeline cache is an unordered_map keyed by SpritePipelineKey. Its
    // operator== decides reuse; its hash must agree with it. An earlier defect built every sprite
    // pipeline with depth compare Always regardless of the batch's DepthStencilState -- the depth and
    // stencil fields below are what keep that from coming back silently.
    using Key = WebGPURenderer::SpritePipelineKey;
    using Hash = WebGPURenderer::SpritePipelineKeyHash;
    Key base;
    base.targetFormat = WGPUTextureFormat_BGRA8Unorm;
    const Hash hash;
    EXPECT_TRUE(base == Key(base));
    EXPECT_EQ(hash(base), hash(Key(base)));

    std::vector<std::pair<const char*, Key>> variants;
    const auto vary = [&](const char* name, auto mutate) { Key v = base; mutate(v); variants.emplace_back(name, v); };
    vary("blend enable", [](Key& v) { v.blendEnabled = true; });
    vary("colour source", [](Key& v) { v.colorSrc = Ord(Blend::SourceAlpha); });
    vary("colour destination", [](Key& v) { v.colorDst = Ord(Blend::InverseSourceAlpha); });
    vary("alpha source", [](Key& v) { v.alphaSrc = Ord(Blend::SourceAlpha); });
    vary("alpha destination", [](Key& v) { v.alphaDst = Ord(Blend::InverseSourceAlpha); });
    vary("colour function", [](Key& v) { v.colorFunc = Ord(BlendFunction::Subtract); });
    vary("alpha function", [](Key& v) { v.alphaFunc = Ord(BlendFunction::Subtract); });
    vary("colour write mask", [](Key& v) { v.colorWriteMask = 0x3; });
    vary("sample mask", [](Key& v) { v.multiSampleMask = 0x5u; });
    vary("target format", [](Key& v) { v.targetFormat = WGPUTextureFormat_RGBA8Unorm; });
    vary("sample count", [](Key& v) { v.sampleCount = 4; });
    vary("colour attachment count", [](Key& v) { v.colorAttachmentCount = 3; });
    vary("MRT slot 1 format", [](Key& v) {
        v.colorAttachmentCount = 2; v.mrtColorFormats[1] = WGPUTextureFormat_R32Float; });
    vary("depth format", [](Key& v) { v.depthFormat = WGPUTextureFormat_Depth16Unorm; });
    vary("depth test", [](Key& v) { v.depthTest = true; });
    vary("depth write", [](Key& v) { v.depthWrite = true; });
    vary("depth function", [](Key& v) { v.depthFunc = Ord(CompareFunction::Less); });
    vary("stencil enable", [](Key& v) { v.stencil.enable = true; });
    vary("stencil function", [](Key& v) { v.stencil.func = Ord(CompareFunction::Equal); });
    vary("stencil pass", [](Key& v) { v.stencil.stencilPass = Ord(StencilOperation::Replace); });
    vary("stencil fail", [](Key& v) { v.stencil.stencilFail = Ord(StencilOperation::Zero); });
    vary("stencil depth fail", [](Key& v) { v.stencil.depthFail = Ord(StencilOperation::Zero); });
    vary("stencil read mask", [](Key& v) { v.stencil.readMask = 0x0F; });
    vary("stencil write mask", [](Key& v) { v.stencil.writeMask = 0x0F; });
    vary("two-sided", [](Key& v) { v.stencil.twoSided = true; });
    vary("ccw function", [](Key& v) { v.stencil.ccwFunc = Ord(CompareFunction::Never); });
    vary("ccw pass", [](Key& v) { v.stencil.ccwPass = Ord(StencilOperation::Invert); });
    vary("ccw fail", [](Key& v) { v.stencil.ccwFail = Ord(StencilOperation::Invert); });
    vary("ccw depth fail", [](Key& v) { v.stencil.ccwDepthFail = Ord(StencilOperation::Invert); });
    vary("wireframe", [](Key& v) { v.wireframe = true; });

    std::unordered_set<std::size_t> hashes{hash(base)};
    for (const auto& [name, variant] : variants)
    {
        EXPECT_FALSE(variant == base) << name << " does not take part in SpritePipelineKey equality";
        EXPECT_TRUE(variant == Key(variant)) << name;
        EXPECT_EQ(hash(variant), hash(Key(variant))) << name;
        // Not required for correctness (equality decides), but a field the hash ignores turns
        // every variant of it into a bucket collision.
        EXPECT_TRUE(hashes.insert(hash(variant)).second) << name << " is not folded into the hash";
    }

    // Two MRT sets with the same slot count that differ only in slot 1's format: WebGPU validates
    // every slot of targets[] against the pass, so these must be two pipelines.
    Key twoTargets = base;
    twoTargets.colorAttachmentCount = 2;
    twoTargets.mrtColorFormats[1] = WGPUTextureFormat_BGRA8Unorm;
    Key twoTargetsFloat = twoTargets;
    twoTargetsFloat.mrtColorFormats[1] = WGPUTextureFormat_R32Float;
    EXPECT_FALSE(twoTargets == twoTargetsFloat) << "slot 1's format does not take part in equality";
    EXPECT_NE(hash(twoTargets), hash(twoTargetsFloat)) << "slot 1's format is not folded into the hash";
}
