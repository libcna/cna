# Gamer services in the C API

Gamer services is the last module the binding covers, and the one where the honest answer differs
most from what the canonical API describes. **On every verification tree there is no signed-in gamer
and no live service.** Where the canonical API answers a value, the C route answers an ordinary
success with the flag clear; where it throws, the C route reports a documented refusal. Neither
pretends a gamer exists. That is the same rule the absent compass and the absent microphone already
follow: availability is separate from the answer.

## Identities come first, because they need nothing

The gamer and guide identities are fixed-width `uint32_t` values with one macro per canonical value
at its canonical ordinal and a `_MAXIMUM`:

`CNA_GamerPresenceMode`, `CNA_NotificationPosition`, `CNA_GamerZone`, `CNA_LeaderboardKey`,
`CNA_LeaderboardOutcome`, `CNA_MessageBoxIcon`, `CNA_ControllerSensitivity`, `CNA_GameDifficulty`,
`CNA_GamerPrivilegeSetting` and `CNA_RacingCameraAngle`.

None of them needs a gamer, a service or a handle, which is why they land before anything that reads
one. A caller can build and compare presence, difficulty and privilege values with nothing signed in.

**`CNA_GamerPresenceMode` carries all sixty canonical values, `CornflowerBlue` included.** That value
is the framework's own joke, and it is a real presence mode a game may set — dropping it would move
no ordinal but would leave a C caller unable to name something the canonical API accepts. The
identity ends there, so `CNA_GAMER_PRESENCE_MODE_MAXIMUM` is `CNA_GAMER_PRESENCE_MODE_CORNFLOWER_BLUE`.

### What the test actually proves

`GamerIdentitiesSmoke.c` writes every value of all ten identities out **in canonical order** and
asserts each one sits at its own index. That is deliberately stronger than checking a handful of
ordinals: it catches a value inserted or removed in the middle, which moves every later ordinal and
breaks the ABI, while a rename moves nothing and is caught by the compile instead. C and C++ ABI
assertions pin the width, the first and last ordinals and the maximum of each identity separately.

## The avatar identities, and the one sparse enumeration

`CNA_AvatarBodyType`, `CNA_AvatarRendererState`, `CNA_AvatarEyebrow`, `CNA_AvatarEye`,
`CNA_AvatarMouth` and `CNA_AvatarAnimationPreset` follow the same shape as the gamer identities: one
`uint32_t` each, a macro per canonical value at its canonical ordinal, a `_MAXIMUM`.

**`CNA_AvatarBone` does not.** The canonical skeleton numbers its bones sparsely — fifty-five bones
spread over ordinals 0 to 70, with gaps — and those gaps are preserved rather than renumbered. A bone
index is what an avatar animation stores, so closing the gaps would silently repoint every animation
onto the wrong joint. The test pins each bone against its exact canonical ordinal rather than against
its position in a list, and additionally asserts the sequence is strictly ascending, so a duplicate or
a reordering fails as well.

### Two name routes with no gamer in sight

`cna_avatar_animation_preset_get_clip_name_size_ext` / `..._copy_clip_name_ext` and
`cna_avatar_body_type_get_content_name_size_ext` / `..._copy_content_name_ext` are count/copy pairs
over an identity. They take **no game handle and no thread affinity**, because they are pure value
operations — the same shape the static sample computations in the audio surface already use.

The two answer different kinds of string, and the test asserts that difference rather than assuming
it. A clip name is the identity's **own canonical spelling** (`CNA_AVATAR_ANIMATION_PRESET_WAVE` is
`"Wave"`), because that is what the offline asset pipeline writes into a skinned model. A body-type
name is a **content path** (`"avatar/male/avatar"`), because it is what a content manager loads. An
undefined identity is refused at the boundary, so the diagnostic names the identity rather than
surfacing a generic argument failure from underneath.

## Six exceptions, four answers

The gamer-services exception types are not bound; their **conversion** is, in the same exception
firewall the audio exceptions already go through. The six of them answer four different results,
because the differences are what a caller acts on:

| Canonical exception | Result / category | Why |
|---|---|---|
| `GamerServicesNotAvailableException` | `CNA_RESULT_NOT_SUPPORTED` / `NOT_SUPPORTED` | The platform has no gamer services at all. Nothing the caller supplies or retries changes it. |
| `GameUpdateRequiredException` | `CNA_RESULT_NOT_SUPPORTED` / `NOT_SUPPORTED` | The service is there and refuses this build of the title. Also unchangeable by the caller — only shipping a new build fixes it. |
| `NetworkNotAvailableException` | `CNA_RESULT_INVALID_STATE` / `STATE` | The network is a resource that is simply not there right now, the same shape a disconnected storage device already has. |
| `NetworkException` | `CNA_RESULT_PLATFORM` / `PLATFORM` | A network operation failed while the network was available: a native service failed, and a retry may get past it. |
| `GamerPrivilegeException` | `CNA_RESULT_INVALID_STATE` / `STATE` | The operation is supported and well-formed; this gamer's privileges do not currently allow it, and privileges are per-gamer state that can change. |
| `GuideAlreadyVisibleException` | `CNA_RESULT_INVALID_STATE` / `STATE` | The guide is already showing. |

**The arm order is load-bearing, and it crosses a module boundary.** The networking module's
`NetworkSessionJoinException` derives from *this* module's `NetworkException`, so the two modules
share one exception hierarchy. The join arm and the not-available arm both sit before their common
base; get that wrong and an absent network becomes indistinguishable from a network operation that
failed while connected, and a join failure loses the join-error code it carries. The boundary test
throws all three and asserts they stay distinguishable.
