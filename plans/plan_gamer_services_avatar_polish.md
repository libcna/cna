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
- **Format negotiation (GSP-G1).** `avatars.get` takes `formats`; a caller that does not list 2
  (every build before this pass) gets the format 1 copy of a format 2 avatar — same body, colours
  and items, no facial hair or face shape — which it can render, fetching v2+ items by hash. This
  build sends `formats: [1, 2]`. Descriptions exchanged directly between games (bytes over a
  NetworkSession) cannot be negotiated: an older build reads a format 2 description as XNA's
  "results are undefined" case (IsValid by byte 0, renderer Unavailable).
- **Animation pass (GSP-F1).** Measured every preset on both bodies (dev script sampling the
  runtime's own curves at 30 Hz): Celebrate sank both feet 2.3 cm between keys, FemaleIdleFixShoe
  floated on one leg and slid the planted foot, FemaleAngry's stomp and Stand5's tap slid the foot,
  and four face-touch presets put hands inside the larger v2 head. Fixes at the source: poses keep
  their foot targets and the writer re-solves the legs every 1/12 s wherever interpolation would
  move a planted foot more than 4 mm, replacing only those clips' leg channels with denser keys
  (glTF samplers may have their own key times; the reader already allowed it); face touches aim at
  the v2 face surface (`Pose.face`, `beside_head`); FixShoe is a grounded crouch to a shoe a step
  forward; the idle arms are no longer mirror images; Celebrate's arms sweep up through the front.
  Tests over the shipped presets through the public API: looping Stand presets join with no jump,
  feet never sink more than 12 mm, no hand passes within 5 cm of the spine, no joint moves faster
  than 12 cm per 1/60 s. Residual (dev metric): a planted foot drifts at most 1-3 cm over a whole
  clip when a preset authored for one body plays on the other.
- **Materials (GSP-E1).** Audit: the reader consumes position, normal, texcoord 0, joints, weights,
  a base-colour factor, an embedded PNG and CNA extras; the renderer is SkinnedEffect with exactly
  LightDirection/LightColor/AmbientLightColor and a faint specular. Everything v2 needs for
  material feel is cheap and inside that: texture multipliers (skin warmth, strands, fabrics with
  seams) plus one new optional extra, `cnaSpecular`, scaling the one light's highlight per material
  (eyes 2.5-3, lips 1.6, hair 2.2, leather 2, glasses frames 2.5, cloth 0.25-0.5). Older readers
  ignore it; v1 has none (scale 1, unchanged). No shader, lighting-API or state change; the
  renderer still restores blend/depth/rasterizer/sampler state after Draw.
- **AvatarDescription.Changed (GSP-J1).** Evidence (Windows IL, the only IL available; the Xbox
  reference assemblies are metadata-only): `AvatarDescription` keeps `signedInGamerDescriptions[4]`;
  `EndGetFromGamer` for a `SignedInGamer` returns that slot's instance (created once);
  `GamerServicesDispatcher.Update` reads an `AvatarChanged` bitmask per player and
  `OnAvatarChanged` takes the slot's `Changed`, empties the slot, then invokes it with the
  `SignedInGamer` as sender. CNA does the same: the dispatcher re-reads a kept slot's avatar every
  10 s (service `avatars.get`, or the local profile store) and raises when the bytes differ. The C++
  description is a value, so sharp-runtime gained `EventHandler::Share()` (88f6b11f): copies of a
  shared event have one subscriber list, add and remove through any copy. `Gamer` now derives from
  `System::Object`, as in C#, so the sender can be the gamer. Same bytes, a catalog-only change or an
  unreachable service raise nothing; sign-out, another gamer in the slot or a replaced backend drop
  the slot silently. `setLocalProfileAvatar` (store write) and `localProfileAvatar` reading the store
  first let another process's save be seen.
- **Renderer coverage (GSP-E).** OPENGL33 and VULKAN draw the 255-job v2 review identically;
  `EasyGL_AvatarRenderer_Standard` and `Vulkan_AvatarRenderer_Standard` pass. Other renderers were
  not built in this worktree (they share SkinnedEffect; not measured here).
- **Server import validation (GSP-G1).** An independent MIT validator (server `AvatarAssets.cpp`,
  on nlohmann/json, no code from CNA) enforces the same contract as the CNA reader, plus
  catalog-level rules a client would otherwise hit at render time (items fitted to their body's
  rig, 31 presets, complete atlas layout). CNA's real v1 and v2 import as golden fixtures
  (`CNA_AVATAR_CATALOGS`); 30+ malformed models/manifests are refused.

- **Avatar editor (GSP-I1).** A standalone program (`cna_avatar_editor`), not a Guide pane: the
  Guide overlay draws 2D panes over the game's frame, and a lit, animated 3D preview there would
  have to borrow the game's device state mid-frame. A separate program also matches the console,
  where the avatar editor was a dashboard application, and keeps customization out of games
  entirely. The testable part is `AvatarEditorModel` (internal): pages of rows stepping through
  what the catalog offers, fitted to the catalog the owner accepts. Saving uses a new backend call
  (`setAvatar` -> `avatars.set`) or `setLocalProfileAvatar`. Catalogs gained optional face-control
  `name`s (already in v2's manifest) for the labels.

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
| GSP-E* | Materials and renderer polish, per-renderer check | E1 done; renderer coverage in progress |
| GSP-F* | Animation polish and key-pose/loop tests | done (F1) |
| GSP-G1 | Server avatar GLB/manifest validation, malformed fixtures | done (server 7a8a629) |
| GSP-H1 | Avatar cache audit, bounded cleanup, first/warm load measurement | done |
| GSP-I1 | CNA-owned avatar customization (system level) | done |
| GSP-J1 | `AvatarDescription.Changed` | done |
| GSP-K1 | Online host migration | done |
| GSP-K2 | Online `AddLocalGamer` | done |
| GSP-L1..L6 | Account GameDefaults, away/busy, toasts, Stream columns, guests, QoS bandwidth | done (L6: downstream only) |
| GSP-M1 | GamerZone / Reputation | done |
| GSP-O1 | Server concurrency benchmark (`tests/service_benchmark.py`, smoke in CTest) | done |
| GSP-O2..O8 | Production audit fixes: worker pools and scrypt outside the lock, accept survives EMFILE (O2); durability classes and request-ID rules (O3); sign-in pool (O4); per-address admission, 1024 relays (O5); keep-alive and CNA connection reuse (O6); oldest-family sign-out (O7); statistics line (O8); 5 s keep-alive idle so NAT-shared addresses keep their connection share (O9) | done |
| GSP-P1 | Documentation truth pass | done (server README/protocols, CNA docs, register, demos) |
| GSP-Q1 | Final acceptance and register | done |

## Evidence log

- GSP-C1 (catalog v2 + format 2): 111 files, 12.46 MB (v1: 41 files, 2.68 MB); embedding v1+v2 is a
  75 MB generated source that compiles in 9.7 s at 1.44 GB peak. Generator 26 s, `--check` clean.
  GS 508 + 1 skip per process (118 avatar tests, 6 new); the three v1 model digests pinned before
  v2 existed still match with v2 embedded. OPENGL33 `cna_avatar_review`: 255 jobs, 0 failures
  (`/rv/tmp/avatar-polish/review-v2c/`). v2 may still change until this pass ends; it is frozen from
  then on.
- GSP-I1 (avatar editor): 14 `AvatarEditorTest` cases; GS 532 + 1 known skip, Net 495/495 per
  process. OPENGL33 `cna_avatar_editor --capture` on the private runner with a scratch local
  profile: 7 steps, saved avatar read back from the store (`/rv/tmp/avatar-polish/editor/capture/`).
  Server `service_cna_avatars` now also saves an editor edit through `avatars.set` against the
  service's newest (service-only) catalog and reads it back: pass.
- GSP-C2 (garment fit, found in the editor's captures): limb garments now end exactly at their
  `t_range` (the loft had snapped to its 20-step grid, leaving shorts' cuffs 4 cm below the hem and
  boot collars 3.4 cm above the shaft); torso garments clear the tops of the leg lofts, which form
  the hips' outline (skin showed through shirts at the hips at rest); the v2 body leaves out the
  pelvis and thigh tops every bottom covers (a description always names a bottom), which bent
  hips had swung out through the clothes; tops and skirts ride on both thighs below the hip joint,
  shared across the centre, so a crouch lifts a hem instead of pushing the thighs through it; a
  skirt's waist sits inside every top. A nearest-surface weight transfer was tried and dropped: it
  gave trouser tops pelvis weights and tore shirt hems between the legs. Review
  `/rv/tmp/avatar-polish/review-v2d/` (preview and OPENGL33, 255 jobs, 0 failures), close-ups in
  `/rv/tmp/avatar-polish/closeups/`. v2 is 12.88 MB. GS 532 + 1 skip; server avatar tests (with
  v2 as golden import) and `service_cna_avatars` pass. Residual: in the deepest crouch
  (FemaleIdleFixShoe) a few millimetres of trouser front can show just above a shirt hem.
- GSP-D2 (hair and hats): curly hair is 175 short ringlets (coils hanging downhill from their
  roots) instead of 96 faceted balls; the fedora has a real crown -- walls to above the head,
  tapering, pinched at the front sides, a lengthwise crease on top, a front dip -- with its ribbon
  band outside the crown (the old band sat inside it); the cap's button sits on the crown; the
  headband rests on the hair. v2 is 13.82 MB (curly 645 KB per body, the largest asset). Review
  `/rv/tmp/avatar-polish/review-v2e/` (preview and OPENGL33, 255 jobs, 0 failures); GS 532 + 1
  skip; server avatar tests and `service_cna_avatars` pass.
- GSP-H1 (cache audit). Found: writes were already atomic and reads verified, but a full cache
  stopped caching (no eviction), a crash between write and rename left a temporary forever, a
  damaged entry stayed until a download succeeded, none of it was unit tested (it lived inside the
  online backend), and the in-process model cache kept the key of every avatar ever drawn. Now
  `AssetDiskCache` (internal) evicts least recently used entries within 256 MiB (reads refresh
  recency), deletes damaged entries and temporaries older than ten minutes, and has 10 tests
  (damage, truncation, interruption, LRU order, oversize, links, six concurrent writers); the model
  cache prunes expired keys. Load times (Debug, loopback TLS service, `service_cna_avatars`):
  embedded avatar first in the process 70-72 ms, another 40-44 ms, the same again 0.1 ms (model
  cache); an avatar with a service-only item 339 ms on first use (manifest + download) and 89 ms
  from the disk cache (manifest request + verified read).
- GSP-L2 (away/busy): an account-wide status the player chooses in the system Guide (Online
  status: Online / Away / Busy), stored by the service (schema 15, `presence.status`, capability
  `presence-status`) and reported to accepted, online friends as `away`/`busy`, which
  `FriendGamer.IsAway`/`IsBusy` return. It is never inferred from inactivity: Xbox's status was
  the player's choice, and the service has no reliable idle signal. Tests: server unit (status
  per title and account, invalid values), CNA `SystemGuideTest.OnlineStatusIsWhatFriendsSeeAsAwayOrBusy`,
  and the live transport e2e (a real Busy friend). While here the server's "future schema" unit
  check was repaired (broken since migration 014) by one `SchemaVersion` constant.
- GSP-K1 (online host migration). XNA's IL only applies a HostChanged the native layer decided
  (old host flag cleared, `IsHost` recomputed, event at Update) and puts no Ranked restriction on
  AllowHostMigration; the choice of new host is the service's. Server schema 16: optional
  `allowHostMigration` on create/update (capability `host-migration`), in member snapshots only
  while set; host leave or lapsed host lease hands over to the lowest remaining ordinal; a closed
  relay cuts a machine's lease to 20 s (reconnect restores it), so a dead host is detected in about
  20 s. Client: the engine keeps a session whose host vanished for up to 30 s, follows the new host
  (becoming host, or reconnecting upstream), IDs unchanged; the binding raises HostChanged after
  the old host's GamerLeft. Tests: 3 new `OnlineNetworkSessionTest`, server directory (+16
  assertions) and relay authorization, new e2e `service_cna_session_migration` (8.8 s) and
  `service_cna_session_host_crash` (NAT namespaces, host SIGKILLed, 47 s), all other session e2e
  pass; Net 498/498, GS 543 + 1 skip.
- GSP-K2 (online AddLocalGamer). XNA validates synchronously and the kernel reports the gamer
  through GamerJoined later; CNA does the same over `sessions.addMembers` (public slots, <= 4 per
  machine, no add while a game refuses joiners). The host announces a grown group as a whole
  (the packet policy requires complete groups). Found and fixed on the way: online Join and
  JoinInvited with a gamer list set the session's local-gamer limit to the list's size, where the
  reference passes 4. Tests: 3 new `OnlineNetworkSessionTest`, server directory (+15 assertions),
  new e2e `service_cna_session_add_gamer` (NAT, both kinds, 10.8 s); Net 501/501, GS 543 + 1 skip;
  20/20 server session/relay tests.
- GSP-L3 (Guide notifications). XNA's `NotificationPosition` setter hands the position to the
  system, which draws its toasts there; CNA's Guide overlay now does: a queue (one shown for four
  seconds, eight kept), drawn at the position inside a 5 % title-safe margin without taking input,
  for sign-in/sign-out and first-time achievement unlocks (the service's `achievements.award` now
  says whether the call earned it, and its name). Invitations keep their Guide pane. Tests:
  `GuideNotificationTest` (nine positions, timing, burst, sign-in/award/sign-out); the avatar
  editor's captures draw the overlay, and `/rv/tmp/avatar-polish/editor/capture/01-body.png`
  shows "Editor signed in" at bottom center.
- GSP-L1 (account GameDefaults). The service keeps the local-profile `gameDefaults` object per
  account (schema 17, `profile.gameDefaults`/`profile.setGameDefaults`, capability
  `game-defaults`, strict keys and values, admin `game-defaults`); CNA reads it during sign-in and
  resume (under the transport lock, so through `exchange`: a first version deadlocked the live
  e2e) and applies it exactly as a local profile's. No in-game editor: XNA has none either (the
  console dashboard set them). Tests: server unit (+10), `SystemGuideTest.AnAccountCarriesItsServiceGameDefaults`,
  live transport e2e (Alice's stored defaults after a real sign-in).
- GSP-L4 (leaderboard Stream columns). Evidence: the Windows IL throws ProFeatureNotSupported for
  every PropertyDictionary accessor (leaderboards were Xbox-only), the Xbox reference assembly is
  metadata only, and its documentation lists `GetValueStream` but no `SetValue(Stream)`: a Stream
  column is written by writing into the stream a writer's entry returns. CNA does exactly that:
  the writer creates a writable MemoryStream per key on first use (a write, so gameplay-gated),
  commits its bytes as a `stream` column (lowercase hex, 256 bytes, CNA's bound -- the Xbox limit
  is not documented), and reads return read-only streams. Server type `stream` validated; offline
  local boards skip streams as before. Tests: server unit (+8),
  `OnlineLeaderboardTest.AStreamColumnIsWrittenThroughItsStreamAndReadBack`; Net 502, GS 547 + 1.
- GSP-L5 (guest sign-in). Evidence: the XNA IL only carries `IsGuest` as native player state
  (also replicated on NetworkGamer); the Xbox documentation says "If onlineOnly is true, local
  gamers can sign in as guests of a profile currently signed in", a guest is "the guest of an Xbox
  LIVE-enabled profile", and LocalWithLeaderboards "allows guests ... to join". CNA: with
  `ShowSignIn(..., true)` typing Guest signs in "Alice (1)" (Xbox naming) as a guest of the
  lowest-numbered signed-in account, without a password or a service call; `IsGuest`,
  `IsSignedInToLive` true, no online-session privilege (the service authenticates every
  participant), `AwardAchievement` refused, signs out with its host or from the system Guide.
  Tests: 3 new `SystemGuideTest`; GS 550 + 1 skip, Net 502.
- GSP-L6 (QoS bandwidth). A SystemLink host now follows each discovery announce with eight 1200-byte
  probes sent back to back; the searcher, which waits on the socket during its 150 ms window,
  times their arrivals and reports (probes - 1) x 1200 bytes over the spread as
  `BytesPerSecondDownstream` (two processes on loopback: about 231 MB/s). Upstream stays 0: the host
  answers discovery from its frame-driven `Poll()`, so datagrams queued between frames are read back
  to back and their arrival spacing is lost; a discovery thread would fix that and was judged not
  worth the risk to the SystemLink corpus now. Old CNA builds drop the new tag (unknown tags were
  already discarded). The qos_probe demo's stale "client RTT not tracked" text is corrected (client
  RTT has been tracked since `ApplyClientRoundtrips`). Tests: 2 new discovery tests; Net 504/504.
- GSP-M1 (GamerZone, Reputation). Xbox documentation: GamerZone is "the style of social gaming
  preferred by this member" (a member's choice), Reputation "a number of stars ranging 0 to 5". The
  service keeps the chosen zone (schema 18, `profile.setGamerZone`, capability `gamer-zone`; the
  system Guide's Gamer zone) and derives stars from the reviews CNA already stores: 5 x prefer /
  (prefer + avoid), in quarters, sent only when the member has been reviewed. Nothing invented: no
  reviews, no rating (0). Tests: server social (+9), `SystemGuideTest.GamerZoneIsChosenInTheGuideAndReputationComesFromReviews`.
- GSP-O (server concurrency and production audit). `tests/service_benchmark.py` (server) drives a
  scratch title from one process per player, each from its own loopback address (the sign-in limit is
  per address); Release build in `build-probe/`, 64 players, 15 s windows; JSON in
  `/rv/tmp/avatar-polish/bench/{baseline,o2,o3,o3b,o3c,o5,o6-keepalive,after}.json`. Baseline at
  server a1b2a51: steady 73 req/s (p50 703 ms, p99 3.6 s); under a sign-in storm 42 req/s; one
  address holding 160 idle connections locked out 60 of 64 players; 90,000 request IDs: 188 req/s
  once the fsync fix was in; a server at 48 descriptors facing 80 idle connections exited. Causes
  and fixes, one commit each (server 171b8cf, 5b71f19, 512e0b9, fb43d9a, dcca1b4, 541679c, cb1d6e4;
  CNA 24ce2bb79, 9ddfcf4d4):
  service work ran on the two network threads under one lock that sign-in held through scrypt;
  accept errors ended the process; two or three separately fsynced commits per request at 5.4 ms
  each; the 30 s heartbeat spent a title's 100,000 daily request IDs after about 35 always-on
  players and the count scanned the table per request; 128 connections shared by control and
  relays (96 relays per server); a handshake per request (server `Connection: close`, CNA a new
  curl handle per request); past 32 live refresh families sign-in was refused, locking out clients
  that sign in on every launch; no operational output. After: steady 1,602 req/s (p50 38 ms, p99
  67 ms); storm 1,193 req/s with ordinary p50 49 ms and sign-in about 11/s; idle attack no failures;
  200,000 IDs 1,964 req/s; descriptor exhaustion survived. Avatar e2e service item 339 -> 308 ms
  cold, 89 -> 81 ms cached on loopback. Retained (README audit table): one process and SQLite
  writer, many-address floods need a firewall/proxy, in-memory account budget, certificate reload
  by restart, polling. Tests: server unit +3 (repeated read/heartbeat IDs, account budget, family
  eviction), `service_benchmark_smoke`; full corpus 28/28 with the CNA harnesses. O9 (server
  4ba7f95): the 15 s keep-alive idle window let a client hold its connection half of its 30 s
  heartbeat period, so more than about 64 active players behind one NAT address would meet the
  per-address admission; 5 s keeps bursts on one connection. Per-address limits NAT users share
  (control connections, ten sign-ins a minute) are recorded as retained.
- GSP-P1 (documentation). Server README rewritten from dated checkpoint notes to the current state
  (capabilities, not provided, build/administration/operations, capacity, audit, tests);
  `protocol/v1.md` and `relay-v1.md` lose stale "pending/unfinished" claims (host migration,
  public NetworkSession, relay transport, arbitration, EndGame ordering) and state the new limits;
  CNA `docs/gamer-services-server.md` (persistent connection, eviction, not-implemented list,
  Stream), `docs/xna-4-api-coverage.md` (header, §3/§4/§5/§9/§10/§11: online AddLocalGamer was
  still listed as refused), the final register (Recent-window gap, service limits); demo scope
  comments below.
- GSP-P1 demos. Eleven GamerServices/Net demos audited (a subagent, reviewed): besides stale
  stub-era comments, the GamerServices game demos never let GamerServicesDispatcher.Update run
  and had no graphics device service, so nobody signed in and four of them (and the dispatcher
  watchdog) crashed; fixed in 7e613498c, all run to exit 0.
- GSP-Q1 (final acceptance). Results in the register's Evidence section: server corpus 28/28 (no
  skips), GS 551 + 1 skip, Net 504, Runtime 192 + 2 skips, C API 95/98 (standing smokes) and the
  twelve `CApi*` gates (the coverage inventory had missed Gamer's new override/GetTypeName
  declarations: 82a3a094c), ABI and drift clean, the four sample acceptances (`*-20260929-polish`), the 255-job v2 review and the
  demos. `next` is one commit ahead (2c70eaf0f, BL-18: CNAEXT placement on `using` lines, only
  visible under CNA_STRICT_XNA_API); it touches none of this branch's files, and merging it here
  would rebuild three trees for no acceptance value, so it is left for the merge into `next`.
- Visual evidence lives outside the repositories, as the sample evidence does:
  `/rv/tmp/avatar-polish/evidence/{before,after}/`.
- BEFORE (catalog v1, 2026-09-29): `evidence/before/{jobs.json, preview/, opengl33/,
  preview-sheets/, opengl33-sheets/}` (207 jobs, OPENGL33 on the private runner, 0 failures) and
  `old-preview-tint-only.png` (what the removed preview showed). Observations: slim mannequin
  proportions with a small ellipsoid head; capsule limbs with visible joins; ellipsoid palms and
  tube fingers; clothing as inflated body shells without hems, cuffs or collars; hair as a smooth
  head cap; flat single-tint materials; decal eyes that read clearly but sit on a featureless
  sphere; animations readable, FemaleIdleFixShoe lifts the whole body off the ground.
