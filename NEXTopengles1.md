# NEXTopengles1.md — `feature/opengles1` session handoff

> Scoped to the OpenGL ES 1.1 backend only. `NEXT.md` is a different branch's handoff and is
> deliberately left untouched.

**Branch:** `feature/opengles1` (13+ commits ahead of `develop`, pushed to `origin`).
`develop` is checked out in a *different* worktree (`/rv/data/development/github.com/openeggbert/cna`)
and is deliberately not merged into from here — that is a human decision.

---

## Current state (2026-07-22)

The backend is real, runs, and is verified: **Phases 1–2 are fully ✅ (41 rows), plus
`OPENGLES1-78` parity.** Phase 3 (14 new rows, `OPENGLES1-81`–`94`) was just opened from an
EasyGL feature-parity audit and is where work continues.

### How to build and run (this is the non-obvious part)

Debian builds Mesa with `-Dgles1=disabled`, so **no stock Debian Mesa driver can create an ES1
context** — `radeonsi` on real hardware fails exactly like `llvmpipe`. A locally built
`-Dgles1=enabled` Mesa (software `softpipe` is enough) lives at `~/deps/mesa-es1-install`, built
rootless; the full recipe and its three pitfalls are in `docs/opengles1-backend.md`.

```bash
# build (never more than -j4 on this shared machine)
cmake --build cmake-build-opengles1 -j4

# tests -- the wrapper selects the ES1 Mesa and the virtual display :99
scripts/opengles1-test-env.sh ctest --test-dir cmake-build-opengles1 -R OpenGLES1

# 39-scene XNA parity corpus (measurement, not a gate)
scripts/opengles1-test-env.sh scripts/run-oracle-corpus-diff-opengles1.sh \
    ./cmake-build-opengles1/cna_oracle_render_opengles1 [tolerance]
```

Use the virtual display `:99`, never `:0` — the wrapper defaults to it so test runs do not pop
windows onto the desktop. A second build dir `cmake-build-easygl` exists for EasyGL baseline
comparisons (`cna_oracle_render_easygl` only).

### Validation status

- `ctest -R OpenGLES1`: **7/7 pass**, 51 pixel-asserted checks.
- Parity corpus: **6/39 exact at tolerance 0, 11/39 at tolerance 1**, vs **10/39** for EasyGL on
  the same corpus and machine. See `docs/opengles1-parity-report.md`.

---

## Six real defects found by running the code

All had shipped as "✅ code complete" after line-by-line review. Recorded because the pattern
matters more than the individual bugs: **review did not catch any of them; execution caught all
six.**

1. `OPENGLES1-24` — alpha test **inverted** for the Less/Greater family. The `AlphaTest` vec4's
   `z`/`w` are *branch outcomes* (`a < x ? z : w`, discard when negative), not pass/fail weights.
   Equal/NotEqual came out right by luck, which hid it.
2. `OPENGLES1-72` — `RenderTarget2D::GetData()` returned all zeroes; `ITextureBackend::GetData` is
   a no-op default the render target never overrode.
3. `OPENGLES1-11` — textures were not rebuilt after a context loss (sampled plain white).
4. `OPENGLES1-80` — nor were vertex/index buffer contents.
5. Sampler state was applied to **whichever texture happened to be bound**, not the one the 3D draw
   binds — `GraphicsDevice` pushes sampler state down before `DrawPrimitivesEx`, and GL keeps
   filter/wrap on the texture object. Point-filtered scenes rendered blurred.
6. `TextureAddressMode::Mirror` silently degraded to Wrap despite `GL_OES_texture_mirrored_repeat`
   being available.

---

## Traps worth knowing before touching this backend

- **`Clear` forces the depth mask writable** so the clear value always lands (`OPENGLES1-7`,
  matching EasyGL). A `SetDepthWriteEnabled(false)` issued *before* a clear is therefore undone by
  it. Not a bug; set the toggle after the clear.
- **`GraphicsDevice` holds a raw pointer to the last-applied `Effect`.** An effect created and
  destroyed inside a helper function segfaults the next draw. Keep effects alive across the draw.
- **NDC→window Y is flipped.** Compare pixel *counts* (e.g. wireframe vs solid) rather than
  guessing where an edge lands on screen.
- **A relinked library does not relink dependent test binaries** in every `cmake --build`
  invocation — a "failing" test after a source revert may just be a stale binary. Rebuild
  explicitly before believing a result.

---

## Immediate next steps

1. `OPENGLES1-81` — `DualTextureEffect` ignores the second UV set (design decision 7's claim that
   both units share one UV set is **factually wrong**; the `dualtexture_quad` scene has two).
2. `OPENGLES1-82` — linear fog when `FogEnd <= FogStart`; check FNA's own formula first, since
   EasyGL diverges here too (19661 px) — the ES1 side may not be the one at fault.
3. Then the rest of Phase 3 in table order (`OPENGLES1-83` onward).

## Open decisions / assumptions

- `OPENGLES1-93` (compressed texture upload) needs a **shared** `ImageData`/`Texture2D.cpp` change
  affecting every backend. Recorded but deliberately **not started** without a human decision.
- OPENGLES1 is intentionally **not** wired into `scripts/run-all-backend-smoke-tests.sh` or CI: it
  needs a hand-built Mesa nobody else has, so adding it would fail other people's runs.
- The parity corpus is a **measurement, not a gate** (same precedent as `D9-A6`). It exits non-zero
  only when a scene fails to render, never merely because it differs.
