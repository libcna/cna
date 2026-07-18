# AUDIT_DECISIONS.md — Assumption / Ambiguity Log

Append-only log of every conservative assumption made once the audit was underway, plus the four preflight
decisions the user made explicitly before the audit started. Each entry: what the ambiguity was, what was decided,
and why. Referenced by shorthand (`D-n`) from other audit documents.

## Preflight decisions (user-approved before audit start, 2026-07-18)

**D-P1 — Orchestration model: Hybrid.** Use the `Workflow` multi-agent tool for large mechanical batch fan-outs
(bulk first-pass per-file audits of tests/examples/simple headers), but keep backend cross-comparison, FNA parity
judgment calls, and final synthesis in direct reasoning rather than delegated to subagents.

**D-P2 — `sharp-runtime` sibling repo: reference only.** Treated like the FNA reference tree — read for
API/behavior context when auditing CNA files that use `SharpRuntime` types, never given its own audit reports,
never committed to.

**D-P3 — Documentation scope: audit `docs/`, exempt root planning docs.** `docs/*.md` technical/behavioral
write-ups get real per-file audit reports; root-level process artifacts (`plan_*.md`, `NEXT*.md`, `AUDIT.md`,
`CHECKLIST.md`, `TODO.md`, `RAM.md`, `tasks/backlog/*.md`, etc.) are `EXEMPT` as project-management artifacts, not
source — consulted only as secondary context, per the instruction not to trust prior plan docs as proof of
correctness.

**D-P4 — Build/runtime verification: opportunistic.** Build and run tests/sanitizers for backends actually
feasible on this Linux sandbox (EasyGL, SdlRenderer, SdlGpu, Headless, Software, Ascii, Canvas, Vulkan/WebGPU if
available) where it meaningfully strengthens a specific finding, but keep the bulk of the file-level audit as
careful static/source/cross-file reading. Windows-only backends (D3D9, D3D11, D3D12, Dx3) get static-only audit,
explicitly labeled as not runtime-verified in this sandbox.

## In-audit decisions (recorded as encountered, per the "ambiguity → conservative default → record → continue" rule)

**D-1 — Classifier extension gaps (first pass).** Initial classification left 7 files as `NEEDS_REVIEW`:
`Android.mk`/`Application.mk` (Android NDK build), `proguard-rules.pro` (Android build config), `gradlew`/
`gradlew.bat` (Gradle wrapper scripts), and one `.ogg` file that only failed due to a shell-quoting artifact (see
D-2). Resolved: `.mk`, `.pro`, `.bat` added to the recognized build/script extension set (all **AUDIT**, first-party
Android build config, none found under `third_party/`); `gradlew` (no extension) special-cased as **AUDIT** under
the "executable text file without extension that contains a script" category the audit prompt explicitly calls out.

**D-2 — Git quoted-path Unicode bug.** `git ls-files` by default C-quotes non-ASCII filenames (e.g.
`03 - \303\211toile.ogg`), which broke naive extension parsing on one file. Fixed by re-running inventory with
`git -c core.quotePath=false ls-files -z` (NUL-delimited, unquoted). Re-verified the file
(`tests/assets/media/music/Artist Two/Album Delta/03 - Étoile.ogg`) classifies correctly as `binary-or-data-asset`
(`.ogg`). No other files were affected (only one repository file contains non-ASCII bytes in its path).

**D-3 — Documentation split, applying D-P3.** `docs/` vs. root-level split confirmed clean: every file directly
under `docs/` (78 total, including two image subdirectories whose actual PNG contents fall under the existing
binary-asset exemption) is `AUDIT`; every root-depth `*.md` file (44 total including `tasks/backlog/*.md` and
`dx9-spike/README.md`, folded into the same bucket) is `EXEMPT` `planning-tracking-doc`. `CLAUDE.md` itself (project
instructions, not a subsystem behavior doc) is included in the exempt bucket on the same reasoning — it is binding
guidance to follow, not a file whose *implementation accuracy* is being audited.

**D-4 — (see D-P2)** `sharp-runtime` policy confirmed unchanged after full inventory; no sharp-runtime files exist
inside the `cnaaudit` git tree to misclassify.

**D-5 — Vendored D3D9 stock-effect shaders.** `THIRD_PARTY_NOTICES.md` confirms
`src/CNA/Internal/Backends/D3D9/shaders/xna/` (`AlphaTestEffect.fx`, `BasicEffect.fx`, `Common.fxh`,
`DualTextureEffect.fx`, `EnvironmentMapEffect.fx`, `LICENSE`, `Lighting.fxh`, `Macros.fxh`, `README.md`,
`SkinnedEffect.fx`, `SpriteEffect.fx`, `Structures.fxh` — 12 files) is copied byte-for-byte from FNA, with an
integrity-check script (`scripts/verify-d3d9-stock-effects-vendored.sh`). Classified `EXEMPT`
`vendored-verbatim-stock-effect`, distinct from the general `third-party-vendored` bucket since it isn't under
`third_party/`. The D3D9 backend's own `.cpp`/`.hpp` consumer code (constant-table binding, compilation via
`D3DCompile`, entry-point dispatch) remains fully in scope and is exactly where a shader-interface mismatch would
actually surface as a CNA bug — flagged as a required cross-file check for the `backend-d3d9` shard audit.

**D-6 — Other first-party sibling-repo dependencies discovered mid-recon, extending D-P2's policy.** While mapping
the graphics backend directories it became clear several backends are thin single-file CNA adapters over external
libraries that live in *other* git repositories on this machine, not inside `cnaaudit`:
  - `EasyGL` backend (`src/CNA/Internal/Backends/EasyGL/EasyGLGraphicsBackend.cpp`, 4733 lines — the entire backend
    is one file) links `easy-gl` (`cmake/BackendLibraries.cmake` line ~72), a sibling repo at
    `/rv/data/development/github.com/openeggbert/easy-gl`.
  - `Dx3` backend (`Dx3GraphicsBackend.cpp`, also single-file) links `free-direct`, matching prior audit memory of
    a separate `cnadx3`/`free-direct` repo pairing.
  - `Bgfx` backend links `bgfx`/`bx` (genuine upstream OSS, not an openeggbert sibling); `Vulkan` links the system
    Vulkan SDK; `WebGPU` links `wgpu-native`, which `THIRD_PARTY_NOTICES.md` already documents as a downloaded
    binary never copied into the tree.
  Decision: apply the identical "reference only, opaque external dependency" policy the user approved for
  `sharp-runtime` (D-P2) to all of these. The CNA-side adapter file for each backend is audited normally (it is
  the actual first-party code in this repository); the external library itself is out of scope, not given audit
  reports, and not treated as part of "100% coverage" for this audit. This is the same conservative, consistent
  extension the user's own preflight answer implies, applied without re-asking per the audit's autonomy rules.
  Noted for the final report: this means a full "did CNA's OpenGL backend implement X correctly" question may
  bottom out at "X is actually implemented in `easy-gl`, not in this repository" for some findings — those are
  recorded as scope boundaries, not silently dropped.

**D-7 — Manifest sharding structure.** The audit prompt names four fixed top-level files
(`AUDIT_SCOPE.md`/`AUDIT_MANIFEST.md`/`AUDIT_PROGRESS.md`/`AUDIT_DECISIONS.md`) but a single flat
`AUDIT_MANIFEST.md` table for 2297 rows would be unwieldy to update incrementally. Kept `AUDIT_MANIFEST.md` as the
literal file the prompt asked for, but made it a master index over 105 per-subsystem shard files under
`audit/manifest/<shard>.md` (e.g. `backend-easygl.md`, `xna-graphics.md`, `tests-xna-graphics.md`,
`examples-tests-vulkan.md`) — every one of the 2297 AUDIT-eligible files appears in exactly one shard, and the
master index's per-shard row links straight to it. This is a structural/organizational choice, not a scope
decision — it changes nothing about which files are in scope or how they're classified.
