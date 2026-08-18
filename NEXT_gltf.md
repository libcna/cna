# glTF campaign continuity

Read this before `plan_gltf.md`. The plan is 2 700 lines and its own header used to say "nothing in
it was implemented", which stopped being true a long time ago; this file is the short version a new
session needs to start work without re-deriving the state.

## Session status

- **Branch:** `feature/gltf`, with local commits. The owner explicitly requested a push when the
  current autonomous run reaches its weekly-limit cutoff; no pull request has been requested.
- **Working document:** `plan_gltf.md`, **476** numbered rows. **Three remain open: `GLTF-344` and
  `GLTF-465` (both `✅/⬜`), and `GLTF-459` (`⬜`).** The campaign retrospective
  (`GLTF-460`) is written: `docs/gltf-campaign-retrospective.md`. Read it before starting the next
  subsystem campaign — its central finding is that this campaign's own L0–L7 ladder reads data and
  source but never asks whether a draw *arrives*, and five defects lived in that gap.
  `GLTF-463` closed on 2026-08-17; `GLTF-472` (the adversarial audit) closed on 2026-08-18 and opened
  three rows; `GLTF-473` closed the same day and opened `GLTF-475`, which closed on 2026-08-18 after
  a live probe showed the defect was in **two** renderers rather than the one the row named —
  `OPENGL4` painted a stride-48 record's `NORMAL` as the surface colour and `DILIGENT` drew the same
  input black, both for a plain XNA `BasicEffect` draw. `OPENGL4` now renders the effect's colour;
  `DILIGENT`, which has no unlit program for that layout, refuses by name before touching GPU state.
  `GLTF-476` closed the same day and is the sharpest finding of the three: `IGL` transported **6 of
  the 20** PBR draw parameters and drew the other fourteen — four of them **core** glTF 2.0 material
  inputs — with the shader's own constants substituted, while the specular inventory labelled it
  "factor-only". It now consumes all twenty, verified on a live IGL OpenGL device in the new
  `cmake-build-igl` tree (40/40 IGL witnesses, 720/720 glTF/PBR tests), which also moved `GLTF-344`
  to 14 of 16.
- **The last renderer that is theoretically reachable here is `WICKED`, and it is blocked twice** —
  both blockers measured on 2026-08-18 and written into `GLTF-465`. It has no stride 60, 76 or 80, so
  it cannot draw dual-UV content at all and `COLOR_0` sits behind that whole family; and configuring
  it fails before any compile because the **shared** `~/deps/WickedEngine` carries the legacy SDL3
  bridge and this branch's reverse-patch step cannot undo it. That checkout was left untouched on
  purpose: repairing it discards another session's state. `METAL` remains uncompilable anywhere here.
  **No renderer in the tree is in the forbidden third state any more** — nothing accepts a valid core glTF primitive and renders it
  with different semantics. `GLTF-465` is half closed: **no PBR renderer draws a `COLOR_0` asset with different
  core semantics any more** (thirteen apply the product, four refuse the draw), and what remains is
  implementing it in those four. §27.2 carries a row-by-row ROBUST assessment; that section, not this
  file, is the record of what the milestone still needs.

- **The milestone in force is `GLTF CORE 2.0 IMPORT/RUNTIME MODEL CORRECT` (§27.1.3), and it is
  stated with its renderer coverage beside it: `PBR renderer coverage: 15/17 apply COLOR_0, 2 refuse
  such a draw by name`.** The unqualified `GLTF CORE 2.0 CORRECT` was written on 2026-08-17, rejected
  by the owner the next day, held as the qualified name, taken back when the owner's condition looked
  met — and **qualified again by `GLTF-472`, which showed the condition had not actually been met when
  it was reclaimed**. Two of the thirteen renderers counted as applying `COLOR_0` could not draw such a
  primitive at all: `SDL_GPU`'s dispatch still selected its PBR queue for `stride == 48`/`68` only, and
  `DILIGENT` picked its stride-80 shader variant and then refused stride 80 nine lines later. Both are
  fixed. The name stays qualified because renderer coverage is not uniform — four PBR renderers refuse
  the feature (`GLTF-465`). `LLGL`, `SDL_GPU` and `DILIGENT` each could not draw content the tables did
  not say was unreachable; `GLTF-474` closed that, and all three now draw every canonical stride. `OPENGLES1` (outside the seventeen) used to accept a PBR
  draw and read a stride-60 record's `NORMAL` as its vertex colour; `GLTF-473` closed that against a
  real ES 1.1 driver, so **the forbidden third state is now empty** and the qualifier survives for a
  different reason — coverage, not wrong output.
  **Read §27.1.3 before §27.1, and §27.1.2 before either.**

  The rejected argument is the rule this campaign now works to: a renderer that **accepts** a valid
  `COLOR_0` metallic-roughness asset and substitutes the opaque-white identity renders a visibly wrong
  picture, and a correct importer plus eight correct renderers does not make CNA as a whole correct.
  A renderer that **explicitly refuses** the combination is limited coverage, not wrong output. So the
  bar is a two-way partition — render it correctly, or refuse it — with **no third state**, and that
  partition is machine-checked over all seventeen PBR renderers by
  `GltfRendererPbrFallbackPolicy.EveryPbrRendererEitherAppliesVertexColourOrRefusesTheDrawExplicitly`.

  **That test alone is not enough, and `GLTF-472` is why.** It — and every other audit in
  `docs/gltf-renderer-pbr-fallbacks.md` — reads *declarations*: a layout row, a shader expression, a
  guard call. None of them can see a renderer whose layout and shader are complete while the draw
  route that selects them still enumerates only the uncoloured strides, which has now happened three
  times (`OPENGL2` under `GLTF-465`, `SDL_GPU` and `DILIGENT` under `GLTF-472`). Two tests close it:
  `EveryStrideGatedPbrRouteAdmitsBothColourCarryingStrides` pins each stride-gated route's acceptance
  predicate, and `RendererStrideConformance.AColourCarryingPbrPrimitiveEitherDrawsOrRefusesByName`
  draws both colour-carrying fixtures through a **live device**. **If you add a renderer to the
  applying set, run the second one under that renderer** —
  `CNA_GRAPHICS_RENDERER=<X> SDL_VIDEODRIVER=x11 DISPLAY=:<n> ./cmake-build-multi/Debug/bin/CnaTests
  --gtest_filter='RendererStrideConformance.*'` — because compiling it proves nothing about whether
  the draw arrives.

  Evidence for the import/runtime half was recaptured whole rather than argued from the old run: 146
  assets / 734 files byte-identical across two generator runs, and **all four** L7 policies (EasyGL,
  Vulkan on lavapipe, SOFTWARE, DirectX11 under Wine + DXVK) re-captured at 146 dispositions = 138 PNG
  pairs + 8 safe rejections.

- **⚠ The 2026-08-15 declaration (`GLTF-458`) was premature.** A 2026-08-17 re-audit against the pinned specification found **four core divergences
  that no §27.1 row asks about**, each sitting inside a row that was green. All four are fixed
  (`GLTF-461`, `GLTF-462`); §27.1.2 records what they were, which row covered each, and why the row
  did not catch it. **Read §27.1.2 before trusting §27.1.** In short:
  1. §3.7.2.1's flat normals were **averaged** at shared vertices rather than produced per face, and
     `GLTF-173` had recorded the averaging as an unavoidable deviation — so the deviation was cited
     as evidence of compliance. `GLTF-461` splits the vertex instead.
  2. §3.7.2.1's "the provided tangents (if present) **MUST** be ignored" was not honoured at all.
  3. §3.7.2.2's "**MUST** calculate flat normals for **each morph target**" was not implemented, so a
     normal-less morphed surface was lit with its **rest-pose** normals at every weight — and no
     delta blend could have fixed it, because §3.7.2.2 forbids such a primitive from carrying
     `NORMAL` deltas. `morph-no-base-normals` existed and passed because its target translates all
     three vertices rigidly: **a fixture that cannot fail.**
  4. `COLOR_0` on a metallic-roughness material abandoned the material model **and took the authored
     `NORMAL` with it** (stride 24 has no normal slot), so the primitive could not be lit at all.
     `GLTF-462` carries it in stride 60's own colour slot.

- **The method lesson.** Three of the four were protected by a *fixture that passed*. The corpus rule
  "a fixture must discriminate" had been applied to **values** and not to **rules**. A conformance row
  should name the specification *sentence* it tests; `GLTF-461`'s new tests assert §3.7.2.1's own
  definition **remap-independently** (every corner of every emitted triangle carries that triangle's
  own geometric normal, derived from the emitted positions), so no numbering or fixture shape can
  satisfy them by accident.

- **Two further defects were found by *running* things that had not run:**
  - Stride 60 — the rigid dual-UV PBR record, live since `GLTF-182` — was implemented by only **7 of
    15** PBR renderers. `OPENGL2` fell through a `stride >= 32` catch-all that reads `TEXCOORD` at
    offset 24, **inside the tangent**, so every dual-UV PBR mesh textured itself from tangent bytes in
    silence. `OPENGL4`/`MAGNUM`/`LLGL`/`DIRECTX9` degraded visibly. All five now bind the record
    (`GLTF-462`).
  - `scripts/gltf-l7-corpus.py`'s renderer-identity check used `line.startswith(prefix)`, and the
    SOFTWARE/DirectX11 markers go through **CNA's own logger**, which prefixes every line — so those
    two policies had been **structurally unrunnable** since the log tag was introduced (`GLTF-467`).
    That is why the SOFTWARE report could only ever verify itself.
  - Two more of the same shape were found on 2026-08-17 by running things that had not run:
    `modules/renderers/common/d3d/src/shaders/compile_shaders_hlsl.py` compiles HLSL through a tiny
    `D3DCompile()` .exe that never creates a D3D11 device, so `run-wine-dxvk.sh`'s DX-85 DXVK gate
    failed it after the first shader compiled fine — it now passes the `CNA_D3D11_SKIP_DXVK_GATE=1`
    the wrapper documents for exactly that case. And `OPENGL2` selected its PBR program only for
    stride 48, so `GLTF-462`'s layout fix alone still left every stride-60 rigid PBR draw shaded by
    the **Blinn-Phong** program.

- **Draco is not optional local state:** `third_party/draco` is a gitlink pinned to Draco **1.5.7**
  (`8786740086a9f4d83f44aa83badfbea4dce7a1b5`). The normal build uses it; the sanitizer workflow also
  runs `CNA_ENABLE_DRACO=OFF` so the named refusal path cannot rot.
- **The corpus defect ledger has no open entries.** All eight audited defects (D1–D8) and `GLTF-241`
  are `fixed` (`tests/assets/gltf/manifest.json` → `defectLedger`); `GLTF-241`'s own known-defect test
  is now a **fix witness**. `GltfKnownDefect.EveryOpenDefectInTheCorpusLedgerHasAnExecutableTestHere`
  keeps that honest in both directions.

## How to verify, exactly

Run everything from the repository root — the corpus paths are relative to it, and running the
test binary from inside the build directory produces ~80 spurious failures and a segfault that
have nothing to do with the code.

```bash
B=build
export CCACHE_DIR=/tmp/cna-gltf-ccache CCACHE_MAXSIZE=4G
cmake --build "$B" --target CnaTests cna_tool_gltf_to_cnj --parallel 3
ctest --test-dir "$B" -L gltf-conformance     # the 10-rung ladder
"$B"/CnaTests                                 # the whole suite, from the ROOT
"$B"/CnaTests --gtest_filter='*Gltf*'         # every glTF suite -- note the LEADING star
scripts/regenerate-gltf-goldens.sh --check    # the corpus, against its own generator
scripts/regenerate-gltf-goldens.sh --determinism
PYTHONPATH=tools python3 -m gltf_fixtures --fetch-validator /tmp/cna-gltf-validator
GLTF_VALIDATOR=/tmp/cna-gltf-validator/gltf_validator \
  scripts/regenerate-gltf-goldens.sh --check  # all 290 containers through the pinned Validator
cmake -S . -B /tmp/cnagltf-nodraco-build -G Ninja \
  -DCNA_GRAPHICS_RENDERER=HEADLESS -DCNA_BUILD_TESTS=ON -DCNA_ENABLE_DRACO=OFF
cmake --build /tmp/cnagltf-nodraco-build --target CnaTests --parallel 3
ctest --test-dir /tmp/cnagltf-nodraco-build -L gltf-conformance
```

Expected as of this writing:

**Measured on the 2026-08-17 `GLTF-463`/`GLTF-465` revision, on a `HEADLESS` + vendored-Draco
`build/`:**

| Check | Expected |
|---|---|
| `ctest -L gltf-conformance` | **10/10 passed** (the `Perf` rung joined on 2026-08-12) |
| full suite, from the ROOT | **7 514 tests: 7 286 passed, 228 skipped, 0 failed** |
| `*Gltf*` | **597 tests: 594 passed, 3 skipped, 0 failed** — the skips are the opt-in licensed ChronographWatch asset and the two opt-in large-reference-asset budgets |
| `*Gltf*` and the ladder on the **OPENGLES3** tree | **598 tests: 595 passed, 3 skipped** (one more case runs there than on HEADLESS) and **10/10** conformance, both under `xvfb-run -a` — §27.1 row 19's cross-renderer claim re-checked with stride 80 and the 146th asset in place |
| generator `--check` / `--determinism` | **148 assets, 744 files — byte-identical**, and two independent generator runs agree byte-for-byte |
| EasyGL L7 corpus (Xvfb, production viewer) | **148 dispositions: 140 deterministic PNGs, 8 deterministic safe rejections**, zero RGB/alpha tolerance |
| Vulkan L7 corpus (Xvfb, lavapipe) | **146 dispositions: 138 PNGs, 8 safe rejections** — export `VK_DRIVER_FILES=/usr/share/vulkan/icd.d/lvp_icd.json` or the report omits the ICD pin and its integrity test fails |
| SOFTWARE L7 corpus (Xvfb) | **146 dispositions: 138 PNGs, 8 safe rejections** — runnable again only since `GLTF-467` |
| DirectX11 L7 corpus (Wine + DXVK under Xvfb) | **146 dispositions: 138 PNGs, 8 safe rejections** — export `CNA_D3D11_VIRTUAL_DESKTOP=CNA,1280x1024` and use `xvfb-run -a -s "-screen 0 1280x1024x24"`, or every asset fails with `AcquireSubsystem(Video) failed: No displays available`. Budget ~25 minutes: two Wine processes per asset |
| pinned Khronos Validator | **270 valid, 20 expected-invalid, 42 warnings** (not re-run on this revision) |
| `*Gltf*`, HEADLESS + vendored Draco, 2026-08-15 revision | **552 tests: 551 passed, 1 opt-in ChronographWatch skip** (14.7 s) — kept for comparison |
| `*Gltf*`, HEADLESS without Draco | **539 tests: 538 passed, 1 opt-in skip**; the ten-test difference is the real decoder/encoder evidence, while the unavailable-path checks still run |
| conformance ladder, Draco `ON` / `OFF` | **10/10 passed** / **10/10 passed** |
| pinned reference renderer subset | **13/13 passed**; the original 12-case bounds remain minimum foreground IoU 0.999579, coverage ratio 0.999891–1.000422 and worst foreground RGB MAE 67.60; `uv1-material` additionally passes after dual-stream support |
| final pinned viewer retake | **14/14 rows, 15/15 cases passed** through two byte-identical viewer captures plus the exact viewer camera in the Khronos renderer; report in `docs/gltf-viewer-retake-report.json` |

**The full suite has no failures on this configuration.** The 18 that the 2026-08-15 table reported
were a `STUB` build's; on `HEADLESS` the capability-gated cases really run. If you build `STUB`, expect
them back — they are the STUB renderer's own capability expectations and the TextureCube DDS fixtures,
and they are **pre-existing and unrelated to glTF.** They are the STUB renderer's
capability expectations (`GraphicsDeviceCapabilityTest.*`), the TextureCube DDS fixtures
(`TextureCubeTest.*`, `Texture3DTextureCubeContentTypeReaderTest.*`,
`XnbBuiltInReaderRegistrationTest.*`) and `CnjCapabilityMatrixTest.TextureCubeDelegatesViaSourceFile`.
Do not attempt to "fix" them as part of this campaign, and do not report a run as clean without
saying they are there.

HEADLESS reports `GraphicsCapability::ThreeD`, so STUB's capability-gated cases really run there.
OPENGLES3 additionally supplies the real framebuffer evidence. When two compatible disposable
trees are available, compare them with

```bash
scripts/gltf-renderer-parity.sh /path/to/headless-build /path/to/opengles3-build
```

which fails on any L1–L5 difference and tolerates only `SKIPPED`-vs-`OK`.

A disposable **sanitizer** tree should be re-created after importer changes:

```bash
A=/tmp/cnagltf-sanitize-build
cmake -S . -B "$A" -G Ninja -DCNA_GRAPHICS_RENDERER=STUB -DCNA_BUILD_TESTS=ON \
  -DCNA_ENABLE_DRACO=ON -DCNA_SANITIZE=address,undefined
cmake --build "$A" --target CnaTests cna_tool_gltf_to_cnj --parallel 3
ASAN_OPTIONS=detect_leaks=0 \
UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=0:exitcode=1 \
  "$A"/CnaTests --gtest_filter='*Gltf*'
```

`detect_leaks=0` is only for this ptrace-restricted local environment; the sanitizer CI keeps its
own leak-detection policy. AddressSanitizer and UndefinedBehaviorSanitizer remain active here.

The current ordinary tree is `-DCNA_GRAPHICS_RENDERER=HEADLESS -DCNA_BUILD_TESTS=ON
-DCNA_ENABLE_DRACO=ON`. Always cap compilation at `--parallel 3`. A fresh worktree needs recursive
submodules, including the pinned Draco gitlink, before configuration. Build trees and the ccache are
disposable and are deliberately removed at the end of an autonomous run.

## What a new session must know before touching anything

**The corpus is generated, not hand-edited.** `tests/assets/gltf/*` is emitted by
`tools/gltf_fixtures/`. Edit the Python, then:

```bash
scripts/regenerate-gltf-goldens.sh    # GLTF-020: regenerate, verify, and report what changed
```

A fixture's `.gltf`, `.glb`, `.expected.json`, L5 `.bin` goldens and any external buffer/image
sidecars all come from one description, so they cannot drift apart. Stdlib only — no third-party
dependency is permitted in the generator.

**Every new `Gltf*` test suite must be registered** in `CNA_GLTF_CONFORMANCE_RUNGS`
(`cmake/UnitTests.cmake`), or `GltfConformanceLadder` fails: it parses that list and asserts the
partition over registered suites is total in both directions. A suite that matches no rung would
still run under a plain `ctest` while silently sitting outside the conformance label.

**`plan_gltf.md` §19's extension table is generated.** Its source of truth is
`GltfExtensionRegistryEXT()` in `modules/content/src/GltfImport/GltfImportCore.cpp`, which the
`extensionsRequired` gate also reads. `GltfExtensionRegistry.Section19AgreesWithTheRegistryOnEveryRow`
compares them row by row and **prints the corrected table on failure** — paste that, do not edit the
rows by hand.

**The vertex stride ABI is a table, not a literal.** Query
`InferredLayoutForStride` (`CNA/Internal/Graphics/VertexDeclarationFidelity.hpp`) rather than
hardcoding an offset; that is `GLTF-278`'s lesson, and a test carrying its own copy of an offset
asserts the copy. The packed GPU layouts are `BuiltInVertexStreams.hpp`'s stream structs, **not** the
public `VertexPosition*` types (`sizeof(VertexPositionColor)` is 40, not 16).

**New public API is gated.** `docs/gltf-api-change-review.md` is `GLTF-025`'s record; the standing
default is *no new public glTF API*. Anything additive goes through §1 with its shape justified.
The owner explicitly reopened `GLTF-034` after naming the viewer as its first consumer, so public
`GltfImportReportEXT` now survives both direct and offline loads and is exposed by `Model`; its
computation-oriented source reports (`NodeGraphReportEXT`, `SkinReportEXT`, `MorphReportEXT`,
`LightReportEXT`, `AnimationReportEXT`) remain internal.

**Stage commits by explicit name.** `CLAUDE.md` forbids `git add -A` / `git add .` — this repo
routinely carries unrelated local changes.

## Working conventions that produced the closed rows

These are not style preferences; they are what made the campaign find things.

1. **A fixture must discriminate.** Author values so that a wrong answer is a *different* answer,
   not a plausible one. The recurring failure is a fixture whose expected value coincides with a
   default: every corpus normal was `(0,0,1)` until `normal-absent` was tilted out of the XY plane,
   and every world 3×3 was diagonal — so its own transpose — until an animation fixture put a
   rotation in one, which immediately exposed a latent transpose bug in the L6 oracle.
2. **Write the control.** Nearly every real find came from the negative case: a decoder that ignores
   percent-encoding passes the positive URI test, so the control names the file literally
   `my%20geometry.bin` and requires *failure*. A report hardwired to fire passes every assertion
   that expects it to fire.
3. **Sweeps must be total in both directions, and must say how much they covered.** Two bugs this
   session were sweeps reporting success over a set they had silently shrunk — an L3 comparison that
   iterated the manifest and so skipped a fixture declaring nothing, and a fuzz that carried
   `JOINTS_0`/`WEIGHTS_0` with no `skins` entry so nothing was ever skinned. Assert a floor.
4. **A loss must be named where it happens.** The campaign's thesis is that every silent drop is a
   bug even when the render looks fine. If something cannot be carried, count it and report it, at a
   severity matching the size of the loss — warning for data that does not arrive, debug for an
   exact-but-renumbering transformation.
5. **Match FNA over preference,** and when CNA deviates, say so in the source. A comment calling a
   divergence harmless is a claim that needs a test: `Matrix::Decompose`'s "edge case with no
   practical impact" was losing every axis-aligned rotation.

## What is blocked in this environment, and why

Rewritten 2026-08-17. **Two of the three previously recorded boundaries were not real**, and both were
found by *running the thing that was said to be impossible* rather than by reasoning about it:

- **DirectX11 L7 was recorded as needing "a DXVK'd Wine prefix this environment does not have".** It
  has one. `wine`, `x86_64-w64-mingw32-g++`, `/usr/lib/dxvk` and `~/.wine-cna-d3d11` are all present,
  the viewer cross-builds with `cmake/toolchains/mingw-w64.cmake` against the prebuilt Windows SDL3,
  and the full 145-asset capture runs through `scripts/run-wine-dxvk.sh` under
  `CNA_D3D11_VIRTUAL_DESKTOP` on Xvfb (`GLTF-471`).
- **The SOFTWARE L7 policy was not blocked by the environment at all** — the harness's own
  identity check made it structurally unrunnable (`GLTF-467`).
- **A real ES 1.1 driver is on this host** (found by `GLTF-473`, 2026-08-18). `docs/opengles1-renderer.md`
  records a side-by-side Mesa build from 2026-07-22 and it is still installed at
  `~/deps/mesa-es1-install` with `gles1: enabled`, so `OPENGLES1` can be built **and run**, not merely
  compiled. Configure `cmake-build-opengles1` normally (`-DCNA_GRAPHICS_RENDERER=OPENGLES1`; CMake
  finds the system `libGLESv1_CM`), then run with the private vendor selected — the one step the
  document does not have, because SDL3 reaches ES 1.1 through EGL and glvnd needs to be pointed at the
  private `libEGL_mesa`:

  ```bash
  MESA=~/deps/mesa-es1-install/lib/x86_64-linux-gnu
  printf '{"file_format_version":"1.0.0","ICD":{"library_path":"%s/libEGL_mesa.so.0"}}\n' "$MESA" > /tmp/es1_egl.json
  LD_LIBRARY_PATH=$MESA LIBGL_DRIVERS_PATH=$MESA/dri __EGL_VENDOR_LIBRARY_FILENAMES=/tmp/es1_egl.json \
    LIBGL_ALWAYS_SOFTWARE=1 SDL_VIDEODRIVER=x11 DISPLAY=:<n> ./cmake-build-opengles1/CnaTests \
    --gtest_filter='*Gltf*:RendererStrideConformance.*:FixedFunctionArrayLayout.*'
  ```

  It reports `OpenGL ES-CM 1.1 Mesa 25.0.7`. That subset is 613/610/3 skipped/0 failed. The **full**
  suite is not runnable there: `GamePlatformOwnershipTest.*` fails and
  `EveryImplementation/GameEventSemanticsGoldenTest.*` segfaults, both windowed platform tests that
  never reach a draw route — the same instability already recorded for OPENGLES3 and FNA3D, not
  anything this renderer's draw paths do.

| Boundary | Rows | Note |
|---|---|---|
| **format/material breadth** | `GLTF-344` | `KHR_materials_specular`'s two texture inputs reach **13 of the 16** PBR renderers. `igl`, `metal` and `wicked` sample neither. `webgpu` joined on 2026-08-18, which also retires the reason this row used to give for all four — "none carries a second UV stream at all" — because a per-map transform is not a second UV stream, and WebGPU's is done without one. `igl` is declaration-driven and needs its generated shader library to grow the two maps; `metal` cannot be compiled here; `wicked` needs WickedEngine shader work. Machine-checked by `GltfRendererPbrFallbackPolicy.SpecularTextureInventoryClassifiesEveryPbrRenderer` — read it rather than any prose count. |
| **renderer shader toolchains** | `GLTF-465` | The **four** PBR renderers that **refuse** a `COLOR_0` draw rather than applying the product, each blocked on something this environment does not have: `directx9` needs the pinned native `d3dcompiler_47.dll` Wine prefix; `metal` cannot be compiled anywhere this repository runs; `webgpu` and `wicked` have no stride-60/80 layout at all. None of them draws such an asset wrongly meanwhile — that is what the shared `RequireVertexColourPbrSupportEXT` guard buys. (`bgfx`, `diligent`, `llgl` and `sdl-gpu` were on this list and are now implemented; `GLTF-472` found that two of them had shipped complete shaders behind a draw route that never selected them, so **treat "the shader is written" and "the draw arrives" as separate claims**.) |
| **corpus asset count** | `GLTF-464` | The corpus is pinned at **146** assets by four L7 provenance reports that enumerate every asset, so a new fixture fails each report's completeness assertion until every policy is re-captured. `GLTF-463` did exactly that and all four were re-captured on 2026-08-17, which is what makes `GLTF-464` (promoting `GLTF-461`/`468`/`470`'s inline probes to real fixtures) ordinary work rather than blocked work — budget ~40 minutes for the recapture, most of it DirectX11. |
| **milestone chain** | `GLTF-459`–`460` | ROBUST is **9 of 12** green in §27.2 (rows 3, 6 and 12 open). `CORE` is declared under the qualified name `GLTF CORE 2.0 IMPORT/RUNTIME MODEL CORRECT` (§27.1.3) with its renderer coverage stated beside it; §27.1.2 records why the 2026-08-15 declaration was premature, and §27.1.3 why the unqualified name was reclaimed and then withdrawn again by `GLTF-472`. |

## Suggested next clusters

Rewritten 2026-08-17 after the re-audit. Ordered by cost, cheapest first.

1. **`GLTF-465`: carry `COLOR_0` into the remaining four PBR renderers.** Thirteen are done (EasyGL,
   SOFTWARE, IGL, OpenGL 2, OpenGL 4, Vulkan, DirectX 11, DirectX 12, Magnum, Diligent, Bgfx, LLGL,
   SDL GPU, DirectX 9, WebGPU) and the other two refuse such a draw, so nothing renders wrongly meanwhile — coverage
   work, not correctness work. **Before marking one done, draw with it:**
   `CNA_GRAPHICS_RENDERER=<X> SDL_VIDEODRIVER=x11 DISPLAY=:<n> ./cmake-build-multi/Debug/bin/CnaTests
   --gtest_filter='RendererStrideConformance.*'`. `GLTF-472` found two renderers marked done whose
   shaders were unreachable, and both had passed every source-text audit in the file. What is left is genuinely harder than what is done: `directx9` needs
   the pinned native `d3dcompiler_47.dll` Wine prefix (`~/.wine-cna-d3d9-spike`, SHA-256 gated) before
   its `vs_3_0`/`ps_3_0` bytecode can be regenerated; `webgpu` and `wicked` have **no stride-60/80
   vertex layout at all**, so each needs a layout, a pipeline variant and a shader change rather than
   just the product (`webgpu` is buildable if you point `CNA_WEBGPU_ROOT` at the wgpu-native artifact
   another tree already downloaded); and `metal` cannot be compiled anywhere this repository runs, so
   its two-line guard call is the honest limit of what this host can verify about it.

   Recipes that worked, in the order they were cheapest:
   - **Magnum / Diligent** — shaders are generated or template-expanded at runtime, so no offline
     tool at all. Diligent's per-variant expansion is the pattern to copy: the attribute exists only
     where the layout supplies it, so there is no unbound-attribute hazard to gate around.
   - **Bgfx** — `cmake -S . -B cmake-build-multi -DCNA_BGFX_BUILD_SHADERC=ON -DBGFX_BUILD_TOOLS=ON`
     then `--target shaderc` (~15 min), then
     `modules/renderers/bgfx/src/shaders/compile_shaders.py <shaderc> ~/deps/bgfx-cmake/bgfx/src`.
     The rebuilt tool reproduces every committed blob, so the diff was only the 12 PBR ones.
   - **LLGL** — `compile_shaders.py --glslang /tmp/cna-glslang-tools/usr/bin/glslangValidator`
     (extracted from the `glslang-tools` package by an earlier session; not on PATH). It reproduces
     every non-PBR blob byte-for-byte, so the committed header was a **mix**: `plan_llgl.md` records
     that some shaders were compiled with a scratch libshaderc script when glslang was unavailable,
     and those are exactly the PBR ones. Only the shaders being changed moved to the documented tool.

   Whichever one you take: implement **both halves** of §3.9.2's product — RGB *and* alpha — remove
   that renderer's `RequireVertexColourPbrSupportEXT` call in the same commit, and move its rows in
   the four inventories. The tests check the halves separately, so a partial implementation fails on
   the one it missed.

2. **`GLTF-464`: promote the three inline spec-rule probes to corpus fixtures.** `GLTF-461`'s two and
   `GLTF-468`/`GLTF-470`'s three are conformance statements living as C++ string literals only because
   the corpus was pinned at 145. All four L7 renderers are re-capturable in this environment now, so
   this is ordinary work: add the fixtures, regenerate, re-capture all four L7 sets, lower the inline
   ceiling back down.

3. **Whatever the next `GLTF-465` renderer teaches about stride 80.** `GLTF-463` added the stride and
   eight renderers bind it; the nine that do not **refuse** an 80-byte record rather than mis-reading
   it, which is the disposition
   `GltfRendererPbrFallbackPolicy.EverySkinnedPbrRendererEitherBindsTheStride80RecordOrRefusesIt`
   pins. Adding the layout to a renderer therefore comes with the colour multiply or not at all —
   binding the record and ignoring offset 76 would put it in neither column.

4. **§27.2 row 12 (Gate C) and row 6 (real point/spot lights).** Unchanged in shape from the
   2026-08-15 assessment, and row 6 is still the largest item between here and ROBUST: the light block
   is shared shader ABI across every renderer, and XNA's `IEffectLights` names exactly three
   directional lights and cannot express a point or spot light. `GLTF-331` carries the design sketch;
   `GLTF-326`/`327` already count and report the loss, so nothing is silent meanwhile.

Then `GLTF-459` (declare ROBUST — but read §27.1.2 first: its precondition moved) and `GLTF-460`
(retrospective, which now has a much better story to tell about *why* a green milestone was wrong).

**Before starting anything, read `docs/gltf-conformance.md` §3.7 and §3.8.** They now record how to
add a fixture and when a document belongs inline instead — both were learned the expensive way.

## What the 2026-08-17 re-audit found (read this before trusting a green MILESTONE)

The 2026-08-12 section below is about not trusting a green **run**. This one is about not trusting a
green **milestone**, and the mechanism is different: every finding here sat inside a §27.1 row that
was green, and none of them needed new tooling to find — four came from reading the pinned
specification's own sentences against the code, and two from running something that had never run.

- **A conformance row that paraphrases the specification will pass over the sentence it paraphrases.**
  Row 7 asks that attributes "decode exactly, with correct defaults for absent attributes". §3.7.2.1
  says *"When normals are not specified, client implementations MUST calculate flat normals and the
  provided tangents (if present) MUST be ignored."* That is two MUSTs, and neither is a decode or a
  default. Both were unmet. Row 15 asks that morph targets "apply position, normal and tangent
  deltas"; §3.7.2.2 says *"When the base mesh primitive does not specify normals, client
  implementations MUST calculate flat normals for each morph target"* — which is not a delta at all,
  and was unimplemented. **Write rows that quote.**

- **A documented deviation is not evidence of compliance, and it will be read as one.** `GLTF-173`
  recorded averaged-instead-of-flat normals as unavoidable ("duplication would change the vertex count
  and every per-vertex stream including morph deltas"), `GLTF-457` copied that into `CHECKLIST.md`, and
  row 7 stayed green. The claim was simply false: duplication is one remap applied to every stream, and
  `GLTF-461` does it in about eighty lines. **A deviation whose justification is "impossible" deserves
  one attempt at the impossible.**

- **A named loss will be accepted as completeness.** Row 18 asks that "every loss the importer does
  perform is reported". `mat-vertex-color-pbr` was a *known-defect fixture* asserting that CNA names
  what it drops — and what it dropped was the entire metallic-roughness material *plus the authored
  `NORMAL`*, because the stride-24 layout it fell to has no normal slot. Row 12 ("metallic-roughness
  PBR is complete") was verified only on primitives with no `COLOR_0`. **A milestone that accepts a
  reported loss as evidence will accept any loss.**

- **A layout fix is not a shader fix, and the two live in different `switch`es.** `GLTF-462` taught
  `OPENGL2` to bind the stride-60 record; a day later `GLTF-465` found that the same renderer still
  chose its **Blinn-Phong** program for that stride, because program selection lives in a different
  function from attribute binding and had its own `stride == 48`. The same shape appeared in `OPENGL4`,
  where widening the PBR compile guard to strides 68/76/80 left the *program* choice on `stride == 68`,
  so a skinned record would have been drawn by the rigid shader. **When a renderer learns a stride,
  grep that renderer for every other mention of the strides it already knew.**

- **A count written into a script is a landmine with a date on it.** `scripts/gltf-l7-corpus.py`
  refused to run at all — `corpus manifest must contain exactly 145 unique assets` — the moment
  `GLTF-463` added an asset, and its progress lines said `/145` too. It now derives both from the
  manifest's own `distinctAssetCount`/`targetDistinctAssetCount`, which is what the check was actually
  for. `compile_shaders_hlsl.py` had the environmental version of the same problem: it inherited
  `run-wine-dxvk.sh`'s DXVK gate, which fails any run that never creates a D3D11 device — which a
  `D3DCompile()`-only tool never does.

- **Three of the four were protected by a fixture that PASSED.** `normal-absent` is a single triangle,
  so averaging and per-face shading coincide *exactly* on it. `morph-no-base-normals` translates all
  three vertices by the same vector, so its target cannot change a face normal. The corpus rule "a
  fixture must discriminate" had been applied to **values** and not to **rules**. `GLTF-461`'s tests
  assert §3.7.2.1's own definition remap-independently — every corner of every emitted triangle
  carries that triangle's own geometric normal, derived from the *emitted* positions — so no numbering
  or fixture shape can satisfy them by accident.

- **Two things were unrunnable rather than passing.** The L7 harness's renderer-identity check used
  `startswith`, and CNA's logger had grown a line prefix, so the SOFTWARE and DirectX11 policies
  completed all 145 assets and then failed their own final check (`GLTF-467`) — which is why the
  SOFTWARE report could only ever verify itself. And stride 60, live since `GLTF-182`, had never been
  bound by most PBR renderers: `OPENGL2` read `TEXCOORD` at offset 24, *inside the tangent*, so every
  dual-UV PBR mesh textured itself from tangent bytes in silence (`GLTF-462`).

- **`NEXT_gltf.md`'s own "blocked" table was wrong twice**, and both entries had been copied forward
  by successive sessions. See the rewritten section above.

## What the 2026-08-12 session found (read this before trusting a green run)

Three of the four findings came from **running something that had never run**, which is worth more
than any single fix:

- **`RuntimeGltfModelTest` was outside every gate.** Its name does not begin with `Gltf`, so the
  ladder's suite check, its CTest registration and the sanitizer job's `--gtest_filter='Gltf*'` all
  missed it. Four of its cases had been failing since `GLTF-215` on any renderer with a 3D pipeline;
  `STUB` skips them, so a green run said nothing. **The filter is `*Gltf*` now — keep the leading
  star.**
- **`cgltf_validate` walked bytes before CNA's alignment check refused them.** Found by the new
  container fuzz under UBSan. Fixed by ordering: the metadata-only checks (alignment, then span
  arithmetic) now run *before* `cgltf_validate`, so it only ever walks bytes something has vouched
  for.
- **Two allocator escapes.** An accessor with no `bufferView` (§3.6.2.1 reads it as zeros, so
  *nothing in the file bounds its count*) and a `reserve` firing before any decode both surfaced as
  `std::length_error` from inside the allocator. Both refuse by name now.
- **`ContentManager` already caches**, so `GLTF-433`'s premise ("re-parsed on every call") was
  wrong. Two `Load`s of one name return the *same instance* — mutating one mutates "both".

## Defects found and fixed outside plan rows

Recorded in `known_bugs.md`, both found *by* new fixtures rather than by inspection:

- **`Matrix::Decompose` lost every axis-aligned rotation.** `std::signbit` is true for `-0.0`, and a
  quarter turn has exact zeros in every row, so `(-1)*0` flipped a row's sign and the normalised 3×3
  became a reflection — the rotation came back as the identity quaternion. FNA's guard is
  `Math.Sign(x) < 0`, which takes the `+1` branch at negative zero.
- **The L6 normal-matrix oracle transposed its world one time too many.** Invisible for the suite's
  whole life because every corpus world 3×3 was diagonal. The renderer was right throughout.

Both have their own regression tests, and the L6 sweep now fails if it sees no asymmetric world.

## File map

| Path | What it is |
|---|---|
| `plan_gltf.md` | The 460-row campaign record. Each closed row carries its own evidence. |
| `tools/gltf_fixtures/` | The corpus generator. Edit here, never the assets. |
| `tests/assets/gltf/` | Generated corpus: **145 assets, 729 files**, including sidecars and `manifest.json`'s defect ledger. Never edited by hand. |
| `modules/content/src/GltfImport/GltfImportCore.cpp` | The importer. Extraction, skeletons, clips, lights, cameras, the extension registry, the stride table. |
| `modules/content/src/Xna/ContentManager.cpp` | The runtime `.gltf` loader **and** the `.cnj` reader. Both must agree; several tests assert exactly that. |
| `tools/gltf_to_cnj/gltf_to_cnj.cpp` | The offline converter — the second loader. |
| `modules/content/tests/CNA/Internal/GltfImport/` | Every `Gltf*` suite, plus the oracles. |
| `cmake/UnitTests.cmake` | `CNA_GLTF_CONFORMANCE_RUNGS` — the ladder's single source of truth (10 rungs since the `Perf` one joined). |
| `scripts/regenerate-gltf-goldens.sh` | Regenerate the corpus, verify it, decode any binary golden that moved, and (`--determinism`) prove two processes emit the same bytes. |
| `scripts/gltf-renderer-parity.sh` | Compare two build directories; fails on any L1–L5 difference, tolerates only `SKIPPED`-vs-`OK`. |
| `scripts/gltf-reference-renderer-compare.py` | Reproduce the 13-asset OPENGLES3-vs-pinned-Khronos capture and metric report. |
| `scripts/gltf-viewer-retake.py` | Reproduce the final pinned 14-row/15-case Gate C viewer retake. |
| `docs/gltf-viewer-retake-report.json` | Committed Gate C provenance, per-process hashes, camera records and comparison metrics. |
| `docs/gltf-conventions.md` | Every decision with a rationale: transforms, mirroring, colour space, effect selection, lighting, animation, extensions. |
| `docs/gltf-performance.md` | Phase 22's measurements and the decision each led to — the parse/cache costs, the 4× unpack ceiling, the 2× morph duplication, the occlusion codec. Reproduce with `--gtest_filter='GltfPerformance.*' --gtest_output=xml:`. |
| `docs/gltf-limitations.md` | The inverse: what cannot be carried, what is approximated, and the report field that names each loss. Its §1 is generated from the extension registry and its report fields are checked against the header — see `GltfLimitationsDoc`. |
| `tools/gltf_fixtures/flatnormals.py` | `GLTF-461`'s **independent** Python statement of §3.7.2.1's flat-normal split, so a golden is a second opinion on the split rather than a restatement of whatever the importer produced. |
| `scripts/gltf-l7-corpus.py` | The four-policy L7 oracle. All four policies (EasyGL, Vulkan, SOFTWARE, DirectX11-under-Wine+DXVK) are runnable in this environment and all four were re-captured on 2026-08-17; `GLTF-467` records why two of them had not been. Its asset count is derived from the manifest's own `distinctAssetCount`/`targetDistinctAssetCount` rather than written in as a constant — it used to be `!= 145`, which made the harness refuse to run at all the moment `GLTF-463` added an asset. See the verification table for the per-policy environment variables; two of them fail hard without one. |
| `docs/gltf-conformance.md` | The oracle ladder and the spec pin. |
| `docs/gltf-api-change-review.md` | `GLTF-025`'s gate. Read §4 before proposing public API. |
| `known_bugs.md` | Defects found outside the plan's own rows. |
