// SPDX-License-Identifier: MS-PL
//
// PLAT-53: Logger owns its own sink.
//
// These tests previously asserted SDL's process-wide log priority after SetMinimumLevel() -- i.e.
// they tested SDL's global state rather than CNA's observable behaviour, and they could not have
// survived the migration. They now capture the sink and assert what a caller can actually see:
// which messages are emitted, at what level and category, and in what format.

#include "CNA/Logger.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace {

using namespace CNA;

struct CapturedLine
{
    LogLevel level;
    LogCategory category;
    std::string message;
};

class LoggerTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        previousLevel_ = Logger::GetMinimumLevel();
        Logger::SetSink([this](const LogLevel level, const LogCategory category,
                               const std::string_view message) {
            captured_.push_back(CapturedLine{level, category, std::string(message)});
        });
    }

    void TearDown() override
    {
        // The sink and the level are process-wide, so leaving either changed would leak into
        // every later test in this binary.
        Logger::ResetSink();
        Logger::SetMinimumLevel(previousLevel_);
    }

    std::vector<CapturedLine> captured_;
    LogLevel previousLevel_ = LogLevel::INFO;
};

// --- what actually reaches the sink -------------------------------------------------------------

TEST_F(LoggerTest, AMessageAtOrAboveTheMinimumLevelReachesTheSink)
{
    Logger::SetMinimumLevel(LogLevel::INFO);
    Logger::Info("hello");
    ASSERT_EQ(captured_.size(), 1u);
    EXPECT_EQ(captured_[0].level, LogLevel::INFO);
    EXPECT_EQ(captured_[0].category, LogCategory::APPLICATION);
}

TEST_F(LoggerTest, AMessageBelowTheMinimumLevelIsDiscarded)
{
    Logger::SetMinimumLevel(LogLevel::WARN);
    Logger::Info("suppressed");
    Logger::Debug("also suppressed");
    EXPECT_TRUE(captured_.empty());
}

TEST_F(LoggerTest, EverySeverityIsDeliveredWithItsOwnLevel)
{
    // The old version pinned each level to a native log priority, which collapsed DEBUG, TRACE and
    // EXPERIMENT onto one value and so could not tell them apart. Owning the sink means the
    // level survives to the consumer intact, and this pins that.
    Logger::SetMinimumLevel(LogLevel::EXPERIMENT);

    Logger::Fatal("f");
    Logger::Error("e");
    Logger::Warn("w");
    Logger::Info("i");
    Logger::Debug("d");
    Logger::Trace("t");
    Logger::Experiment("x");

    ASSERT_EQ(captured_.size(), 7u);
    EXPECT_EQ(captured_[0].level, LogLevel::FATAL);
    EXPECT_EQ(captured_[1].level, LogLevel::ERROR);
    EXPECT_EQ(captured_[2].level, LogLevel::WARN);
    EXPECT_EQ(captured_[3].level, LogLevel::INFO);
    EXPECT_EQ(captured_[4].level, LogLevel::DEBUG);
    EXPECT_EQ(captured_[5].level, LogLevel::TRACE);
    EXPECT_EQ(captured_[6].level, LogLevel::EXPERIMENT);
}

TEST_F(LoggerTest, NonApplicationCategoriesAreNoLongerFilteredSeparately)
{
    // The concrete regression the migration removes. SDL defaulted RENDER (and most other
    // non-application categories) to a stricter threshold than APPLICATION, so a Warn on RENDER
    // could be discarded after CNA's own IsEnabled() had allowed it -- two gates, only one of
    // them CNA's. There is now exactly one gate.
    Logger::SetMinimumLevel(LogLevel::WARN);
    Logger::Warn("render warning", LogCategory::RENDER);

    ASSERT_EQ(captured_.size(), 1u);
    EXPECT_EQ(captured_[0].category, LogCategory::RENDER);
}

TEST_F(LoggerTest, EveryCategoryReachesTheSinkUnchanged)
{
    Logger::SetMinimumLevel(LogLevel::INFO);
    for (const LogCategory category :
         {LogCategory::APPLICATION, LogCategory::ERROR, LogCategory::SYSTEM, LogCategory::AUDIO,
          LogCategory::VIDEO, LogCategory::RENDER, LogCategory::INPUT, LogCategory::TEST,
          LogCategory::GPU})
    {
        Logger::Info("m", category);
    }

    ASSERT_EQ(captured_.size(), 9u);
    EXPECT_EQ(captured_[0].category, LogCategory::APPLICATION);
    EXPECT_EQ(captured_[5].category, LogCategory::RENDER);
    EXPECT_EQ(captured_[8].category, LogCategory::GPU);
}

// --- conditional overloads ------------------------------------------------------------------------

TEST_F(LoggerTest, ConditionalOverloadsRespectTheirCondition)
{
    Logger::SetMinimumLevel(LogLevel::TRACE);
    Logger::WarnIf("emitted", true);
    Logger::WarnIf("skipped", false);
    Logger::InfoIf("emitted", true);
    Logger::InfoIf("skipped", false);

    ASSERT_EQ(captured_.size(), 2u);
    EXPECT_NE(captured_[0].message.find("emitted"), std::string::npos);
    EXPECT_NE(captured_[1].message.find("emitted"), std::string::npos);
}

// --- format ----------------------------------------------------------------------------------------

TEST_F(LoggerTest, FormattedLineCarriesLevelCategoryAndMessage)
{
    Logger::SetMinimumLevel(LogLevel::INFO);
    Logger::Warn("disk almost full", LogCategory::SYSTEM);

    ASSERT_EQ(captured_.size(), 1u);
    EXPECT_EQ(captured_[0].message, "[WARN][SYSTEM] disk almost full");
}

TEST_F(LoggerTest, FormattedLineHasNoTrailingNewline)
{
    // The sink adds its own line ending; embedding one here would double-space every custom sink
    // and corrupt structured consumers.
    Logger::SetMinimumLevel(LogLevel::INFO);
    Logger::Info("x");
    ASSERT_EQ(captured_.size(), 1u);
    EXPECT_EQ(captured_[0].message.find('\n'), std::string::npos);
}

// --- sink lifecycle ----------------------------------------------------------------------------------

TEST_F(LoggerTest, ResetSinkRestoresTheDefaultWithoutCrashing)
{
    Logger::SetMinimumLevel(LogLevel::INFO);
    Logger::ResetSink();
    // Goes to stderr now; the assertion is that logging with no custom sink is safe, which is the
    // state every real application runs in.
    EXPECT_NO_THROW(Logger::Info("to stderr"));
    EXPECT_TRUE(captured_.empty());
}

TEST_F(LoggerTest, DefaultSinkWritesToStderrAndNeverStdout)
{
    Logger::ResetSink();
    Logger::SetMinimumLevel(LogLevel::INFO);

    ::testing::internal::CaptureStdout();
    ::testing::internal::CaptureStderr();
    Logger::Info("terminal-safe-log-destination");
    const std::string standardError = ::testing::internal::GetCapturedStderr();
    const std::string standardOutput = ::testing::internal::GetCapturedStdout();

    EXPECT_TRUE(standardOutput.empty()) << standardOutput;
    EXPECT_NE(standardError.find("terminal-safe-log-destination"), std::string::npos)
        << standardError;
}

TEST_F(LoggerTest, MinimumLevelRoundTrips)
{
    Logger::SetMinimumLevel(LogLevel::ERROR);
    EXPECT_EQ(Logger::GetMinimumLevel(), LogLevel::ERROR);
    Logger::SetMinimumLevel(LogLevel::TRACE);
    EXPECT_EQ(Logger::GetMinimumLevel(), LogLevel::TRACE);
}

} // namespace
