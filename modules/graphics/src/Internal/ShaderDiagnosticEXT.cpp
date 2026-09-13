// SPDX-License-Identifier: MS-PL
#include "CNA/ShaderDiagnosticEXT.hpp"

#include <algorithm>
#include <cctype>
#include <limits>
#include <utility>

namespace CNA
{
    namespace
    {
        [[nodiscard]] bool IsValidSeverity(const ShaderDiagnosticSeverityEXT severity) noexcept
        {
            return severity == ShaderDiagnosticSeverityEXT::Information
                || severity == ShaderDiagnosticSeverityEXT::Warning
                || severity == ShaderDiagnosticSeverityEXT::Error;
        }

        [[nodiscard]] bool IsValidStage(const ShaderStageEXT stage) noexcept
        {
            return stage == ShaderStageEXT::Unknown || stage == ShaderStageEXT::Vertex
                || stage == ShaderStageEXT::Fragment || stage == ShaderStageEXT::Compute;
        }

        [[nodiscard]] std::string Lower(std::string_view value)
        {
            std::string result(value);
            std::transform(
                result.begin(), result.end(), result.begin(),
                [](const unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
            return result;
        }

        [[nodiscard]] bool ParseNumber(
            const std::string_view text, std::size_t& cursor, int& value) noexcept
        {
            if (cursor >= text.size()
                || !std::isdigit(static_cast<unsigned char>(text[cursor]))) return false;
            unsigned long long parsed = 0;
            while (cursor < text.size()
                   && std::isdigit(static_cast<unsigned char>(text[cursor])))
            {
                parsed = parsed * 10 + static_cast<unsigned>(text[cursor] - '0');
                if (parsed > static_cast<unsigned long long>(std::numeric_limits<int>::max()))
                    return false;
                ++cursor;
            }
            value = static_cast<int>(parsed);
            return true;
        }

        void ParseLocation(const std::string_view text, int& line, int& column) noexcept
        {
            // file(line,column)
            for (std::size_t open = text.find('('); open != std::string_view::npos;
                 open = text.find('(', open + 1))
            {
                std::size_t cursor = open + 1;
                int parsedLine = 0;
                int parsedColumn = 0;
                if (ParseNumber(text, cursor, parsedLine) && cursor < text.size()
                    && text[cursor] == ',')
                {
                    ++cursor;
                    if (ParseNumber(text, cursor, parsedColumn) && cursor < text.size()
                        && text[cursor] == ')')
                    {
                        line = parsedLine;
                        column = parsedColumn;
                        return;
                    }
                }
            }

            // source:line(column), including Mesa's numeric source-id form 0:line(column).
            for (std::size_t separator = text.find(':'); separator != std::string_view::npos;
                 separator = text.find(':', separator + 1))
            {
                std::size_t cursor = separator + 1;
                while (cursor < text.size() && text[cursor] == ' ') ++cursor;
                int firstNumber = 0;
                if (!ParseNumber(text, cursor, firstNumber)) continue;

                int parsedLine = firstNumber;
                if (cursor < text.size() && text[cursor] == ':')
                {
                    ++cursor;
                    if (!ParseNumber(text, cursor, parsedLine)) continue;
                }
                if (cursor >= text.size() || text[cursor] != '(') continue;
                ++cursor;
                int parsedColumn = 0;
                if (ParseNumber(text, cursor, parsedColumn) && cursor < text.size()
                    && text[cursor] == ')')
                {
                    line = parsedLine;
                    column = parsedColumn;
                    return;
                }
            }

            // file:line:column: and Mesa's severity: source-id:line: form. A known severity
            // immediately before the first number, or source id zero, identifies the latter.
            for (std::size_t separator = text.find(':'); separator != std::string_view::npos;
                 separator = text.find(':', separator + 1))
            {
                std::size_t cursor = separator + 1;
                while (cursor < text.size() && text[cursor] == ' ') ++cursor;
                int parsedLine = 0;
                if (!ParseNumber(text, cursor, parsedLine)) continue;
                if (cursor >= text.size() || text[cursor] != ':') continue;
                ++cursor;
                int secondNumber = 0;
                if (!ParseNumber(text, cursor, secondNumber))
                {
                    line = parsedLine;
                    return;
                }
                if (cursor >= text.size() || text[cursor] != ':') continue;

                std::string prefix = Lower(text.substr(0, separator));
                const auto last = prefix.find_last_not_of(" \t");
                if (last != std::string::npos) prefix.erase(last + 1);
                const bool numericSourceId = parsedLine == 0
                    || prefix.ends_with("error") || prefix.ends_with("warning")
                    || prefix.ends_with("info");
                if (numericSourceId)
                    line = secondNumber;
                else
                {
                    line = parsedLine;
                    column = secondNumber;
                }
                return;
            }
        }

        [[nodiscard]] std::string_view SeverityName(
            const ShaderDiagnosticSeverityEXT severity) noexcept
        {
            switch (severity)
            {
                case ShaderDiagnosticSeverityEXT::Information: return "Information";
                case ShaderDiagnosticSeverityEXT::Warning: return "Warning";
                case ShaderDiagnosticSeverityEXT::Error: return "Error";
                case ShaderDiagnosticSeverityEXT::Count: return "Count";
            }
            return "InvalidSeverity";
        }

        [[nodiscard]] std::string_view StageName(const ShaderStageEXT stage) noexcept
        {
            switch (stage)
            {
                case ShaderStageEXT::Unknown: return "Unknown";
                case ShaderStageEXT::Vertex: return "Vertex";
                case ShaderStageEXT::Fragment: return "Fragment";
                case ShaderStageEXT::Compute: return "Compute";
                case ShaderStageEXT::Count: return "Count";
            }
            return "InvalidStage";
        }
    }

    ShaderDiagnosticEXT::ShaderDiagnosticEXT(
        const ShaderDiagnosticSeverityEXT severity, const ShaderStageEXT stage,
        std::string sourceLabel, const int line, const int column, std::string message)
        : severity_(severity)
        , stage_(stage)
        , sourceLabel_(std::move(sourceLabel))
        , line_(line)
        , column_(column)
        , message_(std::move(message))
    {
        if (!IsValidSeverity(severity_))
            throw std::invalid_argument("ShaderDiagnosticEXT: severity is not recognized");
        if (!IsValidStage(stage_))
            throw std::invalid_argument("ShaderDiagnosticEXT: stage is not recognized");
        if (line_ < 0 || column_ < 0)
            throw std::invalid_argument("ShaderDiagnosticEXT: location must not be negative");
        if (line_ == 0 && column_ != 0)
            throw std::invalid_argument(
                "ShaderDiagnosticEXT: column requires an available source line");
        if (message_.empty())
            throw std::invalid_argument("ShaderDiagnosticEXT: message must not be empty");
    }

    ShaderDiagnosticSeverityEXT ShaderDiagnosticEXT::getSeverity() const noexcept
    {
        return severity_;
    }

    ShaderStageEXT ShaderDiagnosticEXT::getStage() const noexcept { return stage_; }

    const std::string& ShaderDiagnosticEXT::getSourceLabel() const noexcept
    {
        return sourceLabel_;
    }

    int ShaderDiagnosticEXT::getLine() const noexcept { return line_; }

    int ShaderDiagnosticEXT::getColumn() const noexcept { return column_; }

    const std::string& ShaderDiagnosticEXT::getMessage() const noexcept { return message_; }

    std::vector<ShaderDiagnosticEXT> ShaderDiagnosticEXT::parseCompilerLog(
        const std::string_view log, const ShaderStageEXT fallbackStage,
        std::string sourceLabel)
    {
        if (!IsValidStage(fallbackStage))
            throw std::invalid_argument("ShaderDiagnosticEXT: fallback stage is not recognized");

        std::vector<ShaderDiagnosticEXT> result;
        ShaderStageEXT currentStage = fallbackStage;
        for (std::size_t begin = 0; begin <= log.size();)
        {
            const std::size_t newline = log.find('\n', begin);
            const std::size_t end = newline == std::string_view::npos ? log.size() : newline;
            std::string lineText(log.substr(begin, end - begin));
            if (!lineText.empty() && lineText.back() == '\r') lineText.pop_back();
            const auto first = lineText.find_first_not_of(" \t");
            if (first != std::string::npos)
            {
                lineText.erase(0, first);
                const auto last = lineText.find_last_not_of(" \t");
                lineText.erase(last + 1);
                const std::string lower = Lower(lineText);
                ShaderDiagnosticSeverityEXT severity = ShaderDiagnosticSeverityEXT::Error;
                if (lower.find("warning") != std::string::npos)
                    severity = ShaderDiagnosticSeverityEXT::Warning;
                else if (lower.find("info") != std::string::npos)
                    severity = ShaderDiagnosticSeverityEXT::Information;

                ShaderStageEXT stage = currentStage;
                if (lower.starts_with("vs:") || lower.find("vertex") != std::string::npos)
                    stage = ShaderStageEXT::Vertex;
                else if (lower.starts_with("fs:") || lower.find("fragment") != std::string::npos)
                    stage = ShaderStageEXT::Fragment;
                else if (lower.starts_with("cs:") || lower.find("compute") != std::string::npos)
                    stage = ShaderStageEXT::Compute;
                currentStage = stage;

                int parsedLine = 0;
                int parsedColumn = 0;
                ParseLocation(lineText, parsedLine, parsedColumn);
                result.emplace_back(
                    severity, stage, sourceLabel, parsedLine, parsedColumn,
                    std::move(lineText));
            }
            if (newline == std::string_view::npos) break;
            begin = newline + 1;
        }
        return result;
    }

    std::string ShaderCompilationExceptionEXT::summarize(
        const std::vector<ShaderDiagnosticEXT>& diagnostics)
    {
        if (diagnostics.empty())
            throw std::invalid_argument(
                "ShaderCompilationExceptionEXT: diagnostics must not be empty");
        const auto& first = diagnostics.front();
        std::string result = "Shader compilation failed ("
            + std::to_string(diagnostics.size()) + " diagnostic";
        if (diagnostics.size() != 1) result += "s";
        result += "): " + std::string(SeverityName(first.getSeverity())) + " "
            + std::string(StageName(first.getStage()));
        if (!first.getSourceLabel().empty()) result += " '" + first.getSourceLabel() + "'";
        if (first.getLine() > 0)
        {
            result += ":" + std::to_string(first.getLine());
            if (first.getColumn() > 0) result += ":" + std::to_string(first.getColumn());
        }
        result += ": " + first.getMessage();
        if (diagnostics.size() > 1)
            result += "; " + std::to_string(diagnostics.size() - 1) + " more";
        return result;
    }

    ShaderCompilationExceptionEXT::ShaderCompilationExceptionEXT(
        std::vector<ShaderDiagnosticEXT> diagnostics)
        : std::runtime_error(summarize(diagnostics))
        , diagnostics_(std::move(diagnostics))
    {
    }

    const std::vector<ShaderDiagnosticEXT>&
        ShaderCompilationExceptionEXT::getDiagnostics() const noexcept
    {
        return diagnostics_;
    }
}
