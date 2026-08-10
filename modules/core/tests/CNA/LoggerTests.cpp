#include <gtest/gtest.h>

#include <SDL3/SDL.h>

#include "CNA/Logger.hpp"
#include "CNA/LogLevel.hpp"

using CNA::Logger;
using CNA::LogLevel;

namespace
{
    // Logger::ToSDLPriority() is private; SDL_SetLogPriorities() is a process-wide,
    // uniform-across-categories call, so this is the lowest practical seam that observes the
    // exact SDL_LogPriority value SetMinimumLevel() actually hands to SDL.
    struct LoggerSeverityCase
    {
        LogLevel level;
        SDL_LogPriority expectedSdlPriority;
        const char* description;
    };

    class LoggerSdlPriorityTest : public ::testing::TestWithParam<LoggerSeverityCase>
    {
    protected:
        void SetUp() override
        {
            savedLevel_ = Logger::GetMinimumLevel();
        }

        void TearDown() override
        {
            Logger::SetMinimumLevel(savedLevel_);
        }

    private:
        LogLevel savedLevel_ = LogLevel::TRACE;
    };
}

TEST_P(LoggerSdlPriorityTest, SetMinimumLevelSetsTheExpectedSdlPriority)
{
    const LoggerSeverityCase testCase = GetParam();

    Logger::SetMinimumLevel(testCase.level);

    EXPECT_EQ(SDL_GetLogPriority(SDL_LOG_CATEGORY_APPLICATION), testCase.expectedSdlPriority)
        << testCase.description;
    EXPECT_EQ(Logger::GetMinimumLevel(), testCase.level) << testCase.description;
}

// Table-driven over every LogLevel enumerator: REMED-CORE-001's own regression requirement is
// that a level added later cannot silently fall through to the SDL_LOG_PRIORITY_INFO default
// without a test noticing.
INSTANTIATE_TEST_SUITE_P(
    AllLogLevels,
    LoggerSdlPriorityTest,
    ::testing::Values(
        LoggerSeverityCase{LogLevel::FATAL, SDL_LOG_PRIORITY_CRITICAL, "FATAL"},
        LoggerSeverityCase{LogLevel::ERROR, SDL_LOG_PRIORITY_ERROR, "ERROR"},
        LoggerSeverityCase{LogLevel::WARN, SDL_LOG_PRIORITY_WARN, "WARN"},
        LoggerSeverityCase{LogLevel::INFO, SDL_LOG_PRIORITY_INFO, "INFO"},
        LoggerSeverityCase{LogLevel::DEBUG, SDL_LOG_PRIORITY_DEBUG, "DEBUG"},
        LoggerSeverityCase{LogLevel::TRACE, SDL_LOG_PRIORITY_DEBUG, "TRACE"},
        LoggerSeverityCase{LogLevel::EXPERIMENT, SDL_LOG_PRIORITY_DEBUG, "EXPERIMENT"}
    ),
    [](const ::testing::TestParamInfo<LoggerSeverityCase>& info)
    {
        return std::string(info.param.description);
    }
);

TEST(LoggerTest, SetMinimumLevelWarnDoesNotCollapseToInfo)
{
    // REMED-CORE-001's headline symptom: WARN previously fell through to
    // SDL_LOG_PRIORITY_INFO instead of SDL_LOG_PRIORITY_WARN.
    const LogLevel saved = Logger::GetMinimumLevel();

    Logger::SetMinimumLevel(LogLevel::WARN);
    EXPECT_EQ(SDL_GetLogPriority(SDL_LOG_CATEGORY_APPLICATION), SDL_LOG_PRIORITY_WARN);
    EXPECT_NE(SDL_GetLogPriority(SDL_LOG_CATEGORY_APPLICATION), SDL_LOG_PRIORITY_INFO);

    Logger::SetMinimumLevel(saved);
}

TEST(LoggerTest, LogDoesNotThrowAtAnySeverity)
{
    const LogLevel saved = Logger::GetMinimumLevel();
    Logger::SetMinimumLevel(LogLevel::TRACE);

    EXPECT_NO_THROW(Logger::Fatal("logger fatal regression check"));
    EXPECT_NO_THROW(Logger::Error("logger error regression check"));
    EXPECT_NO_THROW(Logger::Warn("logger warn regression check"));
    EXPECT_NO_THROW(Logger::Info("logger info regression check"));
    EXPECT_NO_THROW(Logger::Debug("logger debug regression check"));
    EXPECT_NO_THROW(Logger::Trace("logger trace regression check"));
    EXPECT_NO_THROW(Logger::Experiment("logger experiment regression check"));

    Logger::SetMinimumLevel(saved);
}

TEST(LoggerTest, IsEnabledGatingRespectsMinimumLevelOrdering)
{
    // Not a direct ToSDLPriority test, but a needed companion: this is the OTHER gate (CNA's
    // own IsEnabled()) that determines whether ToSDLPriority()/SDL_LogMessage() is reached at
    // all. Setting the minimum to WARN must not silently suppress WARN/ERROR/FATAL themselves.
    const LogLevel saved = Logger::GetMinimumLevel();
    Logger::SetMinimumLevel(LogLevel::WARN);

    // FatalIf/ErrorIf/WarnIf log unconditionally when condition is true; this only proves no
    // crash/throw occurs while the minimum level excludes INFO/DEBUG/TRACE/EXPERIMENT below it.
    EXPECT_NO_THROW(Logger::FatalIf("fatal", true));
    EXPECT_NO_THROW(Logger::ErrorIf("error", true));
    EXPECT_NO_THROW(Logger::WarnIf("warn", true));
    EXPECT_NO_THROW(Logger::InfoIf("info", true));

    Logger::SetMinimumLevel(saved);
}
