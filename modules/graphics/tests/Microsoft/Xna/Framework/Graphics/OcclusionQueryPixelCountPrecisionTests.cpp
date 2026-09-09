// SPDX-License-Identifier: MS-PL
// XNA's OcclusionQuery.PixelCount is a COUNT of the fragments that passed. OpenGL ES 3.0 and
// WebGL 2 have no query target that produces one -- their core occlusion target is the boolean
// GL_ANY_SAMPLES_PASSED -- so EasyGL asks the driver for GL_SAMPLES_PASSED and falls back.
//
// The two existing EasyGL occlusion examples assert `PixelCount() > 0` and `PixelCount() <= 0`.
// Those pass identically for a real tally and for a boolean, which is why the per-renderer matrix
// called EasyGL "fully correct" while a game dividing PixelCount() by an area got 1/area. This
// test is the one that can tell them apart: it makes the claim and the value agree or fail.

#include <cstdint>
#include <vector>
#include <gtest/gtest.h>

#include "CNA/GraphicsCapability.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectPass.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectPassCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectTechnique.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsAdapter.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/OcclusionQuery.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/NotSupportedException.hpp"
#include "System/ObjectDisposedException.hpp"

using CNA::GraphicsCapability;
using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Rectangle;
using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Graphics::BasicEffect;
using Microsoft::Xna::Framework::Graphics::BlendState;
using Microsoft::Xna::Framework::Graphics::EffectPass;
using Microsoft::Xna::Framework::Graphics::GraphicsAdapter;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::GraphicsProfile;
using Microsoft::Xna::Framework::Graphics::OcclusionQuery;
using Microsoft::Xna::Framework::Graphics::PresentationParameters;
using Microsoft::Xna::Framework::Graphics::PrimitiveType;
using Microsoft::Xna::Framework::Graphics::RasterizerState;
using Microsoft::Xna::Framework::Graphics::VertexPositionColor;

namespace
{
    TEST(OcclusionQueryProfileContractTest, ReachProfileRejectsConstruction)
    {
        GraphicsDevice reachDevice;
        EXPECT_THROW(OcclusionQuery query(reachDevice), System::NotSupportedException);
    }

    class OcclusionQueryPixelCountPrecisionTest : public ::testing::Test
    {
    protected:
        GraphicsDevice device{GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::HiDef,
                              PresentationParameters()};

        void SetUp() override
        {
            if (!device.SupportsCapability(GraphicsCapability::ThreeD))
                GTEST_SKIP() << "Renderer explicitly does not support 3D draws";
            if (!device.SupportsCapability(GraphicsCapability::OcclusionQuery))
                GTEST_SKIP() << "Renderer explicitly does not support occlusion queries";
        }
    };

    // SOFTWARE-199: recovered Microsoft XNA 4.0 code owns this public state machine above the
    // native query. FNA delegates these invalid sequences to FNA3D/GL, but that lower-authority
    // behavior cannot replace XNA's explicit InvalidOperationException contract.
    TEST_F(OcclusionQueryPixelCountPrecisionTest, XnaLifecycleRejectsUnavailableAndInvalidSequences)
    {
        OcclusionQuery fresh(device);
        EXPECT_FALSE(fresh.getIsCompleteProperty());
        EXPECT_THROW((void) fresh.getPixelCountProperty(), System::InvalidOperationException);
        EXPECT_THROW(fresh.End(), System::InvalidOperationException);

        fresh.Begin();
        EXPECT_THROW(fresh.Begin(), System::InvalidOperationException);
        fresh.End();
        EXPECT_THROW(fresh.End(), System::InvalidOperationException);

        OcclusionQuery reusable(device);
        reusable.Begin();
        reusable.End();
        EXPECT_THROW(reusable.Begin(), System::InvalidOperationException)
            << "XNA requires IsComplete/PixelCount to be queried before reusing the object";

        // XNA records that IsComplete was queried even when the native result is not ready yet.
        // That observation, rather than completion itself, permits the next Begin.
        (void) reusable.getIsCompleteProperty();
        EXPECT_NO_THROW(reusable.Begin());
        EXPECT_NO_THROW(reusable.End());

        OcclusionQuery disposed(device);
        disposed.Dispose();
        EXPECT_THROW((void) disposed.getIsCompleteProperty(), System::ObjectDisposedException);
        EXPECT_THROW((void) disposed.getPixelCountProperty(), System::ObjectDisposedException);
        EXPECT_THROW(disposed.Begin(), System::ObjectDisposedException);
        EXPECT_THROW(disposed.End(), System::ObjectDisposedException);
    }

    TEST_F(OcclusionQueryPixelCountPrecisionTest, PixelCountMatchesWhatTheQuerySaysItIs)
    {
        const int width = device.getViewportProperty().getWidthProperty();
        const int height = device.getViewportProperty().getHeightProperty();
        ASSERT_GT(width * height, 4000) << "the backbuffer must be big enough to tell a tally "
                                           "from a flag";

        // A full-NDC quad, so the covered area is the whole viewport rather than a few pixels.
        const Color quadColor(255, 0, 0, 255);
        const std::vector<VertexPositionColor> quad{
            {Vector3(-1, -1, 0), quadColor},
            {Vector3(1, -1, 0), quadColor},
            {Vector3(-1, 1, 0), quadColor},
            {Vector3(1, 1, 0), quadColor},
        };

        device.Clear(Color(0, 255, 0, 255));
        device.SetDepthTestEnabled(false);
        device.setBlendStateProperty(BlendState::Opaque);
        // An NDC quad winds CCW under CNA's default RasterizerState, so it needs CullNone.
        device.setRasterizerStateProperty(RasterizerState::CullNone);

        BasicEffect effect(device);
        effect.VertexColorEnabled = true;

        OcclusionQuery query(device);

        query.Begin();
        for (EffectPass& pass : effect.getCurrentTechniqueProperty()->getPassesProperty())
        {
            pass.Apply();
            device.DrawUserPrimitives(PrimitiveType::TriangleStrip, quad.data(), 0, 2,
                                      VertexPositionColor::getVertexDeclarationStatic());
        }
        query.End();

        // GetBackBufferData reads back through glReadPixels, which flushes; it also proves the
        // quad really did cover the frame, so a count of 1 cannot be explained by a tiny draw.
        auto sample = [&](int x, int y) {
            Color pixel(0, 0, 0, 0);
            Rectangle region(x, y, 1, 1);
            device.GetBackBufferData(&region, &pixel, 0, 1);
            return pixel;
        };

        Color centre = sample(width / 2, height / 2);
        bool complete = query.getIsCompleteProperty();
        for (int attempt = 0; attempt < 30 && !complete; ++attempt)
        {
            sample(width / 2, height / 2);
            complete = query.getIsCompleteProperty();
        }
        ASSERT_TRUE(complete) << "the query never completed";

        EXPECT_EQ(255, centre.getRProperty()) << "the quad did not actually cover the frame";
        EXPECT_EQ(255, sample(2, 2).getRProperty());
        EXPECT_EQ(255, sample(width - 3, height - 3).getRProperty());

        const int pixelCount = query.getPixelCountProperty();
        EXPECT_GT(pixelCount, 0) << "a fully visible quad must not read as occluded";

        if (query.isPixelCountPreciseEXT())
        {
            // A real tally of a full-viewport quad is the viewport's own area, not a flag.
            EXPECT_GT(pixelCount, 4000)
                << "the query claims a precise count but answered like a boolean";
        }
        else
        {
            // GL_ANY_SAMPLES_PASSED: 1 for "any", however many pixels were actually covered.
            EXPECT_LE(pixelCount, 1)
                << "the query claims to be boolean-only but answered with a tally";
        }
    }
}
