// SPDX-License-Identifier: MS-PL
//
// PLAT-77a: Mouse's desktop-space extensions read the platform, not SDL.
//
// The old version injected a fake `ISystemMouseBackend`. The canned answers now arrive through
// `IPlatformMouse`, which is the seam that survives the migration — and the coverage is kept
// intact rather than degraded to "does not crash", because CI has no desktop cursor and these
// values are otherwise unobservable.

#include <gtest/gtest.h>

#include "CNA/Platform/PlatformTestDecorator.hpp"
#include "Microsoft/Xna/Framework/Input/Mouse.hpp"

#include <memory>

using CNA::Platform::Testing::PlatformTestDecorator;
using CNA::Platform::Testing::ScopedCurrentPlatform;
using Microsoft::Xna::Framework::Input::Mouse;

namespace
{
    /// Canned desktop-pointer answers; every window-scoped call forwards to the real service, so
    /// nothing else about the mouse is stubbed out.
    class CannedGlobalMouse final : public CNA::Platform::IPlatformMouse
    {
    public:
        explicit CannedGlobalMouse(CNA::Platform::IPlatformMouse* inner) : inner_(inner) {}

        float globalX = 0.0f;
        float globalY = 0.0f;
        bool globalAvailable = true;

        int captureCalls = 0;
        bool lastCaptureEnabled = false;
        bool captureResult = true;

        int warpCalls = 0;
        float lastWarpX = 0.0f;
        float lastWarpY = 0.0f;
        bool warpResult = true;

        bool SetCapture(const bool enabled) override
        {
            ++captureCalls;
            lastCaptureEnabled = enabled;
            return captureResult;
        }

        [[nodiscard]] bool TryGetGlobalPosition(float& x, float& y) const override
        {
            if (!globalAvailable)
            {
                return false;
            }
            x = globalX;
            y = globalY;
            return true;
        }

        bool SetGlobalPosition(const float x, const float y) override
        {
            ++warpCalls;
            lastWarpX = x;
            lastWarpY = y;
            return warpResult;
        }

        void Update() override { if (inner_ != nullptr) inner_->Update(); }
        [[nodiscard]] const CNA::Platform::MouseSnapshot& GetSnapshot() const override
        {
            return inner_ != nullptr ? inner_->GetSnapshot() : fallback_;
        }
        void SetPosition(CNA::Platform::IPlatformWindow& window, const int x, const int y) override
        {
            if (inner_ != nullptr) inner_->SetPosition(window, x, y);
        }
        void SetCursorVisible(const bool visible) override
        {
            if (inner_ != nullptr) inner_->SetCursorVisible(visible);
        }
        void SetCursor(const CNA::Platform::SystemCursor cursor) override
        {
            if (inner_ != nullptr) inner_->SetCursor(cursor);
        }
        void SetRelativeMode(const bool enabled) override
        {
            if (inner_ != nullptr) inner_->SetRelativeMode(enabled);
        }
        [[nodiscard]] bool IsRelativeMode() const override
        {
            return inner_ != nullptr && inner_->IsRelativeMode();
        }

    private:
        CNA::Platform::IPlatformMouse* inner_;
        CNA::Platform::MouseSnapshot fallback_{};
    };

    class CannedMousePlatform final : public PlatformTestDecorator
    {
    public:
        CannedMousePlatform() : mouse_(PlatformTestDecorator::GetMouse()) {}

        [[nodiscard]] CNA::Platform::IPlatformMouse* GetMouse() override { return &mouse_; }

        CannedGlobalMouse& Canned() { return mouse_; }

    private:
        CannedGlobalMouse mouse_;
    };

    class MouseGlobalEXTTest : public ::testing::Test
    {
    protected:
        CannedMousePlatform platform;
        std::unique_ptr<ScopedCurrentPlatform> installed;

        void SetUp() override { installed = std::make_unique<ScopedCurrentPlatform>(platform); }
        void TearDown() override { installed.reset(); }
    };
}

TEST_F(MouseGlobalEXTTest, SetCaptureForwardsFlagAndReturnsTheServiceResult)
{
    platform.Canned().captureResult = true;
    EXPECT_TRUE(Mouse::SetCaptureEXT(true));
    EXPECT_EQ(platform.Canned().captureCalls, 1);
    EXPECT_TRUE(platform.Canned().lastCaptureEnabled);

    platform.Canned().captureResult = false;
    EXPECT_FALSE(Mouse::SetCaptureEXT(false));
    EXPECT_EQ(platform.Canned().captureCalls, 2);
    EXPECT_FALSE(platform.Canned().lastCaptureEnabled);
}

TEST_F(MouseGlobalEXTTest, GetGlobalPositionReadsTheServiceAndTruncatesTowardZero)
{
    platform.Canned().globalX = 640.9f;
    platform.Canned().globalY = -12.5f;

    int x = 0;
    int y = 0;
    Mouse::GetGlobalPositionEXT(x, y);
    EXPECT_EQ(x, 640);  // truncated toward zero, matching a float->int cast
    EXPECT_EQ(y, -12);
}

TEST_F(MouseGlobalEXTTest, GetGlobalPositionZeroesItsOutputsWhenNoPositionIsAvailable)
{
    // The old SDL call always wrote both outputs. The platform's TryGetGlobalPosition leaves them
    // untouched on false, so this accessor has to supply the zero itself -- otherwise a caller's
    // stale coordinates would silently pass through as a real reading.
    platform.Canned().globalAvailable = false;

    int x = 777;
    int y = 888;
    Mouse::GetGlobalPositionEXT(x, y);
    EXPECT_EQ(x, 0);
    EXPECT_EQ(y, 0);
}

TEST_F(MouseGlobalEXTTest, WarpGlobalForwardsCoordinatesAndReturnsTheServiceResult)
{
    platform.Canned().warpResult = true;
    EXPECT_TRUE(Mouse::WarpGlobalEXT(100, 200));
    EXPECT_EQ(platform.Canned().warpCalls, 1);
    EXPECT_FLOAT_EQ(platform.Canned().lastWarpX, 100.0f);
    EXPECT_FLOAT_EQ(platform.Canned().lastWarpY, 200.0f);

    platform.Canned().warpResult = false;
    EXPECT_FALSE(Mouse::WarpGlobalEXT(0, 0));
}

TEST_F(MouseGlobalEXTTest, TheRealPlatformIsSafeForAllThree)
{
    // Without the canned service. A platform with no pointer at all returns null from GetMouse(),
    // which the previous SDL calls had no branch for.
    installed.reset();

    int x = 0;
    int y = 0;
    EXPECT_NO_THROW((void)Mouse::SetCaptureEXT(false));
    EXPECT_NO_THROW(Mouse::GetGlobalPositionEXT(x, y));
    EXPECT_NO_THROW((void)Mouse::WarpGlobalEXT(0, 0));
}
