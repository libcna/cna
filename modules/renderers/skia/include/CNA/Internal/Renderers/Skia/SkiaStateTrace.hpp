#pragma once

#include <cstdarg>
#include <cstdlib>
#include <cstdio>

namespace CNA::Internal::Renderers::Skia
{
    /// Emits opt-in renderer state diagnostics without changing any rendering state.  This remains
    /// intentionally environment-driven so normal applications and CTests have no trace output.
    [[nodiscard]] inline bool IsSkiaStateTraceEnabled() noexcept
    {
        const char* value = std::getenv("CNA_SKIA_STATE_TRACE");
        return value != nullptr && value[0] != '\0' && !(value[0] == '0' && value[1] == '\0');
    }

    inline void TraceSkiaState(const char* format, ...)
    {
        if (!IsSkiaStateTraceEnabled())
            return;

        std::fputs("[SkiaState] ", stderr);
        va_list arguments;
        va_start(arguments, format);
        std::vfprintf(stderr, format, arguments);
        va_end(arguments);
        std::fputc('\n', stderr);
    }
} // namespace CNA::Internal::Renderers::Skia
