# Audit: docs/spritefont-support.md

## Metadata
- Source file: `docs/spritefont-support.md` (139 lines)
- Audit status: AUDITED (full read)
- Subsystem: `docs` shard
- File type: Markdown documentation (closes Phase 48, `plans/plan_graphics.md` Tasks 421-430)
- Cross-references: `xna-graphics` shard's `SpriteFont.cpp`/`SpriteBatch.cpp` audit (2 confirmed HIGH
  findings, audited directly by the parent session, not a fork)

## Purpose
Documents `SpriteFont`'s API-surface audit, `MeasureString` fixes, pixel-verified `DrawString`
rendering across SDL_Renderer/EasyGL, the UTF-8 decoding architecture, and known limitations.

## Executive Verdict
Mostly an exemplary piece of honest documentation — §7 ("Known limitation: no combined
`SpriteEffects` flip") independently and correctly discloses the exact same 3-vs-4-entry
axis-direction-table gap the `xna-graphics` shard audit confirmed as a HIGH-severity finding. **But
this doc materially understates that finding's real severity**, and separately never mentions the
sibling HIGH finding (default-character-fallback UB) at all.

## Checklist Results
- §7's technical description of the gap (`axisDirX`/`axisDirY`/`axisIsMirroredX`/`axisIsMirroredY`
  tables having only 3 of the 4 entries FNA's own tables have) is accurate and matches the
  `xna-graphics` shard's independent finding exactly — genuine convergent confirmation from two
  separate audit passes.
- The doc's own framing calls this "a genuine, **minor** API gap versus FNA... flagged here for
  visibility rather than silently left undocumented" and explicitly states index 3 is "unreachable
  through any normal calling convention and was not worth including speculatively."
- **This framing is contradicted by this audit's own confirmed finding**: the `xna-graphics` shard
  audit found index 3 (`FlipHorizontally | FlipVertically`) IS already reachable in the existing
  codebase via an explicit `static_cast<SpriteEffects>(3)`, already used at
  `examples/sdlgpu_2d_test.cpp:126` — not a hypothetical, but a real call site in this repository
  today. Reaching index 3 through that live call site causes an out-of-bounds stack read on the
  4-entry-sized lookup tables (which only have indices 0-2). This is a real memory-safety defect
  reachable from existing example code, not merely "an ergonomic gap nobody would hit."
- The doc's "Unknown-character → DefaultCharacter fallback" row (§4 table, `✅ Task 693`/`✅ Task
  427`) claims this behavior is fully pixel-verified on both backends. This is true for the tested
  case (an unknown character falls back to a `DefaultCharacter` that IS itself present in the
  character set) but **the doc never mentions the separate, sibling HIGH finding**: when
  `DefaultCharacter` itself is not in the character set, `MeasureString()`/`DrawString()`'s fallback
  dereferences an invalid `unordered_map::end()` iterator — real UB — where FNA's real equivalent
  throws a clean `KeyNotFoundException` in the same case. This edge case is outside what Tasks
  427/693 tested and is not disclosed anywhere in this document.

## Detailed Findings

### MEDIUM — §7's severity framing understates a finding this audit confirmed as HIGH (reachable OOB read, not just an ergonomic gap)
See Checklist Results. The doc's own technical description is accurate; its characterization of
impact/severity is not — "minor API gap... not worth including speculatively" undersells a real,
already-reachable out-of-bounds stack read. Recommend (report-only, no source changes made per this
audit's scope) updating this section once the underlying `SpriteBatch.cpp` fix (adding the 4th table
entry, or gating construction of an invalid `SpriteEffects` combination) lands, and reframing the
severity in the interim.

### LOW — Doc is silent on the separate default-character-fallback UB finding
Not a false claim (the doc's tested scope is accurate for what Tasks 427/693 actually covered), but
an omission of a real, confirmed-HIGH, closely-related defect in the exact same subsystem this
document is otherwise very thorough about. A reader relying on this doc as the complete picture of
`SpriteFont`'s known gaps would not learn about this second issue.

## Cross-File Observations
Directly, independently corroborates the `xna-graphics` shard's SpriteEffects axis-table finding —
genuine convergent evidence from two separate audit passes (this doc, written by the project's own
prior development work, and this audit's direct code-level review) that the gap is real. Extends,
rather than being extended by, the earlier finding: this doc additionally reveals a real reachable
call site (`examples/sdlgpu_2d_test.cpp:126`) the earlier finding did not cite.

## Missing or Weak Tests
The doc itself states Vulkan/Bgfx have no dedicated SpriteFont pixel-verification pass — an honestly
disclosed coverage gap, not a false claim of parity.

## Positive Findings
§6 ("Character encoding")'s disclosure of "invalid or truncated UTF-8 sequences decode to `'?'`...
byte index always advances by at least one byte, so malformed input can never cause an infinite
loop" is a precise, valuable safety guarantee statement. §4's "trailing newline adds a full second,
empty line's height" callout is a genuinely non-obvious FNA behavior correctly identified and
explained with the exact reason (both the `\n` handler's own height-add and the loop's unconditional
final height-add fire) — the kind of subtle-but-important behavioral detail that's easy to get wrong
without this note.

## Final Assessment
One MEDIUM finding (this doc's own §7 undersells a real, already-reachable OOB read as "minor"), one
LOW finding (silent on the separate default-character-fallback UB). Otherwise an unusually rigorous,
well-organized, and honest documentation file — its §7 disclosure independently corroborates this
audit's own HIGH finding, strengthening confidence in both.
