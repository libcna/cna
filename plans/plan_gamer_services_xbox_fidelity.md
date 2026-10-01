# GamerServices / Avatar Xbox-fidelity pass (living plan)

> **HISTORICAL CHECKPOINT.** This document describes CNA GamerServices/Net as of the GSX pass,
> 2026-09-29 to 2026-09-30 (e46db9822..943a34617; GSX-000..GSX-Q1 merged into `next` as 7d7141c6b,
> GSX-Q2 the final truth cleanup). It is not current capability documentation: several statements
> below (for example "`FriendGamer.HasVoice` stays false" and "`GamerPrivileges.AllowCommunication`
> is honoured (Everyone today)") no longer hold. The "Audit" and "System UI inventory" sections
> record the state at the start of this pass (e46db9822), not even the end of it. For the current
> implementation and every remaining limitation -- what is still not implemented, partial,
> platform-limited, unverified or decided differently from Xbox -- see
> [`docs/gamer-services-known-limitations.md`](../docs/gamer-services-known-limitations.md); it
> supersedes every "open" or "gap" wording below.

Task ids `GSX-*`. Continues `plan_gamer_services_avatar_polish.md` (GSP-*, closed) and
`plan_gamer_services_server.md` (GS-*, closed). This file is the authoritative plan for this pass
and is updated as work lands.

## Evidence classes

Every statement below is one of:

| Mark | Meaning |
|---|---|
| **XNA** | XNA/Xbox-observable behaviour, confirmed from the XNA 4.0 IL (`xna4-decomp`) or its XML documentation |
| **POL** | CNA backend policy: XNA exposes the outcome but not how it is decided, so CNA chose |
| **ART** | Visual approximation / art direction (original CNA work, Xbox 360-era look as a reference only) |
| **DONE** | Implemented and tested |
| **PART** | Partially implemented; the boundary is stated |
| **NO** | Intentionally unsupported, with the reason |
| **UNV** | Impossible to verify here (no hardware, no public reference) |
| **STALE** | Documentation that no longer matches the code |

## Verified starting point (2026-09-29, read from the repositories)

| Repository | Branch | Commit |
|---|---|---|
| CNA (worktree `cnawork/cna-gamer-services`) | `feature/gamer-services-server` | e46db9822 (29 GSP commits after e8ecc7197, which is in `next`) |
| CNA `next` | | 2c70eaf0f (BL-18, CNAEXT placement on `using` lines; touches no file of this branch) |
| cna-gamer-services-server | `feature/gamer-services-server` | 4ba7f95 |
| cna-samples (worktree `cna-samples-gamer-services`) | `feature/gamer-services-samples` | 378483b |
| sharp-runtime | `feature/gamer-services-collections` | 88f6b11f (GSP-J1 `EventHandler::Share`; the prompt's 007280bd is its parent) |

Baseline rerun at e46db9822, `cmake-build-debug` (HEADLESS), one process per gtest case
(`/rv/tmp/xbox-fidelity/baseline/`): see the evidence log.

## The avatar ownership model (decision GSX-A)

**POL.** XNA says only that `AvatarDescription.BeginGetFromGamer` returns the gamer's 1,021-byte
description and that `AvatarRenderer` draws it; how the console stores assets is not observable.
On the Xbox 360 the avatar *assets* were system content on the console (updated by system
updates), and the network carried only the description. CNA now follows that shape:

```text
CNA service account --> AvatarDescription (1,021 bytes: format, catalog version, ...) + revision
                                     |
CNA client (runtime) --> installed canonical CNA avatar catalogs --> AvatarRenderer
                         (compiled into the release; plus exact catalog packs installed by update)
```

- **Layer A, installed catalogs.** The catalogs of a CNA release are compiled into it. A
  description naming one of them renders offline, and `BeginGetFromGamer` + `AvatarRenderer`
  transfer nothing but the description.
- **Layer B, catalog update.** A description naming a catalog the client does not have is served
  by installing that exact catalog as one immutable pack: the service describes it (version,
  manifest SHA-256, pack format, reader level, description formats, total size), the client
  downloads the files it lacks by content hash into a staging directory, verifies every hash,
  validates the whole catalog with the same reader the renderer uses, and activates it with one
  atomic rename. An interrupted or invalid pack is never visible; installed packs are kept (not a
  cache) and are never downloaded again.
- **Negotiation.** `avatars.get` states the formats the client reads, the catalogs it has,
  whether it accepts updates and how large. The service returns the canonical description when
  the client can render it (locally, or after an update it accepts); otherwise an explicitly
  marked projection onto a catalog the client has. The stored avatar is never changed for a
  client.
- **Fallback order.** (1) exact catalog installed; (2) exact catalog pack installed now; (3) the
  service's projection; (4) the deterministic CNA default avatar (newest installed body, the
  description's body type, height, build and colours, every item its slot default); (5)
  `Unavailable` only when not even a body can be drawn. A description is never read against
  another catalog's ids.

## Audit

HISTORICAL CHECKPOINT (at e46db9822, the start of this pass) -- superseded by the task table and
evidence log below.

### Avatars (at e46db9822)

| Finding | Class | Evidence / plan |
|---|---|---|
| `BeginGetFromGamer` transfers only the description (`avatars.get`); the renderer's loader resolves assets | DONE (already) | `AvatarDescription.cpp`, `GamerServicesBackend.cpp::avatars`; GSX-A1 adds the proof tests |
| A compiled-in catalog resolves with no network use | DONE, untested | `AvatarCatalog.cpp::resolveAsset` checks compiled-in files first; GSX-A1 tests it with the asset endpoint disabled |
| A catalog that is not compiled in: manifest (`avatars.catalog`) plus each item file fetched separately (`assets.read`, 12 KB hex chunks) into a 256 MiB LRU cache | replaced | per-item streaming, not a coherent catalog; evictable; ~1,200 round trips for a 14 MB catalog; GSX-A2/A3 replace it with packs |
| `avatars.get` negotiates only description formats (1, 2) | PART | GSX-A3/A4 add catalogs, updates, size, reader |
| Server stores one description + revision per account; catalog files once per content hash | DONE (already) | `avatars`, `assets`, `avatar_catalog_assets` tables; GSX-A5 adds the determinism test |
| Catalog v1 frozen (pinned by test) | DONE | `AvatarCatalogTest.CatalogV1IsFrozen` |
| Catalog v2: stored accounts, server golden fixtures and the GSP evidence name it; GSP evidence log says "frozen from then on" (the pass ended) | frozen now | GSX-B1 pins it; new art goes to catalog v3 |
| v2 look: slim teen proportions, small mitten hands, stringy hair locks with dark gaps, very large round irises, flat line mouth, button nose, subtle face controls, stiff small-motion idles | ART gap | `/rv/tmp/avatar-polish/review-final/sheets/`; GSX-B* |

### Documentation defects inherited from GSP

| Statement | Class | Fix |
|---|---|---|
| Polish plan task rows "GSP-C* first pass done; art iteration continues", "GSP-D* first pass done", "GSP-E* renderer coverage in progress" | STALE | GSX-P1: rows state the final outcome, checkpoints marked historical, AFTER summary added |
| Register "Guests in PlayerMatch/Ranked … refused": correct, but "guests" elsewhere must never read as "fully implemented" | exact matrix needed | GSX-P1 (matrix below) |
| Reputation "derived from reviews" | must say POL | 5 x prefer / (prefer + avoid) in quarters is a CNA formula, not Xbox LIVE's |
| Host migration "lowest remaining ordinal" | must say POL | XNA shows only the migration and `HostChanged` |

Guest matrix at e46db9822 (`GamerServicesDispatcher.cpp:118-124`, `Guide.cpp:439-525`):

| Session type | Guest may create / join / be added |
|---|---|
| Local, LocalWithLeaderboards | yes |
| SystemLink | yes |
| PlayerMatch, Ranked | no: `allowOnlineSessions` is false for a guest (the service authenticates each participant) |

### Register rows (`gamer_services_server_final_register.md`) to re-audit

Research at e46db9822 / server 4ba7f95 (read-only; file:line in the task rows when each is done):

| Gap | XNA evidence | Current CNA | Feasibility |
|---|---|---|---|
| Voice | **XNA**: `NetworkGamer.HasVoice`/`IsTalking`/`IsMutedByLocalUser` are native state flags; `EnableSendVoice(remote, enable)` is a per-pair kernel command, "by default voice is enabled for all gamers"; routing is automatic (no sample API); `AllowCommunication` suppresses it; Windows IL cannot show whether the Windows proxy carried audio (UNV) | flags always false, `EnableSendVoice` checks arguments only | feasible: real capture on SDL3/ALSA (`Microphone.cpp`), Opus 1.5.2 on the system (not vendored for Windows/Emscripten), a free message tag and an unreliable ENet channel on both transports; online relay is TCP (jitter) |
| Browser multiplayer | **XNA**: none (Xbox/Windows only) | Emscripten refuses the service endpoint and the relay | framing is browser-ready; needs server CORS/OPTIONS, a fetch control transport, an ENet datagram shim over `emscripten/websocket.h`, asynchronous relay hello, yielding End waits; emsdk (`~/emsdk`), headless Chrome 152 and Firefox 140 exist here |
| Push | **XNA**: not observable (the console had a live connection) | polling: heartbeat 30 s, invitations 5 s, directory 1 s, avatar check 10 s | needs a new account-scoped WSS event channel; the relay is session-scoped |
| Single server process | n/a | no lock; two processes can share one database; `RelayHub` routes only its own channels | ownership guard feasible now; horizontal scale needs shared relay routing |
| Rate limits | n/a | per-address/connection/account/title/relay limits exist; no asset download limit; all per process | add asset download budget and per-address connection rate |
| QoS upstream | **XNA** XML: "estimate of the available bandwidth" | 0: the host reads discovery once per frame and there is no client-to-host train | needs a responder thread with receive timestamps and a report message |
| Parties | **XNA**: `SignedInGamer.PartySize` (setter internal; 0 without parties; only native `PartyMembersChanged` moves it), `ShowParty`/`ShowPartySessions` (PlayerIndex.One only on Windows; `ShowPartySessions` "shows the Friends screen instead" without a party), `SendPartyInvites` throws "at least two party members" when `PartySize < 2` (IL; the XML's "no effect" is wrong) | `partySize_` starts at **1** and its setter is public (C API uses it); panes explanatory; `SendPartyInvites` refuses | a service party makes `PartySize`, both panes and `SendPartyInvites` real |
| Marketplace / trial | **XNA**: one `ShowMarketplace(PlayerIndex)`; `IsTrialMode` starts true and is latched from `SimulateTrialMode` at each `Update` (a purchase shows as it turning false later, no event); `AllowOnlineSessions` false in trial; Framework resources carry a "Test Purchase" emulation prompt | `IsTrialMode` answers `SimulateTrialMode` at once; `AllowOnlineSessions` ignores trial; marketplace pane says "fully licensed" even while simulating trial | fix the three divergences; a CNA title-content pane |
| Partner tokens | **XNA**: static `GetPartnerToken(audienceUri)` family; Windows IL throws `NotSupportedException` ("only available for Xbox LIVE Registered Developers"); Windows Phone only | throws the same | decide in GSX-E5 |
| Title updates | **XNA**: `InstallingTitleUpdate` raised when the GFWL proxy starts an installer, then every call throws `GamerServicesNotAvailableException`; `GamerServicesComponent` exits the game on it; `GameUpdateRequiredException` from any proxy call when LIVE refuses the version | event never raised; component does not subscribe `Exit`; exception never thrown | map a service "this title version is no longer accepted" answer to `GameUpdateRequiredException`; component exits on the event |
| Recent keys | **XNA**: key string only; no window in IL, XML or any document found | no window | retain (evidence insufficient) |
| TrueSkill | **XNA**: games observe only `LeaderboardEntry.Rating` of a board they read; no document ties skill to matchmaking; Windows IL refuses the handlers | `WriteTrueSkill` raised, arbitrated boards, no skill computed | decide in GSX-E4 |

## System UI inventory (GSX-U0, BEFORE at 065464fec)

Captures: `/rv/tmp/xbox-fidelity/ui/before/` (`cna_guide_review`, 19 screens over a stand-in game,
OPENGL33, private display; `sheet-before.png`) and `/rv/tmp/xbox-fidelity/ui/before-editor/`
(`cna_avatar_editor --capture`, 7 steps). Every Guide surface below is drawn by two renderers,
`Guide::RenderPendingMessageBoxEXT` and `RenderPendingKeyboardInputEXT`: a white box with the 5x7
dot-matrix system font at 1x, buttons in a row, no dimming of the game, no avatar anywhere.

| Screen | Purpose | Current layout / controls | Avatar | Navigation | Weaknesses | Xbox 360 analogue | CNA treatment |
|---|---|---|---|---|---|---|---|
| Sign-in (`ShowSignIn`) | choose a profile for a player | keyboard box "Username for player 1", then password box | none | type text, Enter/Esc | looks like an HTTP form; no slots, no profiles to pick | profile picker over four controller slots | four player slots, profile cards with portraits, online/local/guest state, masked password step |
| System Guide (Home key) | hub | message box, six buttons in one row | none | Left/Right, Enter | a row of buttons; no identity | the Guide blade with the gamer's picture | identity header (portrait, gamertag, score, status) and a category rail |
| Friends (`ShowFriends`) | who is online, doing what | text lines "Bob - online - Racing..." + Find/More/Close | none | buttons only; no row focus | cannot select a friend; eight per page | friend list with pictures, status and presence | friend rows with portrait, status dot, presence, joinable/invite marks; A opens the card |
| Gamer card (`ShowGamerCard`) | another player's identity | four text lines + buttons | none | buttons | a property dump; no zone/reputation/presence shown | gamer card with avatar, rep stars, zone, score | large animated avatar, stats, stars, zone, presence, relationship, actions |
| Messages (`ShowMessages`) | inbox | one message per box, Next/Reply/Delete/Close | none | buttons | no list; cannot see what is unread | inbox list | inbox rows (sender portrait, unread mark, excerpt), message view |
| Compose | write a message | keyboard box | none | typing | no recipient chips | message composer | composer card with recipients and text field |
| Achievements (`ShowAchievementsEXT`) | this title's achievements | "[x] First Steps" lines | none | OK | no pictures, score or dates | achievement list with tiles, score, unlocked state | tiles with picture (service) or CNA trophy, score, earned date, totals |
| Players (`ShowPlayers`) | recently met | name lines | none | buttons | no list focus | recent players | rows with portraits, leading to the card |
| Player review | prefer/avoid | four buttons | none | buttons | no subject shown | player review | card with the subject and two clear choices |
| Game invite (`ShowGameInvite`) | invite friends | refuses without a session (correct), no pane | none | none | nothing explains the refusal to the player (the API throws, as XNA) | friend picker | friend picker with checks when a session exists |
| Invitation received | accept a friend's invitation | message box "Bob invited Alice..." Accept/Decline | none | buttons | not an event; no sender identity; no "view profile" | toast then invitation card | toast with portrait, invitation card: sender, title, session, Accept/Decline/View profile |
| Party (`ShowParty`) | party | explanatory box | none | OK | no party exists | party blade | party screen (after the party service) |
| Marketplace (`ShowMarketplace`) | store | "no marketplace ... fully licensed" | none | OK | wrong while SimulateTrialMode (see gaps) | marketplace | CNA title content: installed, updates, avatar catalogs; trial purchase emulation |
| Message box (public API) | game's question | white box | none | buttons, mouse | tiny text, no icon | system dialog | dialog card with icon, wrapped text, focused buttons |
| Keyboard (public API) | game's text entry | white box | none | typing | no on-screen keyboard for a pad | virtual keyboard | text field card, on-screen keyboard for the pad |
| Notifications | events | dark box, green bar, one line, 2x pixel font | none | none | no icon, no portrait, no transition | toast with icon | toast with icon or portrait, slide/fade, at NotificationPosition |
| Leaderboards | scores | no system surface (games draw their own through `LeaderboardReader`; `demo_leaderboard_viewer`) | none | - | - | per-game boards | a Guide page for this title's boards (CNA system feature, not an XNA API) |
| Avatar editor (`cna_avatar_editor`) | customize the avatar | left property panel (Body/Features/Style pages, value rows, numeric sliders), preview right | preview only | Up/Down/Left/Right, Q/E pages | a developer tool: numbers, internal item names, no item previews, fixed camera per page, toast over the hints | avatar editor: categories, item carousel with renders, contextual camera | large live preview, category rail, rendered item cards, contextual camera, human face controls, transactional save |

## Tasks

| Id | Task | Status |
|---|---|---|
| GSX-000 | Verify state, baseline, audit, this plan | done |
| GSX-A1 | Local-first proof: warm installation renders with the asset endpoint off, zero avatar downloads | done |
| GSX-A2 | Client installed-catalog store and catalog-pack update (stage, verify, validate, atomic activate, dedupe, retry) | done |
| GSX-A3 | Server: pack descriptor, binary content-addressed file route, reader level, negotiation and projection | done |
| GSX-A4 | Client negotiation and fallback order; old-client behaviour | done |
| GSX-A5 | Identity vs assets: determinism across clients | done |
| GSX-B1 | Freeze catalog v2; generator writes v3 | done |
| GSX-B2..B9 | Catalog v3 art: head/face, atlas, body, hands, clothing, hair/facial hair, materials, animation | PART: head, face atlas, proportions, hands (re-sculpted), hair clumps, face controls, idles, hoodie pocket done; baked occlusion open (the avatar renderer has no occlusion input) |
| GSX-B10 | BEFORE/AFTER review with identical cameras, inspected | done |
| GSX-U0 | System UI audit: every Guide pane, editor and demo; deterministic BEFORE screenshots; visual inventory | done |
| GSX-U1 | One CNA system visual language (panel, title, tabs, focus, buttons, identity, presence, toast, dialog, loading, error) shared by Guide and editor | done |
| GSX-U2 | Console-style avatar editor: large live preview, categories, rendered item cards, contextual camera, human face controls, coherent randomize, transactional save/cancel | done |
| GSX-U3 | Sign-in as profile selection over four player slots | done |
| GSX-U4 | Gamer Card with avatar, presence, zone, reputation, relationship and actions | done |
| GSX-U5 | Friends, invitations (send/receive as system events), notifications | done |
| GSX-U6 | Achievements and leaderboards presentation | done |
| GSX-U7 | Party and title-content panes in the same system; catalog updates as one product | done |
| GSX-U8 | Transitions, reduced motion, original system sounds (if an appropriate audio path exists) | done |
| GSX-U9 | Semantic UI tests; BEFORE/AFTER sheets and an ordered interaction sequence | done |
| GSX-E* | Remaining register gaps, one decision each | done: trial/test purchase (E1), voice (E1), parties (E2), upstream QoS (E3), title versions (E4), single process and connection rate (E5), social notifications and push (E6), browser multiplayer kept out of scope by the owner (E7); partner tokens, TrueSkill and Recent windows kept with their evidence |
| GSX-P1 | Documentation and register | done |
| GSX-Q1 | Final acceptance | done (acceptance matrix below) |
| GSX-Q2 | Final truth cleanup: documentation reconciled with the code (push, voice, guests, policies, avatar ownership, capabilities), `docs/gamer-services-known-limitations.md`, and the small defects found on the way | done (entry below) |

## Evidence log

- Baseline at e46db9822 (`/rv/tmp/xbox-fidelity/baseline/`): CnaGamerServicesTests 551 + 1 known
  skip, CnaNetTests 504/504, CnaRuntimeTests 192 + 2 environment skips -- the reported numbers hold.
- GSX-A (catalog ownership). Client: `AvatarCatalogStore.cpp` (installed packs; `catalogManifest`
  = compiled in, installed, pack install, else null; `resolveAsset` never downloads);
  `avatars()` returns records (description, revision, projected) and sends `catalogs`,
  `catalogUpdates`, `maxCatalogBytes`, `reader`; `catalogFile` is a binary GET on the persistent
  connection; `Changed` compares revisions when both sides have one (bytes otherwise, as for local
  profiles); configuration `avatarCatalogUpdates`/`maxAvatarCatalogBytes`,
  `CNA_AVATAR_CATALOG_UPDATES`, store `CNA_GAMER_SERVICES_CATALOGS_DIR`. Server: capabilities
  `avatar-catalog-packs` and `files`, `avatars.catalogPack`, `GET /cna/v1/files/<sha256>` (title
  assets, pictures, catalog files and manifests by the hash of their stored text; 1 GiB per account,
  title and hour), `avatars.get` negotiation and `projectAvatarDescription`; manifests may state
  `reader` (import accepts 1). No schema change: pack facts derive from the stored manifest, cached
  per immutable version. Tests: 10 new GS cases (endpoint off, install once, interrupted + resume,
  malformed manifest, bad hash, unreadable model, wrong skeleton, animations without every preset,
  oversized, declining client, two clients same digest, revision-based `Changed`) plus a
  configuration case; server `avatar_tests` 94 assertions (+ pack descriptor, file authorization,
  negotiation, projection); `service_cna_avatars` e2e rewritten: first run installs the newer
  catalog as a pack (Debug, loopback: 852 ms including whole-catalog validation), the second run
  lists it before any request and reuses it (38 ms), a client with updates off receives the
  projection onto catalog 2 and draws it with nothing substituted. GS 561 + 1 skip, Net 504/504.
- GSX-U1/U4/U5 (the CNA system UI). `modules/gamer-services/src/Internal/Guide/`: an original
  typeface and icon family (`GuideStrokes`: monoline skeletons, round caps, rasterized by distance at
  each size, so no font or icon file), the palette and primitives (`GuideStyle`: premultiplied
  colours, nine-slice rounded panels and soft shadows, pad and key glyphs), avatar views
  (`GuidePortraits`: the standard `AvatarRenderer` into render targets, head or full-body framing
  from the avatar's own bind pose, idle-animated where live), the screen stack (`GuideScreens`:
  input from keyboard, pad and mouse with priming and key repeat; dim over the game; a shell with an
  identity rail, category rail switched by LB/RB, header and hints; dialogs; transitions that honour
  `CNA_GAMER_SERVICES_REDUCED_MOTION=1`; the game's message box and keyboard, with an on-screen
  keyboard for a pad; toasts with icon or portrait at `NotificationPosition`), and the screens
  (`GuideSocial`: home, friends, gamer card, messages and message, achievements, recent players,
  review, invite with add-by-gamertag, settings, party, game content, received invitation). Every
  screen reads the service asynchronously (`load`), shows loading, empty, offline-profile and error
  states, and never blocks the game thread. `Guide.Show*` open the matching screen after XNA's own
  checks; invitations arrive as a notification plus an invitation card answered once
  (Accept/Decline/Later, the card closing counts as Later); structured notifications behind the
  unchanged `postGuideNotification` text. The public EXT renderers delegate to the new dialogs.
  Tests: 9 `GuideUiTest` (order, focus, rail, card actions, own card, unreachable service, find
  gamer, unread count, invitation answered once); social, system Guide, invitation and harness
  flows migrated from message-box clicks to the semantic API (`GuideUi::*ForTesting`). Found on the
  way: tests creating and destroying devices reuse addresses (resources now follow
  `GraphicsDevice.Disposing`), and static teardown after the device (the UI state is never
  destroyed). Evidence: `/rv/tmp/xbox-fidelity/ui/after1/`. GS 570 + 1 skip, Net 504/504,
  Runtime 192 + 2, server corpus 28/28 with every CNA harness.
- GSX-U3 (sign-in). `ShowSignIn` opens a picker: the four player slots (who is signed in, the one
  choosing, the ones still to come), then what this player may be: this computer's local profiles
  with their avatars, a new profile, a CNA account, or a guest of the signed-in account for an
  online-only sign-in. Typing starts a name at once (the name or account field opens with the
  first character), so keyboard players and every existing automated sign-in still just type;
  Enter alone takes the focused profile; cancelling a name or password returns to the picker, Back
  there ends sign-in. The slot shows "Signing in" while the service answers. Tests and the TLS
  client harness follow the picker (21 sign-in and system-Guide tests; harness 120 checks);
  `service_tls_e2e`, `service_cna_session`, `service_cna_invite`, `service_cna_avatars` pass.
- GSX-U2 (avatar editor). The editor is a full-screen Guide screen (`GuideAvatarEditor.cpp`):
  **POL** it opens from the Guide's Home over a running game, and `cna_avatar_editor` hosts the same
  screen after sign-in. Model (`AvatarEditorModel`): twelve categories of rows (cards, swatches,
  sliders, presets) that offer exactly what the catalog has; face presets move named controls
  together (Balanced resets); sliders read in words; coherent randomize (natural colours mostly,
  harmonious clothes, faces within +/-0.55, glasses 20 %, hats 15 %, facial hair only on the male
  body) and per-category shuffle. Screen: large live avatar (own `AvatarRenderer` into a render
  target; Wave on open, idles, Celebrate on save), the camera eases between body, head, upper,
  lower and feet shots computed from the avatar's bind pose (`shotFor`, also new portrait
  framings), try-on of the focused card, turn/zoom by stick, keys, drag and wheel, mouse hit-testing
  of categories, cards, swatches and slider tracks; save/discard/keep dialog; saving failures
  explained with the edits kept; stored avatar changed elsewhere detected (revision or profile
  bytes). New icons (figure, drop, face, eye, mouth, hair, beard, shirt, trousers, shoe, glasses,
  hat, dice); the icon atlas became a grid (a Reach texture is at most 2048 wide). Tests: 18
  `AvatarEditorTest`, 9 `GuideAvatarEditorTest` (Home entry, wrap, try-on vs keep, sliders, save to
  the service and identity, discard/keep editing, failed save, change elsewhere, local profile,
  nobody signed in, catalog unavailable). Evidence: `/rv/tmp/xbox-fidelity/ui/after-editor/`
  (15 steps, OPENGL33, private display; `after-editor-sheet.png`), inspected: framings fit the band
  between header and footer, hints do not overlap. GS 583 + 1 skip (one process); Net 504/504
  (nine discovery/leaderboard cases share ports or a store when run 8-way parallel and pass alone).
- GSX-U6 (achievements, leaderboards). **POL** A Leaderboards page joins the Guide rail (XNA has
  no system leaderboard UI; games read their boards through `LeaderboardReader`): the service's new
  `leaderboards.list` (capability `leaderboard-list`, read-only, unrecorded) lists the title's
  provisioned boards with entry counts; a board shows the top players, the players around you (the
  focus starts on your row) or your friends, with ranks in medal colours, portraits and grouped
  ratings, and opens a gamer card. Achievements open a detail card: the picture (service asset or
  the offline profile's title content), points, unlock date, how to earn, secrets kept secret.
  Tests: 3 `GuideLeaderboardTest`, rail order, server `service_unit` (board list, modes, title
  isolation, token). Evidence: `/rv/tmp/xbox-fidelity/ui/after3/` (24 screens incl.
  20-achievement-detail, 21-23 leaderboards, 24 Home -> Edit avatar). GS 586 + 1 skip, Net 504.
- GSX-E1 (trial and marketplace, **XNA**). `Guide.IsTrialMode` starts true and every
  `GamerServicesDispatcher.Update` latches it from `SimulateTrialMode` before sign-in changes (IL:
  `isTrialMode = true`, `Guide.IsTrialMode = (guideState & IsTrialMode) != 0` in Update);
  `GamerPrivileges.AllowOnlineSessions` is false in trial (IL). `ShowMarketplace` keeps XNA's
  checks and opens Game content; a game simulating trial mode gets the "Test Purchase" emulation
  (XNA's own resource strings, CNA wording for the store sentence): Yes turns `SimulateTrialMode`
  off and `IsTrialMode` follows at the next update, No leaves the trial. The C ABI documents the
  latch; `cna_guide_simulate_keyboard_input_cancel_ext` also ends the sign-in picker (Escape),
  which the U3 picker had broken for `CApi_GuideSmoke`. C API coverage, limitations and release
  gate summaries regenerated (stale since GSX-A added two `Configuration` fields, planned under
  CBIND-127).
- GSX-U7 (content). Game content shows the game's license (full, or trial with a test purchase)
  and the avatar catalogs as one product: the catalogs of this release, the updates installed
  since, and the update setting with its size bound. Tests: `SystemGuideTest` trial latch and
  marketplace, updated trial/privilege tests. CApi 105/110 (content and audio smokes fail in this
  HEADLESS/NULL-audio tree, unrelated to GamerServices). GS 587 + 1 skip, Net 504.
- GSX-B (catalog v3, **ART**, original CNA work). v2 frozen (`CatalogV2IsFrozen`, manifest SHA-256
  7a27a9d6...; generator at d1730d4f3); the generator writes v3 with exactly v2's 39 item ids and
  slots and 4 facial-hair ids. Changes: head a fifth larger (scale 1.30/1.24) with rounder cheeks and
  a softer jaw on a shorter neck; shoulders, chest and hips lowered (shorter legs); limbs 17 %/14 %
  sturdier; hands 1.40/1.28; torso 4 % broader; almond eye openings (wider than tall) with a smaller
  iris under a bolder lid line and an outer wing, closed-eye shapes widened to match; fuller upper
  and lower lips with a gentle resting smile and a wider mouth patch; a larger rounded nose; hair as
  a fifth fewer, a third broader clumps with softer crevice shading; face controls reach about half
  again as far; the standing idle sways foot to foot with head turn and nod, the weight-shift idle
  shifts further; FemaleShocked's hands kept off the spine on the new proportions. Review: the same
  255 jobs for v2 and v3 (close-ups framed in proportion to each avatar's own head, identical numbers
  for v2), real OPENGL33 renderer: `/rv/tmp/xbox-fidelity/art/before/`, `/rv/tmp/xbox-fidelity/art/after/`
  (sheets views, diverse, faces, animations, expressions) and `art/before-after.png`, inspected:
  heads read larger and friendlier, eyes almond rather than round, lips visible, hair as masses,
  every preset still plants its feet and keeps hands out of the body. The editor's head view and the
  Guide's head portraits frame in proportion to the head (v3's larger head was cropped).
  Open at this point: clothing re-sculpt (folds, thicker garments), hand re-sculpt (finger
  separation), baked occlusion/material pass; the hands and the hoodie pocket were done in the next
  GSX-B entry, folds and baked occlusion remain. GS 588 + 1 skip.
- GSX-E4 (title updates, **XNA** + **POL**). `GamerServicesComponent.Initialize` subscribes
  `InstallingTitleUpdate` and exits the game (XNA IL); the event is still never raised because CNA
  installs no title updates (**NO**, reason stated). **POL** version gate: a title may set the oldest
  game version it accepts (server migration 019, admin `title-minimum-version`, envelope field
  `titleVersion` sent only to a service advertising `title-version`); older clients and clients
  stating none get `UPDATE_REQUIRED`, which the sign-in Guide explains and a starting network session
  throws as `GameUpdateRequiredException` (XNA's `LIVEnTitleUpdateRequired` mapping). Client
  `titleVersion` / `CNA_GAME_VERSION`. Tests: server `service_unit` (syntax, order, refusal, hello),
  e2e `service_tls_e2e` (older and missing versions refused with the explanation), configuration.
  The GamerServicesComponent subscription itself has no unit test, per the file's recorded reason
  (a Game needs a live backend). Also: the client checks `leaderboard-list` before `leaderboards.list`.
- GSX-E2/U7 (parties, **XNA** surface + **POL** service). Server migration 020: account-level parties
  (one per account, eight people, hour-long invitations, the leader passing to the longest-standing
  member) and `invites.joinFriend` (an invitation a friend's or party member's joinable player-match
  game grants the asker, never listed). Client: `SignedInGamer.PartySize` follows the party at
  gamer-services updates (0 without one; XNA's setter is internal, so the public C++ setter is gone,
  the C ABI setter stays for hosts and is overwritten by the service); party invitations arrive as
  notifications; the Guide's Party page (members with portraits, presence, leader crown and joinable
  mark; invitations with Accept/Decline; Invite friends picker; Leave party); `ShowPartySessions`
  lists joinable party members' games and shows Friends without a party (XNA XML); gamer cards offer
  "Join game" for a joinable friend and "Invite to party"; joining raises `InviteAccepted` through
  the normal invitation path; `SendPartyInvites` invites the rest of the party to the current online
  session after XNA's checks. Tests: server social unit (party lifecycle, leadership, friends only,
  join requests stay out of inboxes and friend states), 5 `GuidePartyTest`, the TLS e2e drives a
  party through the Guide against the real service. GS 594 + 1 skip, Net 504.
- GSX-U8 (motion and sound, **ART**). Transitions stay short and restrained (shell 0.18 s, content
  slide 0.16 s, dialogs, toast slide and fade, the full-screen editor fading in from black, the
  editor camera easing between views); `CNA_GAMER_SERVICES_REDUCED_MOTION=1` finishes every one at
  once and stops idle gestures. Original system sounds, synthesized in-process (no sound files) and
  played quietly through the standard `SoundEffect`: open, move, accept, back, notification bell,
  error knock; only frames a player drives play them (semantic tests stay silent),
  `CNA_GAMER_SERVICES_SOUNDS=0` turns them off, and without an audio device the Guide is silent.
  Test: `GuideUiTest.SystemSoundsPlayOrStaySilentWithoutFailing`; the 24-step review runs with
  them. UNV: how they sound was not listened to here (no audio output on this host).
- GSX-U9 (tests and review). Semantic tests drive every system surface through the Guide's own
  input (`sendForTesting`/`clickForTesting`/labels/focus), and `GuideInputTest` drives the Guide's
  real frame from a canned keyboard and a canned controller behind the platform (the key held when
  the Guide opens does nothing; Down/Enter, E/LB between categories, Escape/B closing).
  `cna_guide_review OUTDIR --sequence` is one player's session in order, never reset between
  captures: Home signed out, the sign-in picker, account name and masked password typed, the
  sign-in toast, Home, the avatar editor, trying and keeping a hairstyle, the save question, the
  saved toast back on Home, Friends, Bob's card, a party invitation sent, Bob's game invitation
  arriving, accepted. Inspected on the private display (OPENGL33); it showed two defects, fixed:
  the rail's "Not signed in" was truncated (now a two-line prompt), and a friend's card opened
  focused on "Remove friend" (a friend's actions now come first and removing is last).
  BEFORE/AFTER: the 19 original review captures against the current ones, side by side.
  Evidence: `/rv/tmp/xbox-fidelity/ui/sequence/`, `seq-sheet-{1..4}.png`,
  `before-after-{1..4}.png`, `after6/`. GS Guide groups 47/47, Net invitations 6/6, fake-backend
  harness and `service_tls_e2e` pass.
- GSX-E3 (QoS upstream, **XNA** + **POL**). XNA's `QualityOfService.BytesPerSecondUpstream` ("an
  estimate of the available bandwidth" from this machine to the host) is now measured for SystemLink
  results. A hosting machine runs a small responder thread on its own UDP socket; after its announce
  and downstream train the host sends an `UpstreamInvite` (discovery tag 0x04) naming the responder's
  port, the searcher sends eight padded probes back to back (0x05), the responder timestamps them on
  arrival and returns the rate (0x06) within the 150 ms search. A train that loses its tail is
  measured from what arrived after 250 ms; at most 64 trains are tracked; probes naming another
  session are ignored. Older CNA builds ignore the new tags, so no protocol version change. The
  value reaches `QualityOfService` through a private friend (no new public member). Service search
  results remain unmeasured (**NO**: no path to the host before a join). Tests: both directions from
  a real search, codec round trip and refusal, a partial train from a raw socket; Net discovery and
  SystemLink groups 62/62. C API inventory unchanged (the friendship row recorded not applicable).
- GSX-E (server capacity re-run). Release server at `4bc685c`, 64 players, 15 s windows, machine
  busier than the earlier run (load average about 4.3): steady 1,312 req/s p50 44 ms p99 73 ms;
  sign-in storm 1,024 req/s; 160 idle connections 1,127 req/s; 200,000 request IDs 1,507 req/s;
  descriptors exhausted, still serving; no errors. The pre-pass server `4ba7f95` measured 1,329 and
  992 req/s steady in the same minutes: the difference from the earlier 1,602 is load. The Release
  build of the server's tests had stopped on a fixture reading four bytes past a literal (fixed,
  server `49c260a`). Evidence `/rv/tmp/xbox-fidelity/bench/`. Decisions kept, with their evidence in
  the register: partner tokens refused as XNA for Windows does; `WriteTrueSkill` raised with no skill
  computed; Recent leaderboard keys without a window.
- GSX-E1 (voice, **XNA** + **POL**). XNA routes voice automatically between every gamer of a session
  and exposes `NetworkGamer.HasVoice`/`IsTalking`/`IsMutedByLocalUser` and
  `LocalNetworkGamer.EnableSendVoice` (all enabled at first). CNA now carries it on SystemLink and
  online sessions. **POL**: one microphone per machine, owned by Player One's gamer (else the first
  local gamer), opened as its own recording session (a game's `Microphone` loses no bytes) and only
  while someone could hear it; speech detection against an adaptive noise floor with 300 ms
  hangover; Opus (system libopus, `CNA_ENABLE_VOICE` AUTO/ON/OFF, `CNA_VOICE=0`) at 16 kHz, 20 ms,
  16 kbit/s; new message tag `VoiceData` 0x0D sent unreliably on ENet channel 1 to one gamer per
  remote machine, relayed by the host with the same sender/target authority as game data (the
  online policy validates it; older builds ignore it on SystemLink and count it rejected online); a
  silent frame once a second carries HasVoice; per-talker streams through
  `DynamicSoundEffectInstance`, a lost frame concealed and a late one dropped, over 120 ms queued
  skipped. `GamerPrivileges.AllowCommunication` is honoured (Everyone today). The Guide gamer card
  offers Mute/Unmute (both directions, per local profile, for the life of the process).
  `FriendGamer.HasVoice` stays false (**NO**: the service does not know a friend's hardware). Tests:
  codec, online policy, SystemLink send/receive/concealment/EnableSendVoice/mute/host authority and
  relay, online both ways, two real processes both ways, Guide mute. Net 514/514, GS 598 + 1 skip.
  **UNV**: a real microphone and speaker (none on this host); the capture and playback adapters are
  the platform recording provider and the SoundEffect path the rest of CNA uses.
- GSX-E6 (social notifications and push, **ART** + **POL**). The console raised system notifications
  for a new message, a friend request and a friend coming online; CNA raised none of them (only
  invitations and parties were watched). A social watcher now reads each signed-in account's inbox
  and friends (every 15 s, and at once after sign-in, where it only learns what is there) and posts
  "Message from X" (with an excerpt cut on a whole character), "Friend request from X" and "X is now
  online". **POL** push: the server's account event channel (`/cna/v1/events`, capability `events`,
  server `e3e78a8`) sends coalesced topic hints to every channel of an account another request
  changed, only after that request succeeded; the client keeps one WebSocket per signed-in account
  on a thread of its own (reconnecting with backoff and with a renewed token;
  `CNA_GAMER_SERVICES_EVENTS=0` turns it off), and a hint only moves the invitation, party or social
  watcher's next read forward, so a lost hint costs only the interval. Tests: three
  `SocialNotificationTest`, server hint checks per operation, WSS e2e (delivery across titles,
  refusal, a refused request telling nobody), and the TLS e2e's CNA client seeing a message arrive
  through the channel within 5 s (the read interval is 15 s). GS 601, Net 514.
- GSX-B (hands and garments, **ART**). The v3 hand close-ups showed a mitten: finger tubes wider
  than their spacing on a palm twice the finger row's width and 6 cm thick, a jagged crack where the
  palm met the forearm, and the curled fingers' in-palm parts swinging out of the back of the hand.
  Now the palm is a rounded box as wide as the finger row (fingers slimmer, spread a little wider),
  starts inside the forearm at its own section and flares out (no seam), the in-palm part of each
  finger is bound to the wrist so a curled finger never pokes through, and the thumb is shorter with
  a smaller pad; finer tessellation. The hoodie's pocket is a flat rounded trapezoid with a rolled
  edge hugging the belly (it was a bulging oval) and its ribbed hem sits closer. The review gained a
  back-of-hand view. Real-renderer review `/rv/tmp/xbox-fidelity/art/after-b2/` (257 jobs, 0
  failures) and `art/hands-before-after.png`, inspected; avatar tests 153/153. Open: baked occlusion
  (the avatar renderer takes no occlusion input; adding one touches every renderer).
- GSX-E7 (browser multiplayer, **NO**, owner scope). Kept as decided on 2026-09-28: native builds
  play online, the browser build has the limitations page `docs/browser-network-readiness.md`, which
  names every boundary and what replacing it needs (a browser control transport, a realtime adapter
  over `emscripten/websocket.h`, and the service's origin rules). Voice and the event channel are
  native too.
- GSX-Q1 findings, fixed (found by re-running SAMPLE-096 Invites against the new Guide). (1) The
  sign-in picker took typing as the start of a name only in tests: it never started platform text
  input, so a real keyboard's letters never arrived; it now starts text input while on top and
  leaves it as found. (2) Input the Guide used reached the game: the Escape that closed the Guide
  was still held on the game's next read and the sample's menu, which exits on Escape, exited.
  **POL/ART**, as the console's Guide took the controller: while the Guide is visible the game
  reads a neutral keyboard, neutral connected pads and no mouse buttons, and whatever is held when
  the Guide closes stays hidden from the game until released (`CNA::Internal::Input::SystemInput`;
  the Guide reads through its system calls; touch was already withheld the same way). (3) The
  Home key / Guide button now also closes the Guide it opened, as on the console, never a game's
  own message box or keyboard or a sign-in in progress. Tests: `SystemInputTest` (keys and pads),
  `GuideInputTest` (the Escape that closed the Guide, text input under the picker),
  `SystemGuideTest.TheGuideButtonClosesTheGuideItOpened`. The Invites acceptance driver
  (`/rv/tmp/samples/SAMPLE-096-InvitesSample_4_0/scripts/capture-cna-invites-gs.py`) now signs in
  through the picker and invites through Friends -> Find gamer -> the gamer card -> Invite to game.

- GSX-Q2 (final truth cleanup, 2026-09-30). Documentation reconciled with the code across CNA, the
  server and the samples branch, from three read-only sweeps (code comments, Markdown, server and
  samples) and checks against the source. Corrected: server push described as missing (server
  README, `protocol/v1.md` twice, the invitation section); "there is no voice"/"CNA carries no
  voice" (server README, protocol, C header, a test) -- voice is NetworkSession realtime traffic,
  not a control-plane service; the C API documents (`docs/c-api/GAMER_SERVICES.md`, `NET.md`,
  `FEATURE_MATRIX.md`) and about ninety C/C++ comments still describing the pre-service runtime
  (no friends, no-op Guide screens, inert avatars, always-zero zone/reputation/away/busy/upstream
  QoS, SystemLink-only networking); `README.md`'s GamerServices/Net/Avatar sections; the broken
  guest table and the "System Guide" paragraph in `docs/gamer-services-server.md`, whose slice
  history is now under a "historical" heading; the avatar docs (explicit ownership model, formats
  1-2, catalog v3 released); `docs/coverage.md`, `misc/known_gaps.md`, the browser page; the
  `SystemInput.hpp` comment that said pad sticks keep moving under the Guide (they are centred).
  Labelled historical: `AUDIT.md`/`NEXT.md` checkpoint quotes, `NEXTnet.md`, the `audit/` archive's
  GamerServices records, `plan_net.md`, the acceptance-sources queue, the GS-011 open-limit list,
  this plan's and the polish plan's opening audits. New: `docs/gamer-services-known-limitations.md`,
  44 entries, linked from the README, the service and avatar docs, the register and the plans.
  Defects found and fixed: `NetworkGamer.IsGuest` was always false (a local gamer now reports its
  profile's; a remote SystemLink guest still reads false, documented); a guest in a
  LocalWithLeaderboards game made `StartGame` throw with a service configured (the epoch now names
  only accounts). Tests: `LocalNetworkGamerTest.IsGuestFollowsTheSignedInProfile`,
  `OnlineLeaderboardTest.AGuestInALocalWithLeaderboardsGameLeavesTheAccountsRowsToTheService` (fails
  without the fix), `OnlineInvitationTest.AnInvitationRevokedWhileItsCardIsOpenIsNeverAccepted`,
  `AvatarCatalogTest.CatalogV3IsFrozen` (v3 shipped in `next`; a service refuses a changed import).
  Results (`/rv/tmp/xbox-fidelity/cleanup/`): one process per test, CnaGamerServicesTests 604 + 1
  known skip, CnaNetTests 518/518, CnaRuntimeTests 192 + 2 environment skips; CnaTests in one
  process (less the content-pipeline differential) 9,024 pass with the same 10 HEADLESS
  graphics/content failures; `CApi_*` 107/110 (the three standing audio/content smokes), coverage
  inventory regenerated (header comments), ABI baseline current (3,202 exports); protocol drift and
  platform gates clean; `GamerServices_AvatarCatalogUpToDate` passes; server corpus 28/28 with every
  CNA harness, no skips; the 18 GamerServices/Net demos exit 0 on the private display; SAMPLE-096,
  SAMPLE-075 (SystemLink and online), SAMPLE-087 (16 catalog v3 avatars with shadows) and the
  achievements/leaderboards program pass (`*-20260930-cleanup`). Committed `next` had advanced by
  six commits (0f7166cd8, BINDFIX-037..041, local and not pushed); it was merged into the branch
  (65c1d21e3; only the coverage-inventory hash conflicted, regenerated) and everything above was run
  again with the same results (`cleanup/after-merge/`, samples `*-20260930-cleanup-merged`): GS
  604 + 1, Net 518/518, Runtime 192 + 2, CnaTests 9,026 with the same 10 failures, C API 111/114
  (next's four new tests pass; the same three smokes fail), server 28/28, 18 demos, four samples.
  `next` then advanced again (e6d562454, BINDFIX-042..046, including an avatar-loader shutdown fix)
  and was merged too (829bb5c7f, no conflict); rerun (`cleanup/after-merge2/`): GS 604 + 1, Net 518/518,
  Runtime 192 + 2, C API 112/115 (the same three smokes), 18 demos, SAMPLE-087 and SAMPLE-096, and the
  server's `service_tls_e2e`, `service_cna_session` and `service_cna_avatars`. Then 4a31b3b7d
  (BINDFIX-047, C API gamer routes accept a network gamer) was merged too; C API 112/115 and
  `service_tls_e2e` (the C harness) pass.

## Acceptance matrix (GSX-Q1, 2026-09-30)

CNA `feature/gamer-services-server`, server `feature/gamer-services-server` 2c974d1, cna-samples
`feature/gamer-services-samples` 378483b (unchanged). Pushed as feature branches on 2026-09-30, then
merged into CNA `next` (7d7141c6b) with sharp-runtime `next` (e03e8465) the same day; the server and
cna-samples branches have no `next` and stay as they are.

| Requirement | Result | Evidence |
|---|---|---|
| Avatars: server-owned description, client-owned catalogs, packs only as update/fallback | met | GSX-A1..A5; `AvatarCatalogStoreTests`, server avatar e2e |
| Catalog immutability; v2 frozen, new art in v3 | met | `CatalogV2IsFrozen`, `GamerServices_AvatarCatalogUpToDate` |
| Avatar fidelity beyond v2, inspected on the real renderer | met, occlusion open | `art/after-b2/` (257 jobs, 0 failures), `art/before-after.png`, `art/hands-before-after.png` |
| Old descriptions and formats preserved | met | format 1/2 codec tests, v1/v2 golden fixtures |
| Console-style Guide, sign-in, gamer card, friends, invitations, achievements, leaderboards, party, content, editor | met | GSX-U1..U9; `ui/after6/`, `ui/sequence/`, `ui/before-after-*.png` |
| Register gaps closed or decided | met | GSX-E1..E7, final register |
| No public CNA machinery added to Microsoft.Xna GamerServices/Net | met | diff of the public headers since e46db9822: private members and friends only; `setPartySizeProperty` removed |
| Tests | met | per process: CnaGamerServicesTests 603 + 1 known skip, CnaNetTests 515/515, CnaRuntimeTests 192 + 2 environment skips; CnaTests (all modules, less the content-pipeline differential) 9,018 pass, 10 HEADLESS graphics/content failures untouched by this pass |
| C API | met | CApi_* 107/110 (the three standing audio/content environment smokes); coverage/limitations/release-gate current (650 planned, as at the start); ABI baseline current (3,202 exports) |
| Server | met | corpus 28/28 with every CNA harness; capacity re-run (README) |
| Samples | met | SAMPLE-087 AvatarShadows (v3 avatars), SAMPLE-096 Invites, SAMPLE-075 SystemLink and online, achievements/leaderboards program: `*-20260930-xbox-fidelity` |
| Demos | met | 18 GamerServices/Net demos exit 0 on the private display, two consecutive runs (`final/demos*.txt`) |
| Platform gates, protocol drift | met | gates pass except the `SDL_TOUCH_MOUSEID` classification inherited from `next`; drift clean |
