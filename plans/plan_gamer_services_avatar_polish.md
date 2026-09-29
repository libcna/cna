# GamerServices / Avatar fidelity and completion pass (living plan)

Task ids `GSP-*`. Continues `plan_gamer_services_server.md` (GS-001..GS-011, closed) and its
register `gamer_services_server_final_register.md`. This file is updated as work lands; the
historical plan is not.

## Verified starting point (2026-09-29, from the repositories, not the prompt)

| Repository | Branch / worktree | Commit | Note |
|---|---|---|---|
| CNA | `feature/gamer-services-server`, worktree `cnawork/cna-gamer-services` | e8ecc7197 | The prompt's f5df1c824 plus df4c7a25d (next merged in), 88718a815 (queue-saturation test fix) and e8ecc7197, which **was merged into `next` and pushed on 2026-09-29** (owner request recorded in GS memory). Branch == `next` == `origin/next`. |
| cna-gamer-services-server | `feature/gamer-services-server` | ce8ff32 | pushed, not merged |
| cna-samples | `feature/gamer-services-samples`, worktree `cna-samples-gamer-services` | 378483b | pushed, not merged |
| sharp-runtime | `feature/gamer-services-collections` | 007280bd | |

`cnawork` itself is another worktree (branch `gamerservicese`, b2fd47a45) that nests this one; it is
not touched. This pass commits on `feature/gamer-services-server` again and does not merge or push.

Baseline rerun today, `cmake-build-debug` (HEADLESS), every gtest case in its own process
(`cmake-build-debug/avatar-polish/baseline/`):

- CnaGamerServicesTests 499: 498 pass + `GuideTest.IsScreenSaverEnabledGetSet` skip. Five
  persistence cases (`LeaderboardWriterTest.SettingRating…`, `…ColumnsPersistAlongsideRating`,
  `LeaderboardReaderTest.ReadCentersOnPivotGamer`, `…ReadRestrictsToGivenGamersList`,
  `SignedInGamerTest.AnOfflineCatalog…`) fail only when run concurrently with each other (one
  shared local store file); serially they pass. GS cases are run serially from here on.
- CnaRuntimeTests 194: 192 pass + 2 platform skips (two tests arrived with `next`).
- CnaNetTests 495/495 (serial; discovery uses UDP 61190).

## Audit of the starting inventory

Categories: **X** confirmed XNA behaviour · **C** confirmed CNA behaviour · **G** implementation
gap · **V** visual/art-quality gap · **D** stale documentation only · **N** deliberate non-goal ·
**U** uncertain, needs evidence.

### Avatars

| Finding | Cat | Evidence |
|---|---|---|
| `AvatarDescription` 1021 bytes, `IsValid` = length and byte 0 != 0; Height/BodyType decode | X | Windows IL `AvatarDescription.cs`; `AvatarDescriptionCodec.cpp` |
| CNA v1 encoding (version 1, "CNA", body, height, build, catalog version, 7 colors, 6 item ids, zero reserved, CRC) | C | `docs/avatars.md`, codec |
| 71-slot skeleton, parent table, local bind/bone transforms, decomposable-bone rule | X/C | IL + GS-009 |
| 31 presets as original CNA clips, Update wrap/clamp rules | C | `AvatarAnimationTests` |
| `catalogManifest(version)` returns the embedded manifest for every `version <= embedded` and looks embedded assets up by file name | G | `AvatarModel.cpp::catalogManifest`, `AvatarCatalog.cpp::embedded` — safe only while one catalog is embedded |
| Unknown (newer) catalog offline falls back to the embedded manifest and looks the *unknown catalog's* item ids up in it | G | same; an id reused with another meaning would render the wrong item |
| Server catalog import: manifest/hash/size checks, but only GLB/PNG framing | G | `cna-gamer-services-server/src/Avatars.cpp::checkFile` |
| Server: an imported version is immutable (identical re-import is a no-op); later versions must keep every earlier item id and slot | C | `importAvatarCatalog` |
| `preview_avatar.py` ignores textures/UVs and the atlas: decals show as opaque rectangles, head texture absent | G | the tool; screenshot in evidence/before |
| Head is an ellipsoid with ellipsoid ears/nose; face features are flat atlas decals; limbs are capsules; hands are ellipsoid palms + tubes; clothing is inflated body primitives; hair is a head shell | V | `body.py`, `wardrobe.py`, `face.py` |
| Materials: one tint per primitive, one untextured material per item except the head's blush texture | V | `catalog.py`, `glb.py` |
| Wardrobe 5/4/3/2/2/2 | V | catalog v1 |
| `AvatarDescription.Changed` never raised | G | XNA: dispatcher reads an `AvatarChanged` bitmask per player, invalidates the per-player cached description returned by `EndGetFromGamer` and raises its `Changed` with the `SignedInGamer` as sender (Windows IL `GamerServicesDispatcher.ReadAvatarChanged`, `AvatarDescription.OnAvatarChanged`); the server already keeps an avatar revision |
| Avatar download cache: content-addressed, verified on read; no bound, no cleanup | U | `GamerServicesBackend.cpp` cache dir; audit in GSP-H |
| No way for a person to choose/edit their avatar (admin CLI only) | G | server `avatar <user> random|set|clear` |

### GamerServices / Net register rows (`gamer_services_server_final_register.md`, "CNA limits")

| Row | Cat | Plan |
|---|---|---|
| Partner tokens | N | Xbox LIVE partner web services |
| `GetFromGamertag` offline | C | offline has no directory |
| Online `AddLocalGamer` | G | GSP-K2 |
| Online host migration | G | GSP-K1 |
| Voice (`EnableSendVoice`, `HasVoice`) | N | voice codec/network stack is not polish |
| Party (`SendPartyInvites`, `ShowParty*`) | N | full party ecosystem |
| Marketplace, `IsTrialMode` | N | no store/licensing |
| `FriendGamer.IsAway/IsBusy` false | G/U | GSP-L2: explicit presence state |
| `GamerZone`/`Reputation` | U | GSP-M: GamerZone is a documented profile preference (Family/Pro/Recreation/Underground/Unknown); Reputation is "stars 0 to 5" |
| QoS bandwidth 0 | U | GSP-L6: XML: "estimate of the available bandwidth ... bytes per second" |
| Account `GameDefaults` unset | G | GSP-L1 |
| `InstallingTitleUpdate` never raised | N | title-update distribution |
| `AvatarDescription.Changed` | G | GSP-J |
| `NotificationPosition` stored, no toasts | G | GSP-L3 |
| `WriteTrueSkill` no skill computed | N | TrueSkill service/algorithm |
| Leaderboard `Stream` columns | U | GSP-L4 |
| Guest sign-in | U | GSP-L5 |
| Browser multiplayer | N | owner scope |

### Documentation (GSP-P)

| Statement | Cat |
|---|---|
| Server README / `protocol/v1.md` / `relay-v1.md`: "Partial implementation", avatar distribution, WSS/ENet bridge, public NetworkSession/XNA adapter, relay data transport "unfinished/pending" | D |
| Demo comments: "four Stub Gamers", "GetFriends always empty", "GetAchievements always empty" | D |

## Decisions

(Recorded as they are made, with the evidence that settled them.)

- **Review pipeline (GSP-A).** One job list (`avatar_review.py jobs`: 14 views, 24 seeded avatars,
  124 preset key frames, 45 expression close-ups) is rendered by the CPU preview and by
  `cna_avatar_review`, which uses only the standard XNA avatar API; sheets compose either. The old
  tint-only `preview_avatar.py` is removed. The preview matches the OPENGL33 captures cell for cell
  (same framing, textures, atlas, lighting), so art iteration can happen on the CPU and be
  confirmed on the renderer.
- **Exact catalogs (GSP-B1).** Every `assets/avatars/v<N>/` directory is compiled in as
  `v<N>/<file>`; `embeddedCatalogs()` checks each manifest describes its own directory's version.
  `catalogManifest(v)` returns exactly version v (compiled in, else the service's), or null — never
  another version. Assets resolve by size + SHA-256 across every compiled-in file, so two versions
  may reuse a file name with different contents, and identical contents are shared. A description
  whose catalog is unavailable draws the newest compiled-in body with every item replaced by its
  slot default (`AvatarModel::catalogUnavailable`, all ids in `substitutedItems`); its ids are never
  read against another version (previously they were read against v1). The codec's hard-coded v1
  item table is gone: `isEncodable` checks items against the compiled-in manifest of the named
  version; `randomDescriptor` draws from the newest one, with optional per-item `random` weights
  per body type in the manifest (absent = 1, so v1 is unchanged). Preset animations come from the
  newest compiled-in catalog: they are system data, not part of what a description names.
  Pinned: `v1/catalog.json` SHA-256 and the digests of three assembled v1 models
  (`AvatarCatalogTest.CatalogV1IsFrozen`, `V1DescriptionsAssembleExactlyAsReleased`), taken while v1
  was the only catalog. The server already kept versions immutable and items growing (GSP-B2 adds
  per-version item checks and exact v1 service after v2).
- **Catalog v2 art (GSP-C/D/E).** The generator (`tools/avatar_builder`) now writes catalog v2; v1 is
  frozen data (its generator is in Git history, 6ca06d069). Pure Python, no numpy, byte-reproducible.
  Head: one parametric surface (an ellipsoid with a jaw taper, flattened face plane, cheek/brow/chin
  bumps and soft sockets), separate button nose and rimmed ears; an ellipsoid-union head was tried
  first and dropped (creases, gaunt lower face). Face atlas: 128 px tiles drawn as anti-aliased
  signed-distance shapes (shaded whites, gradient irises with highlight holes, lids, lashes, smile
  squint; lips translucent so any skin tone shows through). Body: arms and legs are single lofts
  through the elbow/knee (no capsule seams), superellipse torso, sculpted palm/knuckles/fingers.
  Hair: a scalp cap inside a hairline plus flattened tapering locks that follow the head or hang
  (10 styles). Clothing: offset lofts over the body's sections and weights with rolled hems, necklines,
  collars, a hood, pockets, cuffs, waistbands; shoes are a sole plus a dome upper, with shafts
  (boots over trousers, high-tops under them) and socks. Textures are small procedural multipliers
  (skin warmth, strands, knit/rib/cable/fleece/denim/canvas/leather with side seams).
  Wire format stays inside the v1 reader contract (same attributes, embedded PNGs), except that
  normals and UVs are normalized 16-bit (cgltf unpacks them for every reader), so existing CNA builds
  can render v2 items fetched from a service.
- **Hats over hair.** Each hair item has `hatAssets`: the style with everything a covering hat
  (`coversHair`) hides dropped and the rest flattened under one shared hat envelope
  (head + 19 mm, down to a common line); every covering hat is built outside that envelope.
  Readers that ignore these keys draw the full hair under the hat (a documented degradation).
- **Format 2 descriptions (decided).** v1 descriptions can vary only body, height, build, colours and
  six item slots; every face is identical, which is far from the individuality of the era's avatars.
  Format 2 (byte 0 = 2) keeps v1's bytes 1-42 exactly, adds a facial-hair item id (43-44, from the
  catalog's `featureItems`) and sixteen face-shape bytes (45-60, 128 = neutral: head width/height,
  jaw width, chin, cheeks, eye size/spacing/height/tilt, brow height, nose size/width/height, mouth
  width/height, ear size), reserved zero to 1016, CRC-32 at 1017. The parameters are opaque data: the
  catalog's `faceControls` turn them into smooth local deformers (scale/move/rotate with an
  ellipsoidal falloff) on Head-weighted vertices, scoped to the face (body head, decals, facial hair)
  or the whole head (also hair, hats, glasses). A description that uses none of them is still
  encoded as format 1. Public XNA API unchanged (`DescriptionSize` 1021, byte 0 non-zero).

## Tasks

| Id | Task | Status |
|---|---|---|
| GSP-000 | Verify state, baseline, audit (this file) | done |
| GSP-A1 | Texture-aware development preview (UV/texture/atlas/alpha, z-buffer) | done |
| GSP-A2 | Deterministic captures from the real `AvatarRenderer` (views, contact sheets, animation keyframes) | done |
| GSP-A3 | BEFORE evidence of catalog v1 | done |
| GSP-B1 | Exact embedded catalog registry (version -> manifest -> content hash); v1 frozen | done |
| GSP-B2 | Server catalog-version semantics verified and tested | done |
| GSP-C* | Catalog v2: body, head/face, expression atlas, facial individuality decision | first pass done (C1); art iteration continues |
| GSP-D* | Catalog v2 wardrobe and hair | first pass done (C1) |
| GSP-E* | Materials and renderer polish, per-renderer check | todo |
| GSP-F* | Animation polish and key-pose/loop tests | todo |
| GSP-G1 | Server avatar GLB/manifest validation, malformed fixtures | todo |
| GSP-H1 | Avatar cache audit, bounded cleanup, first/warm load measurement | todo |
| GSP-I1 | CNA-owned avatar customization (system level) | todo |
| GSP-J1 | `AvatarDescription.Changed` | todo |
| GSP-K1 | Online host migration | todo |
| GSP-K2 | Online `AddLocalGamer` | todo |
| GSP-L1..L6 | Account GameDefaults, away/busy, toasts, Stream columns, guests, QoS bandwidth | todo |
| GSP-M1 | GamerZone / Reputation | todo |
| GSP-O1 | Server concurrency benchmark and production audit | todo |
| GSP-P1 | Documentation truth pass | todo |
| GSP-Q1 | Final acceptance and register | todo |

## Evidence log

- GSP-C1 (catalog v2 + format 2): 111 files, 12.46 MB (v1: 41 files, 2.68 MB); embedding v1+v2 is a
  75 MB generated source that compiles in 9.7 s at 1.44 GB peak. Generator 26 s, `--check` clean.
  GS 508 + 1 skip per process (118 avatar tests, 6 new); the three v1 model digests pinned before
  v2 existed still match with v2 embedded. OPENGL33 `cna_avatar_review`: 255 jobs, 0 failures
  (`/rv/tmp/avatar-polish/review-v2c/`). v2 may still change until this pass ends; it is frozen from
  then on.
- Visual evidence lives outside the repositories, as the sample evidence does:
  `/rv/tmp/avatar-polish/evidence/{before,after}/`.
- BEFORE (catalog v1, 2026-09-29): `evidence/before/{jobs.json, preview/, opengl33/,
  preview-sheets/, opengl33-sheets/}` (207 jobs, OPENGL33 on the private runner, 0 failures) and
  `old-preview-tint-only.png` (what the removed preview showed). Observations: slim mannequin
  proportions with a small ellipsoid head; capsule limbs with visible joins; ellipsoid palms and
  tube fingers; clothing as inflated body shells without hems, cuffs or collars; hair as a smooth
  head cap; flat single-tint materials; decal eyes that read clearly but sit on a featureless
  sphere; animations readable, FemaleIdleFixShoe lifts the whole body off the ground.
