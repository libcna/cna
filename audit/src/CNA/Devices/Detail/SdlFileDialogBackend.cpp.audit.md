# Audit: src/CNA/Devices/Detail/SdlFileDialogBackend.cpp

## Metadata

- Source file: `src/CNA/Devices/Detail/SdlFileDialogBackend.cpp`
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

Implements SdlFileDialogBackend: builds SDL_DialogFileFilter arrays and dispatches SDL_ShowOpen/SaveFileDialog/SDL_ShowOpenFolderDialog with a one-shot result trampoline.

## Executive Verdict

Healthy — genuinely careful pointer-stability and one-shot-ownership analysis, independently verified correct.

## Checklist Results

### Behavioral correctness / SDL3 parity / C++ correctness / Memory/resource lifetime / Performance / Thread safety / Portability / Maintainability / Robustness
**Verified correct, non-trivial pointer-stability handling**: `DialogContext::FilterNames`/`FilterPatterns` are `reserve()`d to their final size BEFORE any `SdlFilters` entry takes a `.c_str()` pointer into them — correct, since a `std::string`'s small-buffer/heap storage could otherwise move out from under an already-taken pointer if the vector reallocated mid-loop. SDL3's own doc requirement that `filters` remain valid until the async callback fires (which happens after `Show*Dialog()` itself returns) is satisfied by heap-allocating the whole `DialogContext` and releasing ownership via `context.release()` into the SDL userdata slot, reconstructed via `std::unique_ptr<DialogContext> context(static_cast<DialogContext*>(userdata))` in `ResultTrampoline()` — correctly relies on SDL3's documented one-shot callback guarantee (not a leak or double-free risk, as the comment itself correctly reasons). Correctly passes `nullptr` (not a dangling/empty pointer) for empty filters/default-location cases.

### Testing
No dedicated test needed (declaration-only) or covered indirectly.

## Detailed Findings

**Verified correct, non-trivial pointer-stability handling**: `DialogContext::FilterNames`/`FilterPatterns` are `reserve()`d to their final size BEFORE any `SdlFilters` entry takes a `.c_str()` pointer into them — correct, since a `std::string`'s small-buffer/heap storage could otherwise move out from under an already-taken pointer if the vector reallocated mid-loop. SDL3's own doc requirement that `filters` remain valid until the async callback fires (which happens after `Show*Dialog()` itself returns) is satisfied by heap-allocating the whole `DialogContext` and releasing ownership via `context.release()` into the SDL userdata slot, reconstructed via `std::unique_ptr<DialogContext> context(static_cast<DialogContext*>(userdata))` in `ResultTrampoline()` — correctly relies on SDL3's documented one-shot callback guarantee (not a leak or double-free risk, as the comment itself correctly reasons). Correctly passes `nullptr` (not a dangling/empty pointer) for empty filters/default-location cases.

## Cross-File Observations

None.

## Missing or Weak Tests

No dedicated test needed (declaration-only) or covered indirectly.

## Positive Findings

Independently verified correct, non-obvious pointer-stability reasoning (reserve-before-pointer) and one-shot heap-ownership transfer for the async SDL callback contract.

## Final Assessment

See findings above.
