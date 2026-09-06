# Vulkan graphics renderer

## Status of this document

**This document is incomplete, and that is deliberate rather than an oversight.** The full
capability boundary is `plans/plan_vulkan.md` `VULKAN-480`, which is a Phase 13 row and has not been
written yet — it should be written *after* that phase's re-audits (`VULKAN-470`–`VULKAN-474`), so
that what it claims is checked rather than remembered.

What is here is the part that completed rows have measured and that a reader needs *before* the rest
exists. Each section names the row that put it there and the test that keeps it true. Anything not
listed below is not yet documented; read `plans/plan_vulkan.md` rather than assuming.

Select the renderer with:

```bash
cmake -S . -B cmake-build-vulkan -DCNA_GRAPHICS_RENDERER=VULKAN -DCMAKE_BUILD_TYPE=Debug
cmake --build cmake-build-vulkan -j
```

---

## Clip-space depth range: `[0, 1]`, and EasyGL differs

`VULKAN-098`, finding F-19. **Test:** `Vulkan_DepthRangeContract`
(`modules/renderers/vulkan/examples/vulkan_depth_range_contract_test.cpp`).

XNA is a Direct3D 9 programming model, and D3D9 maps clip space to depth over **`[0, 1]`** — a
vertex at `z = 0` is on the **near** plane, `z = 1` on the far plane. Vulkan's native range is the
same, so this renderer matches XNA without doing anything.

**EasyGL, the reference renderer, does not.** It leaves OpenGL's `[-1, 1]` clip depth in place, so
the same `z = 0` vertex lands at depth **0.5**. Measured on 2026-09-05 by compiling the identical
test source against both configurations:

| | Vulkan | EasyGL |
|---|---|---|
| 5×4 truth table (`z` × cleared depth, `LessEqual`) | **20/20** cells | 15/20 |
| `z = 0` survives a depth cleared to `0.4` | drawn | **rejected** |
| `z = -0.5` | **clipped** | drawn, as an ordinary depth of 0.25 |

Ordering is monotonic under both ranges, which is why this went unnoticed for so long: a test that
only asks "does `0.25` occlude `0.75`" passes on either. It takes an absolute comparison — a cleared
depth the two ranges fall on opposite sides of — to see it.

### What it costs, and who owns the other half

Two consequences worth knowing before writing anything that depends on depth:

- **An XNA scene loses half its depth precision on EasyGL.** Content authored for `[0, 1]` uses only
  the upper half of `[-1, 1]` there, so the depth buffer resolves half as finely as XNA's would.
- **A shared fixture cannot encode a depth value and be run on both.** It can compare depths, but
  the moment it asserts one, it is asserting a renderer-specific number. `Vulkan_DepthRangeContract`
  is therefore registered for Vulkan only, on purpose.

This renderer is the one that is right, so `plans/plan_vulkan.md` classifies it `VULKAN_STRONGER`
and changes nothing here. **The EasyGL side is owned by
[`plans/plan_graphics.md`](../plans/plan_graphics.md)** — its Phase 71, "EasyGL final gap closure" —
and it needs a row of its own there. `VULKAN-098` deliberately does not open one on another plan's
behalf; it names the owner so the divergence is not left implicitly nobody's.

---

## Cross-renderer conformance: what must match, what may differ, and how it is judged

`plans/plan_vulkan.md` `VULKAN-437`. **Tools:** `scripts/compare-easygl-vulkan-diagnostic.sh`
(`VULKAN-430`/`VULKAN-431`) and the 17 golden CTests `VULKAN-432`–`VULKAN-436` register.

This section exists because "the renderers agree" is not a measurement until someone says what
agreement means. Everything below is a rule this renderer is actually held to today, with the
number that was measured against it.

### What must match exactly

**Golden images.** Seventeen scenes are compared against the *same* PNGs under
`modules/renderers/easygl/examples/golden/` that EasyGL is compared against — the stock effects,
`EnvironmentMapEffect` and `SkinnedEffect`, the blend/depth-write/cull state goldens, the 2D
rotation and linear-filter goldens, and two golden-harness smoke tests. These are exact
comparisons with the harness's own per-pixel rule; there is no Vulkan-specific golden and there
must not be one. A golden that each renderer keeps its own copy of has stopped being a golden.

**The 2D corpus.** `cross_renderer_2d_corpus.cpp` is built once per renderer and its dumps are
compared byte for byte. Measured 2026-09-06: **max diff 0** between EasyGL and Vulkan. That is the
expected result, not a happy one — the corpus is deliberately built out of constructs where two
conforming 2D renderers have no licence to differ.

### What may differ, and why

**Rasterized coverage of a diagonal edge, by at most a channel step.** The 3D diagnostic scene
measured **max diff 1** at one pixel. Two hardware pipelines evaluating the same triangle's edge
and the same interpolation in a different order of floating-point operations will not always agree
on the last bit of an 8-bit channel. This is the only difference either tool has ever reported.

**What is excluded from the corpus, and stays excluded** (the list is in
`cross_renderer_2d_corpus.cpp` itself): linear filtering of a magnified sprite, mip selection and
mip-linear blending, anisotropy, MSAA, additive blending, and rotation by a non-right angle. Each
is a construct where two conforming renderers may legitimately disagree pixel-for-pixel. Including
one would force a tolerance wide enough to hide a real regression, which is the opposite of what a
conformance corpus is for.

**What is *not* on that list, and must never be added to it:** anything this renderer gets wrong.
`VULKAN-260` is the case in point — `EnvironmentMapEffect`'s Fresnel term was computed per fragment
here instead of per vertex, and the honest resolution was to fix the shader, not to declare Fresnel
a legitimate difference.

### How a tolerance is chosen

`cna_diag_compare` takes a per-channel tolerance. The gate uses **40**, and that number is
**inherited from the existing Software↔EasyGL comparison rather than picked for this pair**, so a
difference that matters here is one that would have mattered there.

The rule that matters more than the value: **a tolerance must not be doing any work.** The same run
that passes at 40 also passes at `--tolerance 2`, and the script records the measured maxima (1 and
0) beside the tolerance for exactly that reason. If a future run needs the tolerance raised, the
answer is an entry in the script's expected-difference list with its reason — never a wider number.
The list is currently empty.

### The standing rule: a conformance test must be able to fail

Every gate here carries its own proof that it can fail:

- `compare-easygl-vulkan-diagnostic.sh --perturb` corrupts two pixels of the Vulkan dump before
  comparing. Both pairs then report `max diff 255 … FAIL`, and the script **exits 3 if the
  perturbation is not detected** — a gate that cannot fail is treated as a failure of the gate.
- Every fix in this renderer's plan records a mutation that turns its test red, and several rows
  exist because the first mutation attempted **passed**: `VULKAN-172` (a mis-sized DXT block is
  invisible to pixels — it is a buffer over-read, so the assertion moved to where the two byte
  counts meet), `VULKAN-095` (a stale MSAA pipeline is invisible to a test that never draws through
  the MSAA path), `VULKAN-177` (a stale descriptor-set cache entry is invisible to both the pixels
  and the validation layer).
- Where a defect is reported at teardown and no in-process assertion can see it — `VULKAN-405`'s
  leaked framebuffer, `VULKAN-407`'s ten leaked handles — the discriminator is `VULKAN-393`'s
  output gate, which fails a CTest whose output contains a `[Vulkan Validation]` line. It covers
  every CTest in the configuration that can create a `VkDevice`.

### Where EasyGL is not the authority

EasyGL is the reference for *coverage and maturity*, never for semantics. Three rows in this plan
changed a shared test rather than this renderer, because the test encoded EasyGL's architecture as
if it were the contract: `VULKAN-095` (EasyGL cannot change MSAA post-construction; this renderer
can), `VULKAN-335` (`PresentationParameters` stores what the device *applied*, and EasyGL never
substitutes so the two look alike there), and `VULKAN-173`/`VULKAN-172`, which replaced
`#if defined(CNA_GL_PROFILE_*)` guards with the renderer's own `ClassifySurfaceFormatEXT` verdict
in three shared sources. And one row changed this renderer because EasyGL was right and it was not:
`VULKAN-260`.
