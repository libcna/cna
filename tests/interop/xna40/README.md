# Microsoft XNA 4.0 interoperability harness

`plans/plan_xnapipeline.md` `XNAP-32` / `XNAP-33` / `XNAP-34`.

## Status, stated plainly

**Executed against a genuine Microsoft XNA 4.0 runtime on 2026-09-06, and every fixture passed.**
Six of six loaded through the real `ContentManager` and every value each expectation manifest
declares matched:

```text
graphics device: AMD Radeon 780M (RADV PHOENIX) (Reach)
PASSED  texture2d_color_mips
PASSED  soundeffect_pcm16_mono_22050
         (not asserted) SoundEffect exposes no sample-rate, channel-count or PCM accessor in
         XNA 4.0, so only Duration is asserted here.
PASSED  spritefont_two_glyphs
PASSED  curve_two_keys
PASSED  list_of_strings
PASSED  model_triangle_basiceffect

fixtures: 6   failed: 0
XNA runtime: 4.0.0.0
```

The host was not Windows. It was Debian with the XNA 4.0 Refresh runtime installed into a Wine
prefix (`~/.wine-cna-xna40`: the GAC, `XnaNative.dll`, and Direct3D 9 through DXVK), the harness
compiled with mono's `mcs` against the SDK reference assemblies, and run under
`DISPLAY=:99` on Xvfb. `plans/plan_xnapipeline_parity.md` `XNAPP-280` records the exact recipe.

One change to the harness itself was needed, and it is worth knowing about: `Game.RunOneFrame()`
returns on this host with `GraphicsDeviceManager.GraphicsDevice` still **null**, so every
`Texture2D`, `SpriteFont` and `Model` failed with `GraphicsDevice component not found` -- a
message that says nothing about the `.xnb` and everything about the host. Calling the documented
`((IGraphicsDeviceManager)manager).CreateDevice()` creates a real device, and the harness now does
that first and falls back to `RunOneFrame()`. The "no usable graphics device" limitation this file
used to describe was therefore avoidable, not inherent.

What was already verified, without XNA, and still is:

| Evidence | What it shows |
|---|---|
| CNA reads back everything it writes (`XnbAssetWriterTests`) | the writer and CNA's own independent reader agree |
| An independent Python parser validates every fixture (`tools/xnb/xnb_conformance.py`) | the bytes satisfy the format specification, judged by code that shares nothing with CNA |
| CNA reproduces a genuine XNA 4.0 file byte for byte (`XnbWriterTest.GoldenXna40ListOfStringsIsByteIdentical`) | for `List<string>`, CNA's container header, type-reader table spelling, 7-bit encoding, object dispatch and string encoding are *identical* to Microsoft's own Content Pipeline output |

What the run does **not** show: only these six roots were exercised, and only the values XNA 4.0's
public API exposes. The `(not asserted)` lines are honest gaps, not passes.

### The LZX corpus does not load, and that is a real defect

The same six assets, written with CNA's own LZX encoder
(`tests/assets/xnb/cna/windows/lzx/`), are **all six refused** by the same runtime, in the same
run, with the same harness:

```text
ERROR   texture2d_color_mips
         InvalidOperationException: Error decompressing content data.
...
fixtures: 6   failed: 6
```

This is exactly the outcome this file says is the most valuable one the harness can produce, and
it is worth being precise about what it does and does not implicate:

* The **container is not the problem.** The header is well formed -- `XNB w 5`, flags `0x80`, a
  file-size field that matches the bytes on disk, and a decompressed-size field -- and the first
  chunk's framing is correct (`FF`, big-endian frame size, big-endian block size, and the block
  length agrees with the remaining bytes).
* **CNA's own decoder reads these files, and so does the independent Python parser**
  (`tools/xnb/xnb_conformance.py` reports `OK ... lzx` for all six). Two implementations that
  share no code agree the stream is valid LZX; Microsoft's does not.
* So the defect is inside the LZX bitstream, in something both CNA decoders are lenient about.
  Even the 220-byte, almost entirely literal `list_of_strings` fails, which points away from
  match encoding and towards the block header or the pretree/tree-length encoding.

Until it is fixed, **CNA must not claim that its LZX output loads in XNA 4.0.** The uncompressed
corpus does; the compressed one does not. `cna-content`'s own `--xnb-compress lzx` help text and
`plans/plan_xnapipeline.md` `XNAP-81` both need that correction.

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
  have nothing to do with CNA; `Curve` and `List<string>` still exercise the container. The
  harness now says which case it is: it prints the adapter and profile when a device was created,
  and a line naming the host when one could not be.
