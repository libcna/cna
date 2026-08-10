#include <limits>

#include <gtest/gtest.h>

#include "CNA/Internal/Backends/Glide/GlideDrawValidation.hpp"

using CNA::Internal::Backends::GpuDrawParams;
using CNA::Internal::Backends::IVertexBufferBackend;
using CNA::Internal::Backends::Glide::MakeGlideExpandedIndexedDrawParams;
using CNA::Internal::Backends::Glide::ValidateGlideDualSamplerAddressModes;
using CNA::Internal::Backends::Glide::ValidateGlideSamplerMipState;
using CNA::Internal::Backends::Glide::ValidateGlideVertexStreams;
using Microsoft::Xna::Framework::Graphics::VertexDeclaration;

namespace
{
    class TestVertexBuffer final : public IVertexBufferBackend
    {
    public:
        void SetData(const void*, int, std::size_t) override {}
        void SetVertexDeclaration(const VertexDeclaration&) override {}
        [[nodiscard]] int GetVertexCount() const override { return 8; }
    };

    GpuDrawParams ValidSingleStream(const IVertexBufferBackend* buffer)
    {
        GpuDrawParams params;
        params.vertexStreamCount = 1;
        params.combinedVertexStride = 24;
        params.vertexStreams[0].buffer = buffer;
        params.vertexStreams[0].strideInBytes = 24;
        params.vertexStreams[0].vertexCount = 8;
        return params;
    }
}

TEST(GlideDrawValidationTest, AcceptsDrawUserAndOneOrdinaryStreamShapes)
{
    TestVertexBuffer primary;
    const GpuDrawParams drawUser;
    EXPECT_NO_THROW(ValidateGlideVertexStreams(drawUser, &primary, 24, 8));

    const GpuDrawParams ordinary = ValidSingleStream(&primary);
    EXPECT_NO_THROW(ValidateGlideVertexStreams(ordinary, &primary, 24, 8));
}

TEST(GlideDrawValidationTest, RejectsMultiStreamAndInstanceInput)
{
    TestVertexBuffer primary;
    GpuDrawParams params = ValidSingleStream(&primary);
    params.vertexStreamCount = 2;
    EXPECT_THROW(ValidateGlideVertexStreams(params, &primary, 24, 8), std::runtime_error);

    params = ValidSingleStream(&primary);
    params.vertexStreams[0].instanceFrequency = 1;
    EXPECT_THROW(ValidateGlideVertexStreams(params, &primary, 24, 8), std::runtime_error);
}

TEST(GlideDrawValidationTest, RejectsUnfoldedOffsetsAndMismatchedMetadata)
{
    TestVertexBuffer primary;
    TestVertexBuffer other;
    GpuDrawParams params = ValidSingleStream(&primary);
    params.vertexStreams[0].vertexOffset = 1;
    EXPECT_THROW(ValidateGlideVertexStreams(params, &primary, 24, 8), std::runtime_error);

    params = ValidSingleStream(&primary);
    params.vertexStreams[0].buffer = &other;
    EXPECT_THROW(ValidateGlideVertexStreams(params, &primary, 24, 8), std::runtime_error);

    params = ValidSingleStream(&primary);
    params.vertexStreams[0].slot = 1;
    EXPECT_THROW(ValidateGlideVertexStreams(params, &primary, 24, 8), std::runtime_error);

    params = ValidSingleStream(&primary);
    params.vertexStreams[0].strideInBytes = 20;
    EXPECT_THROW(ValidateGlideVertexStreams(params, &primary, 24, 8), std::runtime_error);

    params = ValidSingleStream(&primary);
    params.vertexStreams[0].vertexCount = 7;
    EXPECT_THROW(ValidateGlideVertexStreams(params, &primary, 24, 8), std::runtime_error);
}

TEST(GlideDrawValidationTest, DualTmuRequiresSharedAddressModes)
{
    EXPECT_NO_THROW(ValidateGlideDualSamplerAddressModes(0, 1, 0, 1));
    EXPECT_THROW(ValidateGlideDualSamplerAddressModes(0, 1, 2, 1), std::runtime_error);
    EXPECT_THROW(ValidateGlideDualSamplerAddressModes(0, 1, 0, 2), std::runtime_error);
}

TEST(GlideDrawValidationTest, IndexedExpansionClearsOnlyConsumedDrawAddressing)
{
    TestVertexBuffer primary;
    GpuDrawParams params = ValidSingleStream(&primary);
    params.vertexStart = 3;
    params.startIndex = 4;
    params.baseVertex = 5;
    params.diffuseColor[0] = 0.25f;
    params.textureEnabled = true;

    const GpuDrawParams expanded = MakeGlideExpandedIndexedDrawParams(params);

    EXPECT_EQ(expanded.vertexStreamCount, 0);
    EXPECT_EQ(expanded.combinedVertexStride, 0);
    EXPECT_EQ(expanded.vertexStreams[0].buffer, nullptr);
    EXPECT_EQ(expanded.vertexStart, 0);
    EXPECT_EQ(expanded.startIndex, 0);
    EXPECT_EQ(expanded.baseVertex, 0);
    EXPECT_FLOAT_EQ(expanded.diffuseColor[0], 0.25f);
    EXPECT_TRUE(expanded.textureEnabled);
}

TEST(GlideDrawValidationTest, ValidatesRepresentableSamplerMipSubset)
{
    EXPECT_NO_THROW(ValidateGlideSamplerMipState(0, -8.0f));
    EXPECT_NO_THROW(ValidateGlideSamplerMipState(0, 7.75f));
    EXPECT_THROW(ValidateGlideSamplerMipState(1, 0.0f), std::runtime_error);
    EXPECT_THROW(ValidateGlideSamplerMipState(0, -8.25f), std::runtime_error);
    EXPECT_THROW(ValidateGlideSamplerMipState(0, 8.0f), std::runtime_error);
    EXPECT_THROW(ValidateGlideSamplerMipState(0, std::numeric_limits<float>::infinity()),
                 std::runtime_error);
}
