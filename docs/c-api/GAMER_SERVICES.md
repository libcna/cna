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

## The gamer, and what a C caller can hold of one

Every `cna_gamer_*` route accepts **either handle kind** — a gamer or a signed-in gamer — because the
canonical surface belongs to the base both derive from. That is why the test drives the same routes
twice, once through each.

A gamer's **tag** is a caller-owned 64-bit value, not the canonical boxed one. C cannot open a box, so
what it gets is a pointer-sized integer it owns the meaning of — the same choice the graphics
resources already made. A tag this ABI never wrote reads back as zero rather than as a reinterpreted
value.

### Asynchronous routes are one call that still runs the callback

The canonical API pairs `GetX` with `BeginGetX`/`EndGetX`, and every one of those operations here
**completes before `Begin` returns**. So each pair is one C route that produces the answer and then
invokes the completion callback, and the callback receives only the caller's context: no operation
object crosses this ABI. Passing a null callback is fine when the caller only wants the answer.

Two of those operations cannot succeed at all: **looking a gamer up by tag and requesting a partner
token both answer `CNA_RESULT_NOT_SUPPORTED`**, because the canonical implementations refuse outright
on every runtime this ABI builds on. The refusal is the answer rather than a gap, and the callback
does not run when the operation is refused.

### Values, and one object that earns a handle

Presence and privileges are fixed values read from a gamer. Presence is written back **whole**,
because the canonical API hands out a mutable object rather than taking a new one. The privileges keep
the canonical shape rather than flattening it: three of the seven are graded — communication, profile
viewing and user-created content each answer *how widely* — and four are yes-or-no.
`cna_signed_in_gamer_set_presence_mode_string_ext` is the exception that carries no information: the
canonical extension accepts the text and **stores nothing**, and the test asserts the structured
presence is unchanged afterwards so the no-op is visible rather than assumed.

A **profile** earns a handle, because it is disposable and carries a stream. Its numbers are one
snapshot; its motto and its region *name* are count/copy pairs. The gamer picture follows the
availability-separate-from-the-answer rule and is never present here: a clear flag and a zero size,
reported as an ordinary success.

### Friends, and the collection that holds them

A friend is a gamer handle with a friend behind it, so the base routes work on one and the friend-only
routes refuse an ordinary gamer. Its twelve predicates are one snapshot. **A friend's presence is free
text while a signed-in gamer's is a mode and a value** — a canonical asymmetry, preserved rather than
evened out.

No runtime this ABI builds on has a friend service, so `cna_signed_in_gamer_get_friends` answers an
**empty collection**, which is a success and not a refusal, and `cna_signed_in_gamer_is_friend` always
answers negatively. `cna_friend_gamer_create_ext` and `cna_friend_collection_create_ext` map the
canonical factories and are also the only way a C caller obtains a friend to exercise the surface
against — the same shape the sensors' test backends already use.

A collection **keeps every gamer handle it holds alive**, because the canonical collection stores
pointers it does not own, and an index or a cursor hands back the same handle the caller published
rather than a new view of it. The canonical `begin`/`end` pair has **no C form at all** — a C++
iterator has no fixed size, no stable representation and no way to be compared from C — so
`cna_gamer_enumerator_*` and the index routes are how a C caller walks a collection. Removing a gamer
the collection does not hold is a no-op that reports success, which is what the canonical operation
does.

The signed-in collection is **process-wide and gets no handle**, the same rule the display metrics,
the component collection and the game window already follow. Its player-index lookup is
**positional**: it reads the collection at that index rather than searching for the gamer whose own
player index matches, so one published gamer answers at `CNA_PLAYER_INDEX_ONE` whatever index it was
created with.

### Two things with no C form at all

`Gamer::GamerAction` is the canonical asynchronous-result object, and no operation object crosses this
ABI — every begin/end pair is one synchronous route, so there is nothing to hold between two halves
that no longer exist. And the protected members of `Gamer` and `GamerCollection` are unmappable for
the reason already settled for sensors and windows: **a protected member is mappable only when this
ABI supplies a derived class to hang it on**, and it supplies none here.

## The guide: two real screens and thirteen that do nothing

The guide is a static class with a deleted constructor, so it has no handle: every route is a free
`cna_guide_*` function.

**Thirteen of its screens are no-ops on this runtime.** Compose message, friend request, friends,
both game-invite forms, gamer card, marketplace, messages, party, party sessions, player review,
players, sign-in and achievements all validate their arguments and do nothing, and so does
`cna_guide_delay_notifications` — there is no notification system to delay. The routes exist because
the canonical API does, and because a platform that grows these screens would need them. A bad player
index or an invalid gamer handle is still refused, because argument validation is the boundary's job
whether or not anything downstream uses the value.

**Two screens are real, and they are real because this ABI draws them.** The on-screen keyboard and
the message box have no system overlay behind them here, so the runtime renders them and the game
supplies the surfaces — that is what `cna_guide_render_pending_keyboard_input_ext` and
`cna_guide_render_pending_message_box_ext` are for, and why the pending title, description, display
text and focused button are readable at all.

### The one operation that is genuinely deferred

Every other asynchronous pair in this ABI completes before its begin route returns. **These two do
not.** `cna_guide_begin_show_keyboard_input` leaves an input pending and returns; the completion
callback runs only when the user confirms or cancels, and the same is true of the message box. Poll
`cna_guide_get_has_pending_*_ext` or wait for the callback.

Only one of each may be pending at a time — a second start is refused and leaves the first alone —
and the end routes take no operation argument because the C layer keeps that one operation itself.
That is also what stops the canonical operation leaking when a caller never asks for its answer.

Two behaviors are worth naming. **A cancelled keyboard input carries no text at all**: the canonical
implementation clears what was typed, so `cna_guide_was_keyboard_input_canceled_ext` is the only way
to tell a cancellation from a caller who confirmed an empty string. And **discarding is not
completing**: `cna_guide_reset_pending_*_ext` throws the operation away and runs no callback, which is
what makes it the right thing for a game tearing down a screen and the wrong thing for a game
answering one.

### The dispatcher and the one canonical component

The dispatcher is static too. `cna_gamer_services_dispatcher_initialize` takes a game handle and hands
the dispatcher that game's service container, which is the only service provider a C caller has. The
window handle passes through as an integer without being interpreted — nothing in this runtime reads
it.

`cna_gamer_services_component_create` answers an **ordinary game-component handle**: every
`cna_game_component_*` route accepts it. It is the first canonical component this ABI publishes —
every other one is derived from a caller's callback set — so its initialize and update behavior belong
to the runtime rather than to C.

## Achievements: a value, a collection of copies, and something that persists

An achievement is an **owned handle over a value**. Its numbers and flags are one snapshot; its key,
name, description and how-to-earn text are count/copy pairs. `cna_achievement_equals` is the single
route behind both canonical equality operators, and achievements compare **by value across every
field** rather than by identity — which is exactly what makes a handle a collection answered
comparable with one the caller built.

The collection **copies what it is given**, so releasing a source handle afterwards changes nothing
in it, and **both indexers answer a copy rather than a view**: the canonical reference points into
storage that a later insert or remove would invalidate, and copying a value loses nothing. Both
canonical indexers are mapped — by position and by key. Removal here **answers whether anything was
found**, unlike the gamer collection's, because the canonical operation does. `begin`/`end` have no C
form for the reason already settled: a C++ iterator is not expressible across a C ABI.

Two absences differ, and the difference is reported rather than flattened. The **gamer picture** is
*absent* — a clear flag and a zero size, an ordinary success. The **achievement picture** is
*unimplemented* — the canonical accessor says so outright, so `cna_achievement_get_picture_size`
answers `CNA_RESULT_NOT_SUPPORTED`. A caller can tell "there is none" from "this runtime cannot".

`cna_signed_in_gamer_get_achievements` is the one gamer-services read on this runtime that finds real
data: it answers what `cna_signed_in_gamer_award_achievement` **persisted**, so an achievement earned
in one process run is still there in the next. Each entry carries only a key, an earned flag and a
timestamp — no catalog exists here to supply a name, a description or a score, and the binding says so
rather than inventing them. The asynchronous form is one synchronous call that still invokes the
callback, because the canonical read marks itself complete before `Begin` returns precisely so a game
does not spin waiting for work that is already done.

## A variant map across a C ABI

`PropertyDictionary` is a string-keyed map of **boxed** values, which is the one shape a C ABI cannot
carry directly. What crosses instead is a **typed family plus a kind query**:

1. `cna_property_dictionary_try_get_value_kind_ext` says whether a key is present and, if so, which of
   nine kinds the slot holds.
2. The matching typed getter reads it.

That pair is what replaces the canonical indexers, `Add` and `Values`, all of which hand out or take a
box — those four have **no C form**, and neither do `begin`/`end`, for the reason already settled. The
replacement loses nothing that matters: every value the canonical getters themselves understand is one
of the nine kinds, so a caller can reach every usable state through the typed setters.

**Every typed getter checks the kind at the boundary.** Reading a text slot with the integer getter is
`CNA_RESULT_INVALID_STATE`, not the generic internal failure the canonical unboxing would produce — a
caller can act on "that key holds something else". An unknown key is `CNA_RESULT_INVALID_ARGUMENT`, a
deliberately different answer.

Keys are walked by index in ascending order, because the canonical key list and the canonical bulk copy
both answer containers C cannot receive. And one canonical contradiction is reported rather than
corrected: **the dictionary describes itself as read-only and is nonetheless writable** — setting,
removing and clearing all work, so a caller that trusted the flag would be surprised by the routes
rather than by the binding.

`CNA_GameDefaults` is the ordinary case beside it: a fixed value with no behavior, so no handle. Its
two colors are **optional**, and the structure keeps that — a flag beside each says whether the gamer
chose one at all, which is a different thing from having chosen black.
