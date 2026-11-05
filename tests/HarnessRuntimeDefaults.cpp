// SPDX-License-Identifier: MS-PL
// TQ-085: direct focused and aggregate runs must inherit the same tree defaults.
// CTest's per-test ENVIRONMENT cannot configure a binary started by hand.
#include <gtest/gtest.h>
#include "System/Environment.hpp"

#include <optional>
#include <string>

namespace
{
    using System::Environment;

    void DefaultIfMissing(const char* name, const char* value)
    {
        if (!Environment::GetEnvironmentVariable(name).has_value())
            Environment::SetEnvironmentVariable(name, std::string(value));
    }

    struct InitialDefaults
    {
        std::optional<std::string> driverBefore;
        std::optional<std::string> driverAfter;
        std::optional<std::string> audioBefore;
        std::optional<std::string> audioAfter;
        std::optional<std::string> recordingBefore;
        std::optional<std::string> recordingAfter;

        InitialDefaults()
        {
#ifdef CNA_TEST_FNA3D_DEFAULT_DRIVER
            driverBefore = Environment::GetEnvironmentVariable("FNA3D_FORCE_DRIVER");
            DefaultIfMissing("FNA3D_FORCE_DRIVER", CNA_TEST_FNA3D_DEFAULT_DRIVER);
            driverAfter = Environment::GetEnvironmentVariable("FNA3D_FORCE_DRIVER");
#endif
#ifdef CNA_TEST_ALSA_DEFAULTS
            audioBefore = Environment::GetEnvironmentVariable("CNA_AUDIO_DEVICE");
            recordingBefore = Environment::GetEnvironmentVariable("CNA_AUDIO_RECORDING_DEVICE");
            DefaultIfMissing("CNA_AUDIO_DEVICE", "null");
            DefaultIfMissing("CNA_AUDIO_RECORDING_DEVICE", "null");
            audioAfter = Environment::GetEnvironmentVariable("CNA_AUDIO_DEVICE");
            recordingAfter = Environment::GetEnvironmentVariable("CNA_AUDIO_RECORDING_DEVICE");
#endif
        }
    };

    // Observe startup rather than another test's temporarily changed environment.
    const InitialDefaults initial;

    class HarnessRuntimeDefaults : public ::testing::Test
    {
    protected:
        static constexpr const char* probe = "CNA_TEST_DEFAULT_POLICY_PROBE";
        std::optional<std::string> previous;
        void SetUp() override { previous = Environment::GetEnvironmentVariable(probe); }
        void TearDown() override { Environment::SetEnvironmentVariable(probe, previous); }
    };

    TEST_F(HarnessRuntimeDefaults, MissingVariableReceivesTheDefault)
    {
        // Oracle: the harness supplies a default only when the caller omitted the variable.
        Environment::SetEnvironmentVariable(probe, std::nullopt);
        DefaultIfMissing(probe, "configured");
        ASSERT_TRUE(Environment::GetEnvironmentVariable(probe).has_value());
        EXPECT_EQ(*Environment::GetEnvironmentVariable(probe), "configured");
    }

    TEST_F(HarnessRuntimeDefaults, ExplicitVariableWins)
    {
        // Oracle: an explicit driver/device selection must survive a direct invocation.
        Environment::SetEnvironmentVariable(probe, std::string("explicit"));
        DefaultIfMissing(probe, "configured");
        EXPECT_EQ(Environment::GetEnvironmentVariable(probe), std::optional<std::string>("explicit"));
    }

    TEST_F(HarnessRuntimeDefaults, ExplicitEmptyVariableWins)
    {
        Environment::SetEnvironmentVariable(probe, std::string());
        DefaultIfMissing(probe, "configured");
        EXPECT_EQ(Environment::GetEnvironmentVariable(probe), std::optional<std::string>(""));
    }

    TEST_F(HarnessRuntimeDefaults, ProcessStartsWithConfiguredDefaults)
    {
        // Oracle: UnitTests.cmake's CTest defaults for this tree, with explicit input preserved.
#ifdef CNA_TEST_FNA3D_DEFAULT_DRIVER
        EXPECT_EQ(initial.driverAfter, initial.driverBefore.has_value()
            ? initial.driverBefore : std::optional<std::string>(CNA_TEST_FNA3D_DEFAULT_DRIVER));
#endif
#ifdef CNA_TEST_ALSA_DEFAULTS
        EXPECT_EQ(initial.audioAfter, initial.audioBefore.has_value()
            ? initial.audioBefore : std::optional<std::string>("null"));
        EXPECT_EQ(initial.recordingAfter, initial.recordingBefore.has_value()
            ? initial.recordingBefore : std::optional<std::string>("null"));
#endif
#if !defined(CNA_TEST_FNA3D_DEFAULT_DRIVER) && !defined(CNA_TEST_ALSA_DEFAULTS)
        GTEST_SKIP() << "This tree has no FNA3D/ALSA runtime defaults.";
#endif
    }
}
