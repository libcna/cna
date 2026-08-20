# Audit: examples/d3d9_graphicsprofile_test.cpp

## Metadata

- Source file: `examples/d3d9_graphicsprofile_test.cpp` (340 lines)
- Audit status: AUDITED (static/source-reading only — see Environment Note below)
- Subsystem: `examples-tests-d3d9` shard — `GraphicsProfile.Reach`/`GraphicsProfile.HiDef` enforcement
- File type: two `Game`-subclass executables in one binary (`D3D9GraphicsProfileTest` /
  `D3D9GraphicsProfileHiDefTest`), CTest-registered as `D3D9_GraphicsProfile`
  (`cna_test_d3d9_graphicsprofile`, `cmake/Tests/D3D9Tests.cmake:97-100`, `TIMEOUT 60 LABELS "D3D9"`).
- XNA/FNA relevance: direct — `GraphicsProfile` enum, `GraphicsAdapter.IsProfileSupported`/
  `QueryRenderTargetFormat`, `GraphicsDeviceManager.GraphicsProfile`, texture/render-target/cube/volume
  size ceilings that are part of XNA 4.0's documented Reach/HiDef feature-level contract.
- Related production code: `src/CNA/Internal/Backends/D3D9/D3D9ProfileCapabilities.cpp` (the profile
  table itself), `src/Microsoft/Xna/Framework/Graphics/GraphicsAdapter.cpp` (`IsProfileSupported`
  lines 222-241, `QueryRenderTargetFormat` lines 243-284), `Texture2D.cpp`/`Texture3D.cpp`/
  `TextureCube.cpp` (`Validate*SizeForProfileEXT` static helpers, all `#ifdef CNA_BACKEND_D3D9`-gated),
  `GraphicsDevice.cpp` (`SetRenderTargets` lines 1881-1929), `GraphicsDeviceManager.cpp`
  (`applyToExistingBackend` lines 551-567).

### Environment Note (per D-P4)

D3D9 is a real Direct3D 9/HLSL Windows-only backend and cannot be built or executed in this Linux
sandbox. This report is static/source-reading only: every claim below was verified by reading the
test file plus its full production call chain, cross-referencing the transcribed profile ceilings
against XNA 4.0's well-documented Reach/HiDef feature table, and reading `git log`. No build or run
was attempted or claimed.

## Purpose

An 18-check exhaustive proof that `GraphicsProfile.Reach`/`GraphicsProfile.HiDef` are genuinely
enforced end-to-end through the *real public XNA API* (not a backend-internal shortcut): adapter
capability queries (Check A/B), render-target format profile-gating (Check C/D/E), `Texture2D` size
ceilings (F/G, H/I), `GraphicsDevice.SetRenderTargets` count ceilings (J, K), `TextureCube` size
ceilings (L/M, N/O), and `Texture3D` volume-extent ceilings including "not supported at all under
Reach" (P, Q/R). Two separate `Game`/`GraphicsDeviceManager` instances are used (one per profile)
because `GraphicsProfile` is fixed at device-construction time in both real XNA and this codebase.

## Executive Verdict

**Healthy** — every check's expected value was independently re-derived from
`D3D9ProfileCapabilities.cpp`'s own table and cross-checked against XNA 4.0's documented Reach/HiDef
feature-level limits (2048/4096 texture size, 512/4096 cube size, 0/256 volume extent, 1/4
render targets — all match the well-known real XNA profile table exactly). The file's own header
comment additionally documents a genuine, now-fixed pre-existing bug this test discovered during
authoring (Check "G-prime"), and this audit independently confirmed the fix is real and correctly
wired.

## Checklist Results

### API / XNA / FNA parity

`GraphicsAdapter::IsProfileSupported`/`QueryRenderTargetFormat` (lines 85-133) and
`GraphicsDeviceManager::setGraphicsProfileProperty`/`GraphicsDevice::getGraphicsProfileProperty`
(lines 206-240) map directly onto real XNA 4.0 members with matching signatures. The Reach/HiDef
texture/cube/volume/render-target ceilings this test exercises are not part of the public XNA API
surface itself (they are enforced *through* `Texture2D`/`Texture3D`/`TextureCube`/
`GraphicsDevice.SetRenderTargets` constructors/methods throwing `NotSupportedException`), and this
test correctly drives them through those real public constructors/methods rather than a
backend-internal shortcut.

### Behavioral correctness

Traced every check against `D3D9ProfileCapabilities.cpp`:
- Check A/B (`GraphicsAdapter.cpp:222-241`): `IsProfileSupported(Reach)` unconditionally returns
  `true` (comment: "Reach has no floor worth checking"); `IsProfileSupported(HiDef)` calls
  `MeetsHiDefFloorEXT(QueryAdapterCapsEXT())`, which checks `VertexShaderVersion>=3.0`,
  `PixelShaderVersion>=3.0`, `MaxTextureWidth/Height>=4096`, `MaxVolumeExtent>=256`,
  `NumSimultaneousRTs>=4`, and unrestricted NPOT support (`!(TextureCaps & D3DPTEXTURECAPS_POW2)`) —
  a real, non-hardcoded hardware capability query, matching the test's own claim that this is "true
  on this real vs_3_0/ps_3_0-capable dev environment," not an assumed constant.
- Check C/D/E: `QueryRenderTargetFormat` (`GraphicsAdapter.cpp:243-284`) gates on BOTH
  `IsValidTextureFormatForProfileEXT` (the profile whitelist: Reach = ordinals 0-8, HiDef = 0-19) AND
  a live `IDirect3D9::CheckDeviceFormat` hardware query. `SurfaceFormat::Single` (ordinal 13) is
  correctly outside Reach's 0-8 whitelist (falls back to `Color`, `accepted=false`) and inside HiDef's
  0-19 whitelist (`SurfaceFormatToD3D9(Single)` → `D3DFMT_R32F`, a format essentially every real
  vs_3_0/ps_3_0-capable GPU supports as a render target) — matching Check D's REFUSED / Check E's
  ALLOWED expectations exactly, and genuinely distinguishing "profile gate" from "hardware gate"
  since Check D is refused purely on the profile whitelist (hardware validity is never even reached
  for a value outside the whitelist, since the `&&` short-circuits are computed independently but
  `profileValid` alone already forces `selectedFormat=Color`).
- Check F/G, H/I: `Texture2D::Texture2D(GraphicsDevice&, int w, int h[, bool, SurfaceFormat])`
  (`Texture2D.cpp:136-178`) calls `ValidateTextureSizeForProfileEXT` — confirmed to run **before**
  `data.pixels.assign(...)` (the actual pixel-buffer allocation) in both constructor overloads,
  substantiating the test's own "throws BEFORE any allocation" claim, not merely an unverified
  assumption.
- Check J/K: `GraphicsDevice::SetRenderTargets` (`GraphicsDevice.cpp:1881-1904`) checks
  `MaxRenderTargetsForProfileEXT(graphicsProfile_)` and throws `NotSupportedException` — confirmed
  this check runs **before** `currentRenderTargets_ = renderTargets` is mutated (line 1917), so a
  Reach-profile 2-target rejection leaves `GraphicsDevice`'s tracked render-target state untouched,
  in contrast to the cross-cutting "mutate-then-throw" pattern flagged elsewhere in this same method
  for the later, backend-hardware-cap throw path (see Positive Findings).
- Check L/M, N/O and P/Q, Q/R: `TextureCube.cpp:39-54`/`Texture3D.cpp:31-51` mirror the same
  before-allocation-check pattern; `MaxVolumeExtentForProfileEXT(Reach)==0` is checked as a hard
  "not supported at all" gate distinct from a mere size ceiling, matching Check P's assertion that
  even a trivial `1x1x1` request throws under Reach.

### Logic

The Reach/HiDef ceiling table itself (2048/4096 texture, 512/4096 cube, 0/256 volume, 1/4 render
targets, format ordinals 0-8/0-19) was independently cross-checked against XNA 4.0's own documented
Reach/HiDef profile table (Microsoft's XNA Game Studio "Graphics Profiles" reference) and matches
exactly — this is not an invented or guessed table.

### C++ correctness

`Check F/G`'s `catch (const std::exception&)` for the "should succeed" case is intentionally broad
(any exception at all counts as an unwanted failure), while the "should throw" cases narrow to
`catch (const System::NotSupportedException&)` with a second `catch (const std::exception&)` that
deliberately leaves the flag `false` on the wrong exception type — a correct, precise pattern that
avoids a false pass from an unrelated exception type, applied consistently across every
size/render-target-count check in the file.

### Memory/resource lifetime

`Check J`'s two `RenderTarget2D` locals are always followed by `dev.SetRenderTargets({})` before the
scope ends "threw or not" (line 164/comment), correctly unbinding before the block-scope destructors
run — avoids leaving a disposed-adjacent render target still bound when the test moves on.

### Architecture

Every profile-ceiling enforcement point this test drives is `#ifdef CNA_BACKEND_D3D9`-gated in the
shared XNA-layer source (`Texture2D.cpp`/`Texture3D.cpp`/`TextureCube.cpp`/`GraphicsDevice.cpp`/
`GraphicsAdapter.cpp`), confirming the test file's own repeated claim ("real on this backend only")
is architecturally accurate — the other 9 CNA backends compile these same files with the `#ifdef`
block entirely absent, so this is not dead code silently doing nothing elsewhere; it genuinely does
not exist as compiled code on non-D3D9 backends.

### Robustness

Check P/Q's "Reach forbids volume textures entirely" is a materially different code path from a
"ceiling of N" check (returns 0, treated as "unsupported at all" rather than "size 0 max") — the test
correctly distinguishes this from the F/G-style ceiling checks by using a trivial `1x1x1` size that
would trivially pass any ordinary ceiling, isolating the "not supported at all" behavior specifically.

### Testing

18 checks (A through R, some checks like J/K, P/Q sharing a letter across the Reach/HiDef pair) give
close to complete coverage of the D9-100/D9-101/D9-102/D9-103 profile-enforcement surface. One gap:
`GraphicsAdapter::QueryBackBufferFormat` (the back-buffer-specific sibling of `QueryRenderTargetFormat`,
same D9-102 task, `GraphicsAdapter.cpp:286-327`) is never exercised by this file at all — see Missing
Tests below.

## Detailed Findings

No CRITICAL/HIGH/MEDIUM findings. Two LOW/INFO observations:

### F1 — `QueryAdapterCapsEXT()`/`IsRenderTargetFormatSupportedByHardwareEXT()`/etc. each construct a fresh `IDirect3D9` COM object via `Direct3DCreate9` per call

- Severity: LOW
- Confidence: HIGH
- Category: performance (theoretical, not likely-significant)
- Location/symbol: `D3D9ProfileCapabilities.cpp` — `QueryAdapterCapsEXT()` (line 14),
  `IsRenderTargetFormatSupportedByHardwareEXT()` (line 76), `IsBackBufferFormatSupportedByHardwareEXT()`
  (line 93), `ClampMultiSampleCountForFormatEXT()` (line 111) — 4 separate `Direct3DCreate9` calls,
  none reusing the live device's own already-open `IDirect3D9` object.
- Why it matters: this test alone triggers at least 5 of these calls (Check A/B call
  `QueryAdapterCapsEXT` twice total across both `Game` instances; Check C/D/E each call
  `IsRenderTargetFormatSupportedByHardwareEXT`/`ClampMultiSampleCountForFormatEXT`). `Direct3DCreate9`
  is a relatively heavyweight COM factory call (DLL load/COM activation) — negligible for an 18-check
  test but would compound if a real game queried profile support in a hot per-frame path (unlikely in
  practice; XNA games query this once at startup, so this is a theoretical concern, not a live bug).
- Suggested future action (not implemented by this audit): thread the already-open `IDirect3D9`/
  `IDirect3DDevice9` through from the live device where one is available, falling back to a fresh
  `Direct3DCreate9` only for the adapter-enumeration-time (`getAdaptersProperty`) case where no device
  yet exists.

### F2 — `QueryBackBufferFormat` (this test's own D9-102 sibling function) is never exercised here

- Severity: LOW
- Confidence: HIGH
- Category: test-coverage
- Location/symbol: `GraphicsAdapter::QueryBackBufferFormat` (`GraphicsAdapter.cpp:286-327`) — same
  profile-whitelist-then-hardware-query shape as the already-tested `QueryRenderTargetFormat`, using
  `IDirect3D9::CheckDeviceType` instead of `CheckDeviceFormat` (a genuinely different D3D9 API call,
  per the function's own header comment: "the back buffer ... has its own, stricter display-
  compatibility restriction").
- Why it matters: `CheckDeviceType` and `CheckDeviceFormat` can disagree for the same format (the
  function's own comment cites Color's A8B8G8R8 as "texture-valid but not display-valid" as a known
  real case) — this test does not prove the back-buffer-specific code path behaves correctly under
  either profile, only the render-target one.
- Suggested future action (not implemented by this audit): add a Check analogous to C/D/E but calling
  `QueryBackBufferFormat`, ideally against a format known to differ between the two D3D9 queries.

## Cross-File Observations

- The file's own header comment (Check "G-prime" in `D3D9GraphicsProfileHiDefTest::Draw`, lines
  228-239) documents a real, independently-discovered bug: `Game`'s `GraphicsDevice_` member was
  eagerly default-constructed at `GraphicsProfile::Reach` before `GraphicsDeviceManager` existed, so
  `GraphicsDeviceManager::setGraphicsProfileProperty(HiDef)` had no path to reach the live device.
  This audit independently confirmed the fix is real: `GraphicsDeviceManager::applyToExistingBackend()`
  (`GraphicsDeviceManager.cpp:566`) calls the new NOXNA `GraphicsDevice::SetGraphicsProfileEXT()`
  **before** `Reset()`, and `GraphicsDevice::SetGraphicsProfileEXT` (`GraphicsDevice.cpp:509-512`)
  simply assigns `graphicsProfile_ = profile` — a minimal, correctly-placed fix, not a stub.
- Contrasts instructively with the cross-cutting "mutate state before the call that can throw for it"
  pattern documented in `AUDIT_CROSS_CUTTING_FINDINGS.md` (`GraphicsDevice::SetRenderTargets` mutating
  `currentRenderTargets_` before a later backend-hardware-cap throw): this test's own Check J/K
  exercises a *different*, earlier throw point in the same method (the D3D9 profile ceiling,
  `GraphicsDevice.cpp:1887-1904`) which correctly throws *before* any state mutation — a good
  counter-example showing the later-added profile check was written more defensively than the
  pre-existing code path it sits next to.
- `git log --oneline -- examples/d3d9_graphicsprofile_test.cpp` shows a single authoring commit
  (`a1b60cb2 feat(plans/plan_dx9.md): close Phase D9-10 -- GraphicsProfile.Reach/HiDef made real on D3D9`)
  plus a follow-up (`905362ab ... TextureCube/Texture3D/MaxRenderTargets profile enforcement`) that
  extended this same file — consistent with the file's own two-part Check A-I / J-R structure.

## Missing or Weak Tests

See F2 (`QueryBackBufferFormat` uncovered).

## Positive Findings

- This is a rare case of a test file that itself drove a real production-code fix during authoring
  (Check G-prime), and the fix was independently re-verified by this audit rather than taken on
  faith.
- Every numeric ceiling in the test (2048/4096/512/4096/0/256/1/4) was independently cross-checked
  against real XNA 4.0's documented Reach/HiDef profile table and matches exactly — not an invented
  table.
- Consistent, correct use of narrow vs. broad exception matching across every throw/no-throw check
  (see C++ correctness above).

## Final Assessment

A thorough, well-evidenced, and self-correcting test. No correctness defects found in either the test
or its full production call chain after tracing every check to its source. The two findings above are
both minor (a theoretical perf note and a coverage gap for a sibling function), neither blocking a
`Healthy` verdict.
