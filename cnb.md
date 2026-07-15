# CNB: a lightweight, JSON-based content format for CNA (alternative to XNB)

> Companion document to [`xnb.md`](xnb.md)/[`plan_xnb.md`](plan_xnb.md), which describe what it
> would take to make CNA a *consumer* of original binary XNA `.xnb` files. This document describes
> a **different, permanent strategic choice**: CNA never implements a binary `.xnb` reader at all.
> Instead, CNA defines its own simple, human-readable `.cnb` (CNA content Binary/JSON — despite the
> name, it is plain JSON, not binary) sidecar format, and games/assets that used to ship `.xnb` are
> migrated once, offline, to either a directly-loadable native format (PNG/JPEG/WAV, loaded by
> extension) or a `.cnb` JSON file next to the original asset.
>
> **Status: ✅ IMPLEMENTED 2026-07-15 — `plan_cnb.md`'s all 9 phases (`CNB-1`–`CNB-39`) closed.**
> `ContentManager` (`src/Microsoft/Xna/Framework/Content/ContentManager.cpp`,
> `include/Microsoft/Xna/Framework/Content/ContentManager.hpp`,
> `include/CNA/Internal/CnbEnvelope.hpp`, `include/CNA/Internal/Json.hpp`,
> `include/CNA/Internal/CnbSourceFile.hpp`) now implements this document as designed: the `.cnb`
> envelope (`cnbVersion`/`type`/`sourceFile`) parses via a real recursive-descent JSON parser and
> validates against the requested C++ type with a strict `cnbVersion == 1` policy;
> `.cnb` is tried **before** any native extension for every registered type
> (`ResolveAssetPath`), letting it act as an optional metadata sidecar (proven on `Texture2D` via
> `sourceFile` + `colorKey`) rather than only a mutually-exclusive alternative; `SpriteFontTypeReader`,
> `EffectTypeReader`, and `ModelTypeReader` all migrated from their old bespoke extensions
> (`.font.json`/`.shader.json`/`.model.json`) to `.cnb`; and `RegisterCnbLoader<T>` lets a game
> register several differently-named `.cnb` `"type"`s that all produce the same C++ type, for
> data with no dedicated `ContentTypeReader<T>` at all. `SkinnedModelTypeReader`/`.skinnedmodel.json`
> (Avatar, `NOXNA`) was deliberately **kept separate, not migrated** — see `plan_cnb.md` `CNB-22`
> for the full reasoning (real cross-language tooling depends on that extension name, it already
> has the most mature test coverage of any of the four readers, and it's explicitly a distinct
> non-`Model`-shaped system). Phases 0–8 (`CNB-1`–`CNB-31`) landed first; a same-day external
> review then found 2 real bugs (fixed) and prompted Phase 9 (`CNB-32`–`CNB-39`, also same day),
> which added `sourceFile` path-safety enforcement, a per-reader capability matrix, a type-safe
> asset cache, deterministic custom-loader registration, and a headless-runnable `.cnb` test lane
> — see `plan_cnb.md` for the full task-by-task record. Final full-suite regression after all 9
> phases: 4452 tests, 4450 passed, 2 pre-existing unrelated hardware skips, 0 failures. Dedicated
> `.cnb` coverage: 61 test cases across 14 gtest suites, with a 47-test graphics-independent subset
> verified under `SDL_VIDEODRIVER=dummy`. See `plan_cnb.md` for the complete,
> task-by-task implementation record — this document remains the design reference; that one is the
> log of what actually landed and why. See also "Relationship to `xnb.md`/`plan_xnb.md`" below for
> how the two binary-vs-JSON strategies compare and that plan's own adoption status.

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

For any asset CNA is asked to load by logical name (e.g. `"Textures/player"`), resolve it as
follows — **`.cnb` is checked first, native extensions are the fallback**:

```text
1. If "<name>.cnb" exists, load it as a .cnb JSON document. The "type" field
   inside the JSON is cross-checked against the requested C++ type T, then
   either:
     a. the document is self-contained (all data inline or via type-specific
        fields, e.g. SpriteFont/Model/Effect today) -- built from that, or
     b. the document has a "sourceFile" field -- that referenced file is
        loaded through step 2 below (recursively, through the same
        ContentManager), and the .cnb's own fields are applied on top as
        metadata/overrides (see "`.cnb` as metadata for a native sibling
        file" below).

2. Otherwise, if a file with that name AND a recognized native extension
   exists (e.g. "player.png", "player.jpg", "player.wav"), load it directly
   with the reader selected by that extension. No .cnb file is involved.

3. Otherwise: asset not found -- same failure behavior as today's
   ContentManager for a missing file.
```

This order (`.cnb` first) matters for one reason: it lets `.cnb` act as an *optional enrichment
layer* over an otherwise-ordinary native file, not just as a mutually-exclusive alternative to one.
`Content/Textures/ahoj.png` can exist entirely on its own (loads exactly as it does today, zero
`.cnb` involved, zero behavior change) — or it can be joined by `Content/Textures/ahoj.cnb` that adds
metadata the PNG format itself can't carry (color-key transparency, a sprite-sheet sub-rect, a hint
that the "real" pixel data is actually in some other, non-self-describing file). Checking native
extensions first would make that impossible: whichever native file exists would always win, and a
sibling `.cnb` could never be consulted for an asset a native loader already claims. Checking `.cnb`
first costs one extra failed file-exists check per load when no `.cnb` is present — negligible.

Restated: **`.cnb`, when present, always has final say over how an asset name resolves; failing
that, the file extension picks the reader.** This still mirrors how `ContentManager.Load<T>()`
resolves a logical asset name to a file today (`ResolveAssetPath` already tries a list of candidate
paths in order) — it just means `.cnb` needs to be the *first* candidate tried for every registered
type, not appended after that type's own native extensions.

```cpp
// Sketch only -- illustrates the dispatch rule, not a proposed final API.
std::shared_ptr<void> ContentManager::Load(const std::string& assetName)
{
    const auto cnbPath = assetName + ".cnb";
    if (FileExists(cnbPath))
    {
        return LoadCnb(cnbPath);               // JSON "type" field dispatch,
                                                 // possibly delegating to a
                                                 // "sourceFile" below.
    }

    if (auto nativePath = ResolveNativeExtension(assetName))
    {
        return LoadByExtension(*nativePath);   // .png/.jpg/.wav/... reader
    }

    throw ContentLoadException("Asset not found: " + assetName);
}
```

Note on dispatch: CNA's real `ContentManager::Load<T>()` already requires the caller to name `T` at
the call site (`cm.Load<SpriteFont>("Fonts/Consolas")`), exactly like original XNA's own generic
`ContentManager.Load<T>(string assetName)`. So `"type"` inside a `.cnb` file is not the primary
dispatch mechanism — `T` is, same as today, and it's how `ResolveAssetPath` already picks which
reader's candidate list to try (now with `.cnb` prepended ahead of that reader's own native
extensions, per the reordering above). `"type"` exists for the same reason the real `.xnb` format's
own reader-name table exists despite C# callers already knowing `T` at compile time: an integrity
check (catch "asset says Model, caller asked for SpriteFont" with a clear error instead of a
confusing field-parsing failure) and a hook for tooling that doesn't have a compile-time `T` (asset
validators, migration scripts, an editor's "what is this file" inspector).

### Native extensions (the `.cnb`-absent fallback)

| Extension | CNA type | Notes |
|-----------|----------|-------|
| `.png`, `.jpg`/`.jpeg`, `.bmp` | `Texture2D` | Already implemented today via `Texture2D::FromStream` (SDL3_image-backed; see `AUDIT.md`) — this rule formalizes dispatch-by-extension on top of code that already exists. |
| `.dds` | `Texture2D`/`TextureCube` | `FromStream` already decodes DDS/DXT1/3/5 today (folded into the same function per `AUDIT.md`); this plan only needs a dedicated `DDSFromStreamEXT`-style dispatch by extension, not new decode logic. |
| `.wav` | `SoundEffect` | Already implemented (`AUDIT.md`: SDL3_mixer-backed `SoundEffect`). |
| `.ogg`, `.mp3` | `SoundEffect`/`Song` | If/where CNA's audio backend already supports these container formats; otherwise deferred, same as any other format gap — not a `.cnb`-specific concern. |
| (future) any format with an existing native CNA loader | whatever that loader produces | The extension-dispatch rule is intentionally generic — adding a new native format later is just adding one more row here, no `.cnb` schema change needed. |

None of the above need any design work from this document when no `.cnb` sidecar is present — they
are either already implemented or are ordinary "add a loader for format X" tasks independent of the
`.cnb` idea, and they keep working exactly as they do today with zero `.cnb` files anywhere in a
project. What *is* new design surface: any of these native types can now optionally gain a same-named
`.cnb` sidecar (see next section) purely for extra metadata, without giving up native decoding for
the actual pixel/audio bytes.

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
  game-specific type name for migrated custom data) identifying what the document is. As covered in
  "Note on dispatch" above, the primary dispatch key in CNA is still the requested C++ type `T` at
  the `Load<T>()` call site — `type` is a cross-check against that, structurally analogous to how
  `plan_xnb.md`'s reader-name table lets `.xnb` verify what it's reading, but as a plain JSON string
  instead of a binary assembly-qualified name — so no generic-name parsing (`plan_xnb.md`'s
  XNB-13/13A problem) exists in this format at all. For tooling with no compile-time `T` (asset
  validators, migration scripts), `type` doubles as the actual dispatch key into a small
  string-to-handler table.
- `sourceFile` — **optional**. When present, this `.cnb` is a *metadata sidecar* for another file
  (usually a same-named native file, e.g. `ahoj.cnb` with `"sourceFile": "ahoj.png"`), not a
  self-contained descriptor. See "`.cnb` as metadata for a native sibling file" below. When absent,
  the `.cnb` document is expected to be fully self-contained (as `SpriteFont`/`Model`/`Effect` are
  today).
- Everything else is defined per-type (see below). There is no shared binary primitive layer, no
  7-bit encoded integers, no shared-resource object graph — ordinary JSON object/array/number/string
  nesting *is* the object graph. CNA's existing four JSON readers parse fields with small hand-rolled
  helpers (`ExtractJsonStringField`/`JsonInt`/`JsonFloat`/... in `ContentManager.cpp`), not a general
  JSON library — a `.cnb` migration should either keep using/extending those helpers or adopt a real
  JSON library at that point, since a shared envelope makes a common parse-then-dispatch entry point
  worth having either way.

### Referencing other files from within a `.cnb`

A `.cnb` document may reference other assets by logical name/relative path (e.g. a `SpriteFont`
referencing its glyph atlas texture, or a `Model` referencing per-mesh textures). Those references
are just strings resolved through the same two-level dispatch rule above (`.cnb` first, then native
extension), recursively, through the owning `ContentManager` — this gives asset caching and
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
    { "char": 65, "source": [0, 0, 16, 24], "crop": [0, 0, 16, 24], "kerning": [1, 14, 1] }
  ]
}
```

Field names (`char`/`source`/`crop`, integer char code rather than a one-char string) intentionally
match CNA's already-shipping `.font.json` convention (`SpriteFontTypeReader::Read` in
`ContentManager.cpp`) instead of inventing new ones — a future migration to `.cnb` then only needs to
add the envelope (`cnbVersion`/`type`) and rename the extension, not rename every field too.

### `.cnb` as metadata for a native sibling file (the `sourceFile` field)

A `.cnb` document doesn't have to be self-contained. If it has a `"sourceFile"` field, CNA loads that
referenced file the normal way (native extension, recursively through the same `ContentManager` —
same caching/`Unload()` mechanism as any other reference) and treats the rest of the `.cnb`'s fields
as *metadata about* that file rather than as the data itself:

```json
{
  "cnbVersion": 1,
  "type": "Texture2D",
  "sourceFile": "ahoj.png",
  "colorKey": [255, 0, 255],
  "premultipliedAlpha": true
}
```

This is genuinely new capability, not a rename of something that already exists: before this,
`Texture2D`/`SoundEffect`/`TextureCube` only ever loaded a self-describing native file as-is —
there was no way to attach CNA-specific import metadata (color-key transparency, a sprite-sheet
sub-rect, premultiplied-alpha hints, mip generation, ...) without baking it into the pixel data
itself or inventing a whole new file format. `sourceFile` gives every native-payload asset type
that same "metadata + payload" split `Model`
(`.model.json` + `.verts.bin`/`.idx.bin`) and `SkinnedModelEXT` (`.skinnedmodel.json` +
`.skeleton.bin`/`.clip.bin`) already have, generalized to *any* payload — including a normal, already
fully-supported native format the `.cnb` is simply enriching, or a raw/proprietary blob (arbitrary
extension, or none at all) that isn't self-describing and needs the `.cnb`'s fields (width, height,
pixel format, compression, ...) to be interpreted at all. `sourceFile` is resolved through the exact
same two-step rule as the top-level asset (`.cnb` first, then native extension) — it just can't
itself point at another `.cnb` with its own `sourceFile` (no chained/recursive sidecars; one `.cnb`
per physical payload, matching how `Model`'s sidecars are never themselves JSON).

**Enforced safety guarantees** (`plan_cnb.md` CNB-32, `CNA::Internal::ResolveCnbSourceFileSafely()`
in `include/CNA/Internal/CnbSourceFile.hpp`): `sourceFile` cannot be an absolute path; its resolved
target cannot escape the content root (including via `../` or a symlink); and it cannot resolve to
another `.cnb`, whether named explicitly (`"foo.cnb"`) or implicitly (`"foo"`, where the normal
resolver would pick a sibling `"foo.cnb"` over any other candidate — this closes a self-referential
cycle too, since a cycle is just a chain back to the same file). Any violation throws
`ContentLoadException` naming both the referring `.cnb` and the rejected target.

Authoring note: `.cnb`'s presence always wins (see "The core rule" above) — a `Content/ahoj.png` with
an unrelated, coincidentally-same-named `Content/ahoj.cnb` sitting next to it (self-contained, no
`sourceFile`, describing something else entirely) will shadow the PNG, not merge with it. This isn't
a resolver bug, it's the same "one name, `.cnb` wins" rule applied consistently — but it's worth
calling out explicitly as a content-authoring footgun, since nothing on disk visually distinguishes
"enrichment sidecar" from "unrelated same-named asset" other than opening the file and checking for
`sourceFile`.

### The `sourceFile` capability matrix (plan_cnb.md CNB-34)

Not every built-in reader accepts `sourceFile` — the resolver tries `.cnb` for *every* registered
type (per "The core rule"), but only types with an actual native, independently-loadable payload
can meaningfully delegate to one. Implemented, enforced behavior per reader:

| Reader | `sourceFile` | Notes |
|---|---|---|
| `Texture2D` | ✅ Supported, + `colorKey` metadata | The original, proven case (Phase 2) |
| `SoundEffect` | ✅ Supported, no metadata fields yet | Delegates to the native `.wav` decoder |
| `TextureCube` | ✅ Supported, no metadata fields yet | Delegates to `DDSFromStreamEXT` |
| `SpriteFont` | ❌ Rejected | Self-contained descriptor — glyph atlas is referenced via its own `"texture"` field, not `sourceFile` |
| `Effect` | ❌ Rejected | Self-contained descriptor — vertex/fragment sources referenced via their own `"vertex"`/`"fragment"` fields |
| `Model` | ❌ Rejected | Self-contained descriptor — mesh data referenced via its own `"meshes"` fields |
| game-specific `RegisterCnbLoader<T>` | Reader-defined | Not part of the built-in envelope contract at all — a custom factory receives the raw `.cnb` JSON and is free to define (or ignore) its own `sourceFile`-like convention |

A `SpriteFont`/`Effect`/`Model` `.cnb` with a `sourceFile` field throws `ContentLoadException`
immediately (naming the reader and the file), rather than either silently ignoring the field or
letting the reader's own native decoder choke on the raw `.cnb` JSON text with a confusing
low-level error.

### Why one shared extension is not a new risk

A natural objection: doesn't collapsing every JSON type onto one `.cnb` extension mean two
differently-typed assets that happen to share a logical name (e.g. a `SpriteFont` and an
`AnimationClip` both called `"Cursor"`) collide on the same physical file, `Cursor.cnb`?

Yes — but this is exactly how original XNA's real `.xnb` pipeline already behaved, for the same
reason: the content build step always produces one `<name>.xnb` per logical asset name, regardless
of source type, and the *type* is resolved from inside the file (the reader-name table), not from
the extension. A real XNA content project could never contain both an image and a sound that both
built to `hello.xnb` — that was already a build-time conflict in the original tooling, not something
`.cnb` invents. CNA's own current `.font.json`/`.model.json`/`.shader.json`/`.skinnedmodel.json`
split is the actual deviation from original XNA behavior here (a convenience CNA added because it
was easy, not because XNA worked that way). Adopting one shared `.cnb` extension is not a new risk
relative to XNA — it is CNA becoming *more* faithful to how the real `ContentManager` always worked:
one physical asset per logical name, type resolved from content, not filename.

The real, if more modest, cost is opacity: unlike `.font.json` vs. `.model.json`, a person or tool
browsing a `Content/` folder full of `.cnb` files can no longer tell a font from a model without
opening one — same as original `.xnb`, where every asset file also looked identical from outside a
file browser. Tooling (an asset validator, an editor "inspect this file" command) needs to open and
read `"type"` to know what something is, exactly like a `.xnb`-aware tool would.

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

## Custom loaders (game-registered, selected by `.cnb` `"type"`)

CNA already has `ContentManager::RegisterTypeReader<T>()` (`ContentManager.hpp`) — a game can
register its own reader for a whole C++ type `T`. But that registry is keyed by `std::type_index`,
one reader per `T`, fixed by whatever `Load<T>()` call sites use. It has no way for two `.cnb` files
that both request the same `T` to be parsed by two different, independently-registered pieces of
game code chosen at runtime by the `.cnb` itself — e.g. a game with both `"EnemyDefinition"` and
`"LootTable"` `.cnb` `type`s that happen to deserialize into the same generic `T` needs two different
parsing functions, not one.

A second registry closes that gap: keyed by the `.cnb` `"type"` string instead of `std::type_index`.
**Implemented** (`plan_cnb.md` CNB-24/25, `ContentManager.hpp`):

```cpp
template <typename T>
using CnbLoaderFn = std::function<T(const std::string& cnbJson, ContentManager& cm)>;

template <typename T>
void ContentManager::RegisterCnbLoader(const std::string& typeName, CnbLoaderFn<T> factory);
```

One deviation from the original sketch: `CnbLoaderFn`'s first parameter is the raw `.cnb` JSON
text (`const std::string&`), not a `JsonValue` — CNA has no JSON *object* type, only the
hand-rolled string-scanning helpers `.cnb` readers already use throughout this document, so the
factory does its own field extraction the same way `SpriteFontTypeReader`/`ModelTypeReader`/etc.
do.

The first time `RegisterCnbLoader<T>()` is called for a given `T`, it lazily registers a small
built-in `GenericCnbTypeReader<T>` for that `T` (via the existing `RegisterTypeReader<T>()`) — so
no change to `Load<T>()`'s own dispatch was needed. That generic reader parses the envelope, looks
up the `.cnb`'s `"type"` in the table, and invokes whichever factory matches. This only applies to
a `T` with **no existing reader already registered** (built-in or otherwise) — `RegisterCnbLoader`
throws immediately if one already exists for `T`, since that reader would never consult this table.
No `ContentTypeReader<T>` subclass or CNA core change is needed per game-specific `.cnb` `type` —
same "don't grow CNA core for one game's data" principle already used for the plain
game-specific-`type` row in the table above, just now with the dispatch key coming from the `.cnb`
file itself instead of requiring the caller to already know which of several shapes it's asking
for.

Worked example, matching the `"EnemyDefinition"`/`"LootTable"` case described above (see
`tests/Microsoft/Xna/Framework/Content/CnbCustomLoaderTests.cpp` for the full, passing test):

```cpp
struct GameData { std::string kind; };

cm.RegisterCnbLoader<GameData>("EnemyDefinition",
    [](const std::string& json, ContentManager&) { GameData d; d.kind = "Enemy"; return d; });
cm.RegisterCnbLoader<GameData>("LootTable",
    [](const std::string& json, ContentManager&) { GameData d; d.kind = "Loot"; return d; });

// goblin.cnb: {"cnbVersion": 1, "type": "EnemyDefinition"}
// chest.cnb:  {"cnbVersion": 1, "type": "LootTable"}
GameData enemy = cm.Load<GameData>("goblin"); // kind == "Enemy"
GameData loot  = cm.Load<GameData>("chest");  // kind == "Loot"
```

This is the closest practical C++ analog of how real XNA's `.xnb` format let a reader be identified
purely by an assembly-qualified name embedded in the file, decoupled from whatever the calling code
asked for — except as a plain string key into an explicitly pre-registered table instead of runtime
reflection over a loaded .NET assembly, so none of the dynamic type-loading/reflection machinery
`plan_xnb.md` already argues against needs to exist for this either.

**Registration contract (`plan_cnb.md` CNB-37):** `RegisterCnbLoader<T>` is deterministic and
fails fast rather than silently accepting bad input — an empty `typeName` or an empty `factory`
throws `std::invalid_argument` immediately, and re-registering an already-used `(T, typeName)`
pair throws `std::logic_error` rather than quietly replacing the earlier factory (the same
`std::logic_error` the "type already owned by another reader" case throws — see below). Two
*different* `typeName`s for the same `T` remain fully supported, as shown above — only an exact
repeat of both `T` and `typeName` is rejected.

## Relationship to CNA's existing per-type JSON conventions

Before this document was implemented, CNA's JSON content readers predated the `.cnb` envelope and
each used their own bespoke extension. Three of the four were migrated (`plan_cnb.md` Phases 3–5);
one — `SkinnedModelTypeReader` — was deliberately kept separate (`CNB-22`):

| Reader class | Extension before | Extension now | Real field names (for reference) |
|---|---|---|---|
| `SpriteFontTypeReader` | `.font.json` | `.cnb` | `texture`, `lineSpacing`, `spacing`, `defaultCharacter`, `glyphs[].char`/`source`/`crop`/`kerning` |
| `ModelTypeReader` | `.model.json` | `.cnb` | `bones`, `meshes[].vertices`/`indices`/`vertexStride`/`texture` (binary sidecars for vertex/index data) |
| `EffectTypeReader` | `.shader.json` | `.cnb` | see `EffectTypeReader::Read` in `ContentManager.cpp` |
| `SkinnedModelTypeReader` (NOXNA, Avatar-only) | `.skinnedmodel.json` | **unchanged** — see `CNB-22` | `skeleton`, `parts[].vertices`/`indices`/`vertexStride`/`texture`, `animations[].name`/`clip` |

Unifying the three was a **migration of already-working, already-tested code**, not a green-field
build, exactly as anticipated: each reader's `GetExtensions()` changed from its own extension to
`{".cnb"}`, each reader gained a `"cnbVersion"`/`"type"` envelope check, and every existing fixture
in the repo (7 example programs' `.model.json` fixtures, `easygl_bloom_extract_test.cpp`'s
`.shader.json` fixture) was renamed with the two new envelope fields added. None of the readers'
actual field parsing needed to change otherwise — field names were kept exactly as they were, which
kept the diff small and low-risk, confirmed by a clean full-suite regression after each phase.

Two things were genuinely net-new scope beyond the three-reader migration, not just a rename: the
`.cnb`-first resolution order (letting `.cnb` act as an optional metadata sidecar for `Texture2D` —
proven via `sourceFile` + `colorKey` — which had no sidecar mechanism at all before) and the
`RegisterCnbLoader<T>` custom-loader registry (nothing before provided an equivalent). See
`plan_cnb.md` Phases 1–2 and 7 for how each was implemented and tested.

This is separate from (and much smaller than) the XNA→CNA *content migration* effort described below
in "What migration actually requires" (which is about converting original `.fbx`/`.png`/`.wav`/
`.spritefont` XNA source assets into CNA's format at all, whichever format CNA settles on —
orthogonal to whether CNA's own JSON conventions are unified under one extension or four).

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

## Implementation record

The design above was implemented in full via `plan_cnb.md`'s numbered task list (`CNB-1`–`CNB-27`,
Phases 0–7), mirroring how `plan_xnb.md` turned `xnb.md` into concrete tasks — see that file for
the complete, phase-by-phase record (what changed, why, and each phase's regression-test tally).
Summary of what landed, in the order it happened:

1. `.cnb` envelope parsing + validation (`CNA::Internal::CnbEnvelope.hpp`) — Phase 0.
2. `.cnb`-first resolver order in `ResolveAssetPath`, for every registered type — Phase 1.
3. `sourceFile` support, proven on `Texture2D` via `colorKey` (selective pixel transparency) —
   Phase 2.
4. `SpriteFontTypeReader` migrated `.font.json` → `.cnb` — Phase 3.
5. `EffectTypeReader` migrated `.shader.json` → `.cnb` — Phase 4.
6. `ModelTypeReader` migrated `.model.json` → `.cnb` (its pre-existing FNA-fidelity gaps —
   single-bone synthesis, unassigned `ParentBone`, unset `BoundingSphere`/`Tag` — were deliberately
   *not* fixed in the same pass; see `plan_cnb.md` `CNB-21` for the reasoning) — Phase 5.
7. `SkinnedModelTypeReader` decided to stay separate, not migrated — see `plan_cnb.md` `CNB-22` and
   "Relationship to CNA's existing per-type JSON conventions" above — Phase 6.
8. `RegisterCnbLoader<T>` — Phase 7.

Genuinely new `.cnb` types with no existing reader today (e.g. `AnimationClip` for plain `Model`,
further game-specific custom data) remain a natural, open-ended follow-up — `RegisterCnbLoader<T>`
already supports them for game-specific data without any CNA core change; a first-party
`AnimationClip` reader would be new scope beyond what `plan_cnb.md` covered.
