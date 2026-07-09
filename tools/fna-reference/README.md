# FNA reference-app generator

Task 471 (`plan_graphics.md` Phase 53, "FNA comparison harness"). A small C# console app that
references the real FNA.dll and emits selected reference values as JSON, so CNA's own C++ tests
can eventually diff against ground truth produced by *running* FNA itself, not just reading its
source. Not part of the CNA C++ build; never run by CNA at runtime.

## Prerequisites

- `mono`/`xbuild` (this project targets the same legacy `ToolsVersion=4.0` .NET Framework 4.0
  project style FNA's own `FNA.csproj` uses — no `dotnet` CLI or NuGet required).
- A built `FNA.dll` at `/rv/data/library/github.com/FNA-XNA/FNA/bin/Debug/FNA.dll` (this
  project's own documented "Source Reference" checkout, see `CLAUDE.md`). If missing:

  ```bash
  cd /rv/data/library/github.com/FNA-XNA/FNA
  git submodule update --init lib/FAudio lib/FNA3D lib/SDL2-CS lib/SDL3-CS lib/Theorafile lib/dav1dfile
  xbuild FNA.csproj /p:Configuration=Debug
  ```

## Build and run

```bash
cd tools/fna-reference
xbuild FnaReference.csproj /p:Configuration=Debug
mono bin/Debug/FnaReference.exe [output.json]
```

Defaults to writing `reference-values.json` next to the built executable if no output path is
given.

## Status

Task 471 (this scaffold) is done: proves the whole harness end to end — real `FNA.dll` resolves
and loads under mono (`<Private>True</Private>` copies it next to the built exe; the default
`Private=False` reference-only mode fails at runtime with a `FileNotFoundException`, since mono's
assembly resolver doesn't consult the original `HintPath` location), a real non-`GraphicsDevice`-
dependent FNA API call executes correctly (`MathHelper.Pi`/`PiOver2`/`PiOver4`/`TwoPi`,
`Color.CornflowerBlue`'s real packed RGBA value), and the result is written as JSON via
`JsonWriter.cs` (a tiny, dependency-free hand-rolled writer — no NuGet-fetched JSON library is
viable in this sandbox).

Tasks 472-480 (the rest of Phase 53) build real per-class reference-value coverage on top of this
scaffold — not yet started. Task 474 onward (`BasicEffect` defaults, and anything else
constructed via `BasicEffect(GraphicsDevice device)`/similar) will need a real, live
`GraphicsDevice` — FNA's own equivalent of the same windowing/GPU-context problem this project's
own CNA test suite already solves via Xvfb; not yet investigated for the FNA/mono side.
