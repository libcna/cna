# Audit: src/CNA/Logger.cpp

## Metadata

- Source file: `src/CNA/Logger.cpp`
- Audit status: AUDITED
- Subsystem: `cna-root-utilities` shard
- File type: C++ implementation
- XNA/FNA relevance: N/A — pure `CNA` namespace infrastructure (exception type, platform/OS detection,
  logging, entrypoint glue, backend/capability enums), not part of the `Microsoft::Xna` API surface
- Graphics backend relevance: foundational, consumed across the whole project
- Main related tests: see Missing or Weak Tests

## Purpose

Implements Logger: SDL_LogMessage dispatch, level/category-to-SDL-enum mapping, and the minimum-level gate.

## Executive Verdict

Needs attention — CONFIRMED HIGH-SEVERITY BUG: FATAL/ERROR/WARN log calls are silently mistagged with the wrong SDL log priority.

## Checklist Results

### Behavioral correctness / API design / C++ correctness / Memory/resource lifetime / Performance / Thread safety / Portability / Maintainability / Robustness
**Confirmed HIGH-severity bug in `ToSDLPriority()` (lines 170-191): the `FATAL`, `ERROR`, `WARN`, and `INFO` cases are ALL commented out**, with a literal `//todo` marker left in the source:
```cpp
// case LogLevel::FATAL:
//     return SDL_LOG_PRIORITY_CRITICAL;
// case LogLevel::ERROR:
//     return SDL_LOG_PRIORITY_ERROR;
// case LogLevel::WARN:
//     return SDL_LOG_PRIORITY_WARN;
// case LogLevel::INFO:
//     return SDL_LOG_PRIORITY_INFO;

//todo
case LogLevel::DEBUG:
case LogLevel::TRACE:
case LogLevel::EXPERIMENT:
    return SDL_LOG_PRIORITY_DEBUG;
default:
    return SDL_LOG_PRIORITY_INFO;
```
Because FATAL/ERROR/WARN all fall through to the `default` branch, **every `Logger::Fatal()`/`Logger::Error()`/`Logger::Warn()` call (and their `*If()` variants) is tagged with `SDL_LOG_PRIORITY_INFO` instead of `SDL_LOG_PRIORITY_CRITICAL`/`SDL_LOG_PRIORITY_ERROR`/`SDL_LOG_PRIORITY_WARN` respectively** (INFO calls happen to land correctly, coincidentally, since they also target the same default). This is not a cosmetic issue: `SetMinimumLevel()` (line 159) calls this SAME buggy function to set SDL's own native priority threshold via `SDL_SetLogPriorities()` — so `Logger::SetMinimumLevel(LogLevel::WARN)` (intending "show WARN and more severe") actually sets SDL's threshold to `SDL_LOG_PRIORITY_INFO`, not `WARN`. Any code path that relies on SDL's own native priority for filtering, coloring, or routing (e.g. a custom `SDL_SetLogOutputFunction` callback that branches on the reported `SDL_LogPriority`, or SDL's own default priority-based behavior) will see every Fatal/Error/Warn message reported as mere Info — genuinely misleading in exactly the scenario logging exists to help with (surfacing serious problems). This is clearly incomplete, abandoned work (the correct implementation is visibly commented out immediately above the bug, with an explicit `//todo`), not a deliberate design choice.

### Testing
No dedicated GTest coverage found for this specific file's own logic.

## Detailed Findings

**Confirmed HIGH-severity bug in `ToSDLPriority()` (lines 170-191): the `FATAL`, `ERROR`, `WARN`, and `INFO` cases are ALL commented out**, with a literal `//todo` marker left in the source:
```cpp
// case LogLevel::FATAL:
//     return SDL_LOG_PRIORITY_CRITICAL;
// case LogLevel::ERROR:
//     return SDL_LOG_PRIORITY_ERROR;
// case LogLevel::WARN:
//     return SDL_LOG_PRIORITY_WARN;
// case LogLevel::INFO:
//     return SDL_LOG_PRIORITY_INFO;

//todo
case LogLevel::DEBUG:
case LogLevel::TRACE:
case LogLevel::EXPERIMENT:
    return SDL_LOG_PRIORITY_DEBUG;
default:
    return SDL_LOG_PRIORITY_INFO;
```
Because FATAL/ERROR/WARN all fall through to the `default` branch, **every `Logger::Fatal()`/`Logger::Error()`/`Logger::Warn()` call (and their `*If()` variants) is tagged with `SDL_LOG_PRIORITY_INFO` instead of `SDL_LOG_PRIORITY_CRITICAL`/`SDL_LOG_PRIORITY_ERROR`/`SDL_LOG_PRIORITY_WARN` respectively** (INFO calls happen to land correctly, coincidentally, since they also target the same default). This is not a cosmetic issue: `SetMinimumLevel()` (line 159) calls this SAME buggy function to set SDL's own native priority threshold via `SDL_SetLogPriorities()` — so `Logger::SetMinimumLevel(LogLevel::WARN)` (intending "show WARN and more severe") actually sets SDL's threshold to `SDL_LOG_PRIORITY_INFO`, not `WARN`. Any code path that relies on SDL's own native priority for filtering, coloring, or routing (e.g. a custom `SDL_SetLogOutputFunction` callback that branches on the reported `SDL_LogPriority`, or SDL's own default priority-based behavior) will see every Fatal/Error/Warn message reported as mere Info — genuinely misleading in exactly the scenario logging exists to help with (surfacing serious problems). This is clearly incomplete, abandoned work (the correct implementation is visibly commented out immediately above the bug, with an explicit `//todo`), not a deliberate design choice.

## Cross-File Observations

This bug is isolated to `ToSDLPriority()`'s SDL-facing translation — `LogCategory`'s own `ToSDLCategory()` in the same file has no equivalent gap (every category is a real, uncommented case). Worth flagging in `AUDIT_CROSS_CUTTING_FINDINGS.md` given `Logger` is foundational, always-compiled (not gated behind any opt-in flag) infrastructure used project-wide.

## Missing or Weak Tests

No dedicated GTest coverage found for this specific file's own logic.

## Positive Findings

`ToSDLCategory()`/`ToString(LogLevel)`/`ToString(LogCategory)` are all complete, correct, exhaustive switches with sensible `default` fallbacks; `IsEnabled()`'s own internal level-gating (independent of the SDL-priority bug) is correct.

## Final Assessment

See findings above.
