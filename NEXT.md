# NEXT.md

## 1. Project summary

**CNA** is a C++23 reimplementation of the XNA 4.0 programming model
(`Microsoft::Xna::Framework`), built on SDL3 with a pluggable graphics backend
(EasyGL/OpenGL ES 3.2, Vulkan, Bgfx, SDL_Renderer, selected via `CNA_GRAPHICS_BACKEND`). It is a
framework/runtime, not a game — the goal is full XNA 4.0 API coverage with behavior fidelity to
FNA (`/rv/data/library/github.com/FNA-XNA/FNA`), backed by unit tests plus pixel-readback
integration tests for graphics.

**Current phase:** `plan_net.md`'s entire Net/GamerServices/Avatar/docs plan (Phases 1-9) is
done. Phase 10 (a new, CNA-original "Avatar real-rendering" NOXNA/EXT extension, not part of
`plan_net.md`) built a real GPU-skinned-mesh rendering path, but **the user has decided (this
session) to leave Avatar as a non-rendering stub for now** — the engine/pipeline side is done and
merged, but no real body/animation content is wired in, so nothing visible actually draws yet.
Treat Phase 10 as paused, not blocked.

**Architectural decisions that matter for future work:**
- `CNA_GamerServices` and `CNA_Net` are separate CMake static libraries (gated by
  `CNA_ENABLE_NET`), excluded from the main `CNA` GLOB. Avatar types live inside
  `CNA_GamerServices`.
- `GamerServices`/`Net`/`Avatar` are XNA-API-shape-compatible but **not** binary-compatible with
  Xbox Live: `Net` is backed by real ENet (reliable UDP); `Avatar` is faithfully inert off-Xbox,
  matching the real reference assembly's own behavior (see section 6).
- `sharp-runtime` (sibling repo, `../sharp-runtime/`) supplies all `System.*` types. It is
  maintained by a separate, concurrent session — only add new files there; never modify existing
  files or commit/push without asking the user first, **for every single commit**.
- Phase 10's real-rendering extension is entirely additive (`NOXNA`/`*EXT`-tagged); it never
  changes any faithful XNA-spec Avatar behavior. Don't conflate its `NOXNA` marker-macro
  convention with the separate, unrelated `CNA_NOXNA` CMake option described in `NOXNA.md`.

---

## 2. Current status

### Build / test
- **Native Linux** (`cmake-build-debug`, `CnaTests`): clean, **2191/2191** unit tests passing.
  Full `ctest` suite (2257 tests, includes EasyGL integration executables): **2254/2257** — the 3
  failures (`ENetDiscoveryServiceTest.UnregisterHostStopsAnsweringQueries` — flaky, passes alone;
  `EasyGL_MRT_TwoAttachments`; `easy-gl-resource-smoke-tests`) are pre-existing and unrelated to
  any file changed this session.
- **Windows cross-build** (`cmake-build-windows/`, Wine): clean, **2190/2190** (1-test difference
  from Linux is an expected SDL_Renderer-vs-EasyGL backend difference, not a failure).
- **Web/Emscripten cross-build** (`cmake-build-web/`, Node): clean,
  **Net/Gamer/ENet/Packet/Avatar/Skinned filter: 343/343**, 1 intentionally skipped
  (`ENetHostHandleTest.CreateHostBindsToEphemeralPort`, documented platform limit). Full suite
  still hits a pre-existing, unrelated `GameWindowTest` DOM crash under Node (needs a real browser
  `window`).
- **Android NDK cross-build** (`cmake-build-android/`): **build** compiles cleanly. **On-device
  test run currently impossible** — no `/dev/kvm` in this environment, so the emulator that
  worked in an earlier session can't boot here (infrastructure limitation, not a code issue).

### What works
- Real ENet-backed `SystemLink` networking end-to-end on Linux/Windows/Web/Android.
- Full faithful XNA 4.0 Avatar API port (byte-exact match of the real Microsoft reference
  assembly's behavior, including its off-Xbox no-op quirks).
- A real, working GPU-skinned-mesh rendering pipeline (`Graphics::SkinnedEffect`,
  `SkinnedModelEXT`, `AvatarRenderer::DrawRealEXT`), proven via a passing pixel-readback
  integration test — but **not currently wired to any real avatar content** (see below).

### What does not work / is intentionally incomplete
- **Avatar has no real visual content.** `AvatarRenderer::DrawRealEXT` works and is tested, but
  only against a synthetic 1-triangle fixture — no real body mesh, skeleton, or animation clips
  exist in this repo. Calling the real-rendering API in an actual game today draws nothing
  meaningful. This is an accepted, deliberate stopping point, not a bug.
- FFmpeg-backed video decoding: unavailable on Windows/Emscripten/Android cross-builds (no dev
  packages for those targets here). Native Linux unaffected.
- A real browser tab can never be a `NetworkSession` host (browsers can't listen on sockets); LAN
  broadcast discovery doesn't exist on Web at all. Both permanent platform constraints.
- `AvatarDescription::BeginGetFromGamer`'s disposed-`Gamer` throw path has no test — `Gamer` has
  no accessible way to become disposed anywhere in this codebase.
- Android's `TitleContainer::OpenStream` `SDL_LoadFile` fallback segfaults without a real
  JNI/Activity context (bare executable, not a packaged APK) — known, out of scope.

---

## 3. Recent changes

- **Phase 10 (Avatar real-rendering NOXNA/EXT extension) — committed and pushed** to `feature/net`
  (`2b653bc`, `7b56eab`, `1cc42d1`). New types: `Graphics::VertexPositionNormalTextureSkinned`,
  `Graphics::SkinnedModelEXT` (mesh+skeleton+clip container, independent of Avatar's real 71-bone
  arrays), `GamerServices::AvatarAppearanceEXT`, `AvatarAnimationPresetNamesEXT`. New members:
  `AvatarRenderer::EnableRealRenderingEXT/IsRealRenderingEnabledEXT/SetAppearanceEXT/DrawRealEXT`,
  `AvatarAnimation::SetRealClipNameEXT/GetRealClipNameEXT`. New content-pipeline reader
  (`SkinnedModelTypeReader`, `.skinnedmodel.json`/`.skeleton.bin`/`.clip.bin`). New integration
  test (`examples/avatar_real_render_integration_test.cpp`, passing). Docs:
  `docs/avatar-real-rendering-ext.md`, `THIRD_PARTY_NOTICES.md`, `AUDIT.md` updates. Full design
  rationale and file list are in that doc and in git history — not repeated here.
- **A real, unrelated `sharp-runtime` bug was found and fixed** while re-verifying the Web build:
  `String::GetHashCode`'s `h >> 32` shifted by the full width of `std::size_t` on Emscripten's
  32-bit wasm target. Fixed and pushed (`sharp-runtime@e400f92`) with the user's explicit
  go-ahead, merged with a large concurrent batch from the other session maintaining that repo.
- **Asset-acquisition attempts this session, both paused by user decision:**
  - MakeHuman: pip-installable (`pip install makehuman`) but GUI-only (no headless export mode);
    its asset data lives in a separate GitHub+Git-LFS repo; its own website and legacy FTP mirror
    refuse connections from this environment. An automation attempt was correctly stopped by
    Claude Code's permission classifier before running any downloaded code. User chose not to
    pursue further.
  - CharMorph/Blender (considered as an alternative, since Blender can run headless unlike
    MakeHuman): the `CharMorph` and `CharMorph-Vitruvian` repos were cloned to
    `/rv/tmp/charmorph_work/` (survives restart; `/tmp` does not) with the user's explicit,
    specific URL confirmation. Two Git-LFS-backed `.blend` files were fetched via the plain LFS
    HTTP batch API (no `git-lfs` binary needed/installed). Further progress (opening the files in
    Blender) was stopped by the permission classifier, which raised a legitimate concern: `.blend`
    files can embed auto-running Python scripts. **User then decided to stop pursuing real Avatar
    content entirely for now** — nothing in `/rv/tmp/charmorph_work/` has been opened/executed.
  - Lesson for future sessions: don't re-attempt downloading/running third-party tooling for this
    without fresh, specific user sign-off each time — general instructions like "try automation"
    are not sufficient authorization for the permission classifier.

---

## 4. Current blocker / main problem

**There is no active blocker.** `plan_net.md` (Phases 1-9) is complete. Phase 10's
engine/pipeline work is complete, tested, and merged. The remaining piece — real avatar body
mesh + animation content (Phase 10b) — is **intentionally paused by user decision**, not stuck on
a technical problem. Secondary, non-blocking items:
- Android on-device test re-run needs `/dev/kvm`, unavailable in this environment right now.
- Vulkan/Bgfx smoke-testing of the real-rendering extension is optional/best-effort; needs a
  fresh `cmake-build-vulkan` configure and `glslc` (not currently installed here).

---

## 5. Known bugs and limitations

| Status | Issue |
|---|---|
| Design constraint | Only one real `NetworkSession` can exist per OS process (`activeSession_` static gate). |
| Confirmed bug (upstream FNA, preserved) | `NetworkSession::EndCreate`/`EndJoin`/`EndJoinInvited` null `activeAction` *after* constructing the session — a constructor throw strands it non-null. |
| Confirmed bug (upstream FNA, preserved) | `NetworkSession::EndJoin`/`EndJoinInvited` hardcode session type to `PlayerMatch`. |
| Confirmed bug (upstream FNA, preserved) | `NetworkGamer::getIsHostProperty()` always returns `true`. |
| Deviation (documented) | `PacketWriter::Write(Color)` writes 4 bytes; `PacketReader::ReadColor()` reads 4 floats — asymmetric upstream, preserved. |
| Deviation (documented) | `PacketReader`/`PacketWriter`'s `capacity` constructor argument is discarded. |
| Incomplete | Some event-args tests use `nullptr` stand-ins for `NetworkGamer*` instead of real instances. |
| Confirmed bug (graphics) | `SpriteBatch` multiple `Begin()`/`End()` per frame on Vulkan: only the last batch renders. |
| Suspected bug (graphics, unaudited) | `DrawUserIndexedPrimitives` typed overloads likely share the silent-return-on-missing-effect bug. |
| Platform limitation | FFmpeg video decoding unavailable on Windows/Emscripten/Android cross-builds. |
| Permanent platform limitation (Web) | No LAN broadcast discovery; a browser tab can never host a `NetworkSession`; ephemeral port readback is broken (fixed port used instead). |
| Confirmed bug (Android, out of scope) | `TitleContainer::OpenStream`'s `SDL_LoadFile` fallback segfaults without a real JNI/Activity context. |
| Intentional stub (not a bug) | Every faithful `Avatar*` no-op/inert behavior (`Draw()` no-op, `State` always `Unavailable`, `CreateRandom()` not randomizing) — matches the real reference assembly exactly. |
| Intentional gap (user decision) | Phase 10's real-rendering extension has no real body/animation content wired in — see section 2/4. |

---

## 6. Architecture notes

### Module map

| Layer | Location | Notes |
|---|---|---|
| XNA public API (graphics) | `include/Microsoft/Xna/Framework/...` | Must match XNA 4.0/FNA exactly |
| XNA public API (GamerServices incl. Avatar) | `include/Microsoft/Xna/Framework/GamerServices/` | Complete, including Phase 8 Avatar and Phase 10 EXT additions |
| XNA public API (Net) | `include/Microsoft/Xna/Framework/Net/` | Complete API surface; public shapes are a fixed point |
| ENet backend | `include/CNA/Internal/Net/`, `src/CNA/Internal/Net/` | `ENetHostHandle`, `ENetBackend`, `ENetDiscoveryService` (disabled on Emscripten only) |
| Avatar real-rendering extension (Phase 10, NOXNA/EXT) | `Graphics::{VertexPositionNormalTextureSkinned,SkinnedModelEXT}`, `GamerServices::{AvatarAppearanceEXT,AvatarAnimationPresetNamesEXT}`, `AvatarRenderer`/`AvatarAnimation` EXT members, `tools/avatar_asset_pipeline/` | Opt-in, additive; see `docs/avatar-real-rendering-ext.md` |
| sharp-runtime | `../sharp-runtime/` (sibling repo) | `System.*` types; only add new files; ask before every commit |

### Avatar — two layers, don't confuse them

1. **Faithful XNA port** (`AvatarRenderer`, `AvatarAnimation`, `AvatarDescription`): byte-exact
   match of the real Microsoft reference assembly (decompiled via `monodis` from
   `/rv/data/library/github.com/borgesdan/xn65/references/Windows/Microsoft.Xna.Framework.Avatar.dll`
   — FNA has no Avatar implementation at all). Its no-op/inert behavior is correct and must never
   be "fixed."
2. **Real-rendering extension** (`*EXT` members, `SkinnedModelEXT`): a separate, additive,
   opt-in path with its own independent bone hierarchy — never conflate with layer 1's real
   71-bone Xbox arrays. Currently has no real content behind it (see section 2/4).

### Key invariants

- `NOXNA` macro tags every non-XNA extension in public headers; `*EXT` suffixes non-XNA methods
  on otherwise-faithful classes. This is unrelated to the separate `CNA_NOXNA` CMake option in
  `NOXNA.md`.
- C# properties → `getXProperty()`/`setXProperty()`. `System::Exception` is the base for all
  GamerServices/Net exceptions, never `std::runtime_error`.
- ENet code lives entirely under `CNA::Internal::Net`; `enet/enet.h` must never leak into a
  public XNA-facing header. `ENetBuffer` fields must be assigned by name (platform-dependent
  member order), never positional-initialized.
- Doxygen `/** @brief ... */` required on every public member in every `.hpp`.
- sharp-runtime: only add new files; never modify existing ones or commit/push without asking
  first, for every commit.

---

## 7. Useful commands

```bash
cd /rv/data/development/github.com/openeggbert/cna_net

# Native Linux build + full test suite
cmake --build cmake-build-debug --target CnaTests -j"$(nproc)"
cmake-build-debug/CnaTests

# Just Net/GamerServices/Avatar tests
cmake-build-debug/CnaTests --gtest_filter="*Network*:*Gamer*:*ENet*:*Packet*:*Avatar*"

# Avatar real-rendering EasyGL integration test
cmake --build cmake-build-debug --target cna_test_avatar_real_render -j"$(nproc)"
SDL_VIDEODRIVER=x11 DISPLAY=:0 cmake-build-debug/cna_test_avatar_real_render

# Windows cross-build
cmake -B cmake-build-windows -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/mingw-w64.cmake \
      -DCNA_GRAPHICS_BACKEND=SDL_RENDERER -DCNA_ENABLE_NET=ON
cmake --build cmake-build-windows --target CnaTests -j"$(nproc)"
wine cmake-build-windows/CnaTests.exe

# Web/Emscripten cross-build
source /home/robertvokac/Downloads/emsdk/emsdk_env.sh
cmake --build cmake-build-web --target CnaTests -j"$(nproc)"
node cmake-build-web/CnaTests.js --gtest_filter="*Network*:*Gamer*:*ENet*:*Packet*"

# Android NDK cross-build (on-device run needs /dev/kvm, currently unavailable)
cmake --build cmake-build-android --target CnaTests -j"$(nproc)"

# sharp-runtime — check status before touching it
cd ../sharp-runtime && git status
```

Builds can time out on this shared machine if another session is compiling concurrently — retry
with a reduced `-j` and a longer timeout rather than assuming a real compile error.

---

## 8. Next smallest tasks

1. **If/when the user provides real avatar content** (body mesh + skeleton + animation clips, any
   source), wire it through `ContentManager::Load<shared_ptr<SkinnedModelEXT>>` and
   `AvatarRenderer::DrawRealEXT` in a real windowed demo.
   Files: `tools/avatar_asset_pipeline/convert_avatar.py`, `docs/avatar-real-rendering-ext.md`.
   Verify: a visible, animated GPU-skinned mesh in a real (non-headless) demo window.

2. **Re-run the Android on-device test suite once `/dev/kvm` is available.**
   Files: none (build already verified clean).
   Verify: `adb shell` run of `CnaTests --gtest_filter='*Network*:*Gamer*:*ENet*:*Packet*'` on the
   `Medium_Phone` x86_64 emulator, per section 7.

3. **Optional: Vulkan/Bgfx smoke test of the Avatar real-rendering extension.**
   Files: none yet; needs a fresh `cmake-build-vulkan` configure and `glslc` installed.
   Verify: the equivalent of `EasyGL_AvatarRenderer_RealRender` passing on that backend.

4. **Ask the user what's next** — no further work is queued or assumed beyond the above.

---

## 9. Do not do yet

- No changes to `Microsoft::Xna::Framework::Net` public class shapes.
- No "fixing" any documented FNA-preserved bug or Avatar quirk (section 5) — all are verified-real
  behavior, not defects.
- No modifications to existing `sharp-runtime` files, and no commit/push there without asking the
  user first, for every commit.
- No attempting to automate MakeHuman, CharMorph/Blender, or any other third-party tool download
  without fresh, specific user sign-off for that exact action — general approval does not carry
  over between attempts (see section 3).
- No opening or executing anything in `/rv/tmp/charmorph_work/` (already-downloaded but
  never-opened `.blend` files) without the user explicitly deciding to resume that work.
- No reverse-engineering the real, undocumented `AvatarDescription` byte format.
- No unilaterally resuming Phase 10b (real avatar content) — the user paused it deliberately;
  wait to be asked.
- No broad refactors, unrelated cleanup, or API changes without checking compatibility.

---

## 10. Resume prompt

```
Read NEXT.md first, in full, before doing anything else. Inspect only the files needed for
the first task you pick from section 8. plan_net.md (Phases 1-9) and Phase 10's engine/pipeline
work are complete and merged to feature/net. Avatar is intentionally a non-rendering stub right
now (real content acquisition was paused by explicit user decision) - do not resume that work
unless the user asks. Do not refactor unrelated code. Make one small, verified improvement at a
time, run the relevant build/test command, and update NEXT.md after finishing.

Build: cmake --build cmake-build-debug --target CnaTests
Test:  cmake-build-debug/CnaTests
```
