// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/ShaderLanguageEXT.hpp"

#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace CNA
{
    /** @brief Severity of one portable shader compiler or validation diagnostic. */
    enum class ShaderDiagnosticSeverityEXT : int
    {
        /** @brief Informational compiler output. */
        Information = 0,
        /** @brief A warning that did not necessarily prevent compilation. */
        Warning = 1,
        /** @brief An error that prevents the requested shader path from being usable. */
        Error = 2,
        /** @brief Number of declared severities; not itself a severity. */
        Count = 3
    };

    /** @brief One owned renderer-neutral shader compiler or validation diagnostic. */
    class ShaderDiagnosticEXT final
    {
    public:
        /**
         * @brief Creates one validated owned diagnostic.
         * @param severity Diagnostic severity.
         * @param stage Exact stage, or `Unknown` when the producer did not identify one.
         * @param sourceLabel Caller-facing source label, possibly empty.
         * @param line One-based source line, or zero when unavailable.
         * @param column One-based source column, or zero when unavailable.
         * @param message Non-empty owned diagnostic text.
         * @throws std::invalid_argument If an identity, location or message is invalid.
         */
        ShaderDiagnosticEXT(
            ShaderDiagnosticSeverityEXT severity, ShaderStageEXT stage,
            std::string sourceLabel, int line, int column, std::string message);

        /** @brief Returns the diagnostic severity. @return Valid severity. */
        [[nodiscard]] ShaderDiagnosticSeverityEXT getSeverity() const noexcept;

        /** @brief Returns the affected shader stage. @return Stage or `Unknown`. */
        [[nodiscard]] ShaderStageEXT getStage() const noexcept;

        /** @brief Returns the owned caller-facing source label. @return Label, possibly empty. */
        [[nodiscard]] const std::string& getSourceLabel() const noexcept;

        /** @brief Returns the one-based source line. @return Line, or zero when unavailable. */
        [[nodiscard]] int getLine() const noexcept;

        /** @brief Returns the one-based source column. @return Column, or zero when unavailable. */
        [[nodiscard]] int getColumn() const noexcept;

        /** @brief Returns the owned compiler or validation text. @return Non-empty text. */
        [[nodiscard]] const std::string& getMessage() const noexcept;

        /**
         * @brief Parses an owned deterministic diagnostic list from a renderer compiler log.
         *
         * Common `source:line:column`, `source:line(column)` and `file(line,column)` forms are
         * recognized. A location stays zero rather than being invented when the renderer supplies
         * only text. `VS`/`vertex`, `FS`/`fragment` and `CS`/`compute` markers override the fallback
         * stage and remain in effect for continuation lines. Each non-empty log line becomes one
         * independently owned record.
         *
         * @param log Renderer-supplied compiler or validation log.
         * @param fallbackStage Stage used when a line carries no stage marker.
         * @param sourceLabel Caller-facing source label copied into every result.
         * @return Owned diagnostics in log order; empty only when @p log has no non-empty line.
         */
        [[nodiscard]] static std::vector<ShaderDiagnosticEXT> parseCompilerLog(
            std::string_view log, ShaderStageEXT fallbackStage,
            std::string sourceLabel = {});

    private:
        ShaderDiagnosticSeverityEXT severity_;
        ShaderStageEXT stage_;
        std::string sourceLabel_;
        int line_;
        int column_;
        std::string message_;
    };

    /** @brief Shader compilation failure that preserves all structured diagnostics. */
    class ShaderCompilationExceptionEXT final : public std::runtime_error
    {
    public:
        /**
         * @brief Creates an exception with a stable short summary and owned diagnostic records.
         * @param diagnostics Non-empty list whose first record is summarized by `what()`.
         * @throws std::invalid_argument If @p diagnostics is empty.
         */
        explicit ShaderCompilationExceptionEXT(
            std::vector<ShaderDiagnosticEXT> diagnostics);

        /**
         * @brief Returns every diagnostic retained independently of the `what()` summary.
         * @return Non-empty immutable owned diagnostic list.
         */
        [[nodiscard]] const std::vector<ShaderDiagnosticEXT>& getDiagnostics() const noexcept;

    private:
        static std::string summarize(const std::vector<ShaderDiagnosticEXT>& diagnostics);

        std::vector<ShaderDiagnosticEXT> diagnostics_;
    };
}
