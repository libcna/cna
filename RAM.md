# Texture memory in CNA

Where the CPU-side memory of a texture lives, why, and what is known to be improvable. Reconciled
with the code of `next` on 2026-10-17. Earlier revisions of this file (June to October 2026, in Git
history) analysed a single game, mobile-eggbert, mostly on the since-retired `VULKAN` renderer;
section 1 keeps what is still worth knowing from them.

## 1. History

In June 2026 mobile-eggbert's resident memory rose sharply on both renderers it was measured on,
`VULKAN` and EasyGL. Two CNA defects were found and fixed:

- `Texture2D` kept its CPU pixels (`cpuPixels_`, introduced for `GetData` and `SaveAsPng`) in a
  plain `std::vector`, and `ContentManager::Load<T>()` hands out copies of the cached value, so
  every loaded texture existed twice on the CPU: once in the cache and once in the game's copy.
  `cpuPixels_` became a `shared_ptr` that copies share (`e038937f3`).
- EasyGL's texture renderer kept a further copy of the pixels for context-loss restore. It now
  keeps a reference to the `Texture2D` buffer instead, and the loaders move decoded pixels into
  that buffer rather than copying them (`96195db36`).

For mobile-eggbert's thirteen startup textures (about 34 MiB of decoded RGBA) that was the
difference between up to three CPU copies and one. That duplication no longer exists.

The memory figures recorded at the time (between 24 and 158 MB) are not comparable with each other
or with anything measured now: one run crashed before `LoadContent` finished, build types and
validation-layer settings were not recorded, the runs came from different commits and renderers,
and no method (RSS or PSS, at which moment) was written down. The driver-side explanations offered
for them -- Mesa keeping a full shadow of every texture, fixed sizes for validation layers and
Vulkan loaders, mapped Vulkan memory counting fully towards RSS while GL buffers do not -- were
never measured, and are not repeated here as facts.

## 2. Current ownership model

**Decoding.** CNA's image decoder turns a file (the `Texture2D(path, device)` constructor, which the
loose-file content reader uses) or a stream (`FromStream`, `DDSFromStreamEXT`) into one
`ImageData` buffer. The renderer's `CreateTexture` uploads it, and `Texture2D` then moves that same
buffer into `cpuPixels_`: no copy is made. Decoded mip levels beyond the first move into
`extraMipLevels_`. For an uncompressed format the XNB reader creates the texture with the format
constructor, whose zero-filled level 0 its per-level `SetData` then fills in place.
`CreateFromPixels` takes the caller's vector by `const&` and therefore copies it once. Temporary
buffers exist only while a texture is being loaded.

**Texture2D and the renderer.** `ShareCpuPixels` hands the same buffer to the renderer. EasyGL and
WebGPU keep that reference, when context recovery is enabled, to restore the texture after a
context or device loss; every other renderer ignores it. No surviving renderer keeps its own copy
of an uncompressed texture's pixels except `SOFTWARE` and `HEADLESS`, and the Direct3D 9 runtime
under `DIRECTX9` keeps one of its own (section 3).

**Block-compressed textures** (`Dxt1`, `Dxt3`, `Dxt5`, and the `Dxt5SrgbEXT`/`Bc7EXT` extensions)
keep no CPU pixel buffer in `Texture2D`. Their blocks live in the renderer: EasyGL, WebGPU and
SDL_GPU keep every level in their own compressed store, answer `GetData` from it and re-upload it
after a loss, and `SOFTWARE` keeps them as part of its CPU texture. Until 2026-10-17 the format constructor kept a zero-filled level-0 buffer for these
too that nothing ever wrote or read -- 0.5 byte per texel for `Dxt1` and 1 byte for `Dxt3`/`Dxt5`,
4 MiB for a 2048x2048 `Dxt5` texture -- for the life of the texture (fixed in `38b9d8825`).

**Copies of a Texture2D.** The copy constructor and copy assignment share `renderer_`,
`cpuPixels_` and `extraMipLevels_`; a copy costs a reference count and one entry in the device's
resource list, never pixels. `ContentManager::Load<T>()` stores one copy in its cache and returns
another, so the cache and every value the game holds share one texture.

**Where a second copy can appear.** `SetData(const Color*, int)` on an ordinary texture of a
four-byte format builds a new renderer texture and a new buffer for the handle it is called on; the
cache and any other copies keep theirs, so that texture then exists twice. The other `SetData`
forms -- a rectangle, an explicit level or start index, other element types, `SetDataRGBA` --
write into the shared storage in place. That the overloads differ is recorded here only because it
decides where memory is duplicated; whether they should is a separate compatibility question.

**Context recovery off.** `GraphicsDevice::SetContextRecoveryEnabled(false)` (CNAEXT, to be called
before textures exist) frees each texture's level-0 buffer after upload. Only tests turn it off --
no CNA component and no C API route does -- and it is not an XNA-compatible mode: `GetData` of
level 0 and a partial `SetData` then throw `NotSupportedException`. Mip-level buffers are kept
either way.

## 3. Intentional memory costs

- **One CPU copy of every uncompressed texture** (plus about a third for a full mip chain) while
  context recovery is on, which is always in practice. `GetData` and partial `SetData` read and
  write it on every renderer, and EasyGL and WebGPU restore from it. XNA itself does not need such
  a copy -- FNA reads textures back from the GPU -- but replacing it with read-back would make
  `GetData` depend on the renderer: SDL_GPU, EasyGL and `DIRECTX9` cannot read an ordinary texture
  back today.
- **The content cache.** A `ContentManager` keeps every asset until `Unload()` or its disposal, as
  XNA's does, so loading level after level into one manager grows until the game unloads it; XNA's
  own idiom is one manager per level or world. `Unload()` drops the cache's references only: a
  texture's memory is released once the copies the game still holds are gone as well, because
  CNA's `Unload()` does not dispose them (another separate compatibility question).
- **Per renderer**, beyond the GPU texture itself:

  | Renderer | Extra CPU storage per texture |
  |---|---|
  | EasyGL (`OPENGLES3`, `OPENGL33`, `WEBGL2`) | none for uncompressed pixels (shares `cpuPixels_`); every DXT level |
  | `WEBGPU` | none for uncompressed pixels (shares `cpuPixels_`); every BC/DXT level |
  | `SDL_GPU` | none for uncompressed pixels; every DXT level |
  | `FNA3D` | none |
  | `DIRECTX9` | textures are `D3DPOOL_MANAGED`, so the Direct3D 9 runtime keeps its own system-memory copy (not measured) |
  | `SOFTWARE` | the texture itself, as declared-format bytes, RGBA8 and float samples (24 bytes per `Color` texel) |
  | `HEADLESS` | a copy of the pixels (section 4) |
  | `STUB` | none |

## 4. Known opportunities

Low priority; neither causes growth, wrong output or a crash, and neither is scheduled.

- **`TextureCube` keeps per-face CPU copies most renderers never read.** A `Color` cube keeps a
  level-0 RGBA copy of each face it is given data for (6 x size x size x 4 bytes: 24 MiB for a
  1024x1024 cube) and shares it with the renderer. Only WebGPU's device-loss restore reads it;
  EasyGL keeps the reference but restores cubes without it, and the other renderers ignore it. The
  copy is kept whether or not context recovery is enabled. Removing it needs a clean answer to
  which renderers restore cube content and from where, not a renderer-specific special case.
- **`HEADLESS` keeps pixel copies nothing reads.** Its texture renderer copies every upload and its
  render-target renderer allocates and fills a width x height x 4 buffer, and no code reads either.
  In a headless process that doubles texture memory and costs about 8 MiB per 1080p render target.

## 5. What has not been measured

There is no current memory measurement of CNA: sections 2 to 4 come from reading the code. Process
RSS alone cannot attribute memory to a driver, a mapped GPU allocation or CNA. A meaningful
measurement would fix one commit, one Release build, one renderer and one content set, turn renderer
debug and validation layers off, and record `VmHWM`, `/proc/<pid>/smaps_rollup`, the DRM `fdinfo`
memory keys and a heap profile (for example heaptrack) after loading and again at a steady state --
first on `HEADLESS`, which isolates CNA's own memory, then on each GPU renderer.
