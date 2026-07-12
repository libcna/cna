# CNB: a lightweight, JSON-based content format for CNA (alternative to XNB)

> Companion document to [`xnb.md`](xnb.md)/[`plan_xnb.md`](plan_xnb.md), which describe what it
> would take to make CNA a *consumer* of original binary XNA `.xnb` files. This document describes
> a **different, permanent strategic choice**: CNA never implements a binary `.xnb` reader at all.
> Instead, CNA defines its own simple, human-readable `.cnb` (CNA content Binary/JSON — despite the
> name, it is plain JSON, not binary) sidecar format, and games/assets that used to ship `.xnb` are
> migrated once, offline, to either a directly-loadable native format (PNG/JPEG/WAV, loaded by
> extension) or a `.cnb` JSON file next to the original asset.
>
> **Nothing in this document is implemented yet.** This is a design/planning document only, in the
> same spirit as `xnb.md` — it exists so the two strategies (full binary XNB support vs. this
> lightweight `.cnb` approach) can be compared and a decision made before any code is written. See
> the "Relationship to `xnb.md`/`plan_xnb.md`" section at the end for how the two documents/plans
> are meant to coexist.

## Why this alternative exists

`xnb.md`/`plan_xnb.md` estimate a full, high-quality native `.xnb` reader (binary container, LZX
decompression, ~40 standard `ContentTypeReader`s, shared-resource fixups, `Model`/`Effect` object
graphs, and eventually a Lua-scripted custom-reader layer for game-specific readers) at several
hundred to well over a thousand hours/credits of work by the time broad real-world XNA compatibility
is reached (see `xnb.md`'s own credit-estimate discussion). That is a large, permanent maintenance
surface: a binary protocol parser, a compression codec, and a reader registry that must keep working
correctly forever, for a format CNA itself will never produce.

The alternative proposed here trades "drop-in binary compatibility with existing `.xnb` files" for
"a one-time, offline migration step per game/asset pack", in exchange for:

- No binary protocol to parse, version, or harden against malformed/adversarial input.
- No LZX (de)compression code to write, test, and maintain.
- No assembly-qualified generic type-name parsing, no shared-resource fixup graph, no reader-version
  negotiation — none of the `.xnb`-specific machinery in `plan_xnb.md`'s Phase A–D exists at all.
- A format that is trivially readable/diffable/editable by a human or a script, instead of an
  opaque binary blob.
- No dependency on ever being able to decode a *future* or *unknown* `.xnb` variant (MonoGame vs.
  FNA vs. original XNA vs. Windows Phone/Xbox 360 platform variants) — CNA only ever needs to read
  its own format, which it fully controls.

The explicit cost, accepted up front: a game that still ships original `.xnb`/`.fbx`/`.x` files
cannot be pointed at CNA and "just run". Its content must be converted first. See "What migration
actually requires" below for how large that cost really is in practice.

## The core rule

For any asset CNA is asked to load by logical name (e.g. `"Textures/player"`), resolve it as follows:

```text
1. If a file with that name AND a recognized native extension exists
   (e.g. "player.png", "player.jpg", "player.wav"), load it directly with the
   reader selected by that extension. No .cnb file is involved at all.

2. Otherwise, if "<name>.cnb" exists, load it as a .cnb JSON document. The
   "type" field inside the JSON selects which CNA loader/deserializer handles
   the rest of the document (see "Per-type .cnb conventions" below).

3. Otherwise: asset not found -- same failure behavior as today's
   ContentManager for a missing file.
```

Restated even more simply, per the issue description: **the file extension picks the reader, unless
the file is itself a `.cnb`, in which case the `"type"` field inside the JSON picks the reader.**
This is a strict two-level dispatch, not a fallback chain with many rungs — it deliberately mirrors
how `ContentManager.Load<T>()` already resolves a logical asset name to a file today, just with one
extra rule (`.cnb` sidecar) bolted on.

```cpp
// Sketch only -- illustrates the dispatch rule, not a proposed final API.
std::shared_ptr<void> ContentManager::Load(const std::string& assetName)
{
    if (auto nativePath = ResolveNativeExtension(assetName))
    {
        return LoadByExtension(*nativePath);   // .png/.jpg/.wav/... reader
    }

    const auto cnbPath = assetName + ".cnb";
    if (FileExists(cnbPath))
    {
        return LoadCnb(cnbPath);                // JSON "type" field dispatch
    }

    throw ContentLoadException("Asset not found: " + assetName);
}
```

### Natively-loadable extensions (no `.cnb` involved)

| Extension | CNA type | Notes |
|-----------|----------|-------|
| `.png`, `.jpg`/`.jpeg`, `.bmp` | `Texture2D` | Already implemented today via `Texture2D::FromStream` (SDL3_image-backed; see `AUDIT.md`) — this rule formalizes dispatch-by-extension on top of code that already exists. |
| `.dds` | `Texture2D`/`TextureCube` | `FromStream` already decodes DDS/DXT1/3/5 today (folded into the same function per `AUDIT.md`); this plan only needs a dedicated `DDSFromStreamEXT`-style dispatch by extension, not new decode logic. |
| `.wav` | `SoundEffect` | Already implemented (`AUDIT.md`: SDL3_mixer-backed `SoundEffect`). |
| `.ogg`, `.mp3` | `SoundEffect`/`Song` | If/where CNA's audio backend already supports these container formats; otherwise deferred, same as any other format gap — not a `.cnb`-specific concern. |
| (future) any format with an existing native CNA loader | whatever that loader produces | The extension-dispatch rule is intentionally generic — adding a new native format later is just adding one more row here, no `.cnb` schema change needed. |

None of the above need any design work from this document — they are either already implemented or
are ordinary "add a loader for format X" tasks independent of the `.cnb` idea. The interesting new
design surface is entirely in `.cnb` itself.

## `.cnb` file shape

A `.cnb` file is always a single JSON object with (at least) these top-level fields:

```json
{
  "cnbVersion": 1,
  "type": "SpriteFont",
  "...type-specific fields...": "..."
}
```

- `cnbVersion` — an integer schema version for the *envelope* itself (not per-type). Bump only if the
  envelope shape changes (e.g. if a `"assetName"`/`"sourceTool"` provenance field is added later).
  Per-type schema evolution is handled by each type's own fields/versioning, not this field.
- `type` — a stable string key (e.g. `"SpriteFont"`, `"Model"`, `"Effect"`, `"AnimationClip"`, or a
  game-specific type name for migrated custom data) that a registry maps to a C++
  deserializer/factory, structurally analogous to how `plan_xnb.md`'s reader registry maps an XNA
  reader name to a `ContentTypeReader`, but over a JSON key instead of a binary
  assembly-qualified name — so no generic-name parsing (`plan_xnb.md`'s XNB-13/13A problem) exists
  in this format at all.
- Everything else is defined per-type (see below). There is no shared binary primitive layer, no
  7-bit encoded integers, no shared-resource object graph — ordinary JSON object/array/number/string
  nesting *is* the object graph, resolved by whatever JSON library CNA already uses/adopts.

### Referencing other files from within a `.cnb`

A `.cnb` document may reference other assets by logical name/relative path (e.g. a `SpriteFont`
referencing its glyph atlas texture, or a `Model` referencing per-mesh textures). Those references
are just strings resolved through the same two-level dispatch rule above (native extension first,
then `.cnb`), recursively, through the owning `ContentManager` — this gives asset caching and
`Unload()` semantics "for free" reusing the identical mechanism already used for the top-level
asset. There is no separate "external reference" object model needed (contrast with `plan_xnb.md`'s
`ContentReader::ReadExternalReference<T>()`, which exists only because raw `.xnb` has no such
built-in path-resolution convention).

```json
{
  "cnbVersion": 1,
  "type": "SpriteFont",
  "texture": "Fonts/Consolas_atlas.png",
  "lineSpacing": 24,
  "spacing": 0.0,
  "defaultCharacter": "?",
  "glyphs": [
    { "character": "A", "sourceRect": [0, 0, 16, 24], "cropping": [0, 0, 16, 24], "kerning": [1, 14, 1] }
  ]
}
```

## Per-type `.cnb` conventions

Only a sketch of the initial set — the exact field names/shapes are a follow-up design task per
type, not fixed by this document. The point of this section is to establish *scope and grouping*,
matching the same practical asset categories `xnb.md`/`plan_xnb.md` already identified as mattering
for real XNA content, so nothing important is silently dropped by switching strategies.

| `.cnb` `type` | Replaces (from `xnb.md`'s XNA reader inventory) | Shape |
|---|---|---|
| `SpriteFont` | `SpriteFontReader` | JSON metadata (glyphs, kerning, line spacing) + a reference to a plain `.png` glyph atlas texture (no custom pixel packing logic needed inside `.cnb` itself — the atlas is just an ordinary image file). |
| `Model` | `ModelReader` + `VertexBufferReader`/`IndexBufferReader`/`VertexDeclarationReader` | JSON metadata (bone hierarchy, mesh/mesh-part list, per-part material/effect references) referencing either (a) a conventional interchange mesh format CNA can already load (e.g. glTF/OBJ, if/when supported) for raw vertex/index data, or (b) a small CNA-owned binary vertex/index blob file referenced by path, kept *outside* the JSON (JSON is a poor fit for large raw float/index arrays). |
| `AnimationClip` / skeletal animation data | `SkinningDataReader`-style custom readers seen in several samples (see `xnb.md`'s Lua discussion) | JSON keyframe/bone-transform data; large tracks may reference an external raw binary blob the same way `Model` does, to avoid bloating JSON with thousands of matrices. |
| Stock effect parameters (`BasicEffect`/`SkinnedEffect`/etc.) | Stock-effect XNA readers (`xnb.md`'s "stock effects" section) | Plain JSON parameter object (colors, texture references, lighting flags) consumed directly by CNA's existing native stock-effect C++ classes — structurally the same idea `plan_xnb.md`'s XNB-32 already committed to (deserialize the stock effect's own fields, construct the native CNA object), just via JSON fields instead of a binary reader. |
| General custom/`.fx`-based `Effect` | `EffectReader` (compiled platform shader bytecode) | **Explicitly out of scope**, exactly as `plan_xnb.md`'s XNB-32A/XNB-14B already conclude for the binary case: no format, JSON or binary, changes the fact that an original D3D9-era compiled shader blob cannot be run through bgfx. A `.cnb` `Effect` entry can only ever reference a *rebuilt* CNA-native shader (source `.fx`/CNA shader recompiled through CNA's own pipeline), never carry the original bytecode meaningfully. |
| Game-specific custom data (the ~82 hand-written `ContentTypeReader` classes found across `RolePlayingGame`, `Movipa`, `RobotGame`, etc. — see this session's sample survey) | Custom `ContentTypeReader` subclasses | A game-specific `type` string, with fields chosen by whoever migrates that game's content — no CNA core change needed per game, same "don't grow CNA core for one sample" principle `xnb.md`'s Lua section already argued for, just without needing a Lua sandbox/host at all: it is only ever *data*, read once by a small piece of C++ (or even generic reflection-free JSON field access) written for that one game. |

Note the recurring pattern: every category that `plan_xnb.md` treats as a "hard, later-phase"
problem (stock effects vs. general `EffectReader`, custom readers, large binary payloads) keeps
essentially the same hard/impossible boundary here. `.cnb` does not make the general `EffectReader`
problem solvable, and does not make large per-vertex arrays pleasant in JSON — it only removes the
*binary protocol and reader-registry* complexity around the parts that were never protocol-hard to
begin with (primitives, math structs, simple metadata, font/model metadata).

## What migration actually requires

This is the part that is easy to underestimate, and is the direct cost of not implementing an
`.xnb` reader:

1. **A working original-XNA (or MonoGame) runtime somewhere**, capable of loading the game's
   original `.xnb` files through the real `ContentManager`/`ContentTypeReader` machinery — typically
   Windows (or Wine) with XNA Game Studio 4.0, or a MonoGame-based content loader, since that is the
   only reliably correct implementation of the very object graph `plan_xnb.md` would otherwise have
   to reimplement in CNA. This session's survey of `/rv/tmp/XNAGameStudio/Samples` confirms these
   samples ship only source `.contentproj` projects and raw source assets (`.fbx`/`.x`/`.png`/`.fx`/
   `.spritefont`/`.wav`), not pre-built `.xnb` — so even testing migration requires a build step
   first, on both strategies equally.
2. **A one-time export tool** (could be a small MonoGame/XNA console program) that, for each asset
   type it knows about, loads the object via the real runtime and serializes what it finds into the
   `.cnb`/native-file conventions above. This tool is new code, but it is *much* smaller than a
   binary `.xnb` reader: it only ever needs to go one direction (typed .NET object → JSON/native
   file) using the official runtime's own deserialization, never reimplementing that deserialization
   itself.
3. **A manual pass for game-specific custom readers.** Automation only gets you so far here — same
   caveat `xnb.md`'s Lua-porting discussion already raised for "AI-assisted, not guaranteed" C#→Lua
   conversion applies equally to C#→`.cnb`-exporter conversion. The export tool needs one small
   per-game plugin (or hand-written export function) for each custom `ContentTypeReader`, but this
   is a strict subset of the same "port this one reader" work `plan_xnb.md`'s Phase H already
   accounted for — the difference is only where the ported logic ends up (an offline exporter vs. a
   runtime Lua reader), not whether porting work exists at all.
4. **No solution for general custom `.fx` effects**, same as the binary approach — 83 `.fx` files
   were found across the sample corpus in this session's survey; any model/effect relying on one
   still needs its shader manually rebuilt for CNA's backend regardless of which content strategy is
   chosen.

None of this is free, but it is a **bounded, one-time cost per game/asset pack**, rather than a
**permanent, open-ended maintenance surface inside CNA itself** (a binary parser + compression codec
+ reader registry that must keep working for every `.xnb` variant anyone might ever throw at it).

## Relationship to `xnb.md`/`plan_xnb.md`

This document does **not** retroactively invalidate `xnb.md`/`plan_xnb.md` — both remain accurate
records of what a full binary `.xnb` reader would cost and how it should be sequenced *if* CNA ever
decides to build one. This document exists so that choice can be made explicitly, by comparing:

| | Binary `.xnb` reader (`xnb.md`/`plan_xnb.md`) | `.cnb` JSON + native-by-extension (this document) |
|---|---|---|
| End-user experience | Drop original `.xnb` next to the game, it just loads | Must run a one-time migration/export step first |
| CNA maintenance surface | Permanent: binary protocol, LZX, reader registry, forever | Minimal: a JSON envelope + per-type field conventions CNA fully controls |
| Implementation cost | Very large (`xnb.md`'s own credit estimate: hundreds to low thousands of credits for broad coverage) | Small (a resolver + a handful of per-type (de)serializers; native-extension loading for PNG/JPEG/WAV already exists today) |
| Handles unknown/future `.xnb` variants | Only if explicitly implemented (MonoGame vs. FNA vs. platform variants — `plan_xnb.md`'s XNB-27/XNB-30C) | Not applicable — CNA never reads `.xnb` at all under this strategy |
| General custom `.fx` effects | Explicitly unsupported either way (`plan_xnb.md` XNB-32A) | Explicitly unsupported either way (same shader-porting problem) |
| Game-specific custom readers | Ported to Lua at runtime (`plan_xnb.md` Phase H) | Ported to a one-time export-tool plugin (this document) |

**If this strategy is adopted, the recommendation is to formally freeze `xnb.md`/`plan_xnb.md` as
"researched, not adopted" rather than deleting them** — the phase breakdown and cost analysis inside
them remain useful reference material if the decision is ever revisited, and several of their
protocol-accuracy findings (e.g. the true difficulty of the general `EffectReader`/compiled-shader
problem, or the FBX/model-importer cost on the writer side) apply identically here.

## Suggested next step (not started)

If this direction is confirmed, the natural follow-up is a `plan_cnb.md` numbered task list
(`CNB-1`, `CNB-2`, ...), mirroring how `plan_xnb.md` turned `xnb.md` into concrete tasks — covering:
the extension/`.cnb` resolver in `ContentManager`, the JSON envelope + registry, the first two or
three per-type schemas (`SpriteFont` and stock-effect parameters are the cheapest, highest-value
targets, matching this session's sample survey showing `SpriteFont`/texture/audio as the dominant
asset types), and only afterward `Model`/animation data and the export-tool design. No such task
list has been created yet — this document is analysis/design only, per the request that scoped it
to a plan document, not implementation.
