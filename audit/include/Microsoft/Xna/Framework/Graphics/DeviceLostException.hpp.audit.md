# Audit: include/Microsoft/Xna/Framework/Graphics/DeviceLostException.hpp

## Metadata
- Source file: `include/Microsoft/Xna/Framework/Graphics/DeviceLostException.hpp` (22 lines)
- Audit status: AUDITED (full read)
- Subsystem: `xna-graphics` shard
- File type: C++ header
- XNA/FNA relevance: Direct XNA type; FNA reference: `src/Graphics/DeviceLostException.cs`
- Main related tests: not independently located in this pass

## Purpose
The exception thrown when the graphics device is lost.

## Executive Verdict
Two real gaps relative to both FNA's actual type and this project's own established exception
convention: derives from `std::runtime_error` instead of this project's own `System::Exception`
hierarchy, and is missing the `(message, innerException)` constructor overload FNA's real type has.

## Checklist Results
No Doxygen or NOXNA issues; the gaps are structural.

## Detailed Findings

### MEDIUM — derives from `std::runtime_error`, not `System::Exception`; missing the
`(message, innerException)` constructor
Real FNA's `DeviceLostException` (`DeviceLostException.cs`) is `public sealed class
DeviceLostException : Exception` with three constructors: default, `(string message)`, and
`(string message, Exception inner)`. This port derives from `std::runtime_error` and has only two
constructors (default, `(const std::string& message)`) — the inner-exception-preserving overload is
entirely absent. This is the same class of gap this audit previously found in
`ContentLoadException` (`xna-content` shard, MEDIUM) and is consistent with this codebase's
established convention elsewhere of custom exceptions deriving from `System::Exception` (e.g.
`NetworkException`, `GamerServicesNotAvailableException`, both audited in sibling shards this
session) — a caller catching `System::Exception&` (the idiomatic base for this codebase's own
exception hierarchy) would not catch a `DeviceLostException` thrown here, and no inner exception
can ever be attached/preserved through this type.

## Cross-File Observations
Shares the identical gap with `DeviceNotResetException`/`NoSuitableGraphicsDeviceException`
(audited in this same batch) — all three FNA sibling types (`DeviceLostException.cs`,
`DeviceNotResetException.cs`, `NoSuitableGraphicsDeviceException.cs`) have the identical
three-constructor `: Exception` shape, and all three CNA ports share the identical
`std::runtime_error`-based, two-constructor gap.

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
The default-message text and `(message)` constructor are correctly preserved.

## Final Assessment
One MEDIUM finding, shared with two sibling exception types in this same batch.
