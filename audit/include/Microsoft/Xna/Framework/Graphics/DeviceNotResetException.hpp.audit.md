# Audit: include/Microsoft/Xna/Framework/Graphics/DeviceNotResetException.hpp

## Metadata
- Source file: `include/Microsoft/Xna/Framework/Graphics/DeviceNotResetException.hpp` (22 lines)
- Audit status: AUDITED (full read)
- Subsystem: `xna-graphics` shard
- File type: C++ header
- XNA/FNA relevance: Direct XNA type; FNA reference: `src/Graphics/DeviceNotResetException.cs`
- Main related tests: not independently located in this pass

## Purpose
The exception thrown when a draw call is attempted while the graphics device has not been reset.

## Executive Verdict
Same MEDIUM finding as the sibling `DeviceLostException` in this same batch.

## Checklist Results
No Doxygen or NOXNA issues; the gap is structural.

## Detailed Findings

### MEDIUM — derives from `std::runtime_error`, not `System::Exception`; missing the
`(message, innerException)` constructor
Real FNA's `DeviceNotResetException` (`DeviceNotResetException.cs`) is `public sealed class
DeviceNotResetException : Exception` with the standard three constructors (default, `(message)`,
`(message, inner)`). This port derives from `std::runtime_error` with only two constructors — see
`DeviceLostException.hpp`'s audit report (this same batch) for the full analysis, identical here.

## Cross-File Observations
See `DeviceLostException.hpp`'s report — all three of this group's exception types
(`DeviceLostException`, `DeviceNotResetException`, `NoSuitableGraphicsDeviceException`) share the
identical gap.

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
The default-message text and `(message)` constructor are correctly preserved.

## Final Assessment
One MEDIUM finding, shared with two sibling exception types in this same batch.
