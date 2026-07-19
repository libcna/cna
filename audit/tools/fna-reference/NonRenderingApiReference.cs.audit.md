# Audit: tools/fna-reference/NonRenderingApiReference.cs

## Metadata
- Source file: `tools/fna-reference/NonRenderingApiReference.cs` (147 lines)
- Audit status: AUDITED (full read)
- Subsystem: `tools-fna-reference` shard
- File type: C# tool (Task 472, part of the `FnaReference` console app)
- XNA/FNA relevance: generates authoritative FNA reference data (enum numeric values, built-in
  state-object preset property values) using the real compiled FNA.dll
- Main related tests: consumed by `tools/cna-reference/` + `scripts/compare-fna-reference.py`

## Purpose
Reflectively dumps the numeric values of 21 `Graphics`-namespace enums and every public
instance-property value of all 16 built-in `BlendState`/`DepthStencilState`/`RasterizerState`/
`SamplerState` presets, using real FNA reflection rather than hand-transcribing each value.

## Executive Verdict
Correct, and a genuinely well-reasoned design choice: using `Enum.GetValues`/`Convert.ToInt32` and
generic property reflection instead of hand-listing each enum member's numeric value or each
preset's field values removes any chance of a hand-copied transcription error, and automatically
picks up a state-class member added or renamed later without this file needing an update.

## Checklist Results
- `DumpEnum()` (lines 89-97) is fully generic — correctly handles enums with non-sequential/
  explicit values (`ClearOptions`/`ColorWriteChannels` as bit flags; `PresentInterval`/
  `SpriteSortMode`/`SpriteEffects` with explicit non-zero-based numbering) since it reads each
  member's real runtime value via `Convert.ToInt32`, not an assumed sequential index.
- `DumpProperties()` (lines 105-145) correctly skips indexers (`GetIndexParameters().Length > 0`)
  and gracefully skips a property that throws `TargetInvocationException` on read (a documented,
  reasonable defensive choice: "property genuinely not readable in this state; skip rather than
  crash") rather than letting one unreadable property abort the entire dump.
- Enum-valued properties are correctly converted via `Convert.ToInt32` (matching `DumpEnum`'s own
  approach) rather than `ToString()`, avoiding a locale/culture-dependent enum-name string where a
  stable numeric value is what a cross-language comparison actually needs.

## Detailed Findings
None.

## Cross-File Observations
The README's own Task 472 status note (`tools/fna-reference/README.md`) credits this file's purely
reflection-based, non-hand-picked design with surfacing "one genuine, previously-unremarked
finding purely from the generic reflection approach: `BlendState` has 4 separate
`ColorWriteChannels`/`1`/`2`/`3` properties (one per MRT render-target slot)" — independently
consistent with this session's own `xna-graphics` shard audit, which confirmed CNA's `BlendState`
correctly implements all 4 MRT-target `ColorWriteChannels` properties.

## Missing or Weak Tests
Not independently located in this pass; "Exceptions" (this task's own third originally-scoped item,
per this file's own top comment) is explicitly and honestly deferred, not silently skipped, since
every exception-throwing Graphics-namespace validation guard needs a live `GraphicsDevice` this
sandbox cannot construct.

## Positive Findings
The reflection-based, not-hand-transcribed design is exactly the right engineering choice for a
tool whose entire purpose is eliminating hand-transcription error — and it already found one real,
useful, previously-unremarked fact (`BlendState`'s 4 MRT-slot `ColorWriteChannels` properties)
purely as a side effect of that design choice.

## Final Assessment
No findings.
