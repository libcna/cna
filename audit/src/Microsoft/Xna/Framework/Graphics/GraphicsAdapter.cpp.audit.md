# Audit: src/Microsoft/Xna/Framework/Graphics/GraphicsAdapter.cpp

## Metadata
- Source file: `src/Microsoft/Xna/Framework/Graphics/GraphicsAdapter.cpp` (453 lines)
- Audit status: AUDITED (full read, diffed against FNA line-by-line for every real/stub
  distinction)
- Subsystem: `xna-graphics` shard
- File type: C++ implementation
- XNA/FNA relevance: Direct XNA type; FNA reference: `src/Graphics/GraphicsAdapter.cs`
- Main related tests: not independently located in this pass

## Purpose
Implements adapter enumeration (via SDL3 display APIs and, on Linux, sysfs PCI ID queries),
display-mode queries, and format/profile capability queries (with a real, hardware-backed path on
the D3D9 backend and an honest fallback on the other nine).

## Executive Verdict
Correct, and confirms every claimed real-vs-stub distinction the header documents. Several methods
are genuine, deliberate enhancements beyond FNA's own acknowledged stubs; the rest faithfully
preserve FNA's real (including deliberately quirky) behavior.

## Checklist Results

### Confirmed genuine enhancements over FNA's own stubs
- `DeviceId`/`Revision`/`SubSystemId`/`VendorId`: FNA's real getters are unconditional
  `throw new NotImplementedException();` (confirmed by direct read of `GraphicsAdapter.cs` lines
  43-49, 95-109, 123-129). This port instead queries real PCI vendor/device IDs via
  `/sys/class/drm/cardN/device/{vendor,device}` on Linux (falling back to 0 elsewhere or on
  failure), and returns a hardcoded 0 for `Revision`/`SubSystemId` with an honest "not queryable
  via SDL" comment rather than throwing.
- `IsProfileSupported`: FNA's real implementation is unconditional `return true;` (its own comment:
  "TODO: This method could be genuinely useful! ... -flibit"). This port has a real,
  hardware-capability-backed check on the D3D9 backend (`MeetsHiDefFloorEXT(QueryAdapterCapsEXT())`
  for `HiDef`; `Reach` always passes, correctly reasoned as having no real floor worth checking) and
  an honest, explicitly-commented `return true;` fallback on the other nine backends — the comment
  correctly explains why (no `D3DCAPS9` equivalent exists to consult; a hardcoded table would be
  "pretending to be a capability query," which this project's own `plans/plan_dx9.md` explicitly refuses).
- `QueryRenderTargetFormat`: on D3D9, a real two-part check (profile-format whitelist AND real
  `IDirect3D9::CheckDeviceFormat`-backed hardware support), correctly falling back to `Color` if
  either fails — matching XNA's own documented fallback contract. The non-D3D9 fallback
  (`isSupportedRenderTargetFormat`) reproduces FNA's own hardcoded OpenGL-3.0-spec format whitelist
  exactly (11 formats, confirmed identical list against `GraphicsAdapter.cs` lines 208-218).

### Confirmed faithful preservation of FNA's real (quirky) behavior
- `QueryBackBufferFormat`'s non-D3D9 path unconditionally sets `selectedFormat = SurfaceFormat::Color`
  regardless of the requested format — this is not a stub-simplification bug, it is FNA's own real,
  intentionally-quirky behavior (`GraphicsAdapter.cs` line 243: `selectedFormat =
  SurfaceFormat.Color; // Seriously?` — the maintainer's own sarcastic comment about the hardcoded
  downgrade). Correctly preserved as-is.
- `AdaptersChanged()`'s device-name/description convention (synthetic Windows-style
  `\\.\DISPLAY{n}` device name, real SDL display name as description) is explicitly commented as
  matching FNA's own `SDL3_FNAPlatform.GetGraphicsAdapters()` convention, kept even on non-Windows
  platforms — a deliberate, disclosed real-XNA-convention preservation, not an oversight.
- `queryDisplayModes()`'s reverse-iteration-with-width/height-dedup matches FNA's own documented
  approach for skipping refresh-rate duplicates at the same resolution.

## Detailed Findings
None. One LOW-severity note: `getDefaultAdapterProperty()` throws a raw `std::runtime_error` if
`adapters_` is empty (line 81) — inconsistent with this project's `System::` exception convention,
but this branch is realistically unreachable: `AdaptersChanged()` (the only path that populates
`adapters_`) always pushes at least one synthetic default adapter even when SDL reports zero real
displays (lines 339-355), so `adapters_` can never actually be empty once initialized. Effectively
dead defensive code, not a live behavioral gap — not worth a MEDIUM rating.

## Cross-File Observations
`getCurrentDisplayModeProperty()`/`getMonitorHandleProperty()` use a stored `displayIndex_` set at
construction rather than FNA's own `Adapters.IndexOf(this)` re-derivation
(`GraphicsAdapter.cs` lines 25-27, 89-91) — a reasonable O(1) optimization with no behavioral
divergence, since `AdaptersChanged()` constructs adapters in the same order as their final position
in `adapters_` (confirmed: `for (int i = 0; i < count; ++i) { ... new GraphicsAdapter(i, ...) ... }`).

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
Every deviation from a literal byte-for-byte FNA port — whether a genuine enhancement or a
preserved quirk — is confirmed correct against the real FNA source, and the D3D9-vs-other-backends
split for `IsProfileSupported`/`QueryRenderTargetFormat` is consistently and honestly justified
rather than silently applied to some backends and not others.

## Final Assessment
No findings beyond one LOW/dead-code exception-type note.
