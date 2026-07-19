# Audit: include/Microsoft/Xna/Framework/Content/ContentLoadException.hpp

## Metadata
- Source file: `include/Microsoft/Xna/Framework/Content/ContentLoadException.hpp`
- Audit status: AUDITED (full read, 28 lines)
- Subsystem: `xna-content` shard
- File type: C++ header
- XNA/FNA relevance: Direct XNA type; FNA reference: `src/Content/ContentLoadException.cs`
- Main related tests: not independently located in this pass

## Purpose
Declares the exception thrown throughout the content pipeline when an asset cannot be loaded.

## Executive Verdict
Needs attention. Two confirmed findings, both relative to FNA's real three-constructor,
`Exception`-derived class: (1) the default (parameterless) constructor is missing entirely, and
(2) the base class is `std::runtime_error` rather than the project's own established
`System::Exception` (used for at least 7 other direct-`Exception`-derived XNA exception types
elsewhere in this codebase), which also means the "message + inner exception" constructor cannot
preserve the actual inner exception object — only its flattened text.

## Checklist Results

### MEDIUM: missing default (parameterless) constructor relative to FNA
FNA's real `ContentLoadException` has three constructors (`ContentLoadException.cs` lines 19-31):
default (`base()`), message-only, and message+inner. This header declares only two (lines 18, 26) —
the message-only constructor is `explicit`, and there is no parameterless constructor at all. Any
FNA-ported code path that does `throw new ContentLoadException();` (no message) has no direct C++
equivalent here; a caller must always supply at least a message string.

### MEDIUM: base class is `std::runtime_error`, not the project's established `System::Exception`
This class derives from `std::runtime_error` (line 10) rather than `System::Exception`, which is
confirmed present in sharp-runtime and is the established convention for at least 7 other
direct-`Exception`-derived XNA exception types elsewhere in this codebase (`NoMicrophoneConnectedException`,
`GamerServices::NetworkException`, `GuideAlreadyVisibleException`, `GameUpdateRequiredException`,
`GamerPrivilegeException`, `GamerServicesNotAvailableException`,
`Devices::Sensors::SensorFailedException`). `System::Exception` provides a real
`getInnerExceptionProperty()` (returning `std::exception_ptr`) that preserves the original inner
exception object — a capability `ContentLoadException` lacks entirely as a result (see the paired
`.cpp` report for the concrete consequence).

## Detailed Findings
1. **[MEDIUM] Missing default constructor relative to FNA's three-constructor shape** — declared
   lines 18, 26; cf. FNA `ContentLoadException.cs` lines 19-31.
2. **[MEDIUM] Base class is `std::runtime_error`, not the project's established `System::Exception`
   convention** — line 10; full consequence in
   `src/Microsoft/Xna/Framework/Content/ContentLoadException.cpp.audit.md`.

## Cross-File Observations
This class is the primary exception type thrown throughout `ContentManager`/`ContentReader`/
`ContentTypeReaderManager` (all audited separately in this shard) — its base-class choice affects
every one of those throw sites' ability to preserve a genuine inner-exception chain.

## Missing or Weak Tests
Not independently located in this pass. A test constructing `ContentLoadException(message, inner)`
and asserting the original `inner` exception object (not just its stringified message) is
recoverable would directly catch finding #2.

## Positive Findings
The message-only and message+inner constructors that do exist match FNA's parameter shapes exactly.

## Final Assessment
Two MEDIUM findings: a missing default constructor, and a base-class choice that loses genuine
inner-exception preservation relative to this project's own established `System::Exception`
convention.
