// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>

#include "CNA/Internal/Input/SystemMouseBackend.hpp"
#include "Microsoft/Xna/Framework/Input/Mouse.hpp"

using CNA::Internal::Input::ISystemMouseBackend;
using CNA::Internal::Input::SetSystemMouseBackendForTests;
using Microsoft::Xna::Framework::Input::Mouse;

namespace
{
    // A fake global-mouse source, so the capture / global-position / warp extensions are exercised
    // deterministically (CI has no real desktop cursor).
    class FakeSystemMouseBackend final : public ISystemMouseBackend
    {
    public:
        // GetGlobalMouseState reports this.
        float globalX = 0.0f;
        float globalY = 0.0f;

        // Introspection.
        int captureCalls = 0;
        bool lastCaptureEnabled = false;
        bool captureResult = true;

        int warpCalls = 0;
        float lastWarpX = 0.0f;
        float lastWarpY = 0.0f;
        bool warpResult = true;

        bool CaptureMouse(bool enabled) override
        {
            ++captureCalls;
            lastCaptureEnabled = enabled;
            return captureResult;
        }
        void GetGlobalMouseState(float* x, float* y) override
        {
            if (x != nullptr) *x = globalX;
            if (y != nullptr) *y = globalY;
        }
        bool WarpMouseGlobal(float x, float y) override
        {
            ++warpCalls;
            lastWarpX = x;
            lastWarpY = y;
            return warpResult;
        }
    };

    class MouseGlobalEXTTest : public ::testing::Test
    {
    protected:
        FakeSystemMouseBackend fake;
        void SetUp() override { SetSystemMouseBackendForTests(&fake); }
        void TearDown() override { SetSystemMouseBackendForTests(nullptr); }
    };
}

TEST_F(MouseGlobalEXTTest, SetCaptureForwardsFlagAndReturnsBackendResult)
{
    fake.captureResult = true;
    EXPECT_TRUE(Mouse::SetCaptureEXT(true));
    EXPECT_EQ(fake.captureCalls, 1);
    EXPECT_TRUE(fake.lastCaptureEnabled);

    fake.captureResult = false;
    EXPECT_FALSE(Mouse::SetCaptureEXT(false));
    EXPECT_EQ(fake.captureCalls, 2);
    EXPECT_FALSE(fake.lastCaptureEnabled);
}

TEST_F(MouseGlobalEXTTest, GetGlobalPositionReadsBackendAndTruncatesToInt)
{
    fake.globalX = 640.9f;
    fake.globalY = -12.5f;

    int x = 0;
    int y = 0;
    Mouse::GetGlobalPositionEXT(x, y);
    EXPECT_EQ(x, 640); // truncated toward zero, matching a float->int cast
    EXPECT_EQ(y, -12);
}

TEST_F(MouseGlobalEXTTest, WarpGlobalForwardsCoordinatesAndReturnsBackendResult)
{
    fake.warpResult = true;
    EXPECT_TRUE(Mouse::WarpGlobalEXT(100, 200));
    EXPECT_EQ(fake.warpCalls, 1);
    EXPECT_FLOAT_EQ(fake.lastWarpX, 100.0f);
    EXPECT_FLOAT_EQ(fake.lastWarpY, 200.0f);

    fake.warpResult = false;
    EXPECT_FALSE(Mouse::WarpGlobalEXT(0, 0));
}
