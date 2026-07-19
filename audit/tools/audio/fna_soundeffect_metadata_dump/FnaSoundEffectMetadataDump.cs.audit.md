# Audit: tools/audio/fna_soundeffect_metadata_dump/FnaSoundEffectMetadataDump.cs

## Metadata
- Source file: `tools/audio/fna_soundeffect_metadata_dump/FnaSoundEffectMetadataDump.cs` (144 lines)
- Audit status: AUDITED (full read)
- Subsystem: `tools-audio` shard
- File type: C# tool (real FNA/.NET code, run via `mono`/`mcs` — not part of CNA's C++ build)
- XNA/FNA relevance: directly re-implements FNA's own real `SoundEffectReader`/`ContentReader`
  parsing logic, verbatim where noted, as an independent differential-testing reference
- Main related tests: differentially compared against `SoundEffectContentTypeReaderTests.cpp`'s own
  fixtures per this file's own top comment

## Purpose
Parses a real, uncompressed `.xnb` file's `SoundEffectReader` body using FNA's own real field-
reading logic and prints every `WAVEFORMATEX` field plus loop points/duration as key=value lines,
for differential comparison against CNA's own C++ parsing of the same fixture bytes.

## Executive Verdict
Correct, and a genuinely valuable differential-testing design: this runs on an independent runtime
(mono, not CNA's own C++/SDL3 stack) executing FNA's *actual* parsing logic, not a hand-authored
expectation — a materially stronger verification method than "does CNA's parser produce the value
the test author expected," since a bug shared between both a hand-authored C++ test expectation and
CNA's own implementation would not be caught by that weaker method but would be caught here.

## Checklist Results
- `Swap()` overloads (lines 36-52) are explicitly noted as "copied verbatim (MS-PL permits this --
  same license as CNA itself)" — consistent with this project's own established, already-recorded
  convention of copying FNA source/comments verbatim under MS-PL (see this project's persistent
  memory note on this exact point).
- `MiniReader`'s `Read7Bit()` (lines 26-30) correctly exposes `BinaryReader`'s `protected
  Read7BitEncodedInt()` via a minimal subclass — explicitly noted as mirroring "the same reason
  FNA's real ContentReader does" this exact same-shape workaround, not an ad-hoc invention.
- The XNB header parsing (lines 65-77) and type-reader-table preamble (lines 85-97) correctly use
  real `.NET` `BinaryReader` methods (`ReadByte`/`ReadUInt32`/`ReadString`/`Read7BitEncodedInt`) —
  matching the file's own claim of "not a reimplementation of them."
- Compressed `.xnb` files are correctly detected and rejected with a clear, scoped error message
  (lines 78-83) rather than attempting to parse compressed data as if uncompressed.

## Detailed Findings
None.

## Cross-File Observations
This tool's stated counterpart, `tools/audio/xnb_audio_metadata_dump.cpp` (audited alongside this
file), deliberately does NOT report the same raw `WAVEFORMATEX` fields this tool does — that file's
own comment explicitly cites this one as the "from-first-principles WAVEFORMATEX field dump...if
that level of detail is ever needed," correctly avoiding duplicating scope between the two tools.

## Missing or Weak Tests
N/A — this file is itself a differential-testing tool; its "test" is the comparison against CNA's
own C++ parsing of the same fixture, not located/verified as an automated step in this pass (the
top comment implies a manual/scripted comparison workflow, consistent with this tool needing `mono`,
not guaranteed on every build machine).

## Positive Findings
The explicit "not a reimplementation of [.NET's own BinaryReader methods]" design choice, and the
verbatim-copy license note, both reflect careful, deliberate engineering rather than an
undocumented shortcut.

## Final Assessment
No findings.
