> **This is the original architectural proposal, kept for its rationale. It is NOT the format
> specification.**
>
> The authoritative, byte-level description of CNB as implemented is **`docs/cnb-format.md`**;
> the engineering record is `plans/plan_cnb.md`. The header and `ChunkEntry` sketches further down
> this document were a starting point and **differ from what was built** — the implemented header
> is 64 bytes and the table-of-contents entry 48, with different fields and widths, and chunks are
> addressed by per-type ordinal rather than by numbered identifiers such as `VB00`. Where this
> document and `docs/cnb-format.md` disagree, `docs/cnb-format.md` is right and this one is
> history. `plans/plan_cnb.md` §2 records which proposals were adopted, changed or rejected.

Yes — **it is realistic, and in the current CNA I think it is already starting to make sense**. But only if CNB is not “XNB reinvented,” but rather an **official CNA-native compiled content format**.

After looking through `cna-next.zip`, I would even say that CNA’s current architecture is already surprisingly well prepared for a real `.cnb` format.

## My verdict

**I would implement CNB**, but with this clear separation of responsibilities:

| Format                     | Role                                                    |
| -------------------------- | ------------------------------------------------------- |
| `.xnb`                     | compatibility with XNA / MonoGame / FNA                 |
| `.cnj`                     | human-readable, editable, debugging/intermediate format |
| **`.cnb`**                 | **fast, compact, self-contained CNA runtime format**    |
| `.gltf/.glb/.png/.wav/...` | source / interchange formats                            |

The ideal pipeline would therefore be:

```text
FBX / glTF / PNG / WAV / CNJ / ...
                 |
                 v
        cna-content-compiler
                 |
                 v
              .cnb
                 |
                 v
      ContentManager::Load<T>()
```

That is the real reason for CNB to exist.

MonoGame uses exactly this general build-time content pipeline principle: a source asset is processed into a binary runtime format before execution so that the runtime does not have to perform all of the work during loading. ([docs.monogame.net][1])

---

# Why it makes sense in CNA today

While looking through your archive, I found one very specific problem that CNB could solve very elegantly.

Today, `gltf_to_cnj` produces files such as:

```text
robot.cnj
robot_mesh0_verts.bin
robot_mesh0_idx.bin
robot_mesh0_morph.bin
robot_mesh1_verts.bin
robot_mesh1_idx.bin
robot.skeleton.bin
robot_walk.clip.bin
robot_run.clip.bin
robot_tex0.png
...
```

And `ContentManager.cpp` contains custom binary readers for things such as `.clip.bin`, `.skeleton.bin`, morph data, and so on.

That means **part of CNB already exists de facto**; it simply has not yet been formalized as a single format.

Instead, the compiler could produce:

```text
robot.cnb
```

containing:

```text
Metadata
BoneHierarchy
MeshTable
VertexBuffer 0
IndexBuffer 0
MorphTargets 0
VertexBuffer 1
IndexBuffer 1
Skeleton
Animation: Walk
Animation: Run
MaterialTable
TextureReferences
...
```

In my view, this is the strongest argument for CNB.

---

# CNB should not copy XNB

XNB has complexity that CNA simply does not need in its own native format.

For example, the current MonoGame XNB format contains platform/version/flags information, supports compression variants, and then uses runtime `ContentReader` and type readers. MonoGame still has both LZX and LZ4 flags. ([GitHub][2])

MonoGame itself also documents that XNB is closely tied to its framework and is not designed as a general-purpose interchange format. ([docs.monogame.net][3])

Your current CNA XNB subsystem is already fairly large. In the attached snapshot, I counted approximately:

```text
XNB runtime implementation: ~5,549 lines
XNB tests:                  ~6,327 lines
42 files in the XNB subsystem itself
```

CNB does not need assembly-qualified names such as:

```text
Microsoft.Xna.Framework.Content.Texture2DReader,
Microsoft.Xna.Framework.Graphics, Version=4.0.0.0, ...
```

It does not need:

```text
ReflectiveReader<T>
ContentTypeReader table
.NET generic type parsing
XNA reader version negotiation
XNB shared-resource fixup protocol
LZX compatibility
XNA platform identifiers
```

All of that is needed because of **compatibility with an external format**.

With CNB, you own both the writer and the reader.

---

# How I would design CNB

The most important decision: I would make it **chunk-based**, not a serialization of C++ objects.

For example:

```text
CNB header

magic             "CNB\0"
formatMajor       uint16
formatMinor       uint16
flags             uint32
assetType         uint32
assetSchema       uint32
fileSize          uint64
tocOffset         uint64
chunkCount        uint32
```

Followed by a TOC:

```text
ChunkEntry
{
    uint32 type;
    uint32 flags;

    uint64 offset;
    uint64 storedSize;
    uint64 unpackedSize;

    uint32 checksum;
    uint32 alignment;
}
```

And, for example, a Model:

```text
CNB
 ├── META
 ├── STRINGS
 ├── BONES
 ├── MESHES
 ├── MATERIALS
 ├── VB00
 ├── IB00
 ├── VB01
 ├── IB01
 ├── MORPH00
 ├── SKELETON
 └── ANIMATIONS
```

This has several very useful properties.

A newer reader can skip a chunk it does not understand.

An older reader can reject only a new mandatory chunk.

The entire file does not have to be loaded into RAM.

Large vertex/index blocks can be read directly.

Individual chunks can use their own compression.

And in the future, some chunks could potentially be memory-mapped.

---

# Two separate kinds of versioning are important

I would not use only:

```text
cnbVersion = 7
```

I would use:

```text
containerVersion = 1
assetType = Model
assetSchemaVersion = 3
```

Because a change such as:

```text
Texture2D schema 1 -> 2
```

should not imply a change to the CNB container format itself.

For example:

```text
CNB container 1.0

Model schema 4
Texture2D schema 2
AnimationClip schema 1
SpriteFont schema 1
Curve schema 1
```

That kind of design could remain usable for decades.

---

# Never serialize C++ structures using memcpy

For example, I would consider this a mistake:

```cpp
file.write(reinterpret_cast<char*>(&model), sizeof(model));
```

Likewise, I would not rely on:

```cpp
sizeof(Vector3)
sizeof(VertexDeclaration)
sizeof(ModelMeshPart)
```

because of:

* padding,
* alignment,
* ABI,
* compiler differences,
* architecture differences,
* endianness,
* changes to the C++ implementation.

CNB should define its primitives precisely:

```text
u8
u16le
u32le
u64le
i32le
f32le
f64le
UTF-8 string
```

And use explicit structures.

Your existing sidecar formats already partially follow a similar approach, so the migration would not be dramatic.

---

# I would not make CNB binary JSON

That would be one possible approach:

```text
CNJ DOM
   ↓
MessagePack-like encoding
   ↓
CNB
```

It would be very easy to implement.

But I think it would waste the biggest opportunity.

If CNB is created, it should be a:

> **compiled runtime representation**

rather than:

> JSON with `{`, `"`, and `:` removed.

For example, a Model CNJ might say:

```json
{
  "vertexFile": "foo_verts.bin",
  "indexFile": "foo_idx.bin"
}
```

CNB should instead contain actual:

```text
MESH descriptor
VB chunk
IB chunk
```

without filename lookups, a JSON parser, and multiple filesystem operations.

---

# On the other hand, I definitely would not remove CNJ

CNJ is extremely useful for CNA precisely because it is textual.

I would see the relationship like this:

```text
              development
                  |
                  v
glTF ---------> CNJ
                  |
                  | cna-content-compiler
                  v
                 CNB
                  |
                  v
               runtime
```

CNJ:

```text
human-readable
git-diff friendly
manually editable
good for debugging
good as an intermediate representation
```

CNB:

```text
compact
fast to load
self-contained
better for distribution
no JSON parser required at startup
```

So the two formats complement each other very well.

---

# I would make the first CNB compiler CNJ → CNB

I would not try to create a new MGCB immediately.

The first version should be:

```text
cna_tool_cnj_to_cnb
```

For example:

```bash
cna_tool_cnj_to_cnb character.cnj character.cnb
```

The compiler would:

1. load the CNJ,
2. validate it using the current rules,
3. load all of its `.bin` sidecars,
4. create the chunks,
5. write `character.cnb`.

That is a relatively small project.

And, most importantly, you could verify equivalence:

```text
Load<Model>("character.cnj")
             ==
Load<Model>("character.cnb")
```

at the level of the resulting object graph.

Only after that:

```text
glTF -> CNB
PNG  -> CNB
WAV  -> CNB
...
```

---

# I would implement Model very early

Normally, I would start a new format with something simple such as `Curve`.

But from the perspective of **value to CNA**, I would go in this order:

1. `Curve`
2. `AnimationClip`
3. **Model**
4. `SpriteFont`
5. `Texture3D`
6. `Texture2D`
7. `SoundEffect`
8. stock Effects

`Curve` validates the infrastructure.

`AnimationClip` validates larger arrays.

And `Model` validates the actual reason CNB exists:

```text
.cnj + 15 sidecars
```

→

```text
1 .cnb
```

---

# Textures have one complication

I would avoid overengineering CNB v1 here.

It might be tempting to say:

```text
Texture2D CNB = directly GPU-ready texture
```

But CNA supports many renderers and different hardware.

For example:

```text
RGBA8
BC1
BC3
BC5
BC7
ETC2
ASTC
...
```

are not universally available.

So initially I would allow something like:

```text
Texture chunk:
    format = RGBA8 / BC1 / BC3 / ...
    mipCount
    width
    height
    data
```

with a clearly defined portable baseline.

Later, CNB could contain multiple variants:

```text
TEX0_RGBA8
TEX0_BC7
TEX0_ASTC
```

and the runtime could select the best supported one.

That would even be something XNB historically handles more through target-specific builds.

---

# I would not automatically embed Song and Video

For things such as:

```text
Model
Texture
Animation
SpriteFont
SoundEffect
```

a self-contained CNB makes a lot of sense.

But:

```text
Song
Video
```

can be large streamed files.

So, for example:

```text
music.cnb
    META
    externalFile = music.ogg
```

might be better than putting a 150 MB Ogg or video stream inside a compressed CNB and then making streaming unnecessarily complicated.

Embedded payloads could be added later as an option.

---

# Compression: v1 can simply have none

This is surprisingly important.

I would not invent:

```text
CNB Compression Algorithm™
```

and I definitely would not port LZX just because XNB uses it.

I also did not find a general-purpose zstd/lz4 dependency in the attached tree that would naturally be reused immediately.

So:

### CNB 1

```text
Compression::None
```

### CNB 1.x

possibly:

```text
None
LZ4
Zstd
```

at the individual chunk level.

PNG/JPEG/Ogg and similar formats are already compressed anyway, so additional compression often provides little benefit.

---

# I would add checksums

At minimum, each chunk should have something like:

```text
CRC32
```

or another inexpensive checksum.

Not as protection against an attacker, but against:

```text
truncated file
corruption
incorrect offset
broken build output
```

Your XNB implementation already has a fairly strong adversarial/bounds-checking approach. I would carry that over into CNB from day one:

```text
offset + size <= fileSize
chunk count limit
string size limit
array count limit
decompressed size limit
overflow-safe multiplication
no overlapping chunks unless explicitly permitted
```

---

# I would handle custom content much more simply than XNB

For example:

```cpp
RegisterCnbLoader<MyLevel>(
    CnbTypeId::FromString("MyGame.Level"),
    [](CnbReader& reader)
    {
        ...
    });
```

The file might contain:

```text
assetType = custom
typeId = hash("MyGame.Level")
schema = 3
```

And optionally also a debug string:

```text
"MyGame.Level"
```

You do not need .NET reflection or assembly names.

---

# I would not make Type ID only a hash

The ideal combination would be:

```text
uint64 stableTypeId
string optionalDebugName
```

Built-in CNA types would receive reserved IDs:

```text
1 Texture2D
2 Texture3D
3 TextureCube
4 SpriteFont
5 Model
6 AnimationClip
7 Curve
8 SoundEffect
...
```

Custom types could use, for example:

```text
0x80000000...
```

I would not base runtime type identification on `std::type_index`, because it is not a serialization ABI.

---

# References between assets

CNB should support both approaches.

### Embedded

```text
Model.cnb
 └── VB
 └── IB
 └── skeleton
```

### External

```text
level.cnb
  texture = "Textures/wall"
```

Then normally:

```cpp
cm.Load<Texture2D>("Textures/wall");
```

This preserves `ContentManager` caching and allows a single texture to be shared by a hundred models.

So:

> **CNB should not mean that all game content must become one gigantic blob.**

One `.cnb` = one logical asset.

Later, an entirely separate format could be introduced:

```text
.cnapak
```

for packages.

I would not mix those two projects together.

---

# What I definitely would not do

I would not burden CNB v1 with:

```text
❌ reflection
❌ arbitrary object graph serializer
❌ C++ object serialization
❌ scripting
❌ custom compression algorithm
❌ encryption
❌ monolithic whole-game archive
❌ memory-mapped zero-copy for every object
❌ shader compilation for all 46 renderers
❌ hot reload
❌ incremental distributed build system
```

All of that could potentially come later.

CNB v1 mainly needs to:

> **deterministically convert the existing CNA asset representation into a robust binary file and back again.**

---

# How much work I think it would take

Given what CNA already contains, I would not see this as a thousand-hour project.

For a solid first version, I would estimate approximately:

| Part                                     | Rough estimate |
| ---------------------------------------- | -------------: |
| format specification + versioning        |        10–20 h |
| `CnbReader`/`CnbWriter`, TOC, validation |        20–40 h |
| tests/fuzz/corruption suite              |        20–40 h |
| CNJ → CNB compiler                       |        20–40 h |
| Curve + AnimationClip + simple types     |        15–30 h |
| Model + embedding current sidecars       |        40–80 h |
| texture/font/audio support               |        30–60 h |
| ContentManager integration + manifest    |        15–30 h |
| documentation/tool polish                |        15–30 h |

So roughly:

> **~150–300 hours for a genuinely solid CNB 1.0.**

MVP:

> **~50–100 hours.**

A complete MGCB-like content build system including importers, dependency graphs, incremental builds, platform-specific processing, shader pipelines, and so on:

> potentially **400–800+ hours**.

But that is already a different project from the binary format itself.

---

# An interesting historical coincidence

Your own `misc/cnj.md` says that `.cnj` was **originally called `.cnb`**, but it was renamed because `cnb` implied a binary format even though it was still JSON at the time.

So this would actually make for a nice evolution:

```text
original .cnb
    ↓
realization: it is not binary
    ↓
.cnj
    ↓
actual binary format
    ↓
.cnb
```

This time, the name would finally match the contents exactly.

The only thing I would be careful about is the historical task IDs such as `CNB-1`, `CNB-40`, `CNB-97`, etc. in `plan_cnj.md`. For the new project, I would therefore use something like:

```text
CNBF-001
CNBF-002
...
```

so that grepping for `CNB-` does not mix the historical CNJ tasks with the new binary-format work.

---

# What I would do specifically in CNA

I would propose this roadmap:

```text
Phase 0
CNB specification v1

Phase 1
CnbReader
CnbWriter
header
TOC
chunks
bounds checks
checksum
tests/fuzz

Phase 2
Curve.cnb
AnimationClip.cnb

Phase 3
cna_tool_cnj_to_cnb

Phase 4
Model.cnb
- vertex buffers
- index buffers
- bones
- skeleton
- morph targets
- animations
- material metadata

Phase 5
SpriteFont
Texture3D

Phase 6
Texture2D
TextureCube
SoundEffect

Phase 7
custom CNB loaders

Phase 8
gltf_to_cnb directly
```

And then you could eventually have:

```bash
cna-content build Content/
```

with:

```text
Content/
    models/car.cnb
    textures/road.cnb
    fonts/ui.cnb
    sounds/crash.cnb
```

---

## The resulting architecture would be very clean

```text
                    ┌──────────── XNB
                    │        legacy compatibility
                    │
ContentManager ─────┼──────────── CNB
                    │        CNA shipping/runtime
                    │
                    ├──────────── CNJ
                    │        human-readable/intermediate
                    │
                    └──────────── PNG/WAV/glTF/...
                             loose/source formats
```

And in my view, this is a **much stronger architectural reason for CNB than simply “I want an alternative to XNB.”**

**So yes: I would add CNB to CNA.** Not as a priority ahead of finishing the framework’s fundamental components, but as a separate future content-pipeline module. Given CNA’s current state, it is technically very realistic, and the Model/CNJ sidecar system already creates enough of a concrete problem for a new format to provide real practical value rather than merely cosmetic value.

[1]: https://docs.monogame.net/articles/tutorials/building_2d_games/05_content_pipeline/?utm_source=chatgpt.com "Chapter 05: Content Pipeline | MonoGame"
[2]: https://github.com/MonoGame/MonoGame/blob/develop/MonoGame.Framework/Content/ContentManager.cs?utm_source=chatgpt.com "MonoGame/MonoGame.Framework/Content/ContentManager.cs at develop · MonoGame/MonoGame · GitHub"
[3]: https://docs.monogame.net/articles/getting_to_know/whatis/content_pipeline/CP_Architecture.html?utm_source=chatgpt.com "What is the Content Pipeline Architecture? | MonoGame"
