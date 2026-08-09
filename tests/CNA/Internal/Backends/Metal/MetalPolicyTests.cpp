// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>

#include "CNA/Internal/Backends/Metal/MetalPolicy.hpp"

using namespace CNA::Internal::Backends;
using namespace CNA::Internal::Backends::Metal;

TEST(MetalPolicy, CapabilitiesAreExhaustiveAndConservative)
{
    EXPECT_TRUE(MetalSupportsCapability(CNA::GraphicsCapability::ThreeD));
    EXPECT_TRUE(MetalSupportsCapability(CNA::GraphicsCapability::DepthStencilBuffer));
    EXPECT_FALSE(MetalSupportsCapability(CNA::GraphicsCapability::MultiSampleAntiAliasing));
    EXPECT_FALSE(MetalSupportsCapability(CNA::GraphicsCapability::MultipleRenderTargets));
    EXPECT_TRUE(MetalSupportsCapability(CNA::GraphicsCapability::AnisotropicFiltering));
    EXPECT_TRUE(MetalSupportsCapability(CNA::GraphicsCapability::WireFrame));
    EXPECT_TRUE(MetalSupportsCapability(CNA::GraphicsCapability::OcclusionQuery));
    EXPECT_FALSE(MetalSupportsCapability(CNA::GraphicsCapability::CustomEffects));
    EXPECT_TRUE(MetalSupportsCapability(CNA::GraphicsCapability::Texture3D));
    EXPECT_FALSE(MetalSupportsCapability(CNA::GraphicsCapability::MultiStreamVertexInput));
    EXPECT_FALSE(MetalSupportsCapability(CNA::GraphicsCapability::Instancing));
    EXPECT_TRUE(MetalSupportsCapability(CNA::GraphicsCapability::StencilBuffer));
    EXPECT_TRUE(MetalSupportsCapability(CNA::GraphicsCapability::AdditiveBlending));
    EXPECT_FALSE(MetalSupportsCapability(static_cast<CNA::GraphicsCapability>(999)));
}

TEST(MetalPolicy, FormatAndSampleCountNeverOverclaim)
{
    using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
    EXPECT_TRUE(MetalSupportsSurfaceFormat(static_cast<int>(SurfaceFormat::Color)));
    EXPECT_FALSE(MetalSupportsSurfaceFormat(1));
    EXPECT_FALSE(MetalSupportsSurfaceFormat(26));
    EXPECT_EQ(MetalAppliedMultiSampleCount(-1),0);
    EXPECT_EQ(MetalAppliedMultiSampleCount(0),0);
    EXPECT_EQ(MetalAppliedMultiSampleCount(2),0);
    EXPECT_EQ(MetalAppliedMultiSampleCount(8),0);
}

TEST(MetalPolicy, BlendWriteStateAcceptsOnlyTheImplementedDefault)
{
    BlendWriteState state{};
    EXPECT_TRUE(MetalSupportsBlendWriteState(state));
    state.colorWriteChannels[0]=7;
    EXPECT_FALSE(MetalSupportsBlendWriteState(state));
    state=BlendWriteState{};
    state.colorWriteChannels[3]=0;
    EXPECT_FALSE(MetalSupportsBlendWriteState(state));
    state=BlendWriteState{};
    state.multiSampleMask=0x7FFFFFFFu;
    EXPECT_FALSE(MetalSupportsBlendWriteState(state));
}

TEST(MetalPolicy, DrawStreamsAcceptLegacyEmptyAndOnePerVertexBinding)
{
    GpuDrawParams empty{};
    EXPECT_EQ(DescribeMetalDrawStreamPolicy(empty),MetalDrawStreamPolicy::Supported);

    struct DummyVertexBuffer final : IVertexBufferBackend
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

TEST(MetalPolicy, DrawStreamsRejectMultiStreamAndInstancing)
{
    struct DummyVertexBuffer final : IVertexBufferBackend
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
    EXPECT_EQ(DescribeMetalDrawStreamPolicy(multi),MetalDrawStreamPolicy::MultiStreamUnsupported);

    multi.vertexStreams[1].instanceFrequency=1;
    EXPECT_EQ(DescribeMetalDrawStreamPolicy(multi),MetalDrawStreamPolicy::InstancingUnsupported);

    GpuDrawParams count{};
    count.instanceCount=2;
    EXPECT_EQ(DescribeMetalDrawStreamPolicy(count),MetalDrawStreamPolicy::InstancingUnsupported);
}
