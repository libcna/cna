// SPDX-License-Identifier: MS-PL
//
// plans/plan_runtimerenderer.md RTR-P5-15 and design decisions 6/7: GraphicsDevice's platform
// video-subsystem reference is balanced on EVERY resolution path.
//
// Why this suite exists rather than being folded into the fallback suites: the defect it pins was
// invisible to all of them. Both campaigns that met in the 2026-08-16 merge added an
// AcquireSubsystem(Video) of their own -- one where the window is created, one per fallback
// candidate -- while the single matching Release stayed in Dispose(). Every windowed device
// therefore took two references and returned one, and every failed candidate in a chain took one
// more. Nothing failed: the renderer worked, the window appeared, the tests passed, and the only
// symptom was that the platform's video subsystem never came back down for the rest of the
// process. A leak that changes no observable behaviour needs a test that counts, so this one
// counts -- it asserts the NUMBER of acquisitions, not merely that things still work.
//
// The counting is done by decorating the real platform rather than faking one: what is under test
// is CNA's own bookkeeping against a genuinely reference-counted service, and a fake that answered
// every call successfully would have passed just as happily before the fix as after it.

#include <gtest/gtest.h>

#include "CNA/GraphicsRendererFallbackRecord.hpp"
#include "CNA/GraphicsRendererSelection.hpp"
#include "CNA/GraphicsRendererType.hpp"
#include "CNA/Internal/Renderers/Common/GraphicsRendererRegistry.hpp"
#include "CNA/Platform/CurrentPlatform.hpp"
#include "CNA/Platform/IPlatform.hpp"
#include "CNA/Platform/PlatformFactory.hpp"
#include "CNA/Platform/PlatformTestDecorator.hpp"

#include "System/Environment.hpp"

#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"

#include <exception>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using CNA::GraphicsRendererSelection;
using CNA::GraphicsRendererType;
using CNA::Platform::PlatformSubsystem;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;

namespace
{
    /// A real platform that also counts how often each subsystem is acquired and released.
    class CountingPlatform final : public CNA::Platform::Testing::PlatformTestDecorator
    {
    public:
        explicit CountingPlatform(std::unique_ptr<CNA::Platform::IPlatform> inner)
            : PlatformTestDecorator(std::move(inner))
        {
        }

        void AcquireSubsystem(const PlatformSubsystem subsystem) override
        {
            // Counted only after the inner platform accepted it: an acquisition that threw took no
            // reference, and counting it would make a machine with no display server look like a
            // leak.
            PlatformTestDecorator::AcquireSubsystem(subsystem);
            ++acquired_[subsystem];
        }

        void ReleaseSubsystem(const PlatformSubsystem subsystem) override
        {
            ++released_[subsystem];
            PlatformTestDecorator::ReleaseSubsystem(subsystem);
        }

        [[nodiscard]] int Acquired(const PlatformSubsystem subsystem) const
        {
            const auto it = acquired_.find(subsystem);
            return it == acquired_.end() ? 0 : it->second;
        }

        [[nodiscard]] int Released(const PlatformSubsystem subsystem) const
        {
            const auto it = released_.find(subsystem);
            return it == released_.end() ? 0 : it->second;
        }

        /// References taken and not yet given back. Zero is the only correct answer once every
        /// device this platform served has been disposed.
        [[nodiscard]] int Outstanding(const PlatformSubsystem subsystem) const
        {
            return Acquired(subsystem) - Released(subsystem);
        }

    private:
        std::map<PlatformSubsystem, int> acquired_;
        std::map<PlatformSubsystem, int> released_;
    };

    class GraphicsDeviceSubsystemLifecycleTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            GraphicsRendererSelection::ResetForTestingEXT();
            System::Environment::SetEnvironmentVariable(
                "CNA_DEBUG_UNAVAILABLE_RENDERERS", std::nullopt);
            System::Environment::SetEnvironmentVariable(
                "CNA_DEBUG_FAIL_RENDERER_INIT", std::nullopt);

            namespace Renderers = CNA::Internal::Renderers;
            std::vector<GraphicsRendererType> available;
            for (const auto& descriptor : Renderers::GraphicsRendererRegistry::All())
                available.push_back(descriptor.type);
            CNA::GraphicsRendererSelectionAccessEXT::PublishAvailable(
                available, Renderers::GraphicsRendererRegistry::Default().type);
        }

        void TearDown() override
        {
            System::Environment::SetEnvironmentVariable(
                "CNA_DEBUG_UNAVAILABLE_RENDERERS", std::nullopt);
            System::Environment::SetEnvironmentVariable(
                "CNA_DEBUG_FAIL_RENDERER_INIT", std::nullopt);
            GraphicsRendererSelection::ResetForTestingEXT();
        }

        /// Marks a renderer as failing during initialization, after its window already exists.
        static void ForceInitFailure(GraphicsRendererType type)
        {
            System::Environment::SetEnvironmentVariable(
                "CNA_DEBUG_FAIL_RENDERER_INIT",
                std::string(CNA::getGraphicsRendererName(type)));
        }

        /// The build default -- always present, whatever this configuration compiled in.
        [[nodiscard]] static GraphicsRendererType Built()
        {
            return CNA::Internal::Renderers::GraphicsRendererRegistry::Default().type;
        }

        /// A compiled-in renderer that genuinely wants the video subsystem, if this build has one.
        ///
        /// The reference multi set (HEADLESS;SOFTWARE;STUB) deliberately has none -- that is what
        /// makes it runnable with no display server -- so the acquisition-count assertions state
        /// their own precondition rather than silently proving nothing.
        [[nodiscard]] static std::optional<GraphicsRendererType> WindowedRenderer()
        {
            for (const auto& descriptor : CNA::Internal::Renderers::GraphicsRendererRegistry::All())
            {
                if (descriptor.needsVideoSubsystem)
                    return descriptor.type;
            }
            return std::nullopt;
        }

        /// An identity this build does NOT contain, so a chain can start with a real rejection
        /// even in a single-renderer build.
        [[nodiscard]] static std::optional<GraphicsRendererType> AbsentRenderer()
        {
            for (int ordinal = 0;
                 ordinal <= static_cast<int>(GraphicsRendererType::PixiJs); ++ordinal)
            {
                const auto candidate = static_cast<GraphicsRendererType>(ordinal);
                if (!GraphicsRendererSelection::IsAvailable(candidate))
                    return candidate;
            }
            return std::nullopt;
        }
    };
}

TEST_F(GraphicsDeviceSubsystemLifecycleTest, ADisposedDeviceHasReturnedEveryVideoReferenceItTook)
{
    CountingPlatform platform(CNA::Platform::PlatformFactory::Create());
    const CNA::Platform::Testing::ScopedCurrentPlatform installed(platform);

    {
        GraphicsDevice device;
        device.Dispose();
    }

    EXPECT_EQ(0, platform.Outstanding(PlatformSubsystem::Video));
    EXPECT_EQ(platform.Acquired(PlatformSubsystem::Video),
              platform.Released(PlatformSubsystem::Video));
}

// The direct regression pin. Before the fix this reported 2: resolveRenderer() took one reference
// for the candidate and createOrAttachWindow() took a second for the window it then created.
TEST_F(GraphicsDeviceSubsystemLifecycleTest, AWindowedDeviceTakesExactlyOneVideoReference)
{
    const std::optional<GraphicsRendererType> windowed = WindowedRenderer();
    if (!windowed.has_value())
        GTEST_SKIP() << "no compiled-in renderer needs the video subsystem in this build";

    CountingPlatform platform(CNA::Platform::PlatformFactory::Create());
    const CNA::Platform::Testing::ScopedCurrentPlatform installed(platform);

    GraphicsRendererSelection::SetPreferred(*windowed);

    std::optional<GraphicsDevice> device;
    try
    {
        device.emplace();
    }
    catch (const std::exception& error)
    {
        GTEST_SKIP() << "the " << CNA::getGraphicsRendererName(*windowed)
                     << " renderer cannot create a device in this environment: " << error.what();
    }

    EXPECT_EQ(1, platform.Acquired(PlatformSubsystem::Video))
        << "a windowed device must take exactly one video-subsystem reference; more than one is a "
           "leak, because only one is ever given back";
    EXPECT_EQ(0, platform.Released(PlatformSubsystem::Video));

    device->Dispose();
    EXPECT_EQ(1, platform.Released(PlatformSubsystem::Video));
    EXPECT_EQ(0, platform.Outstanding(PlatformSubsystem::Video));
    device.reset();
}

TEST_F(GraphicsDeviceSubsystemLifecycleTest, AFailedConstructionLeavesNoOutstandingReference)
{
    CountingPlatform platform(CNA::Platform::PlatformFactory::Create());
    const CNA::Platform::Testing::ScopedCurrentPlatform installed(platform);

    // Fallback is off, so this is the documented hard failure of design decision 6 -- and the
    // partially-initialised device still has to give back whatever it took.
    ForceInitFailure(Built());
    EXPECT_THROW({ GraphicsDevice device; }, std::exception);

    EXPECT_EQ(0, platform.Outstanding(PlatformSubsystem::Video));
}

TEST_F(GraphicsDeviceSubsystemLifecycleTest, FallingBackFromOneRendererToAnotherStillBalances)
{
    const std::optional<GraphicsRendererType> absent = AbsentRenderer();
    if (!absent.has_value())
        GTEST_SKIP() << "this build contains every identity, so nothing can be rejected as absent";

    CountingPlatform platform(CNA::Platform::PlatformFactory::Create());
    const CNA::Platform::Testing::ScopedCurrentPlatform installed(platform);

    // The chain goes in FIRST. RTR-P4-4 makes naming a renderer this build does not contain a hard
    // error unless fallback has already been opted into, so the reverse order throws out of
    // SetPreferred() before any device exists -- which is the documented contract, not a defect.
    const GraphicsRendererType chain[] = {Built()};
    GraphicsRendererSelection::SetFallbackChain(chain);
    GraphicsRendererSelection::SetPreferred(*absent);

    {
        GraphicsDevice device;
        ASSERT_EQ(Built(), GraphicsRendererSelection::GetActive());
        ASSERT_FALSE(GraphicsRendererSelection::GetFallbackHistory().empty());
        device.Dispose();
    }

    EXPECT_EQ(0, platform.Outstanding(PlatformSubsystem::Video));
}

// The scenario the audit asked for by name: renderer A fails for real, renderer B succeeds, the
// device is disposed, and the platform is back where it started. A failed candidate that got as
// far as its window is the interesting half -- it is the one that took a reference before failing.
TEST_F(GraphicsDeviceSubsystemLifecycleTest, AnInitialisationFailureFollowedByASuccessBalances)
{
    const auto& all = CNA::Internal::Renderers::GraphicsRendererRegistry::All();
    if (all.size() < 2)
        GTEST_SKIP() << "needs at least two compiled-in renderers";

    CountingPlatform platform(CNA::Platform::PlatformFactory::Create());
    const CNA::Platform::Testing::ScopedCurrentPlatform installed(platform);

    const GraphicsRendererType first = all[0].type;
    std::vector<GraphicsRendererType> chain;
    for (const auto& descriptor : all)
    {
        if (descriptor.type != first)
            chain.push_back(descriptor.type);
    }

    ForceInitFailure(first);
    GraphicsRendererSelection::SetFallbackChain(chain);   // chain first, as above
    GraphicsRendererSelection::SetPreferred(first);

    std::optional<GraphicsDevice> device;
    try
    {
        device.emplace();
    }
    catch (const std::exception& error)
    {
        GTEST_SKIP() << "no candidate after " << CNA::getGraphicsRendererName(first)
                     << " could initialise in this environment: " << error.what();
    }

    EXPECT_NE(first, GraphicsRendererSelection::GetActive());
    device->Dispose();
    device.reset();

    EXPECT_EQ(0, platform.Outstanding(PlatformSubsystem::Video))
        << "a candidate that failed after taking the video subsystem must give it back before the "
           "next candidate is tried";
}

TEST_F(GraphicsDeviceSubsystemLifecycleTest, RepeatedDeviceLifetimesDoNotAccumulateReferences)
{
    CountingPlatform platform(CNA::Platform::PlatformFactory::Create());
    const CNA::Platform::Testing::ScopedCurrentPlatform installed(platform);

    // Three lifetimes rather than one: a leak of one reference per device is invisible in a single
    // construct/dispose pair if the assertion only looks at the final count of a single device.
    for (int iteration = 0; iteration < 3; ++iteration)
    {
        GraphicsDevice device;
        device.Dispose();
        EXPECT_EQ(0, platform.Outstanding(PlatformSubsystem::Video))
            << "after device lifetime " << iteration;
    }
}

// Dispose() is idempotent, and so is the release inside it: a second Dispose() must not hand back
// a reference the device no longer holds, which would drop somebody else's.
TEST_F(GraphicsDeviceSubsystemLifecycleTest, DisposingTwiceReleasesTheSubsystemOnlyOnce)
{
    CountingPlatform platform(CNA::Platform::PlatformFactory::Create());
    const CNA::Platform::Testing::ScopedCurrentPlatform installed(platform);

    GraphicsDevice device;
    device.Dispose();
    const int released = platform.Released(PlatformSubsystem::Video);
    device.Dispose();

    EXPECT_EQ(released, platform.Released(PlatformSubsystem::Video));
    EXPECT_EQ(0, platform.Outstanding(PlatformSubsystem::Video));
}
