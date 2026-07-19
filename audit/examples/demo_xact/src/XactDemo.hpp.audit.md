# Audit: examples/demo_xact/src/XactDemo.hpp

## Metadata
- Source file: `examples/demo_xact/src/XactDemo.hpp` (69 lines)
- Audit status: AUDITED (full read)
- Subsystem: `examples-demo_xact` shard
- File type: standalone `Game`-subclass demo header
- XNA/FNA relevance: exercises `Microsoft::Xna::Framework::Audio`'s XACT surface
  (`AudioEngine`/`WaveBank`/`SoundBank`/`Cue`/`AudioCategory`)
- Related production code: `xna-audio` shard (already fully audited); `XactFileGen.hpp` (sibling
  file, audited alongside this one)

## Purpose
Declares a demo that generates its own minimal `.xgs`/`.xwb`/`.xsb` XACT project files at runtime
(via the sibling `XactFileGen.hpp`) and exercises `AudioEngine`/`WaveBank`/`SoundBank`/`AudioCategory`
end-to-end against them.

## Executive Verdict
Correct. `std::optional<AudioCategory>` for `musicCategory_`/`sfxCategory_` (rather than a
default-constructed-then-reassigned value, or a raw pointer) is the right tool for "may not exist
yet / may have failed to load," matching the `std::unique_ptr` members' own not-yet-loaded
semantics.

## Checklist Results
- `GetTypeNameHPP()` present.
- `cuePlayed_[4]`/`cuePlayFrames_[4]` fixed-size arrays correctly sized to the demo's own 4
  hardcoded cue definitions (`kFreqs`/`kCueNames`/`kCueCat` in the `.cpp`) — no off-by-one risk since
  both the array size and the loop bounds in the `.cpp` are the same literal `4`.

## Detailed Findings
None.

## Cross-File Observations
See `XactFileGen.hpp`'s report for the more substantial finding in this shard (independent
verification of the WaveBank `MiniWaveFormatEx` bit-packing).

## Missing or Weak Tests
Not applicable — manual/visual demo; `AudioEngine`/`WaveBank`/`SoundBank`/XACT parsing itself is
unit-tested elsewhere (`XactParserTests.cpp`/`XactParserFuzzTests.cpp`, part of the already-audited
`tests-cna-internal` shard).

## Positive Findings
Correct choice of `std::optional` for possibly-absent `AudioCategory` state.

## Final Assessment
No findings.
