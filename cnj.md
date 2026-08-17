# CNJ: a lightweight, JSON-based content format for CNA (alternative to XNB)

> Companion document to [`xnb.md`](xnb.md)/[`plan_xnb.md`](plan_xnb.md), which describe what it
> would take to make CNA a *consumer* of original binary XNA `.xnb` files. This document describes
> a **different, permanent strategic choice**: CNA never implements a binary `.xnb` reader at all.
> Instead, CNA defines its own simple, human-readable `.cnj` (CNA content JSON) sidecar format, and
> games/assets that used to ship `.xnb` are migrated once, offline, to either a directly-loadable
> native format (PNG/JPEG/WAV, loaded by extension) or a `.cnj` JSON file next to the original asset.
>
> **Naming note (2026-07-17):** this format was originally called `.cnb` ("CNA content
> Binary/JSON" — despite the name, always plain JSON, never binary), which misleadingly implied a
> binary format mirroring XNA's own binary `.xnb`. Renamed to `.cnj` project-wide (extension,
> `CnjEnvelope`/`CnjSourceFile`, the `cnjVersion` envelope field, `RegisterCnjLoader<T>`, and this
> plan's own `CNB-1`–`CNB-39` task numbering) to say plainly what it is.
>
> **Status: ✅ IMPLEMENTED 2026-07-15 — `plan_cnj.md`'s all 9 phases (`CNB-1`–`CNB-39`) closed.**
> `ContentManager` (`src/Microsoft/Xna/Framework/Content/ContentManager.cpp`,
> `include/Microsoft/Xna/Framework/Content/ContentManager.hpp`,
> `include/CNA/Internal/CnjEnvelope.hpp`, `include/CNA/Internal/Json.hpp`,
> `include/CNA/Internal/CnjSourceFile.hpp`) now implements this document as designed: the `.cnj`
> envelope (`cnjVersion`/`type`/`sourceFile`) parses via a real recursive-descent JSON parser and
> validates against the requested C++ type with a strict `cnjVersion` ceiling policy (1 for every
> type; `Model` raised its own to 2 — see "`Model` `.cnj` version 2" below);
> `.cnj` is tried **before** any native extension for every registered type
> (`ResolveAssetPath`), letting it act as an optional metadata sidecar (proven on `Texture2D` via
> `sourceFile` + `colorKey`) rather than only a mutually-exclusive alternative; `SpriteFontTypeReader`,
> `EffectTypeReader`, and `ModelTypeReader` all migrated from their old bespoke extensions
> (`.font.json`/`.shader.json`/`.model.json`) to `.cnj`; and `RegisterCnjLoader<T>` lets a game
> register several differently-named `.cnj` `"type"`s that all produce the same C++ type, for
> data with no dedicated `LooseFileContentTypeReader<T>` at all. `SkinnedModelTypeReader`/`.skinnedmodel.json`
> (Avatar, `NOXNA`) was deliberately **kept separate, not migrated** — see `plan_cnj.md` `CNB-22`
> for the full reasoning (real cross-language tooling depends on that extension name, it already
> has the most mature test coverage of any of the four readers, and it's explicitly a distinct
> non-`Model`-shaped system). Phases 0–8 (`CNB-1`–`CNB-31`) landed first; a same-day external
> review then found 2 real bugs (fixed) and prompted Phase 9 (`CNB-32`–`CNB-39`, also same day),
> which added `sourceFile` path-safety enforcement, a per-reader capability matrix, a type-safe
> asset cache, deterministic custom-loader registration, and a headless-runnable `.cnj` test lane
> — see `plan_cnj.md` for the full task-by-task record. Final full-suite regression after all 9
> phases: 4452 tests, 4450 passed, 2 pre-existing unrelated hardware skips, 0 failures. Dedicated
> `.cnj` coverage: 61 test cases across 14 gtest suites, with a 47-test graphics-independent subset
> verified under `SDL_VIDEODRIVER=dummy`. See `plan_cnj.md` for the complete,
> task-by-task implementation record — this document remains the design reference; that one is the
> log of what actually landed and why. See also "Relationship to `xnb.md`/`plan_xnb.md`" below for
> how the two binary-vs-JSON strategies compare and that plan's own adoption status.
>
> **2026-07-16 update:** the title's "(alternative to XNB)" framing is now only half true — see
> "Update 2026-07-16: `.xnb` is back, ranked above `.cnj` (MVP scope)" further down this document.
> Everything above about `.cnj` itself is unchanged; `.xnb` support was added *alongside* it, not
> instead of it.
>
> **2026-07-17 update — Phase 10, `AnimationClipTypeReader`:** the "Per-type `.cnj` conventions"
> table's `AnimationClip` row, previously a design sketch only, is now implemented
> (`plan_cnj.md` `CNB-40`–`CNB-42`) — a standalone `.cnj` `AnimationClip` document, independent of
> any specific `Model`, loadable via `ContentManager::Load<Graphics::AnimationClipEXT>()`. Supports
> both inline JSON keyframe tracks and a `"clipFile"` reference to an existing `.clip.bin` binary
> blob, matching the two options this document's own table already described. 7 new tests (6 in
> `CnjAnimationClipTests.cpp`, 1 more added to `CnjCapabilityMatrixTests.cpp` for
> `sourceFile`-rejection); full-suite regression after landing: 4661 tests, 4659 passed, the same 2
> pre-existing unrelated hardware skips, 0 failures.
>
> **2026-07-17 update — Phase 11, remaining `.xnb`-vs-`.cnj` type-coverage gaps closed:**
> `Texture3D` (`CNB-43`, self-contained JSON + raw RGBA8 binary sidecar), `Curve` (`CNB-44`,
> self-contained JSON, direct port of `CurveContentTypeReader.hpp`'s field order), and the 5 stock
> effects — `BasicEffect`/`AlphaTestEffect`/`DualTextureEffect`/`EnvironmentMapEffect`/
> `SkinnedEffect` (`CNB-45`, dispatched from inside the existing `EffectTypeReader` by `.cnj`
> `"type"`, since `RegisterTypeReader<T>()` allows only one reader per C++ type) — are all now
> `.cnj`-loadable, closing every gap identified by cross-referencing FNA's real `ContentTypeReader`
> inventory against `.xnb`'s (already complete) and `.cnj`'s (previously partial) coverage. 19 new
> tests across 3 new files; full-suite regression: 4680 tests, 4678 passed, same 2 pre-existing
> hardware skips, 0 failures.
>
> **2026-07-17 update — Phase 11 continued, `AnimationClip` sharing:** `Model`'s/`SkinnedModel`'s
> `"animations"` field can now name a standalone `.cnj` `AnimationClip` asset instead of only a raw
> `.clip.bin` blob (`plan_cnj.md` `CNB-48`/`CNB-49`), letting multiple models share one clip
> (e.g. a common "Idle"/"Walk" library) through `ContentManager`'s normal caching. Dispatched by
> extension; fully backward compatible with every existing `.clip.bin` reference. 3 new tests;
> full-suite regression: 4683 tests, 4681 passed, same 2 pre-existing hardware skips, 0 failures.
>
> **2026-07-17 update — Phase 12, glTF import tool:** implemented at the project owner's explicit
> request, including skeleton/skinning/animation ("melo by to umet i kosti"). `tools/gltf_to_cnj/`
> (`cna_tool_gltf_to_cnj`, built on vendored `cgltf` v1.15) converts a real `.gltf`/`.glb` file into
> a `Model` `.cnj` + vertex/index/skeleton binary sidecars + one standalone, shareable `.cnj`
> `AnimationClip` per clip (CNB-48's mechanism, exercised for real by this tool). Carries forward
> both real bugs `tools/avatar_asset_pipeline/convert_avatar.py` already found (topological bone
> reorder, non-indexed primitives) and adds new animation-resampling logic (glTF's per-component
> channels merged into CNA's own per-keyframe `KeyframeEXT` shape). Verified against a real official
> Khronos `CesiumMan.glb` sample (19 bones, a real walk-cycle clip, genuine `AnimationPlayer`
> playback) and a permanent, network-free adversarial regression test
> (`GltfToCnjToolTests.cpp`).
>
> **Same-day follow-up (`plan_cnj.md` `CNB-53`), at the project owner's explicit request**: the
> three scope cuts above are closed. Sparse accessors now resolve correctly (every accessor read
> goes through `cgltf_accessor_unpack_floats`, not the sparse-rejecting
> `cgltf_accessor_read_float`). Each primitive's material base-color texture is extracted (embedded
> `bufferView`, external file, or base64 `data:` URI all handled) and wired into the mesh's `.cnj`
> `"texture"` field -- `CesiumMan.glb`'s own real 1024×1024 JPEG texture round-trips correctly. A
> file with more than one skin now produces one `Model` `.cnj` per skin (`<baseName>_<skinName>.cnj`),
> not just the first. Full-suite regression: 4689 tests, 4687 passed, same 2 pre-existing hardware
> skips, 0 failures.
>
> **Self-caught fix (`CNB-54`)**: the same-day refactor above accidentally dropped `STEP`
> animation-interpolation handling (silently became `LINEAR`) -- found and fixed the same day, with
> a regression test. See `plan_cnj.md` for the full root-cause writeup.
>
> **Second follow-up (`plan_cnj.md` `CNB-55`), at the project owner's explicit "oprav vsechny tyto
> diry" request**: TEXCOORD set selection now honors the base-color texture's own `"texcoord"`
> index instead of always `TEXCOORD_0`; only nodes reachable from the file's default scene are
> imported; an optional `unitScale` CLI argument corrects a source file not authored in meters
> (positions, bone bind poses, and animated translation keyframes all scale consistently); Draco-
> compressed primitives are rejected with a clear error instead of silently misread; `CUBICSPLINE`
> channels now use the glTF spec's real cubic Hermite basis (tangents included), not just the
> sampled value; and `COLOR_0` vertex color is extracted for unskinned meshes, reusing the real XNA
> `VertexPositionColorTexture` layout and `BasicEffect`'s already-working `VertexColorEnabled`
> shader path (one small `ModelTypeReader` addition: an optional `"vertexColorEnabled"` `.cnj` mesh
> field). **Deliberately still out of scope**: PBR maps beyond base color and vertex color on
> skinned meshes have no shader path on CNA's real-XNA-faithful `BasicEffect`/`SkinnedEffect` at
> all (adding them would mean diverging from XNA fidelity or building a new custom shader
> pipeline); morph targets have no support anywhere in CNA's rendering pipeline. Full-suite
> regression: 4696 tests, 4694 passed, same 2 pre-existing hardware skips, 0 failures.

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
follows — **`.xnb` is checked first (2026-07-16), then the literal caller-given path, then `.cnj`,
with native extensions as the final fallback**:

```text
1. If "<name>.xnb" exists, load it through CNA's own (MVP-scoped) binary .xnb
   reader. A recognized-but-not-yet-implemented reader name, or malformed/
   unsupported content, is a hard ContentLoadException -- it does NOT fall
   through to steps 2-4 below. See xnb.md/plan_xnb.md for what is actually
   implemented at any given time.

2. Otherwise, if the literal asset name/path as given by the caller (with
   whatever extension it already has, if any) exists, load it directly with
   that extension's reader. This is the rare case where the caller already
   passed a full filename.

3. Otherwise, if "<name>.cnj" exists, load it as a .cnj JSON document. The
   "type" field inside the JSON is cross-checked against the requested C++
   type T, then either:
     a. the document is self-contained (all data inline or via type-specific
        fields, e.g. SpriteFont/Model/Effect today) -- built from that, or
     b. the document has a "sourceFile" field -- that referenced file is
        loaded through step 4 below (recursively, through the same
        ContentManager), and the .cnj's own fields are applied on top as
        metadata/overrides (see "`.cnj` as metadata for a native sibling
        file" below).

4. Otherwise, if a file with that name AND a recognized native extension
   exists (e.g. "player.png", "player.jpg", "player.wav"), load it directly
   with the reader selected by that extension. No .cnj or .xnb file is
   involved.

5. Otherwise: asset not found -- same failure behavior as today's
   ContentManager for a missing file.
```

Two separate reasons drive this ordering, one per tier:

- **`.xnb` ranks above everything**, including a literal caller-given path, because it represents
  an authentic, externally-produced compiled asset — if one is genuinely present, it should win
  over CNA's own conveniences rather than be silently shadowed by them. This is a deliberate
  reversal from before `.xnb` support existed; see the "Update 2026-07-16" section above.
- **`.cnj` still ranks above native extensions** (below `.xnb`) for the original reason: it lets
  `.cnj` act as an *optional enrichment layer* over an otherwise-ordinary native file, not just as a
  mutually-exclusive alternative to one. `Content/Textures/ahoj.png` can exist entirely on its own
  (loads exactly as it does today, zero `.cnj`/`.xnb` involved) — or it can be joined by
  `Content/Textures/ahoj.cnj` that adds metadata the PNG format itself can't carry (color-key
  transparency, a sprite-sheet sub-rect, a hint that the "real" pixel data lives in some other,
  non-self-describing file). Checking native extensions first would make that impossible: whichever
  native file exists would always win, and a sibling `.cnj` could never be consulted for an asset a
  native loader already claims. Checking `.cnj` before native extensions costs one extra failed
  file-exists check per load when no `.cnj` is present — negligible, and now serviced by the
  content-manifest cache (see `xnb.md`'s "Content manifest" section) rather than a raw stat call.

Restated: **`.xnb`, when present, always wins outright; failing that, `.cnj`, when present, has
final say over how an asset name resolves; failing that, the file extension picks the reader.**
This still mirrors how `ContentManager.Load<T>()` resolves a logical asset name to a file today
(`ResolveAssetPath` already tries a list of candidate paths in order) — it just means `.xnb` and
`.cnj` are the first two candidates tried for every registered type, not appended after that type's
own native extensions.

```cpp
// Sketch only -- illustrates the dispatch rule, not a proposed final API.
std::shared_ptr<void> ContentManager::Load(const std::string& assetName)
{
    const auto xnbPath = assetName + ".xnb";
    if (FileExists(xnbPath))
    {
        return LoadXnb(xnbPath);               // binary container + type-reader
                                                 // table dispatch (MVP-scoped).
    }

    if (FileExists(assetName))
    {
        return LoadByExtension(assetName);     // caller already gave a full path
    }

    const auto cnjPath = assetName + ".cnj";
    if (FileExists(cnjPath))
    {
        return LoadCnj(cnjPath);               // JSON "type" field dispatch,
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
`ContentManager.Load<T>(string assetName)`. So `"type"` inside a `.cnj` file is not the primary
dispatch mechanism — `T` is, same as today, and it's how `ResolveAssetPath` already picks which
reader's candidate list to try (now with `.cnj` prepended ahead of that reader's own native
extensions, per the reordering above). `"type"` exists for the same reason the real `.xnb` format's
own reader-name table exists despite C# callers already knowing `T` at compile time: an integrity
check (catch "asset says Model, caller asked for SpriteFont" with a clear error instead of a
confusing field-parsing failure) and a hook for tooling that doesn't have a compile-time `T` (asset
validators, migration scripts, an editor's "what is this file" inspector).

### Native extensions (the `.cnj`-absent fallback)

| Extension | CNA type | Notes |
|-----------|----------|-------|
| `.png`, `.jpg`/`.jpeg`, `.bmp` | `Texture2D` | Already implemented today via `Texture2D::FromStream` (SDL3_image-backed; see `AUDIT.md`) — this rule formalizes dispatch-by-extension on top of code that already exists. |
| `.dds` | `Texture2D`/`TextureCube` | `FromStream` already decodes DDS/DXT1/3/5 today (folded into the same function per `AUDIT.md`); this plan only needs a dedicated `DDSFromStreamEXT`-style dispatch by extension, not new decode logic. |
| `.wav` | `SoundEffect` | Already implemented (`AUDIT.md`: SDL3_mixer-backed `SoundEffect`). |
| `.ogg`, `.mp3` | `SoundEffect`/`Song` | If/where CNA's audio backend already supports these container formats; otherwise deferred, same as any other format gap — not a `.cnj`-specific concern. |
| (future) any format with an existing native CNA loader | whatever that loader produces | The extension-dispatch rule is intentionally generic — adding a new native format later is just adding one more row here, no `.cnj` schema change needed. |

None of the above need any design work from this document when no `.cnj` sidecar is present — they
are either already implemented or are ordinary "add a loader for format X" tasks independent of the
`.cnj` idea, and they keep working exactly as they do today with zero `.cnj` files anywhere in a
project. What *is* new design surface: any of these native types can now optionally gain a same-named
`.cnj` sidecar (see next section) purely for extra metadata, without giving up native decoding for
the actual pixel/audio bytes.

## `.cnj` file shape

A `.cnj` file is always a single JSON object with (at least) these top-level fields:

```json
{
  "cnjVersion": 1,
  "type": "SpriteFont",
  "...type-specific fields...": "..."
}
```

- `cnjVersion` — an integer schema version for the envelope. The *ceiling* a reader accepts is
  **per document type**, not global: `Model` accepts 1 and 2, every other type accepts 1 only, and a
  version above a type's own ceiling is rejected by name. Raising it globally would make "a future
  version is rejected" false for every type that has not defined one. Bump a type's ceiling only
  when the change is one an older reader cannot ignore — `Model`'s version 2 adds the `"bones"`
  hierarchy, without which every mesh sits at the origin — and prefer optional, written-only-when-
  non-default fields, which need no bump at all. See "`Model` `.cnj` version 2 — migration notes for
  consumers" below.
- `type` — a stable string key (e.g. `"SpriteFont"`, `"Model"`, `"Effect"`, `"AnimationClip"`, or a
  game-specific type name for migrated custom data) identifying what the document is. As covered in
  "Note on dispatch" above, the primary dispatch key in CNA is still the requested C++ type `T` at
  the `Load<T>()` call site — `type` is a cross-check against that, structurally analogous to how
  `plan_xnb.md`'s reader-name table lets `.xnb` verify what it's reading, but as a plain JSON string
  instead of a binary assembly-qualified name — so no generic-name parsing (`plan_xnb.md`'s
  XNB-13/13A problem) exists in this format at all. For tooling with no compile-time `T` (asset
  validators, migration scripts), `type` doubles as the actual dispatch key into a small
  string-to-handler table.
- `sourceFile` — **optional**. When present, this `.cnj` is a *metadata sidecar* for another file
  (usually a same-named native file, e.g. `ahoj.cnj` with `"sourceFile": "ahoj.png"`), not a
  self-contained descriptor. See "`.cnj` as metadata for a native sibling file" below. When absent,
  the `.cnj` document is expected to be fully self-contained (as `SpriteFont`/`Model`/`Effect` are
  today).
- Everything else is defined per-type (see below). There is no shared binary primitive layer, no
  7-bit encoded integers, no shared-resource object graph — ordinary JSON object/array/number/string
  nesting *is* the object graph. CNA's existing four JSON readers parse fields with small hand-rolled
  helpers (`ExtractJsonStringField`/`JsonInt`/`JsonFloat`/... in `ContentManager.cpp`), not a general
  JSON library — a `.cnj` migration should either keep using/extending those helpers or adopt a real
  JSON library at that point, since a shared envelope makes a common parse-then-dispatch entry point
  worth having either way.

### Referencing other files from within a `.cnj`

A `.cnj` document may reference other assets by logical name/relative path (e.g. a `SpriteFont`
referencing its glyph atlas texture, or a `Model` referencing per-mesh textures). Those references
are just strings resolved through the same two-level dispatch rule above (`.cnj` first, then native
extension), recursively, through the owning `ContentManager` — this gives asset caching and
`Unload()` semantics "for free" reusing the identical mechanism already used for the top-level
asset. There is no separate "external reference" object model needed (contrast with `plan_xnb.md`'s
`ContentReader::ReadExternalReference<T>()`, which exists only because raw `.xnb` has no such
built-in path-resolution convention).

```json
{
  "cnjVersion": 1,
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
`ContentManager.cpp`) instead of inventing new ones — a future migration to `.cnj` then only needs to
add the envelope (`cnjVersion`/`type`) and rename the extension, not rename every field too.

### `.cnj` as metadata for a native sibling file (the `sourceFile` field)

A `.cnj` document doesn't have to be self-contained. If it has a `"sourceFile"` field, CNA loads that
referenced file the normal way (native extension, recursively through the same `ContentManager` —
same caching/`Unload()` mechanism as any other reference) and treats the rest of the `.cnj`'s fields
as *metadata about* that file rather than as the data itself:

```json
{
  "cnjVersion": 1,
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
fully-supported native format the `.cnj` is simply enriching, or a raw/proprietary blob (arbitrary
extension, or none at all) that isn't self-describing and needs the `.cnj`'s fields (width, height,
pixel format, compression, ...) to be interpreted at all. `sourceFile` is resolved through the exact
same two-step rule as the top-level asset (`.cnj` first, then native extension) — it just can't
itself point at another `.cnj` with its own `sourceFile` (no chained/recursive sidecars; one `.cnj`
per physical payload, matching how `Model`'s sidecars are never themselves JSON).

**Enforced safety guarantees** (`plan_cnj.md` CNB-32, `CNA::Internal::ResolveCnjSourceFileSafely()`
in `include/CNA/Internal/CnjSourceFile.hpp`): `sourceFile` cannot be an absolute path; its resolved
target cannot escape the content root (including via `../` or a symlink); and it cannot resolve to
another `.cnj`, whether named explicitly (`"foo.cnj"`) or implicitly (`"foo"`, where the normal
resolver would pick a sibling `"foo.cnj"` over any other candidate — this closes a self-referential
cycle too, since a cycle is just a chain back to the same file). Any violation throws
`ContentLoadException` naming both the referring `.cnj` and the rejected target.

Authoring note: `.cnj`'s presence always wins (see "The core rule" above) — a `Content/ahoj.png` with
an unrelated, coincidentally-same-named `Content/ahoj.cnj` sitting next to it (self-contained, no
`sourceFile`, describing something else entirely) will shadow the PNG, not merge with it. This isn't
a resolver bug, it's the same "one name, `.cnj` wins" rule applied consistently — but it's worth
calling out explicitly as a content-authoring footgun, since nothing on disk visually distinguishes
"enrichment sidecar" from "unrelated same-named asset" other than opening the file and checking for
`sourceFile`.

### The `sourceFile` capability matrix (plan_cnj.md CNB-34)

Not every built-in reader accepts `sourceFile` — the resolver tries `.cnj` for *every* registered
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
| game-specific `RegisterCnjLoader<T>` | Reader-defined | Not part of the built-in envelope contract at all — a custom factory receives the raw `.cnj` JSON and is free to define (or ignore) its own `sourceFile`-like convention |

A `SpriteFont`/`Effect`/`Model` `.cnj` with a `sourceFile` field throws `ContentLoadException`
immediately (naming the reader and the file), rather than either silently ignoring the field or
letting the reader's own native decoder choke on the raw `.cnj` JSON text with a confusing
low-level error.

### Why one shared extension is not a new risk

A natural objection: doesn't collapsing every JSON type onto one `.cnj` extension mean two
differently-typed assets that happen to share a logical name (e.g. a `SpriteFont` and an
`AnimationClip` both called `"Cursor"`) collide on the same physical file, `Cursor.cnj`?

Yes — but this is exactly how original XNA's real `.xnb` pipeline already behaved, for the same
reason: the content build step always produces one `<name>.xnb` per logical asset name, regardless
of source type, and the *type* is resolved from inside the file (the reader-name table), not from
the extension. A real XNA content project could never contain both an image and a sound that both
built to `hello.xnb` — that was already a build-time conflict in the original tooling, not something
`.cnj` invents. CNA's own current `.font.json`/`.model.json`/`.shader.json`/`.skinnedmodel.json`
split is the actual deviation from original XNA behavior here (a convenience CNA added because it
was easy, not because XNA worked that way). Adopting one shared `.cnj` extension is not a new risk
relative to XNA — it is CNA becoming *more* faithful to how the real `ContentManager` always worked:
one physical asset per logical name, type resolved from content, not filename.

The real, if more modest, cost is opacity: unlike `.font.json` vs. `.model.json`, a person or tool
browsing a `Content/` folder full of `.cnj` files can no longer tell a font from a model without
opening one — same as original `.xnb`, where every asset file also looked identical from outside a
file browser. Tooling (an asset validator, an editor "inspect this file" command) needs to open and
read `"type"` to know what something is, exactly like a `.xnb`-aware tool would.

## Per-type `.cnj` conventions

Only a sketch of the initial set — the exact field names/shapes are a follow-up design task per
type, not fixed by this document. The point of this section is to establish *scope and grouping*,
matching the same practical asset categories `xnb.md`/`plan_xnb.md` already identified as mattering
for real XNA content, so nothing important is silently dropped by switching strategies.

| `.cnj` `type` | Replaces (from `xnb.md`'s XNA reader inventory) | Shape |
|---|---|---|
| `SpriteFont` | `SpriteFontReader` | JSON metadata (glyphs, kerning, line spacing) + a reference to a plain `.png` glyph atlas texture (no custom pixel packing logic needed inside `.cnj` itself — the atlas is just an ordinary image file). |
| `Model` | `ModelReader` + `VertexBufferReader`/`IndexBufferReader`/`VertexDeclarationReader` | JSON metadata (bone hierarchy, mesh/mesh-part list, per-part material/effect references) referencing either (a) a conventional interchange mesh format CNA can already load (e.g. glTF/OBJ, if/when supported) for raw vertex/index data, or (b) a small CNA-owned binary vertex/index blob file referenced by path, kept *outside* the JSON (JSON is a poor fit for large raw float/index arrays). |
| `AnimationClip` / skeletal animation data | `SkinningDataReader`-style custom readers seen in several samples (see `xnb.md`'s Lua discussion) | **Implemented** (`AnimationClipTypeReader`, `plan_cnj.md` `CNB-40`, Phase 10) — either inline JSON keyframe/bone-transform data (`"tracks"`), or, for large clips, a reference to an external raw binary blob via `"clipFile"` (the same `.clip.bin` format and shared reader `Model`'s/`SkinnedModelEXT`'s own `"animations"` field already used), to avoid bloating JSON with thousands of matrices. Standalone and independent of any specific `Model` — loaded via `ContentManager::Load<Graphics::AnimationClipEXT>()` (aliased as `Graphics::AnimationClip`). |
| Stock effect parameters (`BasicEffect`/`SkinnedEffect`/etc.) | Stock-effect XNA readers (`xnb.md`'s "stock effects" section) | **Implemented** (`plan_cnj.md` `CNB-45`, Phase 11) — plain JSON parameter object (colors, texture references, lighting flags) dispatched from inside the existing `EffectTypeReader` by `.cnj` `"type"` (`"BasicEffect"`/`"AlphaTestEffect"`/`"DualTextureEffect"`/`"EnvironmentMapEffect"`/`"SkinnedEffect"`, alongside the pre-existing `"Effect"` custom-GLSL shape), consumed directly by CNA's existing native stock-effect C++ classes. Omitted fields fall back to each effect's own real constructor defaults, not an independently-chosen default. |
| `Texture3D` (volume texture) | `Texture3DReader` | **Implemented** (`plan_cnj.md` `CNB-43`, Phase 11) — self-contained JSON (`"width"`/`"height"`/`"depth"`) + a raw RGBA8 binary sidecar (`"data"`), mirroring `Model`'s own vertex/index binary-sidecar convention (no native "volume texture" file format exists in CNA the way `.dds` serves `TextureCube`). Single mip level; no DXT decompression (hand-authored `.cnj` content has no natural source for pre-compressed DXT data). |
| `Curve` | `CurveReader` | **Implemented** (`plan_cnj.md` `CNB-44`, Phase 11) — self-contained JSON (`"preLoop"`/`"postLoop"` + `"keys"`: `position`/`value`/`tangentIn`/`tangentOut`/`continuity`), a direct port of `CurveContentTypeReader.hpp`'s already-FNA-verified field order. |
| General custom/`.fx`-based `Effect` | `EffectReader` (compiled platform shader bytecode) | **Explicitly out of scope**, exactly as `plan_xnb.md`'s XNB-32A/XNB-14B already conclude for the binary case: no format, JSON or binary, changes the fact that an original D3D9-era compiled shader blob cannot be run through bgfx. A `.cnj` `Effect` entry can only ever reference a *rebuilt* CNA-native shader (source `.fx`/CNA shader recompiled through CNA's own pipeline), never carry the original bytecode meaningfully. |
| Game-specific custom data (the ~82 hand-written `ContentTypeReader` classes found across `RolePlayingGame`, `Movipa`, `RobotGame`, etc. — see this session's sample survey) | Custom `ContentTypeReader` subclasses | A game-specific `type` string, with fields chosen by whoever migrates that game's content — no CNA core change needed per game, same "don't grow CNA core for one sample" principle `xnb.md`'s Lua section already argued for, just without needing a Lua sandbox/host at all: it is only ever *data*, read once by a small piece of C++ (or even generic reflection-free JSON field access) written for that one game. |

Note the recurring pattern: every category that `plan_xnb.md` treats as a "hard, later-phase"
problem (stock effects vs. general `EffectReader`, custom readers, large binary payloads) keeps
essentially the same hard/impossible boundary here. `.cnj` does not make the general `EffectReader`
problem solvable, and does not make large per-vertex arrays pleasant in JSON — it only removes the
*binary protocol and reader-registry* complexity around the parts that were never protocol-hard to
begin with (primitives, math structs, simple metadata, font/model metadata).

## `Model` `.cnj` version 2 — migration notes for consumers (`GLTF-455`)

The `plan_gltf.md` campaign changed what a `Model` `.cnj` contains. This section is for anyone who
**reads** one — a tool, a validator, or a game that shipped assets converted by an older
`gltf_to_cnj`.

### The version ceiling is per type, not global

`cnjVersion` is the envelope's version, and the ceiling a reader accepts is passed **per document
type** (`ValidateCnjEnvelopeBaseline`'s `maxVersion`). `Model` accepts **1 and 2**; every other type
still accepts **1 only**. Raising the ceiling globally would have made "a future version is
rejected" false for types that never defined one, which is the whole point of having the field.

A non-integer version (`1.5`) is rejected outright rather than truncated into range.

### What version 2 adds

| Field | Where | Why it exists |
|---|---|---|
| `"bones"` | top level | The scene's node hierarchy: `{ "name", "parent", "transform" }` per node, parent-before-child. Before it, **every mesh was emitted in mesh-local space with an identity bone** — audit defects D1–D3, so a mesh instanced by two nodes drew twice at the origin. |
| `"parentBone"` | each `"meshes"` entry | Which node in `"bones"` places this mesh. The other half of the same fix. |

A version-1 `Model` document has neither, and the reader treats an absent `"bones"` as "no
hierarchy" rather than an error — so an old asset still loads, with exactly the placement behaviour
it had when it was written. **It does not silently gain correct placement**: re-convert the source
`.gltf` to get that.

### Fields added since, all optional and all written only when non-default

Everything below is emitted by `tools/gltf_to_cnj` **only when it differs from the default**, so a
`.cnj` for an ordinary opaque triangle-list asset is byte-identical to what the same tool wrote
before these fields existed. A reader that does not know a field may ignore it and will get the
pre-campaign behaviour for that asset; the *file* is still version 2 whenever it has `"bones"`.

| Field | Default when absent | Added by |
|---|---|---|
| `"primitiveTopology"` | `"TRIANGLES"` | `GLTF-073` — a part's real topology, so a line or point primitive is no longer drawn as a triangle list. |
| `"partOfMesh"` | the placement is its own mesh | `GLTF-139` — groups the parts of a multi-primitive mesh. |
| `"alphaMode"` | `"OPAQUE"` | `GLTF-228` |
| `"alphaCutoff"` | `0.5` | `GLTF-229`. Applied since `GLTF-372`: a `MASK` material's cutoff reaches the shader's alpha test. |
| `"doubleSided"` | `false` | `GLTF-231`. **Carried, not applied** — culling is a `RasterizerState` the application sets. |
| `"unlit"`, and `"diffuseColor"`/`"alpha"` beside it | lit | `GLTF-337` — `KHR_materials_unlit`. The base colour travels with the flag because nothing else on the non-PBR path reads it. |
| `"vertexColorEnabled"` | `false` | vertex colours on the non-PBR path. |
| `"metallicFactor"`, `"roughnessFactor"`, `"emissiveFactor"`, `"diffuseColor"`, `"alpha"` | PBR entries only | `GLTF-216`/`GLTF-219`/`GLTF-221` — written for `PbrEffect`/`SkinnedPbrEffect` entries. |
| `"normalMap"`, `"metallicRoughnessMap"`, `"emissiveMap"`, `"occlusionMap"` | no map | PBR entries only. |
| `"materialVariantNames"` | no selectable variants | `GLTF-341`/`GLTF-342` — the source-order `KHR_materials_variants` name table at the document root. |
| `"variantOf"`, `"materialVariant"` | an ordinary exposed mesh-part entry | A variant is another complete `"meshes"` state record. `variantOf` names the preceding default entry ordinal and `materialVariant` indexes the root name table; the reader captures its effect, compatible vertex buffer, textures and samplers as an alternative instead of exposing another `ModelMeshPart`. Missing mappings therefore retain the default state. |
| `"morphTargets"`, `"morphWeights"`, `"morphWeightTrack"` | no morph targets | the `_morph.bin` sidecar plus its default weights and, when animated, its weight track. |
| `"lights"` | no imported lights | `KHR_lights_punctual`, at most three directional. |
| `"animations"` | none | each entry names a standalone `AnimationClip` `.cnj`. |

### Structured glTF import report (optional root object)

`GLTF-034` subsequently added an optional top-level `"gltfImportReport"` object. Current
`gltf_to_cnj` output always writes it; documents produced before that task omit it and load with an
empty report. This does not raise `cnjVersion`: old readers already ignore unknown root members,
and no existing member changed meaning.

The summary members are non-negative integers: `nodeCount`, `meshInstanceCount`,
`distinctMeshCount`, `sharedMeshCount`, `maxNodeDepth`, `cameraNodeCount`, `lightNodeCount`,
`importedLightCount`, `primitiveCount`, `skinCount`, `animationCount` and `clipCount`.
`diagnostics` is an ordered array whose entries contain:

| Member | Meaning |
|---|---|
| `code` | Stable lower-case machine-readable identifier. |
| `severity` | `Information` or `Warning`. |
| `kind` | `Information`, `GeneratedData`, `InvalidSourceData`, `Approximation`, `DroppedData` or `UnsupportedFeature`. |
| `subject` | Affected primitive, node, clip or extension; may be empty. |
| `count` | Non-negative occurrence count represented by this entry. |
| `worstMagnitude` | Finite measured maximum, or zero when the code defines no magnitude. |
| `details` | Array of affected map, attribute or extension names. |
| `message` | Display text; consumers must not treat its wording as stable API. |

The reader rejects wrong types, negative/fractional/non-finite/overflowing counts, non-finite
magnitudes and unknown enum names. Serialized validation messages omit the converter machine's
absolute input path, keeping generated content relocatable and reproducible.

### Morph-sidecar compatibility

The `_morph.bin` prefix remains the CNB-82 layout: target count, then each target's position deltas
and optional normal deltas. `GLTF-289` adds tangent xyz deltas in an optional trailer beginning with
the little-endian magic `MTAN`, a version (`1`) and the repeated target count. An older reader stops
after the prefix and therefore retains its former behaviour; a current reader accepts both the old
no-trailer file and the new one. Tangent handedness is not duplicated in the trailer: `TANGENT.w`
already lives in the base vertex bytes and morph blending deliberately never changes it.

A target that omits `POSITION` is legal glTF. The legacy prefix nevertheless uses its position
count to size every following semantic, so the writer emits a zero-filled position stream for that
target before its authored normal/tangent deltas. This keeps the old prefix structurally readable
without changing the target's blended result.

## Custom loaders (game-registered, selected by `.cnj` `"type"`)

CNA already has `ContentManager::RegisterTypeReader<T>()` (`ContentManager.hpp`) — a game can
register its own reader for a whole C++ type `T`. But that registry is keyed by `std::type_index`,
one reader per `T`, fixed by whatever `Load<T>()` call sites use. It has no way for two `.cnj` files
that both request the same `T` to be parsed by two different, independently-registered pieces of
game code chosen at runtime by the `.cnj` itself — e.g. a game with both `"EnemyDefinition"` and
`"LootTable"` `.cnj` `type`s that happen to deserialize into the same generic `T` needs two different
parsing functions, not one.

A second registry closes that gap: keyed by the `.cnj` `"type"` string instead of `std::type_index`.
**Implemented** (`plan_cnj.md` CNB-24/25, `ContentManager.hpp`):

```cpp
template <typename T>
using CnjLoaderFn = std::function<T(const std::string& cnjJson, ContentManager& cm)>;

template <typename T>
void ContentManager::RegisterCnjLoader(const std::string& typeName, CnjLoaderFn<T> factory);
```

One deviation from the original sketch: `CnjLoaderFn`'s first parameter is the raw `.cnj` JSON
text (`const std::string&`), not a `JsonValue` — CNA has no JSON *object* type, only the
hand-rolled string-scanning helpers `.cnj` readers already use throughout this document, so the
factory does its own field extraction the same way `SpriteFontTypeReader`/`ModelTypeReader`/etc.
do.

The first time `RegisterCnjLoader<T>()` is called for a given `T`, it lazily registers a small
built-in `GenericCnjTypeReader<T>` for that `T` (via the existing `RegisterTypeReader<T>()`) — so
no change to `Load<T>()`'s own dispatch was needed. That generic reader parses the envelope, looks
up the `.cnj`'s `"type"` in the table, and invokes whichever factory matches. This only applies to
a `T` with **no existing reader already registered** (built-in or otherwise) — `RegisterCnjLoader`
throws immediately if one already exists for `T`, since that reader would never consult this table.
No `LooseFileContentTypeReader<T>` subclass or CNA core change is needed per game-specific `.cnj` `type` —
same "don't grow CNA core for one game's data" principle already used for the plain
game-specific-`type` row in the table above, just now with the dispatch key coming from the `.cnj`
file itself instead of requiring the caller to already know which of several shapes it's asking
for.

Worked example, matching the `"EnemyDefinition"`/`"LootTable"` case described above (see
`tests/Microsoft/Xna/Framework/Content/CnjCustomLoaderTests.cpp` for the full, passing test):

```cpp
struct GameData { std::string kind; };

cm.RegisterCnjLoader<GameData>("EnemyDefinition",
    [](const std::string& json, ContentManager&) { GameData d; d.kind = "Enemy"; return d; });
cm.RegisterCnjLoader<GameData>("LootTable",
    [](const std::string& json, ContentManager&) { GameData d; d.kind = "Loot"; return d; });

// goblin.cnj: {"cnjVersion": 1, "type": "EnemyDefinition"}
// chest.cnj:  {"cnjVersion": 1, "type": "LootTable"}
GameData enemy = cm.Load<GameData>("goblin"); // kind == "Enemy"
GameData loot  = cm.Load<GameData>("chest");  // kind == "Loot"
```

This is the closest practical C++ analog of how real XNA's `.xnb` format let a reader be identified
purely by an assembly-qualified name embedded in the file, decoupled from whatever the calling code
asked for — except as a plain string key into an explicitly pre-registered table instead of runtime
reflection over a loaded .NET assembly, so none of the dynamic type-loading/reflection machinery
`plan_xnb.md` already argues against needs to exist for this either.

**Registration contract (`plan_cnj.md` CNB-37):** `RegisterCnjLoader<T>` is deterministic and
fails fast rather than silently accepting bad input — an empty `typeName` or an empty `factory`
throws `std::invalid_argument` immediately, and re-registering an already-used `(T, typeName)`
pair throws `std::logic_error` rather than quietly replacing the earlier factory (the same
`std::logic_error` the "type already owned by another reader" case throws — see below). Two
*different* `typeName`s for the same `T` remain fully supported, as shown above — only an exact
repeat of both `T` and `typeName` is rejected.

## Relationship to CNA's existing per-type JSON conventions

Before this document was implemented, CNA's JSON content readers predated the `.cnj` envelope and
each used their own bespoke extension. Three of the four were migrated (`plan_cnj.md` Phases 3–5);
one — `SkinnedModelTypeReader` — was deliberately kept separate (`CNB-22`):

| Reader class | Extension before | Extension now | Real field names (for reference) |
|---|---|---|---|
| `SpriteFontTypeReader` | `.font.json` | `.cnj` | `texture`, `lineSpacing`, `spacing`, `defaultCharacter`, `glyphs[].char`/`source`/`crop`/`kerning` |
| `ModelTypeReader` | `.model.json` | `.cnj` | `bones`, `meshes[].vertices`/`indices`/`vertexStride`/`texture` (binary sidecars for vertex/index data) |
| `EffectTypeReader` | `.shader.json` | `.cnj` | see `EffectTypeReader::Read` in `ContentManager.cpp` |
| `SkinnedModelTypeReader` (NOXNA, Avatar-only) | `.skinnedmodel.json` | **unchanged** — see `CNB-22` | `skeleton`, `parts[].vertices`/`indices`/`vertexStride`/`texture`, `animations[].name`/`clip` |

Unifying the three was a **migration of already-working, already-tested code**, not a green-field
build, exactly as anticipated: each reader's `GetExtensions()` changed from its own extension to
`{".cnj"}`, each reader gained a `"cnjVersion"`/`"type"` envelope check, and every existing fixture
in the repo (7 example programs' `.model.json` fixtures, `easygl_bloom_extract_test.cpp`'s
`.shader.json` fixture) was renamed with the two new envelope fields added. None of the readers'
actual field parsing needed to change otherwise — field names were kept exactly as they were, which
kept the diff small and low-risk, confirmed by a clean full-suite regression after each phase.

Two things were genuinely net-new scope beyond the three-reader migration, not just a rename: the
`.cnj`-first resolution order (letting `.cnj` act as an optional metadata sidecar for `Texture2D` —
proven via `sourceFile` + `colorKey` — which had no sidecar mechanism at all before) and the
`RegisterCnjLoader<T>` custom-loader registry (nothing before provided an equivalent). See
`plan_cnj.md` Phases 1–2 and 7 for how each was implemented and tested.

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
   `.cnj`/native-file conventions above. This tool is new code, but it is *much* smaller than a
   binary `.xnb` reader: it only ever needs to go one direction (typed .NET object → JSON/native
   file) using the official runtime's own deserialization, never reimplementing that deserialization
   itself.
3. **A manual pass for game-specific custom readers.** Automation only gets you so far here — same
   caveat `xnb.md`'s Lua-porting discussion already raised for "AI-assisted, not guaranteed" C#→Lua
   conversion applies equally to C#→`.cnj`-exporter conversion. The export tool needs one small
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

> **Update 2026-07-16:** the table below and the "freeze" recommendation after it describe the
> *original* either/or framing (adopt `.cnj` and freeze `.xnb`, or adopt `.xnb` instead of `.cnj`).
> That framing has since been superseded — see "Update 2026-07-16" near the top of this document.
> CNA now does **both**: `.cnj` stays exactly as implemented below, and an MVP-scoped `.xnb` binary
> reader sits above it in resolution order. The comparison table is kept because its per-row
> tradeoffs remain accurate descriptions of each format's own properties (a `.xnb` file still can't
> handle unknown future variants unless someone implements that; `.cnj` still can't offer drop-in
> compatibility with an unconverted original asset) — only the "pick one" conclusion no longer
> holds, and the Lua row below has been updated to reflect the 2026-07-16 rejection.

This document does **not** retroactively invalidate `xnb.md`/`plan_xnb.md` — both remain accurate
records of what a full binary `.xnb` reader costs and how it is sequenced. This document exists so
that choice could be made explicitly, by comparing:

| | Binary `.xnb` reader (`xnb.md`/`plan_xnb.md`) | `.cnj` JSON + native-by-extension (this document) |
|---|---|---|
| End-user experience | Drop original `.xnb` next to the game, it just loads | Must run a one-time migration/export step first |
| CNA maintenance surface | Large for broad coverage: binary protocol, LZX, ~40-reader registry | Minimal: a JSON envelope + per-type field conventions CNA fully controls |
| Implementation cost | Large for broad coverage (`xnb.md`'s own estimate); an MVP slice (container + primitives + `Texture2D`, uncompressed only) is active now | Small (a resolver + a handful of per-type (de)serializers; native-extension loading for PNG/JPEG/WAV already exists today) |
| Handles unknown/future `.xnb` variants | Only if explicitly implemented (MonoGame vs. FNA vs. platform variants — `plan_xnb.md`'s XNB-27/XNB-30C, both still deferred) | Not applicable — `.cnj` never reads `.xnb` itself |
| General custom `.fx` effects | Explicitly unsupported either way (`plan_xnb.md` XNB-32A) | Explicitly unsupported either way (same shader-porting problem) |
| Game-specific custom readers | Native C++ registration (`plan_xnb.md` Phase G); a sandboxed-Lua alternative (former Phase H) was proposed and then **rejected outright** 2026-07-16 | Ported to a one-time export-tool plugin, or `RegisterCnjLoader<T>` at runtime (this document) |

Both formats are active at once, ranked by `ContentManager`'s resolution order (`.xnb` above
`.cnj` above native) — see "The core rule" above for the mechanics, and `xnb.md`/`plan_xnb.md`'s own
status banners for exactly which `.xnb` phases are implemented versus still frozen at any given
time.

## Implementation record

The design above was implemented in full via `plan_cnj.md`'s numbered task list (`CNB-1`–`CNB-27`,
Phases 0–7), mirroring how `plan_xnb.md` turned `xnb.md` into concrete tasks — see that file for
the complete, phase-by-phase record (what changed, why, and each phase's regression-test tally).
Summary of what landed, in the order it happened:

1. `.cnj` envelope parsing + validation (`CNA::Internal::CnjEnvelope.hpp`) — Phase 0.
2. `.cnj`-first resolver order in `ResolveAssetPath`, for every registered type — Phase 1.
3. `sourceFile` support, proven on `Texture2D` via `colorKey` (selective pixel transparency) —
   Phase 2.
4. `SpriteFontTypeReader` migrated `.font.json` → `.cnj` — Phase 3.
5. `EffectTypeReader` migrated `.shader.json` → `.cnj` — Phase 4.
6. `ModelTypeReader` migrated `.model.json` → `.cnj` (its pre-existing FNA-fidelity gaps —
   single-bone synthesis, unassigned `ParentBone`, unset `BoundingSphere`/`Tag` — were deliberately
   *not* fixed in the same pass; see `plan_cnj.md` `CNB-21` for the reasoning) — Phase 5.
7. `SkinnedModelTypeReader` decided to stay separate, not migrated — see `plan_cnj.md` `CNB-22` and
   "Relationship to CNA's existing per-type JSON conventions" above — Phase 6.
8. `RegisterCnjLoader<T>` — Phase 7.
9. `AnimationClipTypeReader` — a standalone, directly-loadable `.cnj` `AnimationClip` document,
   independent of any specific `Model` (inline JSON tracks, or a `"clipFile"` reference to an
   existing `.clip.bin` blob) — Phase 10 (`plan_cnj.md` `CNB-40`–`CNB-42`), added 2026-07-17 as the
   first of the "genuinely new `.cnj` type" follow-ups this section used to flag as open.
10. `Texture3DTypeReader`/`CurveTypeReader`, and stock-effect `.cnj` support
    (`BasicEffect`/`AlphaTestEffect`/`DualTextureEffect`/`EnvironmentMapEffect`/`SkinnedEffect`,
    dispatched from inside `EffectTypeReader`) — Phase 11 (`plan_cnj.md` `CNB-43`–`CNB-47`), closing
    the last `.xnb`-vs-`.cnj` type-coverage gaps identified by cross-referencing FNA's real
    `ContentTypeReader` inventory.
11. `Model`'s/`SkinnedModel`'s `"animations"` field can now name a standalone, shareable `.cnj`
    `AnimationClip` asset (loaded/cached through `ContentManager`, real caching) instead of only a
    raw `.clip.bin` blob per model — Phase 11 continued (`plan_cnj.md` `CNB-48`/`CNB-49`).
12. A real glTF 2.0 → `Model`/`AnimationClip` import tool (`tools/gltf_to_cnj/`, `cna_tool_gltf_to_cnj`,
    vendored `cgltf`), including skeleton/skinning/animation — Phase 12 (`plan_cnj.md` `CNB-50`–`CNB-52`).

Further genuinely new `.cnj` types with no existing reader today (game-specific custom data beyond
what a game registers itself) remain a natural, open-ended follow-up — `RegisterCnjLoader<T>`
already supports them without any CNA core change.

## Update 2026-07-16: `.xnb` is back, ranked above `.cnj` (MVP scope)

CNA's owner decided `.xnb` should become a real, additional runtime format again — **not** a
replacement for `.cnj`, which keeps everything described in this document exactly as implemented.
See [`xnb.md`](xnb.md)'s and [`plan_xnb.md`](plan_xnb.md)'s own status banners for the full
decision; summarized here because it changes "The core rule" below:

- `ContentManager`'s resolution order gains a new top tier: `<name>.xnb`, checked **before** even
  the literal caller-given path. If a real, externally-produced `.xnb` file is present, it wins
  over both `.cnj` and any native file — see the revised "The core rule" section below.
- Only an MVP slice of `plan_xnb.md` is actually active (container parsing, binary primitives, the
  uncompressed case, a first real `Texture2D` reader — that plan's own M1/M2 milestones). LZX
  decompression, `SpriteFont`, stock effects, audio, `Model`, and top-quality hardening remain
  frozen/deferred. An unsupported/not-yet-implemented `.xnb` reader name fails with a clear error
  rather than silently falling through to `.cnj`/native — a present-but-unreadable `.xnb` is a hard
  error, not treated the same as an absent one.
- `plan_xnb.md`'s former Phase H (a sandboxed-Lua custom-reader host) was rejected outright as
  disproportionate complexity — custom `.xnb` readers stay native C++ only, registered the same way
  `RegisterCnjLoader<T>` already works for custom `.cnj` types.
- `ContentManager` also gains a startup content-manifest scan (`plan_xnb.md` Phase B3): an internal
  performance cache, a public `NOXNA` introspection API, and — for any `.xnb` files found — a
  reader-name inventory. This is orthogonal to `.cnj` itself but shares the same `ContentManager`.
- Writing/producing `.xnb` files remains permanently out of scope, exactly as `plan_xnb.md`'s own
  "Scope" section already stated — CNA only ever consumes `.xnb` files built by real XNA/MonoGame/
  FNA tooling.
- **Renamed:** the `.cnj`/loose-file loader interface described throughout this document as
  `ContentTypeReader<T>` is now `LooseFileContentTypeReader<T>`
  (`include/Microsoft/Xna/Framework/Content/LooseFileContentTypeReader.hpp`) — freeing the real
  name for FNA's actual `Microsoft.Xna.Framework.Content.ContentTypeReader`/`ContentTypeReader<T>`
  (`Read(ContentReader&, T)`), which this interface's shape (`Read(const std::string&,
  ContentManager&)`) never matched. Purely a rename — `RegisterTypeReader<T>`,
  `RegisterCnjLoader<T>`, and every existing `.cnj` reader keep their exact same behavior.
