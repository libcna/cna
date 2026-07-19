# Audit: include/Microsoft/Xna/Framework/GamerServices/GuideAlreadyVisibleException.hpp

## Metadata
- Source file: `include/Microsoft/Xna/Framework/GamerServices/GuideAlreadyVisibleException.hpp`
- Audit status: AUDITED (full read, 48 lines)
- Subsystem: `xna-gamerservices` shard
- File type: C++ header
- XNA/FNA relevance: Direct XNA type; FNA has no reference material for this namespace
- Main related tests: `tests/Microsoft/Xna/Framework/GamerServices/GamerServicesExceptionsTests.cpp`
  (`GuideAlreadyVisibleExceptionTest.DefaultCtor`/`MessageCtor`/`MessageAndInnerCtor`)

## Purpose
The exception real XNA 4.0 documents as thrown when a Guide UI element (message box, keyboard
input, etc.) is requested while one is already visible.

## Executive Verdict
The type itself is correct and complete: derives from `System::Exception` (matching real XNA's
`GuideAlreadyVisibleException : Exception`), has the standard default/message/message+inner
constructor set plus a protected serialization constructor. However — see the paired `Guide.hpp`
report — this type is never actually thrown anywhere in this codebase's production code; `Guide`'s
own "already pending" guards throw a generic `InvalidOperationException` instead. This file itself
has no defect; the gap is in `Guide.cpp`'s usage of it (or rather, non-usage).

## Checklist Results
- Doxygen coverage: complete.
- Constructor set matches the standard real-XNA custom-exception shape (default, message, message+
  inner, protected serialization) exactly, consistent with sibling exception types audited in this
  and the `xna-net` shard (e.g. `NetworkSessionJoinException`).

## Detailed Findings
None in this file itself. See `Guide.hpp`'s audit report for the MEDIUM finding: this type is
declared and tested but dead code in production.

## Cross-File Observations
See `Guide.hpp`/`Guide.cpp`'s audit reports.

## Missing or Weak Tests
Tests exist for the type's own constructors but not for the (non-existent) production code path
that should throw it — see `Guide.hpp`'s finding.

## Positive Findings
Correct, complete exception-type shape.

## Final Assessment
No findings against this file directly; see `Guide.hpp` for the related MEDIUM finding about this
type's non-usage.
