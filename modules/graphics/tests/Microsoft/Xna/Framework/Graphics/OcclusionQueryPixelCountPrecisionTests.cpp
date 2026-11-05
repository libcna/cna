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
#include <typeinfo>
#include <vector>
#include <gtest/gtest.h>

#include "CNA/GraphicsCapability.hpp"
#include "CNA/RendererTestGate.hpp"
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
#include "Microsoft/Xna/Framework/Graphics/Viewport.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/NotSupportedException.hpp"
#include "System/NullReferenceException.hpp"
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
    // Refresh OcclusionQuery Begin IL_0008-0025, End IL_0008-0012 and
    // get_PixelCount IL_0008-0012 select these FrameworkResources strings.
    constexpr const char* pairOpen =
        "Begin cannot be called again until End has been successfully called.";
    constexpr const char* notObserved =
        "Begin may not be called on this query object again before IsComplete has been checked.";
    constexpr const char* pairMissing =
        "Begin must be called successfully before End can be called.";
    constexpr const char* unavailable =
        "The query data is not yet available. Use the IsComplete property to determine if the "
        "data is available before attempting to retrieve it.";
    // Begin IL_0026-0035 / End IL_0013-0022 call through the released pointer;
    // the CLR supplies its default NullReferenceException, without an XNA message.
    constexpr const char* nullReference = "Object reference not set to an instance of an object.";

    template <typename Exception, typename Operation>
    void ExpectExactFailure(Operation&& operation, const char* message)
    {
        try
        {
            operation();
            ADD_FAILURE() << "Expected " << typeid(Exception).name() << ": " << message;
        }
        catch (const Exception& error)
        {
            // ObjectDisposedException is an InvalidOperationException subtype; reject it too.
            EXPECT_EQ(typeid(error), typeid(Exception));
            EXPECT_STREQ(error.what(), message);
        }
        catch (...)
        {
            ADD_FAILURE() << "Unexpected exception type; expected " << typeid(Exception).name();
        }
    }

    template <typename Operation>
    void ExpectStateRefusalNotDisposal(Operation&& operation, const char* what, const char* message)
    {
        SCOPED_TRACE(what);
        ExpectExactFailure<System::InvalidOperationException>(operation, message);
    }

    TEST(OcclusionQueryProfileContractTest, ReachProfileRejectsConstruction)
    {
        GraphicsDevice reachDevice;
        ASSERT_EQ(reachDevice.getGraphicsProfileProperty(), GraphicsProfile::Reach);
        // Refresh .ctor IL_0019-0040 + ProfileCapabilities.ThrowNotSupportedException
        // IL_0000-002a; ProfileFeatureNotSupported uses {Reach, OcclusionQuery}.
        ExpectExactFailure<System::NotSupportedException>([&] { OcclusionQuery query(reachDevice); },
            "XNA Framework Reach profile does not support OcclusionQuery.");
    }

    class OcclusionQueryPixelCountPrecisionTest : public ::testing::Test
    {
    protected:
        GraphicsDevice device{GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::HiDef,
                              PresentationParameters::CreateWindowedEXT(800, 480)};

        void SetUp() override
        {
            if (!device.SupportsCapability(GraphicsCapability::OcclusionQuery))
                GTEST_SKIP() << "Renderer explicitly does not support occlusion queries";
        }
    };

    // SOFTWARE-199: recovered Microsoft XNA 4.0 code owns this public state machine above the
    // native query. FNA src/Graphics/OcclusionQuery.cs:97-119 agrees on the pair/observation guards;
    // the Refresh IL and its resource strings remain the independent primary oracle.
    TEST_F(OcclusionQueryPixelCountPrecisionTest, XnaLifecycleRejectsUnavailableAndInvalidSequences)
    {
        OcclusionQuery fresh(device);
        EXPECT_FALSE(fresh.getIsCompleteProperty());
        ExpectExactFailure<System::InvalidOperationException>([&] { (void) fresh.getPixelCountProperty(); }, unavailable);
        ExpectExactFailure<System::InvalidOperationException>([&] { fresh.End(); }, pairMissing);

        fresh.Begin();
        ExpectExactFailure<System::InvalidOperationException>([&] { fresh.Begin(); }, pairOpen);
        fresh.End();
        ExpectExactFailure<System::InvalidOperationException>([&] { fresh.End(); }, pairMissing);

        OcclusionQuery reusable(device);
        reusable.Begin();
        reusable.End();
        ExpectExactFailure<System::InvalidOperationException>([&] { reusable.Begin(); }, notObserved);

        // XNA records that IsComplete was queried even when the native result is not ready yet.
        // That observation, rather than completion itself, permits the next Begin.
        (void) reusable.getIsCompleteProperty();
        EXPECT_NO_THROW(reusable.Begin());
        EXPECT_NO_THROW(reusable.End());

        // TQ-035 (owner decision 2026-11-04): a disposed query follows XNA, not CNA's general
        // use-after-dispose rule. XNA's members have no disposal check; Dispose only cleared the
        // native pointer (OcclusionQuery::ReleaseNativeObject IL_0024-0032).
        OcclusionQuery disposed(device);
        disposed.Dispose();
        ASSERT_TRUE(disposed.getIsDisposedProperty());
        // get_IsComplete IL_000e-0012: a null native query answers false.
        EXPECT_FALSE(disposed.getIsCompleteProperty());
        // get_PixelCount IL_0000-0012: IsComplete false -> InvalidOperationException.
        ExpectStateRefusalNotDisposal([&] { (void) disposed.getPixelCountProperty(); },
                                      "PixelCount after Dispose", unavailable);
        // End IL_0000-0012: no Begin is open, refused before the native call.
        ExpectStateRefusalNotDisposal([&] { disposed.End(); }, "End after Dispose", pairMissing);
        // A fresh query starts observed (.ctor IL_0045-0047), so Begin passes IL_0000-0025 and
        // calls through the released native query (IL_0026-0035).
        ExpectExactFailure<System::NullReferenceException>([&] { disposed.Begin(); }, nullReference);
    }

    // TQ-035: every member's order of checks after Dispose, each pinned against the XNA IL.
    // The state machine (_isInBeginEndPair, _hasIsCompleteBeenQueried) survives Dispose
    // untouched -- ReleaseNativeObject clears only _isAvailable and pComPtr -- so which refusal
    // a disposed query gives depends on where Dispose caught it.
    TEST_F(OcclusionQueryPixelCountPrecisionTest, DisposedQueryKeepsXnasStateMachine)
    {
        // Disposed inside an open Begin/End pair.
        OcclusionQuery open(device);
        open.Begin();
        open.Dispose();
        // Begin IL_0000-0012 refuses the open pair first.
        ExpectStateRefusalNotDisposal([&] { open.Begin(); }, "Begin on a pair left open", pairOpen);
        // End IL_0000-0006 finds the pair open, then IL_0013-0022 reach the released query; the
        // pair stays open because IL_0033-0035 never run.
        ExpectExactFailure<System::NullReferenceException>([&] { open.End(); }, nullReference);
        ExpectExactFailure<System::NullReferenceException>([&] { open.End(); }, nullReference);
        ExpectStateRefusalNotDisposal([&] { open.Begin(); }, "Begin after a failed End", pairOpen);
        EXPECT_FALSE(open.getIsCompleteProperty());

        // Disposed after End, before the result was observed.
        OcclusionQuery unobserved(device);
        unobserved.Begin();
        unobserved.End();
        unobserved.Dispose();
        // Begin IL_0013-0025: IsComplete has not been queried since Begin.
        ExpectStateRefusalNotDisposal([&] { unobserved.Begin(); }, "Begin before observation", notObserved);
        // get_IsComplete IL_0000-0002 records the observation even though it answers false...
        EXPECT_FALSE(unobserved.getIsCompleteProperty());
        // ...so Begin now passes both checks and reaches the released query.
        ExpectExactFailure<System::NullReferenceException>([&] { unobserved.Begin(); }, nullReference);
        // End IL_0000-0012: that Begin failed before IL_004e-004f opened a pair.
        ExpectStateRefusalNotDisposal([&] { unobserved.End(); }, "End with no pair open", pairMissing);
        ExpectStateRefusalNotDisposal([&] { (void) unobserved.getPixelCountProperty(); },
                                      "PixelCount after Dispose", unavailable);

        // The CNA extensions answer for the released query instead of throwing.
        EXPECT_FALSE(unobserved.isPixelCountPreciseEXT());
        EXPECT_FALSE(unobserved.HasRenderer());
    }

    TEST_F(OcclusionQueryPixelCountPrecisionTest, ObservationDuringOpenPairPermitsOnlyTheNextCycle)
    {
        OcclusionQuery query(device);
        query.Begin();
        // Refresh get_IsComplete IL_0000-0002 records even a false observation.
        EXPECT_FALSE(query.getIsCompleteProperty());
        ExpectExactFailure<System::InvalidOperationException>([&] { query.Begin(); }, pairOpen);
        query.End();
        // No observation between End and Begin: the one inside the pair already counts.
        ASSERT_NO_THROW(query.Begin());
        // Refresh Begin IL_005b-005d resets the observation on every successful Begin.
        query.End();
        ExpectExactFailure<System::InvalidOperationException>([&] { query.Begin(); }, notObserved);
    }

    TEST_F(OcclusionQueryPixelCountPrecisionTest, UnavailablePixelCountStillPermitsReuseAfterEnd)
    {
        OcclusionQuery query(device);
        query.Begin();
        // Refresh get_PixelCount IL_0000-0012 first calls IsComplete; its false result
        // throws, but get_IsComplete IL_0000-0002 has already recorded the observation.
        ExpectExactFailure<System::InvalidOperationException>(
            [&] { (void) query.getPixelCountProperty(); }, unavailable);
        query.End();
        ASSERT_NO_THROW(query.Begin());
        EXPECT_FALSE(query.getIsCompleteProperty());
        ExpectExactFailure<System::InvalidOperationException>(
            [&] { (void) query.getPixelCountProperty(); }, unavailable);
        query.End();
        // Successful reuse must still leave a real pair for this End to close.
        ExpectExactFailure<System::InvalidOperationException>([&] { query.End(); }, pairMissing);
    }

    TEST_F(OcclusionQueryPixelCountPrecisionTest, DeviceDisposalReleasesActiveAndUnstartedQueries)
    {
        OcclusionQuery active(device), unstarted(device);
        active.Begin();
        // Refresh device resource manager releases tracked queries; query
        // ReleaseNativeObject IL_0024-0032 clears its native pointer, retaining pair state.
        device.Dispose();
        for (OcclusionQuery* query : {&active, &unstarted})
        {
            // The managed IsDisposed policy differs in CNA's shared teardown; see
            // the report-only regression. This test pins native release and pair state.
            EXPECT_FALSE(query->HasRenderer());
            EXPECT_FALSE(query->getIsCompleteProperty());
            ExpectExactFailure<System::InvalidOperationException>(
                [&] { (void) query->getPixelCountProperty(); }, unavailable);
            EXPECT_NO_THROW(query->Dispose());
        }
        ExpectExactFailure<System::InvalidOperationException>([&] { active.Begin(); }, pairOpen);
        ExpectExactFailure<System::NullReferenceException>([&] { active.End(); }, nullReference);
        ExpectExactFailure<System::InvalidOperationException>([&] { unstarted.End(); }, pairMissing);
        EXPECT_NO_THROW(device.Dispose());
    }

    TEST_F(OcclusionQueryPixelCountPrecisionTest, PixelCountMatchesWhatTheQuerySaysItIs)
    {
        if (CNA::Testing::ActiveRendererIs(CNA::GraphicsRendererType::Headless))
            GTEST_SKIP() << "HEADLESS validates query lifetime but has no raster samples to count";
        // Literal 64x64 viewport, one sample per pixel: a full-NDC quad covers
        // 64 * 64 = 4096 samples. Pixel centers and area are independent of window DPI.
        device.setViewportProperty(Microsoft::Xna::Framework::Graphics::Viewport(0, 0, 64, 64));
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
            EXPECT_EQ(pixelCount, 4096); // 64 * 64, no MSAA.
        }
        else
        {
            // GL_ANY_SAMPLES_PASSED: 1 for "any", however many pixels were actually covered.
            EXPECT_LE(pixelCount, 1)
                << "the query claims to be boolean-only but answered with a tally";
            EXPECT_EQ(pixelCount, 1); // GL_ANY_SAMPLES_PASSED for this nonempty quad.
        }
    }
}
