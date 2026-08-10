// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>

#include "CNA/Input/Power.hpp"
#include "CNA/Input/PowerState.hpp"
#include "CNA/Internal/Input/SystemPowerBackend.hpp"

#include <SDL3/SDL.h>

using CNA::Input::Power;
using CNA::Input::PowerStateEXT;
using CNA::Internal::Input::ISystemPowerBackend;
using CNA::Internal::Input::SetSystemPowerBackendForTests;

namespace
{
    // A fake system power source. Returns a canned state and fills the out-params with canned values,
    // so the SDL_PowerState -> PowerStateEXT mapping and the seconds/percent forwarding can be checked
    // deterministically (CI has no real battery).
    class FakeSystemPowerBackend final : public ISystemPowerBackend
    {
    public:
        SDL_PowerState state = SDL_POWERSTATE_UNKNOWN;
        int seconds = -1;
        int percent = -1;
        int calls = 0;

        SDL_PowerState GetPowerInfo(int* outSeconds, int* outPercent) override
        {
            ++calls;
            if (outSeconds != nullptr)
                *outSeconds = seconds;
            if (outPercent != nullptr)
                *outPercent = percent;
            return state;
        }
    };

    class CnaInputPowerTest : public ::testing::Test
    {
    protected:
        FakeSystemPowerBackend fake;

        void SetUp() override { SetSystemPowerBackendForTests(&fake); }
        void TearDown() override { SetSystemPowerBackendForTests(nullptr); }
    };
}

// The full SDL_PowerState -> PowerStateEXT mapping, with seconds/percent forwarded from the source.
TEST_F(CnaInputPowerTest, GetInfoMapsStateAndForwardsSecondsAndPercent)
{
    struct Case { SDL_PowerState sdl; PowerStateEXT ext; int seconds; int percent; };
    const Case cases[] = {
        {SDL_POWERSTATE_ON_BATTERY, PowerStateEXT::OnBattery, 3600, 85},
        {SDL_POWERSTATE_NO_BATTERY, PowerStateEXT::NoBattery, -1,   -1},
        {SDL_POWERSTATE_CHARGING,   PowerStateEXT::Charging,  1800, 40},
        {SDL_POWERSTATE_CHARGED,    PowerStateEXT::Charged,   -1,   100},
        {SDL_POWERSTATE_UNKNOWN,    PowerStateEXT::Unknown,   -1,   -1},
        {SDL_POWERSTATE_ERROR,      PowerStateEXT::Error,     -1,   -1},
    };
    for (const Case& c : cases)
    {
        fake.state = c.sdl;
        fake.seconds = c.seconds;
        fake.percent = c.percent;

        int secondsLeft = 999;
        int percent = 999;
        EXPECT_EQ(Power::GetInfoEXT(secondsLeft, percent), c.ext);
        EXPECT_EQ(secondsLeft, c.seconds);
        EXPECT_EQ(percent, c.percent);
    }
}

// The out-params are reset to -1 before the query, so a backend that ignores them leaves "unknown".
TEST_F(CnaInputPowerTest, OutParamsDefaultToUnknownWhenSourceLeavesThemUnset)
{
    class IgnoringBackend final : public ISystemPowerBackend
    {
    public:
        SDL_PowerState GetPowerInfo(int*, int*) override { return SDL_POWERSTATE_UNKNOWN; }
    } ignoring;
    SetSystemPowerBackendForTests(&ignoring);

    int secondsLeft = 123;
    int percent = 456;
    EXPECT_EQ(Power::GetInfoEXT(secondsLeft, percent), PowerStateEXT::Unknown);
    EXPECT_EQ(secondsLeft, -1);
    EXPECT_EQ(percent, -1);

    SetSystemPowerBackendForTests(nullptr);
}
