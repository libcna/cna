#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace CNA::Internal::Renderers::EasyGL
{
    /**
     * @brief Where a GLSL source's `#version <number> es` directive line is.
     *
     * plans/plan_apple_m4.md AM4-116: the ES-to-desktop header rewrite used to look for the exact
     * text "#version 300 es\n", so a source with CRLF line endings -- MojoShader writes "\r\n" in
     * a Windows build -- or with spaces inside the directive was not rewritten, and a desktop core
     * context that has no GL_ARB_ES3_compatibility (macOS) refused it. The directive is parsed the
     * way the GLSL preprocessor reads it instead.
     */
    struct GlslEsVersionLine
    {
        /** @brief Offset of the line's first character; npos when there is no such directive. */
        std::size_t begin = std::string::npos;
        /** @brief Offset just past the line's terminator (or the end of the source). */
        std::size_t end = 0;
        /** @brief The line's own terminator: "\r\n", "\n", or empty at the end of the source. */
        std::string terminator;

        /**
         * @brief Whether the directive was found.
         * @return True when the source declares this ES version.
         */
        [[nodiscard]] bool Found() const noexcept { return begin != std::string::npos; }
    };

    namespace GlslVersionDirectiveDetail
    {
        [[nodiscard]] inline std::size_t SkipBlanks(std::string_view text, std::size_t at) noexcept
        {
            while (at < text.size() && (text[at] == ' ' || text[at] == '\t')) ++at;
            return at;
        }

        [[nodiscard]] inline bool ConsumeWord(std::string_view text, std::size_t& at, std::string_view word) noexcept
        {
            if (text.substr(at, word.size()) != word) return false;
            at += word.size();
            return true;
        }
    }

    /**
     * @brief Finds the `#version <number> es` directive, allowing blanks around its tokens and either
     *        line ending.
     *
     * @param source GLSL source text.
     * @param number The version number to look for, for example "300".
     * @return The directive's line, or a result whose Found() is false.
     */
    [[nodiscard]] inline GlslEsVersionLine FindGlslEsVersionLine(const std::string& source, std::string_view number)
    {
        using namespace GlslVersionDirectiveDetail;
        const std::string_view text(source);
        std::size_t lineStart = 0;
        while (lineStart <= text.size())
        {
            const std::size_t newline = text.find('\n', lineStart);
            const std::size_t lineEnd = newline == std::string_view::npos ? text.size() : newline;
            std::size_t at = SkipBlanks(text, lineStart);
            if (at < lineEnd && text[at] == '#')
            {
                at = SkipBlanks(text, at + 1);
                if (ConsumeWord(text, at, "version"))
                {
                    const std::size_t afterKeyword = at;
                    at = SkipBlanks(text, at);
                    // The directive found is the source's version, whatever it says: only the first
                    // #version line counts.
                    if (at == afterKeyword || !ConsumeWord(text, at, number)) return {};
                    const std::size_t afterNumber = at;
                    at = SkipBlanks(text, at);
                    if (at == afterNumber || !ConsumeWord(text, at, "es")) return {};
                    at = SkipBlanks(text, at);
                    std::size_t contentEnd = lineEnd;
                    if (contentEnd > lineStart && contentEnd <= text.size() && contentEnd > at &&
                        text[contentEnd - 1] == '\r')
                        --contentEnd;
                    if (at != contentEnd) return {};
                    GlslEsVersionLine line;
                    line.begin = lineStart;
                    line.end = newline == std::string_view::npos ? text.size() : newline + 1;
                    line.terminator = std::string(text.substr(contentEnd, line.end - contentEnd));
                    return line;
                }
            }
            if (newline == std::string_view::npos) break;
            lineStart = newline + 1;
        }
        return {};
    }

    /**
     * @brief Replaces a `#version <number> es` directive with a desktop one and blanks a `precision`
     *        statement on the line right after it.
     *
     * The line terminator and the number of lines are kept, so a compiler diagnostic still names the
     * line the author wrote. A source without the directive is returned unchanged.
     *
     * @param source GLSL ES source text.
     * @param esNumber The ES version number, for example "300".
     * @param desktopDirective The replacement directive without a terminator, for example
     *        "#version 330 core".
     * @return The rewritten source.
     */
    [[nodiscard]] inline std::string RewriteGlslEsVersionToDesktop(std::string source, std::string_view esNumber,
                                                                   std::string_view desktopDirective)
    {
        const GlslEsVersionLine line = FindGlslEsVersionLine(source, esNumber);
        if (!line.Found()) return source;
        const std::string replacement = std::string(desktopDirective) + line.terminator;
        source.replace(line.begin, line.end - line.begin, replacement);
        // Desktop GLSL before 1.30 has no precision statements, and the ES ones mean nothing there;
        // the line right after the directive is blanked, its terminator kept.
        const std::size_t next = line.begin + replacement.size();
        const std::size_t content = GlslVersionDirectiveDetail::SkipBlanks(source, next);
        if (source.compare(content, 10, "precision ") == 0)
        {
            std::size_t stop = source.find('\n', content);
            if (stop == std::string::npos) stop = source.size();
            if (stop > content && source[stop - 1] == '\r') --stop;
            source.erase(next, stop - next);
        }
        return source;
    }
}
