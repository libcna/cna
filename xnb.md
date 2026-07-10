# XNB binary content pipeline: analysis for a future CNA loader

**Status: analysis only. No implementation exists or is planned for the near term.** This
document exists to record what a real `.xnb` binary loader for CNA would actually require, so a
future task can be scoped accurately instead of guessed at from scratch. It is intentionally
**low priority** — see "Why this is low priority" below — and nothing in `plan_graphics.md` should
be read as scheduling this work soon.

## What `.xnb` actually is

`.xnb` is the binary output format of the XNA/MonoGame **Content Pipeline** build tool. A real XNA
or FNA game never ships loose `.png`/`.fbx`/`.wav` source assets — a build-time tool compiles them
into `.xnb` files, and `ContentManager.Load<T>()` at runtime only ever reads that compiled binary
format. CNA does not have a content pipeline build tool and does not read `.xnb` at all; every
asset type it supports today is loaded directly from a loose, human-authored/exported file (PNG
via `Texture2D::FromStream`, a CNA-original `.model.json` for `Model`, etc.).

This document is grounded in FNA's real, current source (`/rv/data/library/github.com/FNA-XNA/FNA/src/Content/`),
read directly rather than summarized from memory, to keep the analysis accurate.

### File layout

Confirmed from `ContentManager.GetContentReaderFromXnb()` and `ContentReader`:

1. **4-byte magic + platform**: `'X'`, `'N'`, `'B'`, then one ASCII platform-identifier character
   (`'w'` Windows, `'d'`/`'g'`/`'l'` DesktopGL/WindowsGL/Linux — the ones a desktop CNA port would
   plausibly need to accept — plus a dozen console/mobile identifiers CNA would never see).
2. **1-byte version** — FNA accepts `4` or `5` only; other values throw `ContentLoadException`.
3. **1-byte flags** — bit `0x80` means the payload is LZX-compressed.
4. **4-byte little-endian int32**: total XNB file length.
5. If compressed: a further **4-byte int32** decompressed size, then the LZX-compressed payload
   (see "Compression" below). If not compressed, the rest of the stream is read directly.
6. **Type-reader table** (`ContentTypeReaderManager.ReadTypeManifest` — a 7-bit-encoded-int count,
   then for each: a 7-bit-encoded-length string naming the .NET reader type + assembly, and a
   4-byte int32 "reader version"). This table is what a C++ port cannot resolve the way FNA does
   (see "The reflection problem" below).
7. **Shared-resource count** (7-bit-encoded int).
8. **The primary asset object**, read via whichever type reader the table's first-referenced entry
   maps to — nested objects, arrays, and primitives are read recursively through the same
   `ContentReader`, using the same 7-bit-encoded-int/string primitives .NET's `BinaryReader` uses.
9. **Shared resources**, read last, each with a 1-based index; anything read mid-stream via
   `ContentReader.ReadSharedResource<T>()` stores a placeholder index and gets "fixed up" (a
   deferred `Action<object>` callback) once the real object is read from the tail section. This is
   how the format supports resource graphs that aren't strictly tree-shaped (e.g. a `Model` mesh
   part referencing a `VertexBuffer` shared with another part).

### Compression: LZX

FNA's `LzxDecoder.cs` (745 lines) is a from-scratch reimplementation of the Microsoft Cabinet
LZX algorithm (window size fixed at 64KB / 16 bits for XNB), block-framed with a small header per
block (2-byte block size, with an optional 5-byte extended form when a block also specifies a
non-default 32KB frame size — see `ContentManager.GetContentReaderFromXnb` for the exact framing
loop). This is a real, nontrivial, from-scratch bit-level decompressor — not a call into zlib or
any common library. **A C++ port would need to either port this file line-by-line (a self-contained,
well-isolated ~750-line algorithm with no external dependencies) or find/vendor an existing LZX
implementation.** Given the CNA project's own convention of hand-porting rather than wrapping
external libraries where a construct is central to XNA compatibility, direct porting is the more
likely fit here, but the file is large enough that it is its own real subtask, not a rounding
error.

### The reflection problem

`ContentTypeReaderManager` resolves a type reader by looking up `System.Type.GetType(readerTypeName)`
via .NET reflection, then instantiates it — this is fundamentally a runtime string-to-type
mechanism .NET provides natively and C++ does not. FNA itself already had to work around exactly
this on AOT-compiled platforms (iOS) where reflection-based type discovery isn't available: it
maintains a static `Dictionary<string, Func<ContentTypeReader>> typeCreators` map, manually
populated per platform build, plus a `falseflag`-guarded dead code path whose only purpose is to
stop an over-eager linker from stripping reader types it can't see referenced. **This is the
closest real precedent for what a CNA port would need**: a hand-maintained, compile-time
`std::unordered_map<std::string, std::function<...>>` (or equivalent) mapping each known .NET
reader type-name string (e.g. `"Microsoft.Xna.Framework.Content.Texture2DReader"`) to a C++
factory function for the matching CNA type — not a generic reflection substitute, just an
explicit table sized to however many of the ~40 real FNA type readers CNA chooses to support.

### The ~40 real type readers, by realistic priority

FNA ships one reader class per serializable type (`src/Content/ContentReaders/*.cs`, 40 files).
Not all are equally relevant to a CNA port:

**Primitives/math** (trivial once `ContentReader`'s own binary primitives exist — thin wrappers
around `ReadSingle`/`ReadInt32`/etc., or a few fields of `Vector`/`Matrix`/`Color`/`Rectangle`/
`BoundingBox`/`BoundingSphere`/`Curve`): `BooleanReader`, `ByteReader`, `SByteReader`, `Int16/32/64Reader`,
`UInt16/32/64Reader`, `SingleReader`, `DoubleReader`, `DecimalReader`, `CharReader`, `StringReader`,
`DateTimeReader`, `TimeSpanReader`, `Vector2/3/4Reader`, `MatrixReader`, `QuaternionReader`,
`ColorReader`, `PlaneReader`, `PointReader`, `RectangleReader`, `BoundingBoxReader`,
`BoundingSphereReader`, `BoundingFrustumReader`, `RayReader`, `CurveReader`.

**Collections/generics** (need a real generic dispatch mechanism, another reflection-shaped
problem — `ArrayReader<T>`, `ListReader<T>`, `DictionaryReader<K,V>`, `NullableReader<T>` all
recursively invoke another type reader by generic parameter): `ArrayReader`, `ListReader`,
`DictionaryReader`, `NullableReader`, `ReflectiveReader` (the fallback for arbitrary
`[ContentSerializerAttribute]`-decorated POCOs — genuinely reflection-based in FNA and the single
hardest piece to port faithfully; a C++ port would need per-type generated/hand-written readers
instead of a generic fallback).

**Graphics-relevant, i.e. what CNA's own Graphics namespace would actually need**:
`Texture2DReader`/`Texture3DReader`/`TextureCubeReader`/`TextureReader` (base), `ModelReader`
(the big one — bones, hierarchy, meshes, shared `VertexBuffer`/`IndexBuffer`/`Effect` resources;
CNA's own `docs/model-content-pipeline-support.md` already documents in detail how far CNA's
current runtime `Model` API has come and how far its own JSON loader still is from this real
format), `EffectReader`/`EffectMaterialReader` (compiled `.fxb` shader bytecode — a separate,
already-tracked, already-forbidden-for-now concern; see "Relationship to Phase 74" below),
`BasicEffectReader`/`AlphaTestEffectReader`/`DualTextureEffectReader`/`EnvironmentMapEffectReader`/
`SkinnedEffectReader` (stock-effect material parameter blocks), `SpriteFontReader` (glyph atlas +
kerning tables — CNA's own `SpriteFont` is fully implemented at the runtime-API level per this
session's own audits, only content *loading* is the gap, exactly like `Model`), `VertexBufferReader`/
`IndexBufferReader`/`VertexDeclarationReader`.

**Non-Graphics** (out of this analysis's own scope, listed for completeness): `SongReader`,
`SoundEffectReader`, `VideoReader`, `ExternalReferenceReader`.

## Relationship to CNA's existing content approach

CNA's `ContentManager` (`src/Microsoft/Xna/Framework/Content/ContentManager.cpp`) already has a
working, tested, file-extension-based loading scheme for every asset type it currently supports —
`Texture2D` from real image files (PNG/JPG/etc. via `stb_image` or equivalent), `Model` from an
original `.model.json` + binary-sidecar scheme (see `docs/model-content-pipeline-support.md`),
audio from `.wav`/`.xact` project data, and so on. None of this is wire-compatible with real
FNA-produced `.xnb` assets, and a real XNB loader would not replace it — it would be a genuinely
new, additional loading path (`.xnb` files specifically), coexisting with the current
loose-file scheme rather than supplanting it, similar to how FNA itself supports a handful of
loose extensions directly (`.png`/`.jpg`/`.dds`/`.wav`/`.fxb`, per `ContentManager`'s own
`texture2DExtensions`/`soundEffectExtensions`/`effectExtensions` static lists) alongside its
primary `.xnb` path.

## Relationship to Phase 74 (compiled effect bytecode)

`plan_graphics.md` Phase 74 already tracks a separate, narrower concern: real XNA `.fx` compiled
effect bytecode support via MojoShader (for `Effect`'s bytecode constructor, which currently throws
`System::NotImplementedException` — see `src/Microsoft/Xna/Framework/Graphics/Effect.cpp`). That
phase is about interpreting *compiled shader bytecode itself*; a general XNB loader is a superset
concern (the container format everything, including compiled effects, ships inside) but the two are
independently scoped — fixing Phase 74 does not require a general XNB loader (a raw `.fxb` file can
be read directly, matching FNA's own loose `effectExtensions` support), and a general XNB loader
would still need Phase 74's own effect-bytecode work to make `EffectReader`-loaded content usable.

## Why this is low priority

- **No known CNA consumer needs it today.** Every existing CNA example/test/demo uses CNA's own
  loose-file content scheme. No task in `plan_graphics.md` is blocked on `.xnb` support.
- **The reflection-free type-dispatch table and the LZX port are both real, standalone engineering
  efforts** (rough shape: a hand-maintained reader-name→factory map sized to however many of the
  ~40 readers are actually wanted, plus a ~750-line algorithmic port with no external dependencies)
  — not a quick addition to an existing file.
- **The immediate practical benefit is narrow**: it would let CNA load asset files originally
  compiled for a real XNA/FNA game, which matters for someone trying to reuse an existing XNA
  game's *compiled* content without re-exporting it — a real but niche migration scenario (see
  `docs/migration-guide.md`'s own content-pipeline section, which already tells a migrating
  developer to expect to re-export or rewrite their content pipeline rather than assuming `.xnb`
  compatibility).

## If this is picked up later: suggested minimal scope

Not a task list, just an honest ordering suggestion for whoever scopes the real work:

1. `ContentReader`'s own binary primitives (7-bit-encoded int/string, matching .NET's
   `BinaryReader.Read7BitEncodedInt`) — small, no dependencies, needed by everything else.
2. The XNB header + type-reader-table parse (uncompressed case only, initially) — proves the
   container format end-to-end without needing LZX yet.
3. A handful of primitive/math type readers (Vector2/3/4, Matrix, Color, Rectangle) — cheap, and
   needed by every higher-level reader's own fields.
4. **Decision point**: LZX decompression (needed for any real, `xnbc`-packed FNA/XNA-shipped
   asset — most real games compress their `.xnb` files) vs. accepting only uncompressed test
   fixtures initially. Real-world FNA/XNA content is very commonly compressed, so skipping LZX
   long-term would defeat much of the point, but it's a legitimate way to prove the rest of the
   pipeline first.
5. `Texture2DReader` (the most self-contained, highest-value Graphics reader — no shared-resource
   fixups, no generic dispatch) as the first genuinely useful end-to-end target.
6. `ModelReader` and the stock-effect readers, once the shared-resource-fixup mechanism (needed by
   `ModelReader` specifically) is designed — this is also where CNA's own already-documented
   `ModelTypeReader` gaps (bone hierarchy, `ParentBone`, `BoundingSphere`, `Tag` —
   `docs/model-content-pipeline-support.md`) would need to be closed anyway, so there's a natural
   opportunity to fix both gaps together rather than separately.

Everything past step 2 is genuinely substantial, multi-task work — this document does not attempt
to size it in tasks or estimate effort in time, only to describe what the real pieces are.
