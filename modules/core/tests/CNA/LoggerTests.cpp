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

#include <array>
#include <string>
#include <string_view>
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
    // Oracle: Logger.hpp preserves the caller's category at the sink for every enum member.
    const std::array categories{LogCategory::APPLICATION, LogCategory::ERROR, LogCategory::SYSTEM,
                               LogCategory::AUDIO, LogCategory::VIDEO, LogCategory::RENDER,
                               LogCategory::INPUT, LogCategory::TEST, LogCategory::GPU};
    for (const LogCategory category : categories)
    {
        Logger::Info("m", category);
    }

    ASSERT_EQ(captured_.size(), 9u);
    for (std::size_t index = 0; index < categories.size(); ++index)
        EXPECT_EQ(captured_[index].category, categories[index]) << index;
}

// --- conditional overloads ------------------------------------------------------------------------

TEST_F(LoggerTest, ConditionalOverloadsRespectTheirCondition)
{
    // Oracle: Logger.hpp: each *If emits precisely when its condition and level allow it.
    const std::array<void (*)(std::string_view, bool), 7> emitters{
        Logger::FatalIf, Logger::ErrorIf, Logger::WarnIf, Logger::InfoIf,
        Logger::DebugIf, Logger::TraceIf, Logger::ExperimentIf};
    const std::array levels{LogLevel::FATAL, LogLevel::ERROR, LogLevel::WARN, LogLevel::INFO,
                            LogLevel::DEBUG, LogLevel::TRACE, LogLevel::EXPERIMENT};
    const std::array<std::string_view, 7> formatted{
        "[FATAL][APPLICATION] emitted", "[ERROR][APPLICATION] emitted",
        "[WARN][APPLICATION] emitted", "[INFO][APPLICATION] emitted",
        "[DEBUG][APPLICATION] emitted", "[TRACE][APPLICATION] emitted",
        "[EXPERIMENT][APPLICATION] emitted"};
    Logger::SetMinimumLevel(LogLevel::EXPERIMENT);
    for (const auto emitter : emitters)
    {
        emitter("skipped", false);
        emitter("emitted", true);
    }
    ASSERT_EQ(captured_.size(), emitters.size());
    for (std::size_t index = 0; index < emitters.size(); ++index)
    {
        EXPECT_EQ(captured_[index].level, levels[index]);
        EXPECT_EQ(captured_[index].category, LogCategory::APPLICATION);
        EXPECT_EQ(captured_[index].message, formatted[index]);
    }
    captured_.clear();
    Logger::SetMinimumLevel(LogLevel::FATAL);
    for (std::size_t index = 1; index < emitters.size(); ++index)
        emitters[index]("filtered", true);
    EXPECT_TRUE(captured_.empty());
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

TEST_F(LoggerTest, EveryThresholdIncludesItselfAndAllMoreSevereLevels)
{
    // Oracle: Logger.hpp orders severity FATAL through EXPERIMENT; minimum is inclusive.
    const std::array levels{LogLevel::FATAL, LogLevel::ERROR, LogLevel::WARN, LogLevel::INFO,
                            LogLevel::DEBUG, LogLevel::TRACE, LogLevel::EXPERIMENT};
    for (std::size_t threshold = 0; threshold < levels.size(); ++threshold)
    {
        Logger::SetMinimumLevel(levels[threshold]);
        captured_.clear();
        for (std::size_t severity = 0; severity < levels.size(); ++severity)
        {
            Logger::Log(levels[severity], "threshold");
        }
        ASSERT_EQ(captured_.size(), threshold + 1);
        for (std::size_t index = 0; index < captured_.size(); ++index)
            EXPECT_EQ(captured_[index].level, levels[index]);
    }
}

TEST_F(LoggerTest, SinkReceivesTheExactStringViewBytesIncludingEmbeddedNull)
{
    // Oracle: Logger::Log takes string_view, so it must honor length, not C-string termination.
    const std::string storage("xA\0Bz", 5);
    Logger::SetMinimumLevel(LogLevel::INFO);
    Logger::Info(std::string_view(storage.data() + 1, 3), LogCategory::INPUT);
    ASSERT_EQ(captured_.size(), 1u);
    EXPECT_EQ(captured_[0].message, std::string("[INFO][INPUT] A\0B", 17));
}

} // namespace
