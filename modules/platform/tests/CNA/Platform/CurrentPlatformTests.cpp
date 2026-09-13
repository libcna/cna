// SPDX-License-Identifier: MS-PL
//
// The ambient platform accessor that the static XNA API surface reaches through.

#include "CNA/Platform/CurrentPlatform.hpp"
#include "CNA/Platform/PlatformFactory.hpp"
#include "CNA/Platform/PlatformTestDecorator.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace CNA::Platform;

class CountingPlatform final : public Testing::PlatformTestDecorator
{
public:
    CountingPlatform(std::string label, std::vector<std::string>& trace)
        : PlatformTestDecorator(PlatformFactory::Create("Headless")),
          label_(std::move(label)), trace_(trace)
    {
    }

    void AcquireSubsystem(const PlatformSubsystem subsystem) override
    {
        ++acquired_;
        ++outstanding_;
        trace_.push_back(label_ + ":acquire:" + ToString(subsystem));
    }

    void ReleaseSubsystem(const PlatformSubsystem subsystem) override
    {
        ++released_;
        if (outstanding_ > 0)
            --outstanding_;
        trace_.push_back(label_ + ":release:" + ToString(subsystem));
    }

    [[nodiscard]] bool IsSubsystemInitialized(PlatformSubsystem) const override
    {
        return outstanding_ > 0;
    }

    [[nodiscard]] int Acquired() const { return acquired_; }
    [[nodiscard]] int Released() const { return released_; }
    [[nodiscard]] int Outstanding() const { return outstanding_; }

private:
    std::string label_;
    std::vector<std::string>& trace_;
    int acquired_ = 0;
    int released_ = 0;
    int outstanding_ = 0;
};

class CurrentPlatformTest : public ::testing::Test
{
protected:
    void SetUp() override { ResetCurrentPlatform(); }

    void TearDown() override
    {
        // Process-wide state: leaving an installed platform behind would leak a dangling pointer
        // into every later test in this binary once the local instance is destroyed. Clearing only
        // the borrowed installation is insufficient: GetCurrentPlatform() may also have created
        // the owned lazy default, and HasCurrentPlatform() deliberately reports that instance.
        ResetCurrentPlatform();
    }
};

TEST_F(CurrentPlatformTest, AnInstalledPlatformIsWhatGetReturns)
{
    const std::unique_ptr<IPlatform> headless = PlatformFactory::Create("Headless");
    SetCurrentPlatform(headless.get());

    EXPECT_TRUE(HasCurrentPlatform());
    EXPECT_EQ(&GetCurrentPlatform(), headless.get());
    EXPECT_EQ(GetCurrentPlatform().GetName(), "Headless");
}

TEST_F(CurrentPlatformTest, InstallingOverridesTheLazyDefault)
{
    // The static API must follow whatever Game installed, not a default it created earlier on
    // first use -- otherwise storage and input would silently target different platforms.
    (void)GetCurrentPlatform();  // force the lazy default into existence

    const std::unique_ptr<IPlatform> headless = PlatformFactory::Create("Headless");
    SetCurrentPlatform(headless.get());
    EXPECT_EQ(&GetCurrentPlatform(), headless.get());
}

TEST_F(CurrentPlatformTest, ClearingFallsBackToTheDefaultRatherThanFailing)
{
    const std::unique_ptr<IPlatform> headless = PlatformFactory::Create("Headless");
    SetCurrentPlatform(headless.get());
    SetCurrentPlatform(nullptr);

    // XNA code calls StorageDevice with no ceremony at all, so the accessor has to keep working
    // after an install is withdrawn.
    EXPECT_NO_THROW((void)GetCurrentPlatform());
}

TEST_F(CurrentPlatformTest, GetIsStableAcrossCalls)
{
    // Callers may cache the reference; a different instance per call would split state.
    EXPECT_EQ(&GetCurrentPlatform(), &GetCurrentPlatform());
}

TEST_F(CurrentPlatformTest, HasCurrentDoesNotItselfCreateAPlatform)
{
    // Teardown paths need to distinguish "never created" from "created" without constructing one
    // while the process is shutting down.
    ResetCurrentPlatform();
    const bool before = HasCurrentPlatform();
    EXPECT_FALSE(before);
    EXPECT_FALSE(HasCurrentPlatform()) << "querying must not have created one";
}

TEST_F(CurrentPlatformTest, AmbientSubsystemPinTransfersBeforeReleasingPreviousPlatform)
{
    std::vector<std::string> trace;
    CountingPlatform first("first", trace);
    CountingPlatform second("second", trace);
    const int ownerToken = 0;

    SetCurrentPlatform(&first);
    Detail::PinCurrentPlatformSubsystem(&ownerToken, PlatformSubsystem::Video);
    Detail::PinCurrentPlatformSubsystem(&ownerToken, PlatformSubsystem::Video);

    EXPECT_EQ(first.Acquired(), 1) << "pinning the same owner twice must be idempotent";
    EXPECT_EQ(first.Outstanding(), 1);

    SetCurrentPlatform(&second);

    ASSERT_EQ(trace.size(), 3u);
    EXPECT_EQ(trace[0], "first:acquire:Video");
    EXPECT_EQ(trace[1], "second:acquire:Video");
    EXPECT_EQ(trace[2], "first:release:Video");
    EXPECT_EQ(first.Outstanding(), 0);
    EXPECT_EQ(second.Outstanding(), 1);

    Detail::UnpinCurrentPlatformSubsystem(&ownerToken);
    EXPECT_EQ(second.Acquired(), 1);
    EXPECT_EQ(second.Released(), 1);
    EXPECT_EQ(second.Outstanding(), 0);
}

TEST_F(CurrentPlatformTest, ResetReleasesAmbientSubsystemPinBeforeDestroyingPlatform)
{
    std::vector<std::string> trace;
    CountingPlatform platform("platform", trace);
    const int ownerToken = 0;

    SetCurrentPlatform(&platform);
    Detail::PinCurrentPlatformSubsystem(&ownerToken, PlatformSubsystem::Video);
    ResetCurrentPlatform();

    EXPECT_EQ(platform.Acquired(), 1);
    EXPECT_EQ(platform.Released(), 1);
    EXPECT_EQ(platform.Outstanding(), 0);
}

} // namespace
