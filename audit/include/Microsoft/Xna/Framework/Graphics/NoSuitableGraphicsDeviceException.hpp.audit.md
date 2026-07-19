# Audit: include/Microsoft/Xna/Framework/Graphics/NoSuitableGraphicsDeviceException.hpp

## Metadata
- Source file: `include/Microsoft/Xna/Framework/Graphics/NoSuitableGraphicsDeviceException.hpp` (22 lines)
- Audit status: AUDITED (full read)
- Subsystem: `xna-graphics` shard
- File type: C++ header
- XNA/FNA relevance: Direct XNA type; FNA reference: `src/Graphics/NoSuitableGraphicsDeviceException.cs`
- Main related tests: not independently located in this pass

## Purpose
The exception thrown when no suitable graphics device can be found or created.

## Executive Verdict
Same MEDIUM finding as the sibling `DeviceLostException`/`DeviceNotResetException` in this same
batch.

## Checklist Results
No Doxygen or NOXNA issues; the gap is structural.

## Detailed Findings

### MEDIUM — derives from `std::runtime_error`, not `System::Exception`; missing the
`(message, innerException)` constructor
Real FNA's `NoSuitableGraphicsDeviceException` (`NoSuitableGraphicsDeviceException.cs`) is `public
sealed class NoSuitableGraphicsDeviceException : Exception` with the standard three constructors.
This port derives from `std::runtime_error` with only two — see `DeviceLostException.hpp`'s audit
report (this same batch) for the full analysis, identical here. Notably, real FNA also actually
*throws* this specific type in at least one real code path (`DrawInstancedPrimitives`, when
hardware instancing isn't supported — confirmed in `GraphicsDevice.cs` line 1269) — this port's own
`GraphicsDevice.cpp` was not confirmed in this pass to construct/throw this exact type anywhere
(worth a follow-up grep when instancing support is directly audited).

## Cross-File Observations
See `DeviceLostException.hpp`'s report — all three of this group's exception types share the
identical gap.

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
The default-message text and `(message)` constructor are correctly preserved.

## Final Assessment
One MEDIUM finding, shared with two sibling exception types in this same batch.
