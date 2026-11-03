// SPDX-License-Identifier: MS-PL
//
// The ambient platform accessor that the static XNA API surface reaches through.

#include "CNA/Platform/CurrentPlatform.hpp"
#include "CNA/Platform/PlatformFactory.hpp"
#include "CNA/Platform/PlatformException.hpp"
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

    ~CountingPlatform() override { trace_.push_back(label_ + ":destroy"); }

    void AcquireSubsystem(const PlatformSubsystem subsystem) override
    {
        if (rejectAcquire)
        {
            trace_.push_back(label_ + ":reject:" + ToString(subsystem));
            throw PlatformException("AcquireSubsystem", "scripted rejection");
        }
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

    bool rejectAcquire = false;

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

    // Oracle: CurrentPlatform.hpp promises a lazy build-time default after clearing.
    EXPECT_FALSE(HasCurrentPlatform());
    EXPECT_NE(&GetCurrentPlatform(), headless.get());
    EXPECT_EQ(GetCurrentPlatform().GetName(), PlatformFactory::GetDefaultName());
    EXPECT_TRUE(HasCurrentPlatform());
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

TEST_F(CurrentPlatformTest, ClearingRestoresTheSamePreviouslyCreatedDefault)
{
    // Oracle: CurrentPlatform.hpp's owned lazy default survives a borrowed installation.
    IPlatform* const originalDefault = &GetCurrentPlatform();
    auto borrowed = PlatformFactory::Create("Headless");
    SetCurrentPlatform(borrowed.get());
    ASSERT_EQ(&GetCurrentPlatform(), borrowed.get());
    SetCurrentPlatform(nullptr);
    EXPECT_EQ(&GetCurrentPlatform(), originalDefault);
}

TEST_F(CurrentPlatformTest, NullPinOwnerIsRejectedWithoutCreatingTheDefault)
{
    // Oracle: Detail::PinCurrentPlatformSubsystem documents invalid_argument for a null owner.
    EXPECT_THROW(Detail::PinCurrentPlatformSubsystem(nullptr, PlatformSubsystem::Video),
                 std::invalid_argument);
    EXPECT_FALSE(HasCurrentPlatform());
    EXPECT_NO_THROW(Detail::UnpinCurrentPlatformSubsystem(nullptr));
    EXPECT_FALSE(HasCurrentPlatform());
}

TEST_F(CurrentPlatformTest, FailedPinCanBeRetriedAndUnknownUnpinDoesNotReleaseAnotherOwner)
{
    // Oracle: a pin retains one successful acquisition per owner, never a failed acquisition.
    std::vector<std::string> trace;
    CountingPlatform platform("platform", trace);
    Testing::ScopedCurrentPlatform installed(platform);
    const int owner = 0;
    const int stranger = 0;
    platform.rejectAcquire = true;
    EXPECT_THROW(Detail::PinCurrentPlatformSubsystem(&owner, PlatformSubsystem::Sensor),
                 PlatformException);
    EXPECT_EQ(platform.Outstanding(), 0);
    platform.rejectAcquire = false;
    Detail::PinCurrentPlatformSubsystem(&owner, PlatformSubsystem::Sensor);
    Detail::UnpinCurrentPlatformSubsystem(&stranger);
    EXPECT_EQ(platform.Outstanding(), 1);
    Detail::UnpinCurrentPlatformSubsystem(&owner);
    Detail::UnpinCurrentPlatformSubsystem(&owner);
    EXPECT_EQ(platform.Acquired(), 1);
    EXPECT_EQ(platform.Released(), 1);
    EXPECT_EQ(platform.Outstanding(), 0);
}

TEST_F(CurrentPlatformTest, FailedTransferDropsOldPinAndAllowsRetryOnReplacement)
{
    // Oracle: CurrentPlatform's install is not an acquisition request; failed transfers release
    // the old pin and the owner retries on next use (CurrentPlatform.cpp, TransferPins policy).
    std::vector<std::string> trace;
    CountingPlatform first("first", trace);
    CountingPlatform second("second", trace);
    Testing::ScopedCurrentPlatform installed(first);
    const int owner = 0;
    Detail::PinCurrentPlatformSubsystem(&owner, PlatformSubsystem::Video);
    second.rejectAcquire = true;
    EXPECT_NO_THROW(SetCurrentPlatform(&second));
    EXPECT_EQ(&GetCurrentPlatform(), &second);
    EXPECT_EQ(first.Outstanding(), 0);
    EXPECT_EQ(first.Released(), 1);
    EXPECT_EQ(second.Outstanding(), 0);
    EXPECT_EQ(trace, (std::vector<std::string>{"first:acquire:Video", "second:reject:Video",
                                             "first:release:Video"}));
    second.rejectAcquire = false;
    Detail::PinCurrentPlatformSubsystem(&owner, PlatformSubsystem::Video);
    EXPECT_EQ(second.Outstanding(), 1);
    Detail::UnpinCurrentPlatformSubsystem(&owner);
    EXPECT_EQ(second.Released(), 1);
}

TEST_F(CurrentPlatformTest, ChangingAnOwnersSubsystemAcquiresBeforeReleasingAndPreservesOtherOwners)
{
    // Oracle: CurrentPlatform.hpp's acquire-before-release and per-owner pin semantics.
    std::vector<std::string> trace;
    CountingPlatform platform("platform", trace);
    Testing::ScopedCurrentPlatform installed(platform);
    const int firstOwner = 0;
    const int secondOwner = 0;
    Detail::PinCurrentPlatformSubsystem(&firstOwner, PlatformSubsystem::Audio);
    Detail::PinCurrentPlatformSubsystem(&secondOwner, PlatformSubsystem::Audio);
    Detail::PinCurrentPlatformSubsystem(&firstOwner, PlatformSubsystem::Video);
    EXPECT_EQ(trace, (std::vector<std::string>{"platform:acquire:Audio", "platform:acquire:Audio",
                                             "platform:acquire:Video", "platform:release:Audio"}));
    EXPECT_EQ(platform.Outstanding(), 2);
    SetCurrentPlatform(&platform);
    EXPECT_EQ(platform.Acquired(), 3) << "reinstalling the same platform must not transfer pins";
    Detail::UnpinCurrentPlatformSubsystem(&firstOwner);
    EXPECT_EQ(platform.Outstanding(), 1);
    Detail::UnpinCurrentPlatformSubsystem(&secondOwner);
    EXPECT_EQ(platform.Outstanding(), 0);
}

TEST_F(CurrentPlatformTest, FailedSubsystemChangeKeepsTheOriginalPinUntilSuccessfulRetry)
{
    // Oracle: acquire-before-release means an unsuccessful replacement cannot drop a live pin.
    std::vector<std::string> trace;
    CountingPlatform platform("platform", trace);
    Testing::ScopedCurrentPlatform installed(platform);
    const int owner = 0;
    Detail::PinCurrentPlatformSubsystem(&owner, PlatformSubsystem::Video);
    platform.rejectAcquire = true;
    EXPECT_THROW(Detail::PinCurrentPlatformSubsystem(&owner, PlatformSubsystem::Audio),
                 PlatformException);
    EXPECT_EQ(platform.Outstanding(), 1);
    EXPECT_EQ(platform.Released(), 0);
    platform.rejectAcquire = false;
    Detail::PinCurrentPlatformSubsystem(&owner, PlatformSubsystem::Audio);
    EXPECT_EQ(platform.Acquired(), 2);
    EXPECT_EQ(platform.Released(), 1);
    EXPECT_EQ(platform.Outstanding(), 1);
    Detail::UnpinCurrentPlatformSubsystem(&owner);
    EXPECT_EQ(platform.Outstanding(), 0);
}

TEST_F(CurrentPlatformTest, ResetNeverDestroysTheBorrowedInstallation)
{
    // Oracle: CurrentPlatform.hpp: SetCurrentPlatform borrows; reset destroys only its lazy default.
    std::vector<std::string> trace;
    auto borrowed = std::make_unique<CountingPlatform>("borrowed", trace);
    SetCurrentPlatform(borrowed.get());
    ResetCurrentPlatform();
    EXPECT_FALSE(HasCurrentPlatform());
    EXPECT_TRUE(trace.empty());
    EXPECT_EQ(borrowed->GetName(), "Headless");
    borrowed.reset();
    EXPECT_EQ(trace, (std::vector<std::string>{"borrowed:destroy"}));
}

} // namespace
