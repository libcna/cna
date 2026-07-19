# Audit: src/Microsoft/Xna/Framework/Content/ContentLoadException.cpp

## Metadata
- Source file: `src/Microsoft/Xna/Framework/Content/ContentLoadException.cpp`
- Audit status: AUDITED (full read, 15 lines)
- Subsystem: `xna-content` shard
- File type: C++ implementation
- XNA/FNA relevance: Direct XNA type; FNA reference: `src/Content/ContentLoadException.cs`
- Main related tests: not independently located in this pass

## Purpose
Implements the two declared constructors.

## Executive Verdict
Needs attention — confirms the header's base-class finding has a concrete, observable consequence:
the message+inner constructor flattens the inner exception into text and discards the actual
exception object.

## Checklist Results

### MEDIUM: message+inner constructor discards the actual inner exception object
Lines 11-14: `ContentLoadException(message, inner) : std::runtime_error(message + " ---> " +
inner.what()) {}`. This concatenates `inner`'s message text into the outer `.what()` string (in a
style matching .NET's own `Exception.ToString()` "---> " convention) but has no way to retain the
actual `inner` exception object, since `std::runtime_error` has no inner-exception slot at all. A
catch site can recover the combined text but never the original `inner` object — unlike FNA's real
`Exception.InnerException` property (a genuine object reference), and unlike this project's own
`System::Exception`/`ExternalException` pattern (confirmed elsewhere in this exact audit session,
e.g. `StorageDeviceNotConnectedException`, which correctly threads a `std::exception_ptr` through
to a real `getInnerExceptionProperty()`). Adopting `System::Exception` as the base class (see the
paired `.hpp` report) would directly fix this.

## Detailed Findings
1. **[MEDIUM] Message+inner constructor loses the actual inner exception object, keeping only its
   flattened text** — lines 11-14; direct consequence of the header's base-class finding.

## Cross-File Observations
Contrast with `StorageDeviceNotConnectedException` (audited earlier this session, same session's
`xna-storage` shard): that class correctly preserves `std::exception_ptr innerException` via its
`ExternalException` base, demonstrating the project already has, and elsewhere correctly uses, the
exact mechanism this file is missing.

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
The message-only constructor is a correct, simple pass-through.

## Final Assessment
One MEDIUM finding, a direct implementation-level consequence of the header's base-class choice.
