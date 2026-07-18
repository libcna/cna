# Audit: include/CNA/CNAHelper.hpp

## Metadata

- Source file: `include/CNA/CNAHelper.hpp`
- Audit status: AUDITED
- Subsystem: `cna-root-utilities` shard
- File type: C++ header
- XNA/FNA relevance: N/A — pure `CNA` namespace infrastructure (exception type, platform/OS detection,
  logging, entrypoint glue, backend/capability enums), not part of the `Microsoft::Xna` API surface
- Graphics backend relevance: foundational, consumed across the whole project
- Main related tests: see Missing or Weak Tests

## Purpose

Declares the NOXNA marker macro used pervasively across the entire codebase to tag non-XNA API extensions, per CLAUDE.md's own convention.

## Executive Verdict

Needs attention — 1 confirmed maintainability finding; the macro's own logic is correct and its CNA_STRICT_XNA_API enforcement mechanism is confirmed real (not aspirational).

## Checklist Results

### Behavioral correctness / API design / C++ correctness / Memory/resource lifetime / Performance / Thread safety / Portability / Maintainability / Robustness
**Confirmed: this is the ONLY file in the entire `include/CNA/` directory using an old-style manual include guard instead of `#pragma once`** (`#ifndef WINDOWSPHONESPEEDYBLUPI_CNAHELPER_HPP` / `#define WINDOWSPHONESPEEDYBLUPI_CNAHELPER_HPP`) — confirmed via directory-wide grep, every one of the other 14 files in this shard (and, per spot-checks elsewhere in this audit, effectively the whole codebase) uses `#pragma once`. The guard's own name, "WINDOWSPHONESPEEDYBLUPI", bears no relation to this project (CNA/XNA) at all — it strongly suggests this file's skeleton was copy-pasted from an unrelated old/demo project template (a "Windows Phone" + "Blupi" game, going by the name) and never renamed. Not a functional bug (still a unique, working guard), but a genuine, isolated maintainability/cleanliness inconsistency in a widely-depended-upon foundational file. **Confirmed NOT documentation-rot**: the `CNA_STRICT_XNA_API`/`cna_strict_xna_api_check` enforcement mechanism this file's own comment describes is real, working infrastructure — `tools/devices/StrictXnaApiSurfaceCheck.cpp` and the `cna_strict_xna_api_check`/`cna_strict_xna_api_leak_check` CMake targets both genuinely exist (`cmake/Harnesses.cmake`).

### Testing
No dedicated GTest coverage found for this specific file's own logic.

## Detailed Findings

**Confirmed: this is the ONLY file in the entire `include/CNA/` directory using an old-style manual include guard instead of `#pragma once`** (`#ifndef WINDOWSPHONESPEEDYBLUPI_CNAHELPER_HPP` / `#define WINDOWSPHONESPEEDYBLUPI_CNAHELPER_HPP`) — confirmed via directory-wide grep, every one of the other 14 files in this shard (and, per spot-checks elsewhere in this audit, effectively the whole codebase) uses `#pragma once`. The guard's own name, "WINDOWSPHONESPEEDYBLUPI", bears no relation to this project (CNA/XNA) at all — it strongly suggests this file's skeleton was copy-pasted from an unrelated old/demo project template (a "Windows Phone" + "Blupi" game, going by the name) and never renamed. Not a functional bug (still a unique, working guard), but a genuine, isolated maintainability/cleanliness inconsistency in a widely-depended-upon foundational file. **Confirmed NOT documentation-rot**: the `CNA_STRICT_XNA_API`/`cna_strict_xna_api_check` enforcement mechanism this file's own comment describes is real, working infrastructure — `tools/devices/StrictXnaApiSurfaceCheck.cpp` and the `cna_strict_xna_api_check`/`cna_strict_xna_api_leak_check` CMake targets both genuinely exist (`cmake/Harnesses.cmake`).

## Cross-File Observations

None.

## Missing or Weak Tests

No dedicated GTest coverage found for this specific file's own logic.

## Positive Findings

The `NOXNA` marker macro's dual-mode design (no-op normally, `[[deprecated]]` under `CNA_STRICT_XNA_API` + `-Werror=deprecated-declarations`) is a genuinely clever, verified-working compile-time API-surface enforcement mechanism — not just documentation.

## Final Assessment

See findings above.
