# CNA C API Ownership and Lifetime

Ownership belongs to the route that supplies a handle or pointer. A handle's typedef or numeric
value does not tell the caller whether to destroy it. The declarations and family contracts must
state the ownership, validity window and matching release operation.

## Ownership categories

| Category | Caller obligation |
|---|---|
| Owned handle | Release once with the family's destroy, close or unsubscribe route. Copying its bits does not acquire another reference. |
| Owned view handle | Release once even though the native object belongs to another object. The view's documented retention/parent-release rules apply. |
| Borrowed token or pointer | Use only within its documented validity window; do not destroy it or extend its lifetime by saving its bits. |
| Retained input | CNA keeps a documented reference or value copy; the caller still owns its original handle. |
| Transferred input | Apply only where a declaration explicitly consumes ownership; do not infer transfer from a successful setter. |
| Callback context | Keep the function and caller-owned context valid for the documented invocation or registration lifetime. |
| Caller buffer | Keep storage valid for the call. CNA copies or retains data only as the individual route documents. |

There is no public generic retain/release route and no public runtime handle. Successful typed
release normally invalidates that registry entry immediately; a repeated release returns
`CNA_RESULT_INVALID_HANDLE`. A failed destroy normally preserves the handle, but game shutdown
can complete and return `CNA_RESULT_CALLBACK`; see [ERRORS.md](ERRORS.md).

## Parents, devices and aliases

The process permits one C-owned game and multiple independently owned graphics devices.
`cna_game_get_graphics_device` lends a game device during a callback or before the first run;
`cna_graphics_device_create` returns an owned independent device. The loan windows are defined in
[RENDERERS_AND_CAPABILITIES.md](RENDERERS_AND_CAPABILITIES.md#borrowed-graphics-device-handle).

A graphics resource created through a loan can survive that callback. It remains a tracked game
child and must be released before the game. A resource created on an independent device instead
belongs to that device and does not block destruction of an unrelated game. Destroy device
resources before their device. Same-device checks use creating-device identity, not merely the fact
that two objects belong to one process.

Family adapters enforce parent retention, release guards and native disposal; the handle registry
itself does not encode a universal parent graph. Do not assume that every child is deep-copied,
that a parent token remains valid while native state is retained, or that every view blocks parent
release. Current exceptions and defects are recorded in [KNOWN_ISSUES.md](../../KNOWN_ISSUES.md).

Representative rules are:

| Family | Lifetime rule |
|---|---|
| Texture2D / SpriteBatch | Device-bound objects survive a game callback. Submitted textures are retained until a successful End or batch destruction cancels deferred work; already submitted immediate rendering cannot be undone. |
| Effect descendants | Collection/parameter/technique/pass views are independently owned handles retaining the effect resource; each needs its own release. |
| Model graph | Caller-created graph objects and published content-owned tokens have different destruction rights. A loaded Model's child resource tokens must not be destroyed separately. |
| SkinnedModelEXT parts | Attached buffers, mesh part and optional texture remain caller-owned but cannot be destroyed/disposed while retained. A part query can return a new owned alias; an optional texture query returns the protected existing token. |
| ContentManager | Every returned C handle needs cleanup after Unload. Loaded textures, fonts, sounds and videos remain usable; loaded effects and model resources are natively disposed. A valid token does not guarantee continued drawing. |
| SoundEffect / instance | Release instances before their effect, and game-owned audio objects before the game. Mixer state retains no caller PCM buffer after creation. |
| Network roster / receive sender | Each returned view handle must be destroyed and blocks its session's release. Callback gamer tokens are temporary loans. Standalone gamers and nonempty machine copies retain their session and block its disposal/release in the same way; see NET.md. |
| Storage | Close streams before containers, then containers before devices. These resources do not require a game. |

The detailed sources of truth are [EFFECTS.md](EFFECTS.md), [MODELS.md](MODELS.md),
[CONTENT.md](CONTENT.md), [AUDIO.md](AUDIO.md), [NET.md](NET.md) and [STORAGE.md](STORAGE.md).
Ordinary handle calls use their creation thread unless a documented exception or opt-in dispatch
applies; see [CALLBACKS_AND_THREADING.md](CALLBACKS_AND_THREADING.md).

## Values, arrays and caller contexts

POD values cross by value or caller-owned output storage; the adapter may allocate native scratch
storage internally. Input string/array storage is borrowed for the call unless explicitly retained.
Output text and arrays use [STRINGS_AND_BUFFERS.md](STRINGS_AND_BUFFERS.md).

Opaque caller object/context pointers remain caller-owned unless an explicit release callback
contract says otherwise. Model owned-resource contexts, for example, require a context/callback
pair or neither; release follows the last shared resource bundle, which can outlive one Model
handle. A generic C tag stores only a token and never manages its lifetime.

## Events and disposal

An owned registration handle has its own release obligation. Source retention and destruction
behavior differ by event family: some keep the source alive, some use weak references, and static
events belong to the process. Game/window subscriptions do not block game destruction; they become
detached and their registration handles may be released afterwards. Follow each family's rule
instead of assuming universal automatic cleanup or synchronization.

A callback-only handle or pointer is valid only during invocation. Copy needed values before
returning; retaining its numeric token does not turn it into an owned view. Callback functions must
return normally, and their contexts must remain alive while callbacks can still run.

Native `Dispose` and C handle release are separate concepts. A permitted generic graphics-resource
dispose leaves its handle available for disposal queries and typed cleanup, while invalidating
native rendering state. Some families expose disposal without release; others combine both.
A native value may still use a C handle when identity, mutation or enumeration requires it.
The C API does not require the caller to reproduce C++ RAII or Sharp Runtime ownership rules.

## Verification boundary

`CApi_HandleRegistry`, `CApi_StressSmoke`, lifecycle/teardown tests and the family smoke tests
exercise distinct lifetime rules. Registry stale-token tests alone do not establish every native
parent/disposal contract.
