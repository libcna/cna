// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>

#include "CNA/Internal/Renderers/Metal/MetalPolicy.hpp"

using namespace CNA::Internal::Renderers;
using namespace CNA::Internal::Renderers::Metal;

TEST(MetalPolicy, CapabilitiesAreExhaustiveAndConservative)
{
    EXPECT_TRUE(MetalSupportsCapability(CNA::GraphicsCapability::ThreeD));
    EXPECT_TRUE(MetalSupportsCapability(CNA::GraphicsCapability::DepthStencilBuffer));
    EXPECT_FALSE(MetalSupportsCapability(CNA::GraphicsCapability::MultiSampleAntiAliasing));
    EXPECT_TRUE(MetalSupportsCapability(CNA::GraphicsCapability::MultipleRenderTargets));  // AM4-097
    EXPECT_TRUE(MetalSupportsCapability(CNA::GraphicsCapability::AnisotropicFiltering));
    EXPECT_TRUE(MetalSupportsCapability(CNA::GraphicsCapability::WireFrame));
    EXPECT_TRUE(MetalSupportsCapability(CNA::GraphicsCapability::OcclusionQuery));
    EXPECT_TRUE(MetalSupportsCapability(CNA::GraphicsCapability::CustomEffects));  // AM4-077
    EXPECT_TRUE(MetalSupportsCapability(CNA::GraphicsCapability::Texture3D));
    EXPECT_TRUE(MetalSupportsCapability(CNA::GraphicsCapability::MultiStreamVertexInput));
    EXPECT_TRUE(MetalSupportsCapability(CNA::GraphicsCapability::Instancing));
    EXPECT_TRUE(MetalSupportsCapability(CNA::GraphicsCapability::StencilBuffer));
    EXPECT_TRUE(MetalSupportsCapability(CNA::GraphicsCapability::AdditiveBlending));
    EXPECT_FALSE(MetalSupportsCapability(static_cast<CNA::GraphicsCapability>(999)));
}

TEST(MetalPolicy, UnavailableDrawableSkipsOnlyTheCurrentBackbufferFrame)
{
    EXPECT_EQ(DescribeMetalFrameEntryPolicy(true),MetalFrameEntryPolicy::Proceed);
    EXPECT_EQ(DescribeMetalFrameEntryPolicy(false),
              MetalFrameEntryPolicy::SkipUnavailableBackbuffer);
    EXPECT_EQ(DescribeMetalFrameEntryPolicy(true),MetalFrameEntryPolicy::Proceed);
}

TEST(MetalPolicy, UnavailableDrawableAllowsOnlyOneAttemptUntilPresent)
{
    MetalFrameAvailabilityState state;
    EXPECT_TRUE(state.ShouldAttemptBackbufferAcquisition());
    state.RecordBackbufferAcquisition(false);
    EXPECT_FALSE(state.ShouldAttemptBackbufferAcquisition());
    EXPECT_FALSE(state.ShouldAttemptBackbufferAcquisition());
    state.EndLogicalFrame();
    EXPECT_TRUE(state.ShouldAttemptBackbufferAcquisition());
}

TEST(MetalPolicy, CommandFailureResetStartsFreshAvailabilityState)
{
    MetalFrameAvailabilityState state;
    state.RecordBackbufferAcquisition(false);
    EXPECT_FALSE(state.ShouldAttemptBackbufferAcquisition());
    state.ResetAfterCommandFailure();
    EXPECT_TRUE(state.ShouldAttemptBackbufferAcquisition());
}

TEST(MetalPolicy, FormatAndSampleCountNeverOverclaim)
{
    using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
    EXPECT_TRUE(MetalSupportsSurfaceFormat(static_cast<int>(SurfaceFormat::Color)));
    EXPECT_FALSE(MetalSupportsSurfaceFormat(1));
    EXPECT_FALSE(MetalSupportsSurfaceFormat(26));
    // AM4-141: rounded down to a count the device supports; 0/1 are single-sampled.
    const unsigned all=MetalSampleCountBit(2)|MetalSampleCountBit(4)|MetalSampleCountBit(8);
    const unsigned fourOnly=MetalSampleCountBit(4);
    EXPECT_EQ(MetalAppliedMultiSampleCount(-1,all),0);
    EXPECT_EQ(MetalAppliedMultiSampleCount(0,all),0);
    EXPECT_EQ(MetalAppliedMultiSampleCount(1,all),0);
    EXPECT_EQ(MetalAppliedMultiSampleCount(2,all),2);
    EXPECT_EQ(MetalAppliedMultiSampleCount(3,all),2);
    EXPECT_EQ(MetalAppliedMultiSampleCount(8,all),8);
    EXPECT_EQ(MetalAppliedMultiSampleCount(16,all),8);
    EXPECT_EQ(MetalAppliedMultiSampleCount(8,fourOnly),4);
    EXPECT_EQ(MetalAppliedMultiSampleCount(2,fourOnly),0);
    EXPECT_EQ(MetalAppliedMultiSampleCount(4,0u),0);
    // AM4-147: a single-sampled cube target stores an upload; a multisampled one refuses it.
    EXPECT_TRUE(MetalRenderTargetCubeUploadSupported(0));
    EXPECT_FALSE(MetalRenderTargetCubeUploadSupported(2));
    EXPECT_FALSE(MetalRenderTargetCubeUploadSupported(8));
}

// plans/plan_apple_m4.md AM4-147
TEST(MetalPolicy, Bgra4444ReadbackInvertsTheUploadRotation)
{
    for (std::uint32_t texel = 0; texel <= 0xFFFFu; ++texel)
    {
        const auto xna = static_cast<std::uint16_t>(texel);
        ASSERT_EQ(MetalAbgr4ToBgra4444(MetalBgra4444ToAbgr4(xna)), xna);
    }
    // XNA A4R4G4B4 0xA123 (a=A r=1 g=2 b=3) is Metal R4G4B4A4 0x123A.
    EXPECT_EQ(MetalBgra4444ToAbgr4(0xA123u), 0x123Au);
    EXPECT_EQ(MetalAbgr4ToBgra4444(0x123Au), 0xA123u);
}

TEST(MetalPolicy, CubesAndVolumesRefuseTheFormatsXnaKeepsToTexture2D)
{
    using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
    for (const bool volume : {false, true})
    {
        EXPECT_TRUE(MetalIsTexture2DOnlyFormat(static_cast<int>(SurfaceFormat::NormalizedByte2), volume));
        EXPECT_TRUE(MetalIsTexture2DOnlyFormat(static_cast<int>(SurfaceFormat::NormalizedByte4), volume));
        EXPECT_FALSE(MetalIsTexture2DOnlyFormat(static_cast<int>(SurfaceFormat::Color), volume));
        EXPECT_FALSE(MetalIsTexture2DOnlyFormat(static_cast<int>(SurfaceFormat::HdrBlendable), volume));
        EXPECT_FALSE(MetalIsTexture2DOnlyFormat(static_cast<int>(SurfaceFormat::Bgra4444), volume));
    }
    for (const SurfaceFormat dxt : {SurfaceFormat::Dxt1, SurfaceFormat::Dxt3, SurfaceFormat::Dxt5})
    {
        EXPECT_FALSE(MetalIsTexture2DOnlyFormat(static_cast<int>(dxt), false));
        EXPECT_TRUE(MetalIsTexture2DOnlyFormat(static_cast<int>(dxt), true));
    }
}

TEST(MetalPolicy, Texture2DImageRequiresColorExactBaseBytesAndCompleteMipShape)
{
    using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
    const int color = static_cast<int>(SurfaceFormat::Color);
    EXPECT_EQ(DescribeMetalTexture2DImagePolicy(color, 4, 4, 1, 64),
              MetalTexture2DImagePolicy::Supported);
    EXPECT_EQ(DescribeMetalTexture2DImagePolicy(color, 4, 4, 3, 64),
              MetalTexture2DImagePolicy::Supported);
    EXPECT_EQ(DescribeMetalTexture2DImagePolicy(1, 4, 4, 1, 64),
              MetalTexture2DImagePolicy::UnsupportedFormat);
    EXPECT_EQ(DescribeMetalTexture2DImagePolicy(color, 0, 4, 1, 0),
              MetalTexture2DImagePolicy::InvalidDimensionsOrMipCount);
    EXPECT_EQ(DescribeMetalTexture2DImagePolicy(color, 4, 4, 2, 64),
              MetalTexture2DImagePolicy::InvalidDimensionsOrMipCount);
    EXPECT_EQ(DescribeMetalTexture2DImagePolicy(color, 4, 4, 1, 63),
              MetalTexture2DImagePolicy::InvalidBaseByteCount);
    EXPECT_EQ(DescribeMetalTexture2DImagePolicy(color, 4, 4, 1, 65),
              MetalTexture2DImagePolicy::InvalidBaseByteCount);
}

TEST(MetalPolicy, BlendWriteStateAcceptsTargetZeroMasksAndLeavesCoverageToTheTarget)
{
    // plans/plan_apple_m4.md AM4-079: target 0's mask is a pipeline property; AM4-146: so are
    // targets 1..3's, which a compiled pixel shader writes.
    // AM4-135: the coverage mask is judged against the bound target, not refused up front.
    BlendWriteState state{};
    EXPECT_TRUE(MetalSupportsBlendWriteState(state));
    state.colorWriteChannels[0]=7;
    EXPECT_TRUE(MetalSupportsBlendWriteState(state));
    state.colorWriteChannels[0]=0;
    EXPECT_TRUE(MetalSupportsBlendWriteState(state));
    state=BlendWriteState{};
    state.colorWriteChannels[3]=0;
    EXPECT_TRUE(MetalSupportsBlendWriteState(state));
    state=BlendWriteState{};
    state.multiSampleMask=0x7FFFFFFFu;
    EXPECT_TRUE(MetalSupportsBlendWriteState(state));
    state.multiSampleMask=0;
    EXPECT_TRUE(MetalSupportsBlendWriteState(state));
    state.colorWriteChannels[0]=16;
    EXPECT_FALSE(MetalSupportsBlendWriteState(state));
    state=BlendWriteState{};
    state.colorWriteChannels[2]=16;
    EXPECT_FALSE(MetalSupportsBlendWriteState(state));
    state.colorWriteChannels[2]=-1;
    EXPECT_FALSE(MetalSupportsBlendWriteState(state));
}

TEST(MetalPolicy, DrawStreamsAcceptLegacyEmptyAndOnePerVertexBinding)
{
    GpuDrawParams empty{};
    EXPECT_EQ(DescribeMetalDrawStreamPolicy(empty),MetalDrawStreamPolicy::Supported);

    struct DummyVertexBuffer final : IVertexBufferRenderer
    {
        void SetData(const void*,int,std::size_t) override {}
        void SetVertexDeclaration(const VertexDeclaration&) override {}
        int GetVertexCount() const override { return 3; }
    } buffer;

    GpuDrawParams one{};
    one.vertexStreamCount=1;
    one.combinedVertexStride=16;
    one.vertexStreams[0].buffer=&buffer;
    one.vertexStreams[0].strideInBytes=16;
    one.vertexStreams[0].vertexCount=3;
    EXPECT_EQ(DescribeMetalDrawStreamPolicy(one),MetalDrawStreamPolicy::Supported);

    one.vertexStreams[0].buffer=nullptr;
    EXPECT_EQ(DescribeMetalDrawStreamPolicy(one),MetalDrawStreamPolicy::InvalidBinding);
}

// plans/plan_apple_m4.md AM4-143: multi-stream and instanced draws are accepted; the metadata
// must still describe per-vertex streams that tile the combined vertex.
TEST(MetalPolicy, DrawStreamsAcceptMultiStreamAndInstancingWithConsistentMetadata)
{
    struct DummyVertexBuffer final : IVertexBufferRenderer
    {
        void SetData(const void*,int,std::size_t) override {}
        void SetVertexDeclaration(const VertexDeclaration&) override {}
        int GetVertexCount() const override { return 3; }
    } first,second;

    GpuDrawParams multi{};
    multi.vertexStreamCount=2;
    multi.combinedVertexStride=32;
    multi.vertexStreams[0].buffer=&first;
    multi.vertexStreams[0].strideInBytes=16;
    multi.vertexStreams[0].vertexCount=3;
    multi.vertexStreams[1].slot=1;
    multi.vertexStreams[1].buffer=&second;
    multi.vertexStreams[1].strideInBytes=16;
    multi.vertexStreams[1].combinedByteBase=16;
    multi.vertexStreams[1].vertexCount=3;
    EXPECT_EQ(DescribeMetalDrawStreamPolicy(multi),MetalDrawStreamPolicy::Supported);
    multi.vertexStreams[1].combinedByteBase=12;
    EXPECT_EQ(DescribeMetalDrawStreamPolicy(multi),MetalDrawStreamPolicy::InvalidBinding);
    multi.vertexStreams[1].combinedByteBase=16;
    multi.combinedVertexStride=16;
    EXPECT_EQ(DescribeMetalDrawStreamPolicy(multi),MetalDrawStreamPolicy::InvalidBinding);

    // Stream 1 per instance: the combined vertex is stream 0 alone.
    multi.vertexStreams[1].instanceFrequency=1;
    multi.vertexStreams[1].combinedByteBase=0;
    EXPECT_EQ(DescribeMetalDrawStreamPolicy(multi),MetalDrawStreamPolicy::Supported);
    multi.instanceCount=3;
    EXPECT_EQ(DescribeMetalDrawStreamPolicy(multi),MetalDrawStreamPolicy::Supported);
    multi.vertexStreams[1].instanceFrequency=-1;
    EXPECT_EQ(DescribeMetalDrawStreamPolicy(multi),MetalDrawStreamPolicy::InvalidBinding);

    // A zero stride is a buffer without a declaration, whose stride Metal resolves from its upload.
    multi.vertexStreams[1].instanceFrequency=1;
    multi.vertexStreams[0].strideInBytes=0;
    multi.combinedVertexStride=0;
    EXPECT_EQ(DescribeMetalDrawStreamPolicy(multi),MetalDrawStreamPolicy::Supported);

    GpuDrawParams count{};
    count.instanceCount=2;
    EXPECT_EQ(DescribeMetalDrawStreamPolicy(count),MetalDrawStreamPolicy::Supported);
    count.instanceCount=0;
    EXPECT_EQ(DescribeMetalDrawStreamPolicy(count),MetalDrawStreamPolicy::InvalidBinding);
}

TEST(MetalPolicy, StreamZeroMustMatchTheUploadedBufferAndStride)
{
    struct DummyVertexBuffer final : IVertexBufferRenderer
    {
        void SetData(const void*,int,std::size_t) override {}
        void SetVertexDeclaration(const VertexDeclaration&) override {}
        int GetVertexCount() const override { return 3; }
    } uploaded, foreign;

    GpuDrawParams params{};
    EXPECT_TRUE(MetalStreamZeroMatchesUploadedBuffer(params, uploaded, 24));

    params.vertexStreamCount=1;
    params.vertexStreams[0].buffer=&uploaded;
    params.vertexStreams[0].strideInBytes=24;
    EXPECT_TRUE(MetalStreamZeroMatchesUploadedBuffer(params, uploaded, 24));

    params.vertexStreams[0].strideInBytes=20;
    EXPECT_FALSE(MetalStreamZeroMatchesUploadedBuffer(params, uploaded, 24));
    params.vertexStreams[0].strideInBytes=24;
    params.vertexStreams[0].buffer=&foreign;
    EXPECT_FALSE(MetalStreamZeroMatchesUploadedBuffer(params, uploaded, 24));

    // AM4-143: a buffer without a declaration is described with a zero stride, resolved from the upload.
    params.vertexStreams[0].buffer=&uploaded;
    params.vertexStreams[0].strideInBytes=0;
    EXPECT_TRUE(MetalStreamZeroMatchesUploadedBuffer(params, uploaded, 24));
}

// plans/plan_apple_m4.md AM4-134: XNA's DepthBias is normalized depth; Metal counts the
// Depth32Float attachment's resolution in [0.5, 1), 2^-24.
TEST(MetalPolicy, DepthBiasIsConvertedToTwentyFourBitUnits)
{
    // AM4-151: the constant bias moves the viewport's depth range, so it is one offset in
    // normalized depth wherever the fragment lies and whatever the attachment.
    const MetalDepthRange none=MetalBiasedDepthRange(0.0, 1.0, 0.0f);
    EXPECT_DOUBLE_EQ(none.minDepth, 0.0);
    EXPECT_DOUBLE_EQ(none.maxDepth, 1.0);
    const MetalDepthRange pushed=MetalBiasedDepthRange(0.25, 0.75, 0.02f);
    EXPECT_DOUBLE_EQ(pushed.minDepth, 0.25 + (double)0.02f);
    EXPECT_DOUBLE_EQ(pushed.maxDepth, 0.75 + (double)0.02f);
    const MetalDepthRange pulled=MetalBiasedDepthRange(0.0, 1.0, -1.0e-4f);
    EXPECT_DOUBLE_EQ(pulled.minDepth, (double)-1.0e-4f);
}

// plans/plan_apple_m4.md AM4-141: MultiSampleMask is judged against the target's sample count.
TEST(MetalPolicy, SampleMaskIsIgnoredSingleSampledAndPartialMasksAreWrittenByTheFragment)
{
    EXPECT_EQ(DescribeMetalSampleMaskAdmission(0u,1),MetalSampleMaskAdmission::Draw)
        << "XNA: no effect on a single-sample target";
    EXPECT_EQ(DescribeMetalSampleMaskAdmission(0xFFFFFFFFu,4),MetalSampleMaskAdmission::Draw);
    EXPECT_EQ(DescribeMetalSampleMaskAdmission(0xFu,4),MetalSampleMaskAdmission::Draw);
    EXPECT_EQ(DescribeMetalSampleMaskAdmission(0xF0u,4),MetalSampleMaskAdmission::Skip)
        << "bits beyond the target's samples keep nothing";
    EXPECT_EQ(DescribeMetalSampleMaskAdmission(0u,8),MetalSampleMaskAdmission::Skip);
    EXPECT_EQ(DescribeMetalSampleMaskAdmission(0x1u,4),MetalSampleMaskAdmission::Masked);
    EXPECT_EQ(DescribeMetalSampleMaskAdmission(0x7Fu,8),MetalSampleMaskAdmission::Masked);
}

// plans/plan_apple_m4.md AM4-142: the render-target formats Metal stores natively.
TEST(MetalPolicy, RenderTargetStorageCoversXnasFloatAndWideFormats)
{
    using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
    MetalColorStorageInfo info{};
    ASSERT_TRUE(MetalRenderTargetStorageFor(static_cast<int>(SurfaceFormat::Color), info));
    EXPECT_EQ(info.storage, MetalColorStorage::Bgra8);
    EXPECT_TRUE(info.bgraOrder);
    ASSERT_TRUE(MetalRenderTargetStorageFor(static_cast<int>(SurfaceFormat::HdrBlendable), info));
    EXPECT_EQ(info.storage, MetalColorStorage::Rgba16Float);
    EXPECT_EQ(info.bytesPerTexel, 8);
    ASSERT_TRUE(MetalRenderTargetStorageFor(static_cast<int>(SurfaceFormat::HalfVector4), info));
    EXPECT_EQ(info.storage, MetalColorStorage::Rgba16Float);
    ASSERT_TRUE(MetalRenderTargetStorageFor(static_cast<int>(SurfaceFormat::Single), info));
    EXPECT_EQ(info.storage, MetalColorStorage::R32Float);
    EXPECT_EQ(info.channels, 1);
    ASSERT_TRUE(MetalRenderTargetStorageFor(static_cast<int>(SurfaceFormat::Vector2), info));
    EXPECT_EQ(info.bytesPerTexel, 8);
    EXPECT_EQ(info.channels, 2);
    ASSERT_TRUE(MetalRenderTargetStorageFor(static_cast<int>(SurfaceFormat::Vector4), info));
    EXPECT_EQ(info.bytesPerTexel, 16);
    ASSERT_TRUE(MetalRenderTargetStorageFor(static_cast<int>(SurfaceFormat::Rg32), info));
    EXPECT_EQ(info.storage, MetalColorStorage::Rg16Unorm);
    ASSERT_TRUE(MetalRenderTargetStorageFor(static_cast<int>(SurfaceFormat::Rgba64), info));
    EXPECT_EQ(info.bytesPerTexel, 8);
    ASSERT_TRUE(MetalRenderTargetStorageFor(static_cast<int>(SurfaceFormat::Rgba1010102), info));
    EXPECT_FALSE(info.bgraOrder);
    EXPECT_FALSE(MetalRenderTargetStorageFor(static_cast<int>(SurfaceFormat::Dxt1), info));
    EXPECT_FALSE(MetalRenderTargetStorageFor(static_cast<int>(SurfaceFormat::Alpha8), info));
    EXPECT_FALSE(MetalRenderTargetStorageFor(static_cast<int>(SurfaceFormat::Bgr565), info));
}

// plans/plan_apple_m4.md AM4-142: the Texture2D formats Metal stores natively.
TEST(MetalPolicy, TextureStorageCoversEveryUncompressedXnaFormat)
{
    using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
    MetalTextureStorageInfo info{};
    ASSERT_TRUE(MetalTextureStorageFor(static_cast<int>(SurfaceFormat::Alpha8), true, info));
    EXPECT_EQ(info.storage, MetalTextureStorage::A8);
    EXPECT_EQ(info.bytesPerTexel, 1);
    ASSERT_TRUE(MetalTextureStorageFor(static_cast<int>(SurfaceFormat::NormalizedByte2), true, info));
    EXPECT_EQ(info.channels, 2);
    ASSERT_TRUE(MetalTextureStorageFor(static_cast<int>(SurfaceFormat::Single), false, info));
    EXPECT_EQ(info.channels, 1);
    ASSERT_TRUE(MetalTextureStorageFor(static_cast<int>(SurfaceFormat::Bgra4444), true, info));
    EXPECT_TRUE(info.rotate4444);
    EXPECT_FALSE(MetalTextureStorageFor(static_cast<int>(SurfaceFormat::Bgr565), false, info))
        << "packed 16-bit formats need an Apple GPU";
    EXPECT_FALSE(MetalTextureStorageFor(static_cast<int>(SurfaceFormat::Dxt1), true, info));
    // XNA A4R4G4B4 0xA123 (A=A, R=1, G=2, B=3) -> Metal R4G4B4A4 0x123A.
    EXPECT_EQ(MetalBgra4444ToAbgr4(0xA123u), 0x123Au);
}

// plans/plan_apple_m4.md AM4-150
TEST(MetalPolicy, BufferedDrawWindowsMetalCannotIssueAreNotServable)
{
    EXPECT_TRUE(MetalBufferedDrawRangeIsServable(0, 6, 6));
    EXPECT_TRUE(MetalBufferedDrawRangeIsServable(3, 3, 6));
    EXPECT_TRUE(MetalBufferedDrawRangeIsServable(6, 0, 6));
    EXPECT_FALSE(MetalBufferedDrawRangeIsServable(-1, 3, 6));    // a negative vertexStart/startIndex
    EXPECT_FALSE(MetalBufferedDrawRangeIsServable(7, 3, 6));     // starts past the buffer
    EXPECT_FALSE(MetalBufferedDrawRangeIsServable(4, 3, 6));     // runs past its end
    EXPECT_FALSE(MetalBufferedDrawRangeIsServable(0, 3, -1));
    EXPECT_FALSE(MetalBufferedDrawRangeIsServable(1, 0x7fffffffLL, 0x7fffffffLL));
}
