# XNA Game Studio 4.0 Content Pipeline oracle

`plans/plan_xnapipeline_parity.md` Phase 1 (`XNAPP-010`–`XNAPP-013`).

This directory holds the programs that turn a legally installed **Microsoft XNA Game Studio 4.0
Refresh** into a machine-readable, deterministic description of its Content Pipeline — the
denominator every parity percentage in that plan is measured against. Nothing here is guessed
from memory, and nothing here reads a method body.

## What is in the directory

| File | Role |
|---|---|
| `PipelineApiOracle.cs` | Reflection program. Loads the seven `Microsoft.Xna.Framework.Content.Pipeline*.dll` assemblies and writes `tests/reference/xna40/content-pipeline-api.json`: every public/protected type and member, attributes, enum values, importer declarations (file extensions, default processor, display name, `CacheImportedData`), processor declarations (input/output types, public properties and their **instantiated default values**), the public `ContentTypeWriter`/`ContentTypeSerializer` roots, the runtime assembly's `ContentSerializer*` attributes, and every external type the public surface refers to. |
| `merge_xml_docs.py` | Joins that inventory with `Microsoft.Xna.Framework.Content.Pipeline.xml`, the IntelliSense documentation shipped in the SDK, into `tests/reference/xna40/content-pipeline-api-docs.json`: summaries, parameter text, return text and the **documented exceptions** per member, plus both directions of disagreement (public-but-undocumented, documented-but-not-public). |
| `run-oracle.sh` | Compiles the oracle with mono's `mcs`, runs it under the real .NET Framework 4.0 in a Wine prefix, normalizes and publishes the JSON. |

## Provenance boundary

The oracle observes exactly two kinds of thing, both allowed by the plan's provenance rules:

1. **Public metadata** — type names, signatures, attributes, enum values, inheritance — read with
   `System.Reflection`. The `Localized*Attribute` subclasses Microsoft applies to its built-in
   components are read through their public `ContentImporterAttribute`/`ContentProcessorAttribute`
   base properties, which is how MSBuild's `BuildContent` task sees them.
2. **Black-box behaviour** — each built-in processor is constructed with its parameterless
   constructor and its public properties are read back. That is what `BuildContent` does before
   applying `ProcessorParameters_*` item metadata, so the values recorded are the defaults a
   `.contentproj` gets when it says nothing.

It never calls `MethodBase.GetMethodBody()`, `GetILAsByteArray()`, any decompiler, or any
non-public member. It references no XNA assembly at compile time.

Two classes of public type are inventoried and then **excluded from the denominator**, with the
rule written into the JSON itself (`excludedToolchainArtifactRule`, and the visibility rule in
the source): C++/CLI compiler shims in the mixed-mode FBX/X importer assemblies (namespaces `std`,
`<CrtImplementationDetails>`, `fbxsdk_*`), and `public` members nested inside `internal` classes
(`ReflectionEmitUtils.*`, `UnsafeNativeMethods.AudioHelper`), which no consumer can name.

## Prerequisites

* The XNA Game Studio 4.0 reference assemblies: `References/Windows/x86/` from an XNA Game Studio
  4.0 (Refresh) installation. On this machine the extracted installer lives under
  `/rv/tmp/samples/_tools/xna-game-studio-4-refresh/`; set `CNA_XNA40_REFERENCES` elsewhere.
* A Wine prefix with .NET Framework 4.0 (`winetricks dotnet40`), default `~/.wine-cna-xna40`;
  set `CNA_XNA40_WINEPREFIX`. Two importer assemblies are mixed-mode x86, so mono cannot load
  them and the Microsoft runtime is required.
* `mcs` (mono) to compile, `wine` to run. On Windows, `csc.exe` compiles the same source and the
  binary runs natively.

## Running

```sh
tools/xna-pipeline-oracle/run-oracle.sh                     # writes tests/reference/xna40/content-pipeline-api.json
python3 tools/xna-pipeline-oracle/merge_xml_docs.py \
    tests/reference/xna40/content-pipeline-api.json \
    "<References/Windows/x86>/Microsoft.Xna.Framework.Content.Pipeline.xml" \
    tests/reference/xna40/content-pipeline-api-docs.json
```

The output is byte-deterministic: two runs over the same assemblies produce identical files, and
`content-pipeline-api.json` records each assembly's SHA-256 and MVID so a regenerated inventory
can be tied to the exact binaries it describes.

## What the committed inventory says (XNA Game Studio 4.0 Refresh, 2026-09-05)

The `counts` object in `content-pipeline-api.json` is authoritative; the plan quotes it and
`tools/xna-pipeline-oracle/parity_report.py` recomputes every percentage from it. Do not restate
the numbers here.

## Intermediate XML oracle (`intermediate/`)

`intermediate/IntermediateOracle.cs` is a second driver, compiled the same way, that runs the
genuine `IntermediateSerializer` over CNA-authored types: it serializes a corpus of object graphs
(primitives, strings, math types, enums, collections, nullables, polymorphism, shared resources,
external references, serializer attributes, deep nesting, root values of every kind), round-trips
each through the genuine deserializer, and hands the deserializer hand-written XML variants to
record what it accepts and with which message it refuses the rest.

```sh
tools/xna-pipeline-oracle/intermediate/run-intermediate-oracle.sh   # writes tests/reference/xna40/intermediate/
```

The result is the format specification `docs/xna-intermediate-xml-format.md`; the corpus's
`manifest.json` lists every case with its verdict. Extend the driver when a question about the
format comes up — the document only states what a case in the corpus shows.

## Graphics content behaviour oracle (`graphics/`)

`graphics/GraphicsContentOracle.cs` runs the genuine `Microsoft.Xna.Framework.Content.Pipeline.Graphics`
object model -- `BitmapContent`, every `PixelBitmapContent<T>`, the DXT bitmaps, `MipmapChain`,
the `TextureContent` family, `VectorConverter` -- and records what it does: pixel layouts,
conversion rounding, resize and mipmap results, DXT sizes, validation messages, converter tables.

```sh
tools/xna-pipeline-oracle/graphics/run-graphics-oracle.sh   # writes tests/reference/xna40/graphics/graphics-content-oracle.json
```

The texture paths create a Direct3D device, so the driver needs the pipeline's native helper
(`XnaNative.dll`, copied from the installed framework into the ignored build directory) **and an
X display** (`CNA_XNA40_DISPLAY`, `:99` by default); without either, every resampling, DXT and
mipmap case reports "Specified method is not supported", which is the environment, not XNA.

## Not committed

The Microsoft assemblies, the compiled oracles and the Wine-side temporaries live only under the
ignored `build/xna-pipeline-oracle/`. Committing them is prohibited by the plan's provenance
rules and by Microsoft's licence.
