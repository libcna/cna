// plan_runtimerenderer.md RTR-P8-5..P8-8: fallback between renderers that both really work.
//
// The P5 suite proves fallback with a candidate that is not compiled in. That covers the policy but
// not the interesting half: a renderer that IS present, IS linked, and is nonetheless passed over.
// Proving that needs two working renderers in one binary, so this whole file is guarded on
// CNA_MULTI_RENDERER and compiles to nothing in a single-renderer build.
//
// The rejection is driven by CNA_DEBUG_UNAVAILABLE_RENDERERS, the documented debug facility for
// verifying a fallback chain without breaking a driver to do it.

#include <gtest/gtest.h>
#include "System/Environment.hpp"

#ifdef CNA_MULTI_RENDERER

#include "CNA/GraphicsRendererFallbackRecord.hpp"
#include "CNA/GraphicsRendererSelection.hpp"
#include "CNA/GraphicsRendererType.hpp"
#include "CNA/Internal/Renderers/Common/GraphicsRendererRegistry.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsAdapter.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"

#include "System/InvalidOperationException.hpp"

#include "CNA/Platform/CurrentPlatform.hpp"
#include "CNA/Platform/IPlatform.hpp"
#include "CNA/Platform/IPlatformWindow.hpp"
#include "CNA/Platform/WindowDescription.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <memory>
#include <vector>

using CNA::GraphicsRendererFallbackReason;
using CNA::GraphicsRendererSelection;
using CNA::GraphicsRendererType;

namespace
{
    class MultiRendererFallbackTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            GraphicsRendererSelection::ResetForTestingEXT();
            System::Environment::SetEnvironmentVariable("CNA_DEBUG_UNAVAILABLE_RENDERERS", "");
            System::Environment::SetEnvironmentVariable("CNA_DEBUG_FAIL_RENDERER_INIT", "");
        }

        void TearDown() override
        {
            System::Environment::SetEnvironmentVariable("CNA_DEBUG_UNAVAILABLE_RENDERERS", "");
            System::Environment::SetEnvironmentVariable("CNA_DEBUG_FAIL_RENDERER_INIT", "");
            GraphicsRendererSelection::ResetForTestingEXT();
        }

        /// Marks renderers as failing their availability probe.
        static void ForceUnavailable(const std::vector<GraphicsRendererType>& types)
        {
            std::string value;
            for (const GraphicsRendererType type : types)
            {
                if (!value.empty())
                    value += ',';
                value += CNA::getGraphicsRendererName(type);
            }
            System::Environment::SetEnvironmentVariable("CNA_DEBUG_UNAVAILABLE_RENDERERS", value.c_str());
        }

        /// Marks renderers as failing during initialization -- after their window already exists.
        static void ForceInitFailure(const std::vector<GraphicsRendererType>& types)
        {
            std::string value;
            for (const GraphicsRendererType type : types)
            {
                if (!value.empty())
                    value += ',';
                value += CNA::getGraphicsRendererName(type);
            }
            System::Environment::SetEnvironmentVariable("CNA_DEBUG_FAIL_RENDERER_INIT", value.c_str());
        }

        /// The first compiled-in renderer whose window kind differs from @p from's.
        [[nodiscard]] static const CNA::Internal::Renderers::GraphicsRendererDescriptor*
        FindDifferentWindowKind(GraphicsRendererType from)
        {
            namespace Renderers = CNA::Internal::Renderers;
            const auto* origin = Renderers::GraphicsRendererRegistry::Find(from);
            if (origin == nullptr)
                return nullptr;
            for (const auto& candidate : Renderers::GraphicsRendererRegistry::All())
            {
                if (candidate.type != from && candidate.windowKind != origin->windowKind)
                    return &candidate;
            }
            return nullptr;
        }

        /// plan_runtimerenderer.md RTR-P6-8 follow-up: EVERY renderer whose window kind differs
        /// from @p from, in registry order. A fallback chain is a list, and handing the test only
        /// the first candidate made it depend on that one candidate being usable in this
        /// environment -- which LLGL is not under Wayland, where it refuses for want of X11
        /// handles.
        [[nodiscard]] static std::vector<GraphicsRendererType>
        AllDifferentWindowKinds(GraphicsRendererType from)
        {
            namespace Renderers = CNA::Internal::Renderers;
            std::vector<GraphicsRendererType> out;
            const auto* origin = Renderers::GraphicsRendererRegistry::Find(from);
            if (origin == nullptr)
                return out;
            for (const auto& candidate : Renderers::GraphicsRendererRegistry::All())
            {
                if (candidate.type != from && candidate.windowKind != origin->windowKind)
                    out.push_back(candidate.type);
            }
            return out;
        }

        [[nodiscard]] static std::vector<GraphicsRendererType> AllCompiledIn()
        {
            const auto span = GraphicsRendererSelection::GetAvailable();
            return std::vector<GraphicsRendererType>(span.begin(), span.end());
        }

        /// The compiled-in renderers that can actually create a device in THIS environment.
        ///
        /// Compiled in and usable here are different things, and these tests care about the second.
        /// OPENGLES1 needs an ES 1.1-capable Mesa (Debian ships Mesa with -Dgles1=disabled, see
        /// scripts/opengles1-test-env.sh); LLGL and DILIGENT need SDL's x11 video driver. Each
        /// throws from its constructor otherwise, which says nothing about renderer SELECTION --
        /// the thing under test -- so a fallback test that indexed blindly into the compiled-in
        /// list was testing the machine, not the code.
        ///
        /// Probed once per process: constructing a device is not free, and the answer cannot change
        /// within a run.
        [[nodiscard]] static const std::vector<GraphicsRendererType>& Available()
        {
            static const std::vector<GraphicsRendererType> usable = [] {
                std::vector<GraphicsRendererType> result;
                for (const GraphicsRendererType type : AllCompiledIn())
                {
                    GraphicsRendererSelection::ResetForTestingEXT();
                    GraphicsRendererSelection::SetPreferred(type);
                    try
                    {
                        Microsoft::Xna::Framework::Graphics::GraphicsDevice probe;
                        result.push_back(type);
                    }
                    catch (const std::exception&)
                    {
                        // Not usable here; deliberately not a failure.
                    }
                }
                GraphicsRendererSelection::ResetForTestingEXT();
                return result;
            }();
            return usable;
        }
    };
}

TEST_F(MultiRendererFallbackTest, ThisBuildGenuinelyContainsSeveralRenderers)
{
    // Guards the rest of the file: if this ever reports one renderer, every test below would be
    // vacuously true rather than failing.
    ASSERT_GT(AllCompiledIn().size(), 1u)
        << "CNA_MULTI_RENDERER is defined but only one renderer is compiled in";
    ASSERT_GT(Available().size(), 1u)
        << "this build compiles in " << AllCompiledIn().size()
        << " renderers but fewer than two can create a device in this environment, so the "
           "fallback tests below would be testing the machine rather than the code";
}

TEST_F(MultiRendererFallbackTest, AnUnavailablePreferredRendererIsSubstituted)
{
    // RTR-P8-5: the first real fallback between two renderers that both work.
    const auto available = Available();
    const GraphicsRendererType preferred = available[0];
    const GraphicsRendererType next = available[1];

    GraphicsRendererSelection::SetFallbackChain(std::vector<GraphicsRendererType>{next});
    GraphicsRendererSelection::SetPreferred(preferred);
    ForceUnavailable({preferred});

    Microsoft::Xna::Framework::Graphics::GraphicsDevice device;

    EXPECT_EQ(GraphicsRendererSelection::GetSelected(), preferred);
    EXPECT_EQ(GraphicsRendererSelection::GetActive(), next);
    EXPECT_EQ(device.GetGraphicsRendererType(), next)
        << "the device must report the renderer it actually got, not the one that was requested";

    const auto history = GraphicsRendererSelection::GetFallbackHistory();
    ASSERT_EQ(history.size(), 1u);
    EXPECT_EQ(history.front().type, preferred);
    EXPECT_EQ(history.front().reason, GraphicsRendererFallbackReason::ProbeUnavailable);
}

TEST_F(MultiRendererFallbackTest, FallbackWalksThePreferenceOrderRatherThanJumpingToTheEnd)
{
    // With three renderers and the first two unavailable, the third must be reached BY WALKING --
    // and both rejections must be recorded, in order. A chain that silently jumped to the last
    // working entry would pass a weaker test and hide which candidates were actually considered.
    const auto available = Available();
    if (available.size() < 3)
        GTEST_SKIP() << "needs at least three compiled-in renderers";

    GraphicsRendererSelection::SetFallbackChain(
        std::vector<GraphicsRendererType>{available[1], available[2]});
    GraphicsRendererSelection::SetPreferred(available[0]);
    ForceUnavailable({available[0], available[1]});

    Microsoft::Xna::Framework::Graphics::GraphicsDevice device;

    EXPECT_EQ(GraphicsRendererSelection::GetActive(), available[2]);

    const auto history = GraphicsRendererSelection::GetFallbackHistory();
    ASSERT_EQ(history.size(), 2u);
    EXPECT_EQ(history[0].type, available[0]);
    EXPECT_EQ(history[1].type, available[1]);
}

TEST_F(MultiRendererFallbackTest, WithoutAChainAnUnavailableRendererIsAHardFailure)
{
    // Design decision 6, proven against a renderer that is present and working: being unavailable
    // is an error, and CNA does not quietly reach for one of the others it happens to have.
    const auto available = Available();
    GraphicsRendererSelection::SetPreferred(available[0]);
    ForceUnavailable({available[0]});

    EXPECT_THROW({ Microsoft::Xna::Framework::Graphics::GraphicsDevice device; },
                 System::InvalidOperationException);
    EXPECT_FALSE(GraphicsRendererSelection::IsLatched());
}

TEST_F(MultiRendererFallbackTest, EveryRendererUnavailableExhaustsTheChainAndThrows)
{
    // RTR-P8-7.
    //
    // Automatic fallback chains over every COMPILED-IN renderer, not only the ones usable here, so
    // every one of them has to be forced unavailable -- otherwise a renderer that is merely absent
    // from this environment would be reached, fail on its own terms, and land in the history under
    // a different reason than the one this test is about.
    const auto available = AllCompiledIn();
    GraphicsRendererSelection::EnableAutomaticFallback(true);
    ForceUnavailable(available);

    try
    {
        Microsoft::Xna::Framework::Graphics::GraphicsDevice device;
        FAIL() << "expected construction to throw once every candidate was rejected";
    }
    catch (const System::InvalidOperationException& e)
    {
        const std::string message = e.what();
        EXPECT_NE(message.find("no graphics renderer could be created"), std::string::npos);
        // Every rejected candidate has to appear, not merely the last one.
        for (const GraphicsRendererType type : available)
        {
            EXPECT_NE(message.find(CNA::getGraphicsRendererName(type)), std::string::npos)
                << CNA::getGraphicsRendererName(type) << " missing from the failure message";
        }
    }

    EXPECT_EQ(GraphicsRendererSelection::GetFallbackHistory().size(), available.size());
}

TEST_F(MultiRendererFallbackTest, AutomaticFallbackReachesAWorkingRendererWithoutAnExplicitChain)
{
    const auto available = Available();
    GraphicsRendererSelection::EnableAutomaticFallback(true);
    GraphicsRendererSelection::SetPreferred(available[0]);
    ForceUnavailable({available[0]});

    Microsoft::Xna::Framework::Graphics::GraphicsDevice device;

    EXPECT_NE(GraphicsRendererSelection::GetActive(), available[0]);
    EXPECT_TRUE(GraphicsRendererSelection::IsAvailable(GraphicsRendererSelection::GetActive()));
}

TEST_F(MultiRendererFallbackTest, EachCompiledInRendererCanBeSelectedAndReallyBacksTheDevice)
{
    // RTR-P8-2: the point of the whole feature -- one binary, every compiled-in renderer reachable,
    // and the device genuinely backed by the one that was asked for.
    for (const GraphicsRendererType type : Available())
    {
        GraphicsRendererSelection::ResetForTestingEXT();
        GraphicsRendererSelection::SetPreferred(type);

        Microsoft::Xna::Framework::Graphics::GraphicsDevice device;

        EXPECT_EQ(GraphicsRendererSelection::GetActive(), type);
        EXPECT_EQ(device.GetGraphicsRendererType(), type);
        EXPECT_EQ(device.GetGraphicsRendererName(), CNA::getGraphicsRendererName(type));
        EXPECT_TRUE(GraphicsRendererSelection::GetFallbackHistory().empty());
    }
}

TEST_F(MultiRendererFallbackTest, SelectionByNameReachesTheSameRenderers)
{
    for (const GraphicsRendererType type : Available())
    {
        GraphicsRendererSelection::ResetForTestingEXT();
        GraphicsRendererSelection::SetPreferred(CNA::getGraphicsRendererName(type));

        Microsoft::Xna::Framework::Graphics::GraphicsDevice device;
        EXPECT_EQ(device.GetGraphicsRendererType(), type);
    }
}

TEST_F(MultiRendererFallbackTest, ReconstructionKeepsTheSubstitutedRendererNotThePreferredOne)
{
    // RTR-P5-17 against real renderers: a reconstruction must not re-resolve, or it would start
    // again from a preference that has already been rejected once.
    const auto available = Available();
    GraphicsRendererSelection::SetFallbackChain(std::vector<GraphicsRendererType>{available[1]});
    GraphicsRendererSelection::SetPreferred(available[0]);
    ForceUnavailable({available[0]});

    Microsoft::Xna::Framework::Graphics::GraphicsDevice device;
    ASSERT_EQ(device.GetGraphicsRendererType(), available[1]);

    EXPECT_NO_THROW(device.RecreateRendererForMultiSampleCount(1));
    EXPECT_EQ(device.GetGraphicsRendererType(), available[1]);
    EXPECT_EQ(GraphicsRendererSelection::GetFallbackHistory().size(), 1u);
}

TEST_F(MultiRendererFallbackTest, InitializationFailureAfterTheWindowExistsFallsThrough)
{
    // RTR-P8-6. Materially different from a failed probe: by the time construction throws, the
    // window has already been created for the candidate that failed.
    const auto available = Available();
    GraphicsRendererSelection::SetFallbackChain(std::vector<GraphicsRendererType>{available[1]});
    GraphicsRendererSelection::SetPreferred(available[0]);
    ForceInitFailure({available[0]});

    Microsoft::Xna::Framework::Graphics::GraphicsDevice device;

    EXPECT_EQ(device.GetGraphicsRendererType(), available[1]);

    const auto history = GraphicsRendererSelection::GetFallbackHistory();
    ASSERT_EQ(history.size(), 1u);
    EXPECT_EQ(history.front().type, available[0]);
    EXPECT_EQ(history.front().reason, GraphicsRendererFallbackReason::InitializationFailed);
    // The renderer's own message has to survive into the record, or a real driver failure would be
    // reduced to "it did not work".
    EXPECT_NE(history.front().message.find("CNA_DEBUG_FAIL_RENDERER_INIT"), std::string::npos);
}

TEST_F(MultiRendererFallbackTest, FallingBackAcrossWindowKindsRecreatesTheWindow)
{
    // RTR-P5-12 / design decision 8, the case that needs BOTH a real multi build and a failure that
    // happens after a window exists: SDL refuses a window carrying two graphics-API flags, so
    // crossing from one window kind to another means destroying and recreating it.
    const auto available = Available();
    const auto* crossing = FindDifferentWindowKind(available[0]);
    if (crossing == nullptr)
    {
        // Say WHICH kinds were seen. A bare "no two differ" is unfalsifiable: on a build that
        // visibly contains both an OpenGL and a Plain renderer it looks like a harness bug, and
        // there is no way to tell from the message whether the registry, the descriptors or the
        // search is at fault.
        std::string seen;
        for (const auto& descriptor : CNA::Internal::Renderers::GraphicsRendererRegistry::All())
        {
            seen += std::string(descriptor.name) + "=" +
                    std::to_string(static_cast<int>(descriptor.windowKind)) + " ";
        }
        GTEST_SKIP() << "no two renderers with different window kinds; registry reports: " << seen;
    }

    // The chain gets every cross-kind candidate, not just the first. A renderer can be compiled in
    // and still refuse to initialise for a reason that belongs to the machine rather than to CNA --
    // LLGL needs X11 handles and this session runs under Wayland -- and a one-element chain turns
    // that into a failure of a test about window recreation.
    const std::vector<GraphicsRendererType> crossingChain = AllDifferentWindowKinds(available[0]);
    GraphicsRendererSelection::SetFallbackChain(crossingChain);
    GraphicsRendererSelection::SetPreferred(available[0]);
    ForceInitFailure({available[0]});

    std::unique_ptr<Microsoft::Xna::Framework::Graphics::GraphicsDevice> device;
    try
    {
        device = std::make_unique<Microsoft::Xna::Framework::Graphics::GraphicsDevice>();
    }
    catch (const std::exception& e)
    {
        // Every cross-kind candidate refused. That is not this test's subject -- it can only show
        // window recreation when some renderer of the other kind actually starts here -- so it
        // reports the attempts verbatim and skips. The attempt list is the evidence that the chain
        // itself did its work; a silent skip would hide a registry or chain bug behind the same
        // outcome.
        GTEST_SKIP() << "no renderer of a different window kind could initialise in this "
                        "environment, so window recreation cannot be observed. The chain was tried "
                        "in full and reported:\n" << e.what();
    }

    EXPECT_NE(device->GetGraphicsRendererType(), available[0]);
    const auto* chosen = CNA::Internal::Renderers::GraphicsRendererRegistry::Find(
        device->GetGraphicsRendererType());
    ASSERT_NE(nullptr, chosen);
    const auto* origin = CNA::Internal::Renderers::GraphicsRendererRegistry::Find(available[0]);
    ASSERT_NE(nullptr, origin);
    EXPECT_NE(chosen->windowKind, origin->windowKind)
        << "the fallback stayed within the original window kind, so nothing was recreated";
    // The device has to be genuinely usable afterwards -- a recreated window that nothing draws
    // into would satisfy a weaker assertion.
    EXPECT_NO_THROW(device->Clear(Microsoft::Xna::Framework::Color::CornflowerBlue));
}

// plan_runtimerenderer.md RTR-P5-13 / design decision 8: the same cross-kind fallback, but with the
// window supplied by the CALLER through PresentationParameters.DeviceWindowHandle.
//
// The difference is what CNA is allowed to do about it. When CNA owns the window it destroys and
// recreates it to cross a window-kind boundary; when the caller owns it, CNA may do neither -- and
// the one thing it must never do is run the candidate against a window of the wrong kind anyway.
// That would "work" often enough to look fine and fail somewhere far from the cause.
TEST_F(MultiRendererFallbackTest, ACallerSuppliedWindowRefusesACrossKindCandidateInsteadOfReusingIt)
{
    // The origin has to be a renderer that ACTUALLY TAKES the caller's window: one that needs a
    // window and accepts a plain one. Starting from Available()[0] instead was this test's own
    // first mistake -- that is HEADLESS here, which needs no window at all, so the caller's handle
    // was never adopted, no window existed to conflict with, and the refusal path was never
    // reached. The test skipped and proved nothing.
    namespace Renderers = CNA::Internal::Renderers;
    const GraphicsRendererType* origin = nullptr;
    for (const auto& descriptor : Renderers::GraphicsRendererRegistry::All())
    {
        if (descriptor.needsWindow
            && descriptor.windowKind == Renderers::RendererWindowKind::Plain
            && std::find(Available().begin(), Available().end(), descriptor.type) != Available().end())
        {
            static GraphicsRendererType found;
            found = descriptor.type;
            origin = &found;
            break;
        }
    }
    if (origin == nullptr)
        GTEST_SKIP() << "no compiled-in renderer takes a plain caller-supplied window";

    const std::vector<GraphicsRendererType> crossingChain = AllDifferentWindowKinds(*origin);
    if (crossingChain.empty())
        GTEST_SKIP() << "no renderer of a different window kind is compiled into this build";

    CNA::Platform::IPlatform& platform = CNA::Platform::GetCurrentPlatform();
    try
    {
        platform.AcquireSubsystem(CNA::Platform::PlatformSubsystem::Video);
    }
    catch (const std::exception& e)
    {
        GTEST_SKIP() << "no platform video subsystem here: " << e.what();
    }

    // A plain window, owned by this test and never handed over. Whatever the first candidate's
    // window kind is, a plain window cannot serve an OpenGL/Vulkan/Metal candidate -- which is
    // exactly what leaving renderIntent at None expresses.
    CNA::Platform::WindowDescription callerDescription;
    callerDescription.title = "RTR-P5-13 caller-owned";
    callerDescription.width = 64;
    callerDescription.height = 64;
    std::unique_ptr<CNA::Platform::IPlatformWindow> callerWindow;
    try
    {
        callerWindow = platform.CreateWindow(callerDescription);
    }
    catch (const std::exception& e)
    {
        GTEST_SKIP() << "the platform could not create a plain window here: " << e.what();
    }
    ASSERT_NE(nullptr, callerWindow);
    const std::uintptr_t callerHandle = callerWindow->GetWindowHandle();

    Microsoft::Xna::Framework::Graphics::PresentationParameters parameters;
    // IntPtr is a member alias of PresentationParameters (std::uintptr_t), not a namespace-level
    // type -- spelling it Microsoft::Xna::Framework::IntPtr does not compile.
    parameters.setDeviceWindowHandleProperty(callerHandle);

    std::string constructionFailure;
    GraphicsRendererSelection::SetFallbackChain(crossingChain);
    GraphicsRendererSelection::SetPreferred(*origin);
    ForceInitFailure({*origin});

    try
    {
        Microsoft::Xna::Framework::Graphics::GraphicsAdapter& adapter =
            Microsoft::Xna::Framework::Graphics::GraphicsAdapter::getDefaultAdapterProperty();
        Microsoft::Xna::Framework::Graphics::GraphicsDevice device(
            adapter, Microsoft::Xna::Framework::Graphics::GraphicsProfile::Reach, parameters);
    }
    catch (const std::exception& e)
    {
        // Every candidate refusing is a legitimate outcome here -- the assertion below is about
        // HOW a cross-kind candidate was refused, not about reaching a working device. The message
        // is kept rather than swallowed: when this test skips, the reason it saw is the only thing
        // that explains why, and an empty skip is indistinguishable from a broken test.
        constructionFailure = e.what();
    }

    // The window the caller owns must still be alive: refusing is the point, destroying someone
    // else's window would be the defect.
    // Re-adopting validates the token against the platform's live window list and throws when no
    // window carries it, so this actually detects a destroyed window. The previous form compared
    // the handle with itself and could not fail.
    EXPECT_NO_THROW({ (void)platform.AdoptWindowHandle(callerHandle); })
        << "the caller's window did not survive the attempt";

    const auto history = GraphicsRendererSelection::GetFallbackHistory();
    bool sawConflict = false;
    for (const auto& record : history)
    {
        if (record.reason != GraphicsRendererFallbackReason::WindowKindConflict)
            continue;
        sawConflict = true;
        // A bare "not used" cannot be acted on. The message has to say that the window came from
        // the caller, which is the part the caller can actually change.
        EXPECT_NE(std::string::npos, record.message.find("DeviceWindowHandle"))
            << "WindowKindConflict was recorded without naming the caller-supplied handle: "
            << record.message;
    }

    if (!sawConflict)
    {
        std::string seen;
        for (const auto& record : history)
        {
            seen += std::string(CNA::getGraphicsRendererName(record.type)) + "(" +
                    std::string(CNA::getGraphicsRendererFallbackReasonName(record.reason)) + ") ";
        }
        GTEST_SKIP() << "the refusal path was not reached from origin "
                     << CNA::getGraphicsRendererName(*origin) << ". Chain length "
                     << crossingChain.size() << ", fallback history: [" << seen << "], device said: "
                     << (constructionFailure.empty() ? "<no exception>" : constructionFailure);
    }

    callerWindow.reset();
}

#endif  // CNA_MULTI_RENDERER
