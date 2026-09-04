# Microsoft XNA 4.0 interoperability harness

`plans/plan_xnapipeline.md` `XNAP-32` / `XNAP-33` / `XNAP-34`.

## Status, stated plainly

**Nothing in this directory has ever been executed against a real Microsoft XNA 4.0 runtime by
CNA's own automation.** No environment CNA builds in has XNA installed, or Windows, or Wine, or
Mono, or .NET. Until someone runs this harness and records the result in
`plans/plan_xnapipeline.md` `XNAP-34`, every CNA document must describe XNA compatibility as
*unverified*, however well the fixtures parse elsewhere.

What *has* been verified, without XNA:

| Evidence | What it shows |
|---|---|
| CNA reads back everything it writes (`XnbAssetWriterTests`) | the writer and CNA's own independent reader agree |
| An independent Python parser validates every fixture (`tools/xnb/xnb_conformance.py`) | the bytes satisfy the format specification, judged by code that shares nothing with CNA |
| CNA reproduces a genuine XNA 4.0 file byte for byte (`XnbWriterTest.GoldenXna40ListOfStringsIsByteIdentical`) | for `List<string>`, CNA's container header, type-reader table spelling, 7-bit encoding, object dispatch and string encoding are *identical* to Microsoft's own Content Pipeline output |

That third row is the strongest available signal short of running XNA, and it is the reason this
harness is worth building: the remaining risk is concentrated in the per-type payloads, not in the
container.

## What you need

* Windows (XNA 4.0 is 32-bit Windows only).
* **Microsoft XNA Game Studio 4.0** or the **XNA Framework Redistributable 4.0**, so that
  `Microsoft.Xna.Framework*.dll` version 4.0.0.0 is installed.
* .NET Framework 4.0 and MSBuild (Visual Studio, or the standalone .NET Framework 4.0 SDK).

You do **not** need the XNA content pipeline, a `.contentproj`, or `mgcb`. The harness only
*loads* `.xnb` files; it never builds one. That is the whole point.

## The fixtures

The corpus lives at `tests/assets/xnb/cna/windows/uncompressed/` and is committed. It was produced
by CNA's own writer, through:

```sh
cmake --build <build-dir> --target cna_tool_xnb_interop_fixtures
<build-dir>/cna_tool_xnb_interop_fixtures tests/assets/xnb/cna/windows/uncompressed
```

Each fixture `X.xnb` has an expectation manifest `X.expected.json` stating what a correct runtime
must observe, and `fixtures.json` indexes them all with a one-line statement of what each is for.
The manifests are the single source of truth: both the Python conformance parser and this harness
read the same files.

Regenerating is deterministic — a test asserts the committed bytes do not drift — so a mismatch
after a rebuild is a real change in CNA's output, not noise.

## Running it

```bat
msbuild tests\interop\xna40\CnaXnbInterop.csproj /p:Configuration=Release
tests\interop\xna40\bin\x86\Release\CnaXnbInterop.exe tests\assets\xnb\cna\windows\uncompressed
```

Exit code 0 means every fixture loaded and every asserted value matched. Output looks like:

```text
PASSED  texture2d_color_mips
PASSED  soundeffect_pcm16_mono_22050
         (not asserted) SoundEffect exposes no sample-rate, channel-count or PCM accessor in
         XNA 4.0, so only Duration is asserted here.
...
fixtures: 6   failed: 0
XNA runtime: 4.0.0.0
```

The harness prints `(not asserted)` lines wherever the XNA 4.0 public API simply does not expose a
value the manifest declares. Those are honest gaps in what a runtime check can prove, not passes.

## Recording the result

Whatever happens, put it in `plans/plan_xnapipeline.md` under `XNAP-34`:

* the exact XNA runtime version the harness printed,
* the Windows version,
* the full harness output,
* for each fixture: loaded/failed, and every mismatch verbatim.

A failure is the most valuable outcome this harness can produce, because it is the only way CNA
learns something its own reader and the specification parser both agree on but XNA does not. Do
not "fix" a fixture to make the harness pass; fix the writer, regenerate, and record both.

## Known limitations of the harness itself

* `SoundEffect` in XNA 4.0 exposes only `Duration`; sample rate, channel count and PCM bytes are
  not observable, so they are checked by the Python parser and not here.
* `SpriteFont.Glyphs` is not public in XNA 4.0, so glyph rectangles are checked by the Python
  parser and not here; `MeasureString` is the runtime-observable proxy and is described in each
  SpriteFont fixture's `purpose`.
* Texture mip levels above 0 are not read back; only level 0's exact bytes are compared.
* The harness constructs a hidden `Game` to obtain a real `GraphicsDevice`. On a machine with no
  usable graphics device, `Texture2D`, `SpriteFont` and `Model` will fail to load for reasons that
  have nothing to do with CNA; `Curve` and `List<string>` still exercise the container.
