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
