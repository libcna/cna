# Audit: examples/demo_avatar_wardrobe_hotswap/src/HotswapDemo.hpp

## Metadata
- Source file: `examples/demo_avatar_wardrobe_hotswap/src/HotswapDemo.hpp` (79 lines)
- Audit status: AUDITED (full read)
- Subsystem: `examples-demo_avatar_wardrobe_hotswap` shard
- File type: standalone `Game`-subclass demo header (Task 15.16)
- XNA/FNA relevance: exercises `SkinnedModelEXT::AttachPartEXT`/`RemovePartEXT` repeatedly *at
  runtime*, plus `ContentManager::Unload()`
- Related production code: `ContentManager.hpp`/`.cpp` (this header's own comment cites a specific,
  verified claim about `Unload()`'s scope — see below)

## Purpose
Declares a live hairstyle-cycling demo: Tab cycles between baked-in hair, `wardrobe/hair_Cap`, and
`wardrobe/hair_Ponytail` at runtime without restarting the process.

## Executive Verdict
Correct, no findings, and the header's own comment (lines 25-32) demonstrates careful,
source-verified reasoning about a genuinely subtle lifetime question: restoring "baked-in" hair
requires a fresh reload (no `wardrobe/hair_baked` folder exists to attach from, and `RemovePartEXT`
already freed the original part's GPU buffers on first replacement), and the comment explicitly
states this was confirmed safe by *reading* `ContentManager::Unload()`'s implementation before
relying on it ("Confirmed `ContentManager::Unload()` only clears its own cache map... before
writing this").

## Checklist Results
- No `NetworkSession`/`GamerServices`-session dependency; no manual bone-weight-blending logic.
- The reasoning about why `model_`/`renderer_` cannot dangle after `Unload()` (they "hold
  independent shared_ptr/owning references, so clearing the cache cannot dangle them") is sound:
  `ContentManager::Load()` returning a `shared_ptr` that the caller then stores independently is
  exactly the ownership model that makes a cache-clear safe for existing holders.

## Detailed Findings
None.

## Cross-File Observations
This is the only file among the 8 shards in this batch that calls `ContentManager::Unload()` at
runtime (mid-demo, not just at shutdown) — a genuinely distinct lifetime scenario from every sibling
avatar demo, which only load content once in `LoadContent()` and never unload.

## Missing or Weak Tests
Not applicable — manual/visual-validation demo with a `--smoke`/`--screenshot` CI mode.

## Positive Findings
The header's own comment is an excellent example of verifying a lifetime assumption against the
actual `ContentManager::Unload()` implementation before depending on it, rather than assuming safety.

## Final Assessment
No findings.
