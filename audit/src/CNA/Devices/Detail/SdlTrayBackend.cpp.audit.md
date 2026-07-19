# Audit: src/CNA/Devices/Detail/SdlTrayBackend.cpp

## Metadata

- Source file: `src/CNA/Devices/Detail/SdlTrayBackend.cpp`
- Audit status: AUDITED
- Subsystem: `cna-devices` shard
- File type: C++ implementation
- XNA/FNA relevance: N/A — all of `CNA::Devices` is a NOXNA extension gated behind the `CNA_DEVICES` CMake
  option (default OFF), independent of `CNA_NOXNA`; XNA 4.0/WP7 has no equivalent for any of this shard's
  features (camera, file dialogs, message boxes, system tray, locale, power, system info, URL launching,
  display info, clipboard)
- Graphics backend relevance: none directly (device/OS-integration subsystem)
- Main related tests: see Missing or Weak Tests

## Purpose

Implements SdlTrayBackend: SDL_CreateTray/SDL_CreateTrayMenu/SDL_InsertTrayEntryAt lifecycle and per-entry click-callback trampolining.

## Executive Verdict

Healthy — careful callback-lifetime and destruction-ordering verified correct.

## Checklist Results

### Behavioral correctness / SDL3 parity / C++ correctness / Memory/resource lifetime / Performance / Thread safety / Portability / Maintainability / Robustness
**Verified correct destruction ordering**: `Destroy()` calls `SDL_DestroyTray(tray_)` (which destroys the menu and all entries, after which SDL can no longer invoke any entry's click callback) BEFORE clearing `entries_`/`entryCallbacks_` — the callback storage the trampoline's userdata points into stays valid for exactly as long as SDL could still call back into it, no earlier destruction. Also confirmed: `entryCallbacks_`'s own vector reallocation (as more entries are added) cannot invalidate the `callback.get()` raw pointer passed to `SDL_SetTrayEntryCallback()`, since only the `unique_ptr` handles move on reallocation, not the heap-allocated `TrayEntryClickCallback` objects they point to.

### Testing
No dedicated test needed (declaration-only) or covered indirectly.

## Detailed Findings

**Verified correct destruction ordering**: `Destroy()` calls `SDL_DestroyTray(tray_)` (which destroys the menu and all entries, after which SDL can no longer invoke any entry's click callback) BEFORE clearing `entries_`/`entryCallbacks_` — the callback storage the trampoline's userdata points into stays valid for exactly as long as SDL could still call back into it, no earlier destruction. Also confirmed: `entryCallbacks_`'s own vector reallocation (as more entries are added) cannot invalidate the `callback.get()` raw pointer passed to `SDL_SetTrayEntryCallback()`, since only the `unique_ptr` handles move on reallocation, not the heap-allocated `TrayEntryClickCallback` objects they point to.

## Cross-File Observations

None.

## Missing or Weak Tests

No dedicated test needed (declaration-only) or covered indirectly.

## Positive Findings

Independently verified correct destruction ordering (tray/menu/entries destroyed before their callback storage is freed) and correct reasoning about vector-reallocation safety for long-lived raw pointers into heap-allocated callback objects.

## Final Assessment

See findings above.
