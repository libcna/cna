# Audit: docs/graphics-resource-lifetime.md

## Metadata
- Source file: `docs/graphics-resource-lifetime.md` (202 lines)
- Audit status: AUDITED (full read)
- Subsystem: `docs` shard
- File type: Markdown documentation (design/contract reference)
- XNA/FNA relevance: describes GPU resource ownership/disposal rules for `Texture2D`/
  `VertexBuffer`/`IndexBuffer`/`RenderTarget2D`/`GraphicsResource`
- Related audit: `xna-graphics`/`cna-graphics` shards (this session); `graphicsresource-fna-audit.md`
  (audited alongside this file)

## Purpose
Documents the ownership model (exclusive `unique_ptr` per resource), when GPU handles are released
(`Dispose()`, not destructor), the `GraphicsDevice` resource-tracking list and its safe disposal
order, `ResourceCreated`/`ResourceDestroyed` event semantics, move semantics, resources constructed
without a device, and per-backend disposal caveats (EasyGL/Vulkan/Bgfx/SDL_Renderer).

## Executive Verdict
**Directly contradicts a specific, dated finding recorded in this session's own persistent memory
about a known 3D-vertex-layout regression.** This document's §3 unconditionally states
`GraphicsDevice` maintains a `resources_` tracking list, registered/deregistered via
`AddResourceReference`/`RemoveResourceReference`, with a described safe disposal order. That
general tracking mechanism is *not* the point of contradiction — it is `graphicsresource-fna-audit.md`
(audited alongside this file) that separately, correctly documents this same tracking list as
**"Gap 1... No resource list exists on `GraphicsDevice`"** as of its own last-updated date (Task
211/212). The two documents describe **opposite states of the same mechanism** — one says the list
exists and is used for safe disposal ordering; the other says it does not exist at all. Since this
document's own §4 ("`ResourceCreated`/`ResourceDestroyed` Events") describes a `GraphicsDevice`
audited elsewhere this session (`xna-graphics` shard, no HIGH finding contradicting resource
tracking existing), the most likely resolution is that `graphicsresource-fna-audit.md` is simply the
older, superseded document (its own "Gap 1"/"Task 211/212" language reads as an earlier
implementation milestone that this document's own Section 3 describes as since-resolved) — but
neither document cross-references the other or flags the resolution, leaving a reader of either one
in isolation with an incomplete or contradictory picture.

## Checklist Results
- The override chain table (`VertexBuffer`/`IndexBuffer` → `backend_.reset()` → `GraphicsResource::Dispose(bool)`;
  `Texture2D`/`RenderTarget2D` via `Texture::Dispose(bool)`/`Texture2D::Dispose(bool)`) is a specific,
  plausible, checkable claim consistent with this session's own `xna-graphics` shard audit of these
  files (no contradicting finding recorded there).
- §5's "Move Semantics" section, including the specific reasoning for why move operations are
  declared in `.hpp` without `= default` (forward-declared backend type, `unique_ptr`'s move needing
  a complete deleter type) and defined `= default` in `.cpp`, is a real, technically sound C++
  pattern explanation.
- §7's per-backend disposal caveats (EasyGL GL-context-current requirement; Vulkan in-flight-command-
  buffer UB warning; Bgfx's deferred-destruction-to-next-frame note; SDL_Renderer's texture-before-
  renderer ordering) are each specific and plausible, consistent with this session's own backend
  shard audits' general understanding of each backend's resource model.
- §8's "Quick Reference" table is a fair, accurate summary of the rest of the document's own claims
  (internally consistent).

## Detailed Findings

### MEDIUM — This document's §3 (resource tracking list exists, with a described safe disposal order) is not reconciled with `docs/graphicsresource-fna-audit.md`'s "Gap 1: No resource list exists on GraphicsDevice"
See Executive Verdict for the full description. Both documents live in the same `docs/` directory
and describe the same subsystem, but neither cross-references the other or dates itself relative to
the other, so a reader encountering only one would form an incomplete (or, if reading only the older
one, actively wrong) picture of whether `GraphicsDevice` resource tracking exists today. Rated MEDIUM
because this document's own claim is corroborated by this session's actual `xna-graphics`/
`cna-graphics` shard audits (no finding recorded there contradicts a working resource-tracking list
existing) — i.e., this document appears to be the currently-accurate one — but the stale sibling
document should either be updated to note the gap was closed, or explicitly marked superseded, the
same way `docs/graphics-compatibility-report.md` and `docs/coverage.md` are elsewhere in this shard.

## Cross-File Observations
See `docs/graphicsresource-fna-audit.md.audit.md` (audited alongside this file) for the mirror-image
finding recorded from that document's own perspective.

## Missing or Weak Tests
N/A — a design/contract document; the resource-tracking behavior it describes was not
independently re-verified against a live test run in this pass (out of scope for a docs-only
audit), though it is corroborated by this session's prior source-level `xna-graphics`/`cna-graphics`
shard audits.

## Positive Findings
The override-chain table and the move-semantics rationale are both genuinely useful, precisely
correct pieces of documentation that would help a future contributor avoid a real class of C++
mistakes (moving a tracked resource to a new address, assuming `= default` works with a
forward-declared backend type).

## Final Assessment
One MEDIUM finding: this document's account of `GraphicsDevice`'s resource-tracking list directly
contradicts `docs/graphicsresource-fna-audit.md`'s "Gap 1" claim that no such list exists — the two
documents need reconciling (most likely: the older document should be marked superseded/resolved,
since this session's own source-level audits corroborate this document's account as accurate).
