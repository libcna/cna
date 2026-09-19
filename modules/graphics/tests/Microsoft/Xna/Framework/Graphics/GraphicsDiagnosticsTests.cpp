// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>

#if CNA_DIAGNOSTICS_LEVEL >= 1

#include "CNA/Diagnostics/Diagnostics.hpp"
#include "CNA/Internal/Graphics/DiagnosticResource.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <string_view>
#include <unordered_set>

namespace
{
    using CNA::Diagnostics::Accuracy;
    using CNA::Diagnostics::BeginFrame;
    using CNA::Diagnostics::EndFrame;
    using CNA::Diagnostics::FrameSample;
    using CNA::Diagnostics::GetProvider;
    using CNA::Diagnostics::MetricSample;
    using CNA::Diagnostics::Mode;
    using CNA::Diagnostics::ResourceId;
    using CNA::Diagnostics::ResourceKind;
    using CNA::Diagnostics::ResourceRecord;
    using CNA::Diagnostics::SetRuntimeMode;
    using Microsoft::Xna::Framework::Color;
    using Microsoft::Xna::Framework::Matrix;
    using Microsoft::Xna::Framework::Vector2;
    using Microsoft::Xna::Framework::Vector3;
    using Microsoft::Xna::Framework::Graphics::BasicEffect;
    using Microsoft::Xna::Framework::Graphics::BlendState;
    using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
    using Microsoft::Xna::Framework::Graphics::PrimitiveType;
    using Microsoft::Xna::Framework::Graphics::RenderTarget2D;
    using Microsoft::Xna::Framework::Graphics::SpriteBatch;
    using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
    using Microsoft::Xna::Framework::Graphics::Texture2D;
    using Microsoft::Xna::Framework::Graphics::VertexPositionColor;

    [[nodiscard]] std::unordered_set<ResourceId> ResourceIds()
    {
        std::unordered_set<ResourceId> ids;
        for (const ResourceRecord& resource : GetProvider().CaptureSnapshot().resources)
            ids.insert(resource.id);
        return ids;
    }

    [[nodiscard]] const MetricSample* FindFrameMetric(
        const FrameSample& frame, const std::string_view name)
    {
        const auto found = std::find_if(frame.metrics.begin(), frame.metrics.end(),
            [name](const MetricSample& metric) { return metric.name == name; });
        return found == frame.metrics.end() ? nullptr : &*found;
    }

    void ResetFrameCounters()
    {
        BeginFrame();
        EndFrame();
    }

    // Registering a throwaway handle reveals the next resource ID, so the difference between two
    // probes counts every registration made in between, including ones already released again.
    [[nodiscard]] ResourceId ProbeNextResourceId()
    {
        return CNA::Diagnostics::ResourceHandle(CNA::Diagnostics::ResourceDescriptor{}).GetId();
    }

    TEST(GraphicsDiagnosticsTest, TextureMetadataUsesStableEstimatedPayloadAndUnregisters)
    {
        ASSERT_TRUE(SetRuntimeMode(Mode::Stats));
        GraphicsDevice device;
        const std::unordered_set<ResourceId> before = ResourceIds();

        ResourceId textureId = 0;
        {
            Texture2D texture(device, 13, 7, false, SurfaceFormat::Color);
            const auto snapshot = GetProvider().CaptureSnapshot();
            const auto found = std::find_if(
                snapshot.resources.begin(), snapshot.resources.end(),
                [&before](const ResourceRecord& resource)
                {
                    return !before.contains(resource.id) &&
                           resource.kind == ResourceKind::Texture2D &&
                           resource.width == 13 && resource.height == 7;
                });
            ASSERT_NE(found, snapshot.resources.end());
            textureId = found->id;
            EXPECT_EQ(found->estimatedBytes, 13u * 7u * 4u);
            EXPECT_EQ(found->byteAccuracy, Accuracy::Estimated);
            texture.Dispose();
        }

        const std::unordered_set<ResourceId> after = ResourceIds();
        EXPECT_FALSE(after.contains(textureId));
    }

    TEST(GraphicsDiagnosticsTest, AssignedReferenceIdentityHasOneDiagnosticResource)
    {
        ASSERT_TRUE(SetRuntimeMode(Mode::Stats));
        const std::size_t baseline = GetProvider().CaptureSnapshot().resources.size();
        {
            BlendState first;
            BlendState second;
            EXPECT_EQ(GetProvider().CaptureSnapshot().resources.size(), baseline + 2);
            second = first;
            EXPECT_EQ(GetProvider().CaptureSnapshot().resources.size(), baseline + 1);
        }
        EXPECT_EQ(GetProvider().CaptureSnapshot().resources.size(), baseline);
    }

    TEST(GraphicsDiagnosticsTest, StateAssignmentAndSpriteBatchBeginRegisterNoTransientResources)
    {
        // Assignment used to register a new resource that ShareResourceIdentityWith() released a
        // few lines later, so every SpriteBatch::Begin published four create/destroy pairs.
        ASSERT_TRUE(SetRuntimeMode(Mode::Stats));
        GraphicsDevice device;
        Texture2D texture(device, 2, 2, false, SurfaceFormat::Color);
        SpriteBatch batch(device);
        BlendState first;
        BlendState second;
        batch.Begin();
        batch.Draw(texture, Vector2(0.0f, 0.0f), Color::White);
        batch.End();

        const ResourceId before = ProbeNextResourceId();
        for (int iteration = 0; iteration < 50; ++iteration)
        {
            second = first;
            batch.Begin();
            batch.Draw(texture, Vector2(0.0f, 0.0f), Color::White);
            batch.End();
        }
        const ResourceId after = ProbeNextResourceId();
        EXPECT_EQ(after, before + 1) << "only the second probe may have registered";
    }

    TEST(GraphicsDiagnosticsTest, TextureByteEstimateIsExactAndSaturatesAtSupportedLimits)
    {
        using CNA::Internal::Graphics::EstimateTextureBytes;
        EXPECT_EQ(EstimateTextureBytes(16, 8, 1, 1, 1, SurfaceFormat::Color), 512u);
        EXPECT_EQ(EstimateTextureBytes(16, 8, 1, 3, 1, SurfaceFormat::Color),
                  (16u * 8u + 8u * 4u + 4u * 2u) * 4u);
        EXPECT_EQ(EstimateTextureBytes(
                      std::numeric_limits<int>::max(), std::numeric_limits<int>::max(),
                      std::numeric_limits<int>::max(), 1, 6, SurfaceFormat::Color),
                  std::numeric_limits<std::uint64_t>::max());
    }

    TEST(GraphicsDiagnosticsTest, DrawMetricsReportExactIndexedAndNonIndexedWork)
    {
        ASSERT_TRUE(SetRuntimeMode(Mode::Stats));
        GraphicsDevice device;
        BasicEffect first(device);
        first.VertexColorEnabled = true;
        first.setLightingEnabledProperty(false);
        first.setWorldProperty(Matrix::getIdentityProperty());
        first.setViewProperty(Matrix::getIdentityProperty());
        first.setProjectionProperty(Matrix::getIdentityProperty());
        BasicEffect second(device);
        second.VertexColorEnabled = true;
        second.setLightingEnabledProperty(false);
        const std::array vertices = {
            VertexPositionColor(Vector3(-1.0f, -1.0f, 0.0f), Color::Red),
            VertexPositionColor(Vector3(0.0f, 1.0f, 0.0f), Color::Green),
            VertexPositionColor(Vector3(1.0f, -1.0f, 0.0f), Color::Blue)};
        const std::array<std::uint16_t, 3> indices{0, 1, 2};
        ResetFrameCounters();

        BeginFrame();
        first.Apply();
        device.DrawUserPrimitives(PrimitiveType::TriangleList, vertices.data(), 0, 1);
        second.Apply();
        device.DrawUserIndexedPrimitives(
            PrimitiveType::TriangleList, vertices.data(), 0,
            static_cast<int>(vertices.size()), indices.data(), 0, 1);
        EndFrame();

        const auto snapshot = GetProvider().CaptureSnapshot();
        ASSERT_FALSE(snapshot.recentFrames.empty());
        const FrameSample& frame = snapshot.recentFrames.back();
        const MetricSample* draws = FindFrameMetric(frame, "Graphics/DrawCalls");
        const MetricSample* indexed = FindFrameMetric(frame, "Graphics/IndexedDrawCalls");
        const MetricSample* nonIndexed =
            FindFrameMetric(frame, "Graphics/NonIndexedDrawCalls");
        const MetricSample* primitives =
            FindFrameMetric(frame, "Graphics/SubmittedPrimitives");
        const MetricSample* effects = FindFrameMetric(frame, "Graphics/EffectChanges");
        ASSERT_NE(draws, nullptr);
        ASSERT_NE(indexed, nullptr);
        ASSERT_NE(nonIndexed, nullptr);
        ASSERT_NE(primitives, nullptr);
        ASSERT_NE(effects, nullptr);
        EXPECT_EQ(draws->value, 2);
        EXPECT_EQ(indexed->value, 1);
        EXPECT_EQ(nonIndexed->value, 1);
        EXPECT_EQ(primitives->value, 2);
        EXPECT_EQ(effects->value, 2);
    }

    TEST(GraphicsDiagnosticsTest, StateAndSpriteMetricsCountOnlyActualChangesAndSubmissions)
    {
        ASSERT_TRUE(SetRuntimeMode(Mode::Stats));
        GraphicsDevice device;
        Texture2D texture(device, 2, 2, false, SurfaceFormat::Color);
        RenderTarget2D target(device, 4, 4);
        SpriteBatch batch(device);
        ResetFrameCounters();

        BeginFrame();
        device.getTexturesProperty()(0, &texture);
        device.getTexturesProperty()(0, &texture);
        device.getTexturesProperty()(0, nullptr);
        device.SetRenderTarget(&target);
        device.SetRenderTarget(nullptr);
        batch.Begin();
        batch.Draw(texture, Vector2(0.0f, 0.0f), Color::White);
        batch.End();
        EndFrame();

        const auto snapshot = GetProvider().CaptureSnapshot();
        ASSERT_FALSE(snapshot.recentFrames.empty());
        const FrameSample& frame = snapshot.recentFrames.back();
        const MetricSample* bindings =
            FindFrameMetric(frame, "Graphics/TextureBindingChanges");
        const MetricSample* targets =
            FindFrameMetric(frame, "Graphics/RenderTargetChanges");
        const MetricSample* sprites =
            FindFrameMetric(frame, "Graphics/SpriteSubmissions");
        ASSERT_NE(bindings, nullptr);
        ASSERT_NE(targets, nullptr);
        ASSERT_NE(sprites, nullptr);
        EXPECT_EQ(bindings->value, 2);
        EXPECT_EQ(targets->value, 2);
        EXPECT_EQ(sprites->value, 1);
    }
}

#endif
