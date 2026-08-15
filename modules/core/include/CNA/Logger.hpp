#pragma once

#include <functional>
#include <string_view>

#include "CNA/LogLevel.hpp"
#include "CNA/LogCategory.hpp"

namespace CNA
{
    /**
     * @brief Simple logging utility owning its own output sink.
     *
     * Provides convenience methods for logging messages with severity
     * and category metadata.
     */
    class Logger
    {
    public:
        /**
         * @brief Logs a message with an explicit level.
         *
         * @param level Severity level.
         * @param message Message text.
         * @param category Functional category. Defaults to APPLICATION.
         */
        static void Log(
            LogLevel level,
            std::string_view message,
            LogCategory category = LogCategory::APPLICATION,
            bool condition = true
        );

        /**
         * @brief Logs a fatal message.
         *
         * @param message Message text.
         * @param category Functional category. Defaults to APPLICATION.
         */
        static void Fatal(
            std::string_view message,
            LogCategory category = LogCategory::APPLICATION
        );

        /**
         * @brief Logs an error message.
         *
         * @param message Message text.
         * @param category Functional category. Defaults to APPLICATION.
         */
        static void Error(
            std::string_view message,
            LogCategory category = LogCategory::APPLICATION
        );

        /**
         * @brief Logs a warning message.
         *
         * @param message Message text.
         * @param category Functional category. Defaults to APPLICATION.
         */
        static void Warn(
            std::string_view message,
            LogCategory category = LogCategory::APPLICATION
        );

        /**
         * @brief Logs an informational message.
         *
         * @param message Message text.
         * @param category Functional category. Defaults to APPLICATION.
         */
        static void Info(
            std::string_view message,
            LogCategory category = LogCategory::APPLICATION
        );

        /**
         * @brief Logs a debug message.
         *
         * @param message Message text.
         * @param category Functional category. Defaults to APPLICATION.
         */
        static void Debug(
            std::string_view message,
            LogCategory category = LogCategory::APPLICATION
        );

        /**
         * @brief Logs a trace message.
         *
         * @param message Message text.
         * @param category Functional category. Defaults to APPLICATION.
         */
        static void Trace(
            std::string_view message,
            LogCategory category = LogCategory::APPLICATION
        );

        /**
         * @brief Logs an experimental message.
         *
         * @param message Message text.
         * @param category Functional category. Defaults to APPLICATION.
         */
        static void Experiment(
            std::string_view message,
            LogCategory category = LogCategory::APPLICATION
        );
        /**
         * @brief Logs a fatal message if the condition is true.
         *
         * @param message Message text.
         * @param condition Condition that must be true for the message to be logged.
         */
        static void FatalIf(
            std::string_view message,
            bool condition
        );

        /**
         * @brief Logs an error message if the condition is true.
         *
         * @param message Message text.
         * @param condition Condition that must be true for the message to be logged.
         */
        static void ErrorIf(
            std::string_view message,
            bool condition
        );

        /**
         * @brief Logs a warning message if the condition is true.
         *
         * @param message Message text.
         * @param condition Condition that must be true for the message to be logged.
         */
        static void WarnIf(
            std::string_view message,
            bool condition
        );

        /**
         * @brief Logs an informational message if the condition is true.
         *
         * @param message Message text.
         * @param condition Condition that must be true for the message to be logged.
         */
        static void InfoIf(
            std::string_view message,
            bool condition
        );

        /**
         * @brief Logs a debug message if the condition is true.
         *
         * @param message Message text.
         * @param condition Condition that must be true for the message to be logged.
         */
        static void DebugIf(
            std::string_view message,
            bool condition
        );

        /**
         * @brief Logs a trace message if the condition is true.
         *
         * @param message Message text.
         * @param condition Condition that must be true for the message to be logged.
         */
        static void TraceIf(
            std::string_view message,
            bool condition
        );

        /**
         * @brief Logs an experimental message if the condition is true.
         *
         * @param message Message text.
         * @param condition Condition that must be true for the message to be logged.
         */
        static void ExperimentIf(
            std::string_view message,
            bool condition
        );
        /**
         * @brief Sets the minimum enabled log level.
         *
         * Messages below this threshold are ignored.
         *
         * @param level New minimum log level.
         */
        static void SetMinimumLevel(LogLevel level);

        /**
         * @brief Gets the current minimum enabled log level.
         *
         * @return Current minimum log level.
         */
        [[nodiscard]] static LogLevel GetMinimumLevel();

        /**
         * @brief The signature of a log sink.
         *
         * @param level The severity of the message.
         * @param category The category the message was logged under.
         * @param formattedMessage The fully formatted line, without a trailing newline.
         */
        using Sink = std::function<void(LogLevel level, LogCategory category,
                                        std::string_view formattedMessage)>;

        /**
         * @brief Replaces the destination log messages are written to.
         *
         * The default sink writes to **stderr**, deliberately never stdout: a terminal-hosted
         * game draws its frame on stdout, and a log line there would corrupt it. That makes the
         * destination a correctness matter rather than a preference.
         *
         * @param sink The new sink. Passing an empty function restores the default.
         */
        static void SetSink(Sink sink);

        /**
         * @brief Restores the default stderr sink.
         */
        static void ResetSink();

    private:
        [[nodiscard]] static bool IsEnabled(LogLevel level);
        [[nodiscard]] static const char* ToString(LogLevel level);
        [[nodiscard]] static const char* ToString(LogCategory category);

    private:
        static LogLevel minimumLevel_;
    };
}
