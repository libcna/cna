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
