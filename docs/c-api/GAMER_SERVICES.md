# Gamer services in the C API

`CNA/C/gamer_services.h` exposes CNA's gamer-services surface to strict C. Canonical XNA operations
forward to the native GamerServices implementation; `_ext` routes supply C-specific construction,
inspection, testing or deferred-UI control where the XNA shape cannot cross a C ABI directly.

With a configured [CNA account service](../gamer-services-server.md), sign-in, profiles, gamer
lookups, friends, achievements, leaderboards and Guide account pages use that service. Without one,
signed-in gamers are local offline profiles and service-directory operations either return an empty
or absent result where the canonical API permits one, or the documented refusal where it throws.
The service is CNA-owned and is not an Xbox LIVE-compatible endpoint.
The exact remaining native limitations are maintained in
[gamer-services-known-limitations.md](../gamer-services-known-limitations.md); this page describes
only the C boundary.

## Evidence boundary

The strict-C smoke tests are deterministic boundary tests. They run without a configured account
service and use offline or `_ext`-constructed gamers where a positive fixture is needed. They prove
argument validation, result mapping, ownership, callbacks, collection behavior and the no-service
answers; they do not by themselves qualify a deployed service.

`cna_c_api_service_client` is the manual configured-service client. It signs in, reads profile and
achievement pictures, reads and pages a leaderboard and obtains a remote gamer from an entry. It is
built with the harnesses but is not registered as a CTest. Native service qualification and the
current platform matrix remain documented by the two gamer-services pages linked above.

## Fixed identities

The gamer and Guide identities are fixed-width `uint32_t` values with one macro per canonical value
at its canonical ordinal and a `_MAXIMUM`:

`CNA_GamerPresenceMode`, `CNA_NotificationPosition`, `CNA_GamerZone`, `CNA_LeaderboardKey`,
`CNA_LeaderboardOutcome`, `CNA_MessageBoxIcon`, `CNA_ControllerSensitivity`,
`CNA_GameDifficulty`, `CNA_GamerPrivilegeSetting` and `CNA_RacingCameraAngle`.

`CNA_GamerPresenceMode` includes all sixty canonical values, including `CornflowerBlue`; its maximum
is therefore `CNA_GAMER_PRESENCE_MODE_CORNFLOWER_BLUE`.

The avatar identities use the same fixed-width form:

`CNA_AvatarBodyType`, `CNA_AvatarRendererState`, `CNA_AvatarEyebrow`, `CNA_AvatarEye`,
`CNA_AvatarMouth` and `CNA_AvatarAnimationPreset`.

`CNA_AvatarBone` is deliberately sparse. The 55 named bones retain their canonical ordinals from 0
through 70, including the gaps, because animation data stores bone indices. The identity smoke tests
pin every value and ordinal; the C and C++ ABI tests separately pin widths and boundary values.

## Result and exception mapping

Gamer-services exception objects do not cross the ABI. The shared exception firewall maps them to
stable results and diagnostic categories:

| Canonical exception | C result | Diagnostic category |
|---|---|---|
| `GamerServicesNotAvailableException` | `CNA_RESULT_NOT_SUPPORTED` | `NOT_SUPPORTED` |
| `GameUpdateRequiredException` | `CNA_RESULT_NOT_SUPPORTED` | `NOT_SUPPORTED` |
| `NetworkNotAvailableException` | `CNA_RESULT_INVALID_STATE` | `STATE` |
| `NetworkException` | `CNA_RESULT_PLATFORM` | `PLATFORM` |
| `GamerPrivilegeException` | `CNA_RESULT_INVALID_STATE` | `STATE` |
| `GuideAlreadyVisibleException` | `CNA_RESULT_INVALID_STATE` | `STATE` |

The derived networking exceptions must be caught before `NetworkException`.
`NetworkSessionJoinException` retains its join-error payload, and
`NetworkNotAvailableException` remains distinguishable from an available network whose operation
failed. `CApi_BoundaryDetail` exercises that ordering. The general diagnostic contract is in
[ERRORS.md](ERRORS.md).

## Handle and value ownership

The public header is authoritative for each route, but the main lifetime shapes are:

| Surface | C representation and ownership |
|---|---|
| Presence, privileges, game defaults, expressions and leaderboard identities | Fixed values copied through caller-owned structures. |
| Gamers, signed-in gamers, profiles, collections and enumerators | Handles; release them with the matching destroy route. A collection retains the gamers it contains. |
| Process-wide signed-in collection | No collection handle. Element lookups return the already-published signed-in handle, not a new ownership unit. |
| Achievement collection elements | New owned achievement copies. |
| Leaderboard reader elements | New owned entry snapshots. The columns handle aliases its entry and keeps that entry alive. |
| Avatar descriptions, animations and renderers | Owned handles. A renderer retains its description. |

Handles returned by gamer-collection indexing or enumeration are borrowed and remain valid while the
collection lives. Destroying a signed-in gamer is refused while the process-wide collection still
retains it.

## Gamers, profiles and friends

The base `cna_gamer_*` routes accept gamer, friend-gamer and signed-in-gamer handles because those
native objects share the canonical `Gamer` surface. Only the matching owner route may destroy a
handle.

A gamer's C tag is an opaque caller-chosen `uint64_t` token. `cna_gamer_set_tag` stores exactly that
type in the canonical box; `cna_gamer_get_tag` returns zero if the box contains a value that was not
written by this C ABI. The token is fixed at 64 bits and is not defined as a native pointer.

Profiles are owned handles because they are disposable and may carry a picture stream. Scalar
profile fields are snapshots; motto and region name use size/copy pairs. Picture presence is
reported separately from size: no picture is a successful clear flag with zero bytes.

Presence and privileges remain canonical structured values. Three privilege fields are graded and
four are Boolean. `cna_signed_in_gamer_set_presence_mode_string_ext` sets free presence text, which
is published on a later dispatcher update.

Gamertag lookup is a real account-service directory operation. Without a configured service it
returns `CNA_RESULT_NOT_SUPPORTED`; this is not evidence that lookup is globally unsupported.
Partner-token routes always return `CNA_RESULT_NOT_SUPPORTED`, because CNA does not issue Xbox LIVE
partner tokens.

`cna_signed_in_gamer_get_friends` returns the service friend list for a service account and an empty
collection for an offline profile. `cna_friend_gamer_create_ext` and
`cna_friend_collection_create_ext` construct deterministic C-owned fixtures. A collection retains
every gamer it contains, and its index and enumerator routes return borrowed handles to those same
objects. Removing an absent gamer is a successful no-op.

The signed-in collection is process-wide. Positional lookup and player-index lookup are separate:
`cna_gamer_get_signed_in_gamer_at` uses collection position, while
`cna_gamer_get_signed_in_gamer_at_player_index` matches the gamer's `CNA_PlayerIndex` and reports
absence with a successful clear flag.

### Waiting asynchronous routes

The gamer, achievement and leaderboard `begin` routes, plus
`cna_avatar_description_get_from_gamer`, combine the canonical Begin/End pair into one waiting C
call. The route pumps dispatcher progress where needed, writes the answer, then invokes the callback
with the caller context. A null callback is allowed when only the answer matters. An immediate
refusal does not invoke the callback, and no asynchronous-operation object crosses the ABI.

This waiting form is intentionally different from the two deferred Guide workflows below.

## Guide and dispatcher

Guide is static, so its routes are free `cna_guide_*` functions rather than handle methods. Page
routes retain the native XNA checks. Service-backed pages require the appropriate initialized
service and signed-in player; invalid player indices and gamer handles are rejected at the boundary.
`ShowMarketplace` opens CNA Guide's Game content page after those checks. The string overload of
`ShowGameInvite` remains unsupported, matching the native contract.

Keyboard input and message boxes are genuinely deferred. Their begin routes leave one operation of
the corresponding kind pending and return; the callback runs only after completion or cancellation.
Starting a second operation of the same kind is refused without replacing the first. C hosts may
poll the pending-state routes and use the `_ext` inspection/render routes to draw the UI themselves.

A cancelled keyboard operation exposes no result text; the cancellation flag distinguishes it from
a confirmed empty string. Resetting a pending keyboard or message-box operation discards it and does
not invoke its callback. Reset is teardown, not completion.

`cna_gamer_services_dispatcher_initialize` binds the dispatcher to a game and its service container.
The window handle is transported as an integer and is not interpreted by the C layer.
`cna_gamer_services_component_create` returns an ordinary game-component handle, so the common
`cna_game_component_*` routes operate on it.

## Achievements

An achievement is an owned handle over a value. Text uses size/copy routes; equality compares every
field rather than handle identity. Synthetic `create_ext` values and offline catalog/store values
have `earned_online == false`. A service value reports true only when the service records an
authenticated award.

Achievement collections copy their input values, and both indexers return new owned copies. A
missing key returns `CNA_RESULT_INVALID_ARGUMENT` and an invalid output handle. Collections are
read-only even when created with `cna_achievement_collection_create_ext`: add, insert, remove,
remove-at and clear return `CNA_RESULT_NOT_SUPPORTED` after handle validation. That refusal occurs
before index or membership validation, and the scalar output of remove is left unchanged.

An absent gamer-profile picture is an ordinary absent value. An achievement picture instead belongs
to the service catalog or the PNG named by `GamerServices/Achievements.json`; if none is available,
the canonical exception maps to `CNA_RESULT_NOT_SUPPORTED`.

`cna_signed_in_gamer_get_achievements` reads the service catalog for a service account. Offline it
uses the title catalog when present, otherwise the persisted keys awarded through
`cna_signed_in_gamer_award_achievement`, without invented text or score.

## PropertyDictionary and GameDefaults

`PropertyDictionary` stores boxed native values, so C uses a kind query followed by a typed getter.
`cna_property_dictionary_try_get_value_kind_ext` distinguishes an absent key from the nine recognized
kinds: date/time, double, `int32`, `int64`, leaderboard outcome, single, stream, string and time span.
An unrecognized boxed type reports `UNKNOWN`.

Eight kinds have typed setters. Stream is inspection-only at this boundary: C can identify a stream
and read its presence and length through `cna_property_dictionary_get_stream_size_ext`, but cannot
create, replace or copy its contents through this API. The canonical box-taking indexers, `Add`,
`Values` and C++ iterators have no direct C representation.

A typed getter returns `CNA_RESULT_INVALID_ARGUMENT` for an absent key and
`CNA_RESULT_INVALID_STATE` when the key holds another kind. Keys are enumerated by ascending index.
The native dictionary reports itself read-only but supports set, remove and clear; the C boundary
preserves that canonical behavior rather than enforcing the flag.

`CNA_GameDefaults` is a versioned value structure. Each preferred color has a separate presence flag,
so an unset preference remains distinguishable from black.

## Leaderboards

A leaderboard identity is a value with an inline key. A missing terminator or a key larger than the
inline capacity is rejected, never truncated. Read and begin-read routes return only after the page
is available; callback forms use the waiting model described above. Service readers page through the
service, while offline reads use the local store.

Reader entries are owned snapshots. Changing a snapshot's rating does not write back to the board.
Its columns handle aliases the entry's own `PropertyDictionary`, so changing a column does change the
snapshot and the columns handle retains the entry. The rating-changed callback is one replaceable
hook per entry, not a subscription handle.

`LeaderboardWriter` has no C form. The native writer captures its owning gamer's address and cannot
be safely published from the copied gamer values used by this ABI. Exposing it would create a
dangling owner, so score submission remains intentionally absent. The C reader routes also release
the native asynchronous operation after a synchronous read instead of reproducing the native
operation leak.

## Avatars

`CNA_AvatarExpression` is a fixed value and is validated when used. An avatar description is an owned
handle created from exactly `CNA_AVATAR_DESCRIPTION_BYTE_COUNT` bytes; byte validity is reported
separately. Random descriptions are locally generated CNA avatars. Unreadable bytes remain invalid,
report height zero and use the canonical fallback body type.

An animation created from a preset is a real clip with a duration, expression keys and 71 bone
transforms. The canonical animation interface has no separate C implementation hook; the animation
handle is the concrete object accepted by renderer draw routes.

An avatar renderer retains its description. Transform and lighting groups use versioned structures
with per-field presence flags. The renderer is unavailable for an unreadable description, otherwise
progresses through loading to ready. Bind-pose reads require ready state. Drawing also requires gamer
services initialized with a graphics device. The current ABI exposes the standard avatar renderer
surface only; removed extension history belongs in [ABI_VERSIONING.md](ABI_VERSIONING.md), not in
the live contract.

## Deliberately absent C shapes

The following native shapes do not cross this ABI:

- canonical asynchronous-result objects, because waiting routes consume them internally and Guide
  owns its single deferred operation;
- C++ `begin`/`end` iterators, because their representation and comparison are not stable C ABI;
- protected gamer and collection members, because the C API publishes no derived type on which they
  could operate;
- arbitrary boxed `std::any` values and a C-implemented avatar animation interface;
- `LeaderboardWriter`, for the lifetime reason above.

These are representation or safety boundaries, not untracked omissions. Other incomplete native
features and service differences must remain in
[gamer-services-known-limitations.md](../gamer-services-known-limitations.md).
