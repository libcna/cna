# Audit: include/CNA/Devices/Clipboard.hpp

## Metadata

- Source file: `include/CNA/Devices/Clipboard.hpp`
- Audit status: AUDITED
- Subsystem: `cna-devices` shard
- File type: C++ header
- XNA/FNA relevance: N/A — all of `CNA::Devices` is a NOXNA extension gated behind the `CNA_DEVICES` CMake
  option (default OFF), independent of `CNA_NOXNA`; XNA 4.0/WP7 has no equivalent for any of this shard's
  features (camera, file dialogs, message boxes, system tray, locale, power, system info, URL launching,
  display info, clipboard)
- Graphics backend relevance: none directly (device/OS-integration subsystem)
- Main related tests: see Missing or Weak Tests

## Purpose

Declares CNA::Devices::Clipboard: system clipboard text access, backed by SDL3.

## Executive Verdict

Needs attention — confirmed duplicate of CNA::Input::Clipboard, though individually correct.

## Checklist Results

### Behavioral correctness / SDL3 parity / C++ correctness / Memory/resource lifetime / Performance / Thread safety / Portability / Maintainability / Robustness
**Confirmed: this is a fully independent, redundant duplicate of `CNA::Input::Clipboard`** (already audited in the `cna-input` shard) — both wrap the identical `SDL_GetClipboardText`/`SDL_SetClipboardText`/`SDL_HasClipboardText` calls, gated behind 2 different, independent CMake options (`CNA_DEVICES` here vs. always-compiled for `CNA::Input`), with different naming conventions (`getTextProperty()`/`setTextProperty()`/`getHasTextProperty()` here vs. `GetTextEXT()`/`SetTextEXT()`/`HasTextEXT()` there) and one minor behavioral difference (`setTextProperty` returns a success `bool`; `Input::SetTextEXT` is `void`). See `AUDIT_CROSS_CUTTING_FINDINGS.md` for the full writeup.

### Testing
Has dedicated tests: `tests/CNA/Devices/ClipboardTests.cpp`.

## Detailed Findings

**Confirmed: this is a fully independent, redundant duplicate of `CNA::Input::Clipboard`** (already audited in the `cna-input` shard) — both wrap the identical `SDL_GetClipboardText`/`SDL_SetClipboardText`/`SDL_HasClipboardText` calls, gated behind 2 different, independent CMake options (`CNA_DEVICES` here vs. always-compiled for `CNA::Input`), with different naming conventions (`getTextProperty()`/`setTextProperty()`/`getHasTextProperty()` here vs. `GetTextEXT()`/`SetTextEXT()`/`HasTextEXT()` there) and one minor behavioral difference (`setTextProperty` returns a success `bool`; `Input::SetTextEXT` is `void`). See `AUDIT_CROSS_CUTTING_FINDINGS.md` for the full writeup.

## Cross-File Observations

Duplicate of `include/CNA/Input/Clipboard.hpp` (`cna-input` shard) — see `AUDIT_CROSS_CUTTING_FINDINGS.md`'s dedicated section on this pattern.

## Missing or Weak Tests

Has dedicated tests: `tests/CNA/Devices/ClipboardTests.cpp`.

## Positive Findings

Clean, correct, well-documented CNA::Devices extension type.

## Final Assessment

See findings above.
