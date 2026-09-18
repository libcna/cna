# plan_terminal_capi_repair.md — two repairs the renderer curation left behind

Branch `terminal-capi-repair`, cut from `30389564c` (the renderer-curation tip, tree-identical to
`next` at `113be1520`).

This is a **repair** branch, not a design branch. Both workstreams restore intended behaviour that
already has an architecture; neither invents one. The renderer set stays curated at 25 identities and
is not reopened.

Build trees: `cmake-build-debug` (HEADLESS/SDL3, Debug) for the C API and the corpus,
`build-probe/cfg-TERMINAL-SOFTWARE` for the terminal end-to-end path.

## Status

| Task | Subject | Status |
| --- | --- | --- |
| TCR-1 | Baseline audit: HEAD, tree, siblings, renderer invariants, both reproductions | ✅ |
| TCR-2 | Reconnect a CPU renderer to `IPlatformSurfacePresenter` | ⬜ |
| TCR-3 | Terminal regression coverage + re-enable `TerminalSoftwareDemoIntegration` | ⬜ |
| TCR-4 | `CnaCApiEffects.cpp`: effect collections return `T*`, not `T&` | ⬜ |
| TCR-5 | The other C API build breaks the same configure exposes | ⬜ |
| TCR-6 | Canonical ABI measurement against the real built library | ⬜ |
| TCR-7 | Limitations generator and `RELEASE_GATE.md` through their normal paths | ⬜ |
| TCR-8 | Combined validation: invariants, corpus, sanitizers, stale-reference audit | ⬜ |

---

## TCR-1 — Baseline audit

**Starting SHA `30389564c`**, tree clean, branch `terminal-capi-repair` created from it. `git diff
30389564c 113be1520` is empty, so this branch starts from exactly the tree `next` carries.

**Sibling repositories** recorded before any work. Four carry pre-existing local modifications that
are none of this branch's business and must be unchanged at the end: `cna-car-simulator` (2 modified),
`mesh-craft` (1 modified, 2 untracked), `myra-cna` (1 modified, 1 untracked), `cna-java` (untracked
`out/`). `sharp-runtime`, `easy-gl` and `house-simulator` are clean and stay clean.

**Renderer invariants at the start**, read from the canonical registry
(`cmake/RendererIdentities.cmake`): 25 public identities, 26 retired identities with reserved C ABI
values, next free value 52. ABI `0.29.0`; `tools/c-api/abi_baseline.json` records 221 structs and
4055 exports.

### The TERMINAL disconnection, traced end to end

The presenter seam is **complete on the consumer side and absent on the producer side**. Nothing is
missing architecturally:

- `CNA::Platform::SurfaceFrame` (`modules/platform/include/CNA/Platform/IPlatformSurfacePresenter.hpp:35`)
  is the CPU-framebuffer contract: RGBA8, row-major, top row first, `strideBytes == 0` meaning
  tightly packed. `SoftwareFramebuffer::color` is **byte-for-byte that layout already**.
- `IPlatform::CreateSurfacePresenter(IPlatformWindow&)` is pure virtual and implemented by five
  retained backends; `TerminalSurfacePresenter` passes 36 pseudo-TTY tests.
- `GraphicsRendererCreateArgs::surfacePresenter` (`IGraphicsRenderer.hpp:4000`) is the wire.
- `GraphicsDevice::createRenderer()` populates it (`GraphicsDevice.cpp:3985`).

Two links are missing, and both are recorded in the tree rather than discovered here
(`docs/platform-terminal-analysis.md:83-86`):

1. **No renderer sets `needsSurfacePresenter`.** BLEND2D was the only one that did and was retired
   by `RRC-002`.
2. **`SoftwareRenderer::Present()` is empty** (`SoftwareRenderer2DState.cpp:58`), and the descriptor
   sets `needsWindow = false`, so `createOrAttachWindow()` drops the window at
   `GraphicsDevice.cpp:3555` before a presenter could be created against it.

`TerminalSoftwareDemoIntegration` is registered `DISABLED TRUE`
(`modules/graphics/examples/CMakeLists.txt:138`) with the one-line re-enable condition stated at the
registration site.

### The C API build failure — reproduced, and it is not one failure but three

`-DCNA_BUILD_C_API=ON`, built to completion with `-k` so every error is seen rather than the first
eight:

1. **The recorded one.** `modules/c-api/src/CnaCApiEffects.cpp` fails with exactly **8 errors** —
   one `invalid initialization of reference ... from expression of type EffectAnnotation*` at 1691,
   and seven `lvalue required as unary '&' operand` at 2630, 2658, 2963, 2992, 3302, 3332, 3360.
   **Root cause, proven by history rather than inferred:** `e55945cd7`
   (*fix(SOFTWARE-255): restore effect index null semantics*, 2026-09-09) changed
   `operator[]` on `EffectAnnotationCollection`, `EffectParameterCollection`, `EffectPassCollection`
   and `EffectTechniqueCollection` from `T&` to `T*`, because XNA returns null for a lookup that
   finds nothing. That commit touched **zero** files under `modules/c-api/`. The wrapper still writes
   `&(*collection)[i]` — the address of a pointer prvalue — and passes `(*collection)[i]` to a
   `const T&` parameter. The C API is OFF by default, so nothing caught it for nine days.

2. **A configuration break the recorded audit never reached.** With `CNA_ENABLE_NET=OFF` the C API
   does not compile at all: `modules/CMakeLists.txt:399` adds `gamer-services` only under
   `CNA_ENABLE_NET`, while `modules/c-api` links `CNA_GamerServices` unconditionally and
   `CnaCApiDetail.hpp:29` includes a GamerServices header, so every C API TU fails with
   `GameUpdateRequiredException.hpp: No such file or directory`. `CNA_ENABLE_NET` defaults `ON`; the
   shared debug tree had it off, which is how this surfaced. Restoring the default reaches failure 1.

3. **Two stale ABI assertions.** `modules/c-api/tests/pure_c/AbiHeaderC.c:11` and
   `modules/c-api/tests/cpp/AbiHeaderCpp.cpp:11` still assert `CNA_ABI_VERSION_ENCODE(0, 27, 0)`
   while `abi.h` declares `0.29.0`. Neither the `0.28.0` commit (`fc1b6a537`) nor the `0.29.0` commit
   (`dea94ad85`) updated them. These are the two ABI walls, so they fail the moment the library
   builds.

None of the three is caused by this branch, and none is worked around: each is fixed where it is
wrong.
