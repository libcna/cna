# Audit: src/Microsoft/Xna/Framework/Storage/StorageDeviceNotConnectedException.cpp

## Metadata
- Source file: `src/Microsoft/Xna/Framework/Storage/StorageDeviceNotConnectedException.cpp`
- Audit status: AUDITED (full read, 20 lines)
- Subsystem: `xna-storage` shard
- File type: C++ implementation
- XNA/FNA relevance: Direct XNA type; FNA reference:
  `src/Storage/StorageDeviceNotConnectedException.cs`
- Main related tests: not independently located in this pass

## Purpose
Implements the three constructors declared in the paired header.

## Executive Verdict
Minor issue only. The message-and-inner-exception and message-only constructors are exact
pass-throughs to the base class, matching FNA precisely. The default (no-argument) constructor
supplies a specific, descriptive message instead of FNA's generic base-class default.

## Checklist Results

### LOW: default constructor's message diverges from FNA's generic base-class default
Lines 7-9: `StorageDeviceNotConnectedException() : ExternalException("The storage device bound to
the container is not connected.") {}`. FNA's real default constructor (`StorageDeviceNotConnectedException.cs`
lines 19-22) calls `base()` with no arguments, which yields whatever generic default message
`ExternalException`'s own parameterless constructor supplies (a COM-interop-style generic message,
not this specific storage-related text). CNA instead supplies the same descriptive message used
elsewhere in the codebase for this exception (matching `StorageDevice.cpp`'s own two throw sites for
`FreeSpace`/`TotalSpace`). This is arguably a **more helpful** default for any code that surfaces
`.what()`/`.Message` directly to a user or log, but it is an observable behavioral difference from
FNA's actual default-constructor output, and isn't disclosed as intentional anywhere in the file.

## Detailed Findings
1. **[LOW] Default constructor's message text differs from FNA's generic base-class default,
   undisclosed** — lines 7-9; cf. FNA `StorageDeviceNotConnectedException.cs` lines 19-22.

## Cross-File Observations
- The message text used here exactly matches the two throw sites in `StorageDevice.cpp`
  (`getFreeSpaceProperty()`/`getTotalSpaceProperty()`), so the deviation is at least internally
  consistent across the shard.
- See `include/.../StorageDeviceNotConnectedException.hpp.audit.md` for the consolidated
  intra-pair SPDX license-header note (this file uses `MIT` + a copyright line; its own header
  uses `MS-PL`).

## Missing or Weak Tests
Not independently located in this pass. A test asserting the default constructor's message text
would document whether this deviation is intentional.

## Positive Findings
The message-and-inner-exception and message-only constructors are exact, correct pass-throughs to
the base class.

## Final Assessment
One LOW finding (undisclosed default-message deviation from FNA).
