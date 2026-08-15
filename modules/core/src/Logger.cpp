#include "CNA/Logger.hpp"

#include <cstdio>
#include <mutex>
#include <string>

namespace CNA
{
#ifdef NDEBUG
    static constexpr LogLevel defaultLevel = LogLevel::INFO;
#else
    static constexpr LogLevel defaultLevel = LogLevel::TRACE;
#endif

    LogLevel Logger::minimumLevel_ = defaultLevel;

    namespace
    {
        /// Serialises writes so two threads cannot interleave halves of a line. SDL's logger did
        /// this internally; owning the sink means owning that guarantee too.
        std::mutex& SinkMutex()
        {
            static std::mutex mutex;
            return mutex;
        }

        Logger::Sink& CurrentSink()
        {
            static Logger::Sink sink;
            return sink;
        }

        /// stderr, never stdout. A terminal-hosted game draws its frame on stdout, so a log line
        /// there would corrupt the display -- see docs/platform-terminal-analysis.md.
        void WriteToStandardError(LogLevel, LogCategory, const std::string_view formattedMessage)
        {
            std::fwrite(formattedMessage.data(), 1, formattedMessage.size(), stderr);
            std::fputc('\n', stderr);
        }
    }

    void Logger::Log(
        LogLevel level,
        std::string_view message,
        LogCategory category,
        bool condition
    )
    {
        if (!condition)
        {
            return;
        }
        if (!IsEnabled(level))
        {
            return;
        }

        std::string finalMessage =
            std::string("[")
            + ToString(level)
            + "]["
            + ToString(category)
            + "] "
            + std::string(message);

        const std::lock_guard<std::mutex> guard(SinkMutex());
        if (CurrentSink())
        {
            CurrentSink()(level, category, finalMessage);
        }
        else
        {
            WriteToStandardError(level, category, finalMessage);
        }
    }

    void Logger::Fatal(
        std::string_view message,
        LogCategory category
    )
    {
        Log(LogLevel::FATAL, message, category);
    }

    void Logger::Error(
        std::string_view message,
        LogCategory category
    )
    {
        Log(LogLevel::ERROR, message, category);
    }

    void Logger::Warn(
        std::string_view message,
        LogCategory category
    )
    {
        Log(LogLevel::WARN, message, category);
    }

    void Logger::Info(
        std::string_view message,
        LogCategory category
    )
    {
        Log(LogLevel::INFO, message, category);
    }

    void Logger::Debug(
        std::string_view message,
        LogCategory category
    )
    {
        Log(LogLevel::DEBUG, message, category);
    }

    void Logger::Trace(
        std::string_view message,
        LogCategory category
    )
    {
        Log(LogLevel::TRACE, message, category);
    }

    void Logger::Experiment(
        std::string_view message,
        LogCategory category
    )
    {
        Log(LogLevel::EXPERIMENT, message, category);
    }

    void Logger::FatalIf(
        std::string_view message,
        bool condition
    )
    {
        Log(LogLevel::FATAL, message, LogCategory::APPLICATION, condition);
    }

    void Logger::ErrorIf(
        std::string_view message,
        bool condition
    )
    {
        Log(LogLevel::ERROR, message, LogCategory::APPLICATION, condition);
    }

    void Logger::WarnIf(
        std::string_view message,
        bool condition
    )
    {
        Log(LogLevel::WARN, message, LogCategory::APPLICATION, condition);
    }

    void Logger::InfoIf(
        std::string_view message,
        bool condition
    )
    {
        Log(LogLevel::INFO, message, LogCategory::APPLICATION, condition);
    }

    void Logger::DebugIf(
        std::string_view message,
        bool condition
    )
    {
        Log(LogLevel::DEBUG, message, LogCategory::APPLICATION, condition);
    }

    void Logger::TraceIf(
        std::string_view message,
        bool condition
    )
    {
        Log(LogLevel::TRACE, message, LogCategory::APPLICATION, condition);
    }

    void Logger::ExperimentIf(
        std::string_view message,
        bool condition
    )
    {
        Log(LogLevel::EXPERIMENT, message, LogCategory::APPLICATION, condition);
    }

    void Logger::SetMinimumLevel(LogLevel level)
    {
        // Just CNA's own gate now. The previous implementation also had to push the level into
        // SDL's process-wide category filters, because SDL defaulted most non-application
        // categories to a stricter threshold and would otherwise discard a message CNA's own
        // IsEnabled() had already allowed through. Owning the sink removes that second, hidden
        // gate entirely.
        minimumLevel_ = level;
    }

    void Logger::SetSink(Sink sink)
    {
        const std::lock_guard<std::mutex> guard(SinkMutex());
        CurrentSink() = std::move(sink);
    }

    void Logger::ResetSink()
    {
        const std::lock_guard<std::mutex> guard(SinkMutex());
        CurrentSink() = nullptr;
    }

    LogLevel Logger::GetMinimumLevel()
    {
        return minimumLevel_;
    }

    bool Logger::IsEnabled(LogLevel level)
    {
        return static_cast<int>(level) <= static_cast<int>(minimumLevel_);
    }

    const char* Logger::ToString(LogLevel level)
    {
        switch (level)
        {
        case LogLevel::FATAL:
            return "FATAL";
        case LogLevel::ERROR:
            return "ERROR";
        case LogLevel::WARN:
            return "WARN";
        case LogLevel::INFO:
            return "INFO";
        case LogLevel::DEBUG:
            return "DEBUG";
        case LogLevel::TRACE:
            return "TRACE";
        case LogLevel::EXPERIMENT:
            return "EXPERIMENT";
        default:
            return "UNKNOWN";
        }
    }

    const char* Logger::ToString(LogCategory category)
    {
        switch (category)
        {
        case LogCategory::APPLICATION:
            return "APPLICATION";
        case LogCategory::ERROR:
            return "ERROR";
        case LogCategory::SYSTEM:
            return "SYSTEM";
        case LogCategory::AUDIO:
            return "AUDIO";
        case LogCategory::VIDEO:
            return "VIDEO";
        case LogCategory::RENDER:
            return "RENDER";
        case LogCategory::INPUT:
            return "INPUT";
        case LogCategory::TEST:
            return "TEST";
        case LogCategory::GPU:
            return "GPU";
        default:
            return "UNKNOWN";
        }
    }
}
