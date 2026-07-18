# AUDIT_SCOPE.md — Inventory and Classification Rules

## Summary

- **Total tracked files (`git ls-files`, branch `feature/audit`, computed 2026-07-18): 2634**
- **AUDIT-eligible: 2297** (across 105 manifest shards, see `AUDIT_MANIFEST.md`)
- **EXEMPT: 337** (across 8 reason categories, see `exempt/*.md`)
- Invariant verified: `2297 + 337 = 2634`. No file is unclassified. No file appears in more than one shard/exemption bucket (each file classified by a single ordered rule match — see the classifier logic reproduced below).

This inventory was built from `git -c core.quotePath=false ls-files -z` (NUL-delimited, so the one filename
containing a non-ASCII character — `tests/assets/media/music/Artist Two/Album Delta/03 - Étoile.ogg` — decodes
correctly instead of appearing git's octal-quoted form). `git ls-files` was used rather than a filesystem walk so
that build directories, IDE metadata (`.idea/`, `.junie/`), and any local untracked scratch files are excluded from
scope automatically.

## Counts by extension (top 20)

cpp 1341 · hpp 607 · md 126 · png 90 · bin 72 · glsl 58 · scene 39 · hlsl 38 · py 29 · sc 28 · cmake 27 · json 21 ·
xnb 13 · java 12 · h 12 · c 9 · sh 8 · mkv 8 · cs 7 · fx 6 · dox 6 · xml 5 · txt 5 · wav 5 · mp3 4 · fxh 4 · yml 3 ·
mk 3 · gradle 3 · gitkeep 3 · properties 2 · ogg 2 · jpg 2 · gitignore 2 · flac 2 · (1 each) pro, opus, mp4, m3u8,
m3u, lua, keep, jsonl, jar, in, gitmodules, dsp, csproj, cbp, bitbackupignore, bat, am, ac.

## Counts by top-level directory

examples 797 · include 552 · src 531 · tests 400 · tools 124 · docs 84 · third_party 44 · cmake 27 · scripts 12 ·
tasks 5 · .github 3 · plus ~40 root-level files (plan_*.md, NEXT*.md, CMakeLists.txt, CLAUDE.md, etc.), `vendor/`
(1 submodule pointer), and `dx9-spike/` (1 file).

## Classification rule order (first match wins)

1. VCS metadata basenames (`.gitignore`, `.gitmodules`, `.bitbackupignore`, `.gitkeep`, `.keep`) → **EXEMPT** `vcs-meta`.
2. `third_party/**`, `vendor/**` → **EXEMPT** `third-party-vendored` (SDL/SDL_image/SDL_mixer/googletest are git
   submodules; cgltf/enet/stb are vendored copied sources — none is CNA-authored).
3. `src/CNA/Internal/Backends/D3D9/shaders/xna/**` → **EXEMPT** `vendored-verbatim-stock-effect` (FNA/Microsoft XNA
   4.0 Stock Effects HLSL, copied byte-for-byte from FNA per `THIRD_PARTY_NOTICES.md`, integrity enforced by
   `scripts/verify-d3d9-stock-effects-vendored.sh`). The CNA-side `.cpp`/`.hpp` code in the D3D9 backend that
   *compiles and binds* these shaders remains in scope (`backend-d3d9` shard) and is audited normally.
4. `dx9-spike/**`, `tasks/backlog/**` → **EXEMPT** `planning-tracking-doc`.
5. `LICENSE`, `NOTICE.md`, `THIRD_PARTY_NOTICES.md` → **EXEMPT** `legal-text`.
6. Any root-level (`depth==0`) `*.md` file → **EXEMPT** `planning-tracking-doc` (see decision D-3 below —
   `README.md`, `AUDIT.md`, `CHECKLIST.md`, `CLAUDE.md`, `TODO.md`, `RAM.md`, `programs.md`, `known_bugs.md`,
   `NOXNA.md`, `noxna_devices.md`, `input_noxna.md`, `input_noxna_progress.md`, `cnj.md`, `xnb.md`, every
   `NEXT*.md`/`plan_*.md`).
7. `tests/assets/xnb/monogame/**` → **EXEMPT** `vendored-test-fixture` (third-party MonoGame-generated reference
   XNB corpus + its manifests/README — not first-party source).
8. `examples/demo_avatar/Content/**` with extension `bin`/`json` → **EXEMPT** `generated-content-asset` (baked
   avatar content produced by `tools/avatar_asset_pipeline`, not hand-authored).
9. Binary/data extensions (`png,bin,xnb,mkv,wav,mp3,ogg,jpg,jpeg,flac,opus,mp4,m3u8,m3u,jar,jsonl`) → **EXEMPT**
   `binary-or-data-asset`.
10. `docs/**/*.md` → **AUDIT** `documentation` (per decision D-3).
11. `*.json` not `CMakePresets.json` → **EXEMPT** `generated-content-asset` (all remaining JSON in the tree is a
    manifest/content-descriptor data file, never hand-authored build/source config).
12. Recognized source/build/script/shader/config extensions (`cpp,hpp,h,c,glsl,hlsl,sc,fx,fxh,py,sh,lua,java,cs,
    cmake,yml,yaml,xml,properties,gradle,scene,csproj,txt,mk,pro,bat`), `gradlew`, `CMakeLists.txt`, `Doxyfile`,
    `header.txt` → **AUDIT** `source-or-build`.
13. Any remaining `.md` (first-party README/test-doc sitting next to in-scope source, e.g.
    `tests/PackedVectorGolden.md`, `tools/*/README.md`) → **AUDIT** `documentation`.
14. Anything left over → `NEEDS_REVIEW` (verified empty after two classifier passes — see decision log D-1/D-2 for
    the two rounds of fixes: `.mk`/`.pro`/`.bat`/`gradlew` extension gaps, and the git quoted-path Unicode bug).

## Third-party / vendor treatment

`third_party/` (SDL, SDL_image, SDL_mixer as git submodules; cgltf, enet, stb as vendored copied sources) and
`vendor/googletest` (submodule) are entirely **EXEMPT**. `src/CNA/Internal/Backends/D3D9/shaders/xna/` (10 `.fx`/
`.fxh` files + its own `LICENSE` + `README.md`) is vendored verbatim from FNA and is **EXEMPT** on the same basis,
per `THIRD_PARTY_NOTICES.md`'s own statement that "not one line has been edited."

## Generated-file treatment

Two files contain a generated-code marker comment (`include/CNA/Internal/Backends/Common/IGraphicsBackend.hpp`,
`src/CNA/Internal/Backends/D3D9/shaders/D3D9CnaShaderRegisters.hpp`) but are checked in as ordinary first-party
source under version control (not regenerated at build time from a separate IDL/schema found in this repo) — both
remain **AUDIT**-eligible; the audit report for each should note the generation provenance as context rather than
treat it as hand-written logic to second-guess line-by-line.

## Asset / binary treatment

All binary/media test and example assets (images, audio, video, compiled `.xnb`, `.jar`) are **EXEMPT** as
`binary-or-data-asset`, matching the prompt's own exemption guidance. They remain fully accounted for in the
`exempt/binary-or-data-asset.md` listing so nothing silently disappears from the inventory.

## Documentation treatment (per user decision D-3)

- `docs/**/*.md` (78 files: technical/behavioral write-ups like `graphics-compatibility-report.md`,
  `xna-4-api-coverage.md`, per-effect `*-support.md` docs, backend docs) → **AUDIT**, checked for accuracy against
  current code behavior, same as any other in-scope file.
- Root-level planning/status/tracking documents (`plan_*.md`, `NEXT*.md`, `AUDIT.md`, `CHECKLIST.md`, `TODO.md`,
  `RAM.md`, `programs.md`, `known_bugs.md`, `NOXNA.md`, `noxna_devices.md`, `input_noxna*.md`, `cnj.md`, `xnb.md`,
  root `README.md`) and `tasks/backlog/*.md` → **EXEMPT** `planning-tracking-doc`, not given per-file audit
  reports. They are still read and cross-checked as secondary context during subsystem audits — in particular
  `known_bugs.md` documents specific live defects (e.g. the SpriteBatch Begin/End bug) that should be corroborated
  or refuted by the corresponding subsystem's audit rather than ignored.
- `dx9-spike/README.md` folds into the same planning-doc exemption (experimental spike notes).

## Test treatment

`tests/**` source files (`.cpp`/`.hpp` under `tests/Microsoft`, `tests/CNA`) are fully **AUDIT**-eligible and
sharded in parallel with their corresponding production-code area (e.g. `tests-xna-graphics` alongside
`xna-graphics`) so parity between implementation and test coverage can be judged together. `tests/assets/**`
binary/data fixtures are **EXEMPT**; `tests/assets/xnb/monogame/**` is additionally called out as vendored
third-party fixture data. `tests/PackedVectorGolden.md` is first-party test documentation and is **AUDIT**-eligible.

## Example treatment

`examples/**` is fully **AUDIT**-eligible. Two structurally different populations were found and sharded
accordingly:

- ~570 backend-named integration-test executables sitting directly under `examples/` (e.g.
  `easygl_basiceffect_fog_test.cpp`, `vulkan_scissor_test.cpp`, `sdlrenderer_spritebatch_rotation_test.cpp`) —
  sharded per backend (`examples-tests-<backend>`) so they can be audited alongside that backend's own source.
- ~30 `examples/demo_*` directories (full XNA-style sample games/demos, e.g. `demo_avatar`, `demo_net_client_server_arena`)
  — sharded per demo (`examples-<demo_name>`).

## CI/build treatment

Root `CMakeLists.txt`, `cmake/*.cmake` (27 files, includes `cmake/Tests/*.cmake`), `CMakePresets.json`, `Doxyfile`,
`header.txt` (the SPDX header template referenced by `CHECKLIST.md`), and the three `.github/workflows/*.yml` files
are all **AUDIT**-eligible.

## Unknown extension treatment

Every extension encountered was individually inspected before classification (see decision log). None were
silently defaulted; the two rounds of classifier fixes (`.mk`/`.pro`/`.bat`/`gradlew`, and the Unicode git-quoting
bug) are recorded in `AUDIT_DECISIONS.md` as D-1/D-2.

## Sibling first-party repositories (out of primary scope)

Several graphics backends and one namespace depend on code that lives in **separate git repositories**, not inside
this `cnaaudit` checkout:

- `sharp-runtime` (`System.*` reimplementation) — confirmed present as an additional working directory, branch
  `feature/xnb-charreader`, 1602 tracked files. Per explicit user decision (D-4): **reference only**, not audited,
  never committed to.
- `easy-gl` (external library; `CNA_GRAPHICS_BACKEND=EASYGL` links `target_link_libraries(... PRIVATE easy-gl
  SDL3::SDL3)` per `cmake/BackendLibraries.cmake`) — backs the `EasyGL` backend's single-file CNA adapter
  (`EasyGLGraphicsBackend.cpp`/`.hpp`, which ARE in scope).
- `free-direct` (external library backing the `Dx3` backend's single-file CNA adapter
  `Dx3GraphicsBackend.cpp`/`.hpp`, which ARE in scope) — confirmed via a `cmake/BackendLibraries.cmake` comment
  referencing "free-direct's own public target."
- `bgfx`/`bx` and Vulkan SDK and `wgpu-native` are genuine external/upstream libraries (not openeggbert siblings);
  `wgpu-native` in particular is explicitly documented in `THIRD_PARTY_NOTICES.md` as a downloaded binary release,
  never copied into the tree.

Per decision D-6 (extending D-4's approved policy to these newly-discovered cases): all of the above are treated
as **reference-only, opaque external dependencies** — consulted for API/behavior context when auditing the CNA-side
adapter code that consumes them, never given their own per-file audit reports, never modified. This keeps the
"first-party CNA source tree" boundary consistent and matches the explicitly-approved sharp-runtime policy.
