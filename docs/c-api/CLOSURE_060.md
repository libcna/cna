# ABI 0.60 owner-contract closure

The project owner's decisions on the seven owner-contract coverage rows of ABI 0.60
(MTH-CABI-009). ABI 0.60 is additive: three new routes, unchanged layouts.

## Owner-approved dispositions

| Exact IDs | Decision |
|---|---|
| `CPP-A6A4484C5EFD`, `CPP-A06AE344A147` | Owner-approved **partial**: “C Add/Insert cover explicit-handle membership: destroying the original handle unregisters membership; they do not expose the native shared_ptr overload's collection-owned lifetime or permit release of the client token while retaining membership.” |
| `CPP-921CC48EF0E4`, `CPP-9F69184480D2` | Owner-approved **no-C-form**: guest/private-slot state is managed by the network transport/session. C callers read it through the existing getters. |

The approvals are pinned to those four singleton IDs in tools/c-api/coverage_mappings.json;
the partial approval records the exact subset in its limitation object. The two transport-state
dispositions follow the earlier exact-row no-C-form decisions. Existing Add/Insert ownership and
guest/private read routes retain their behavior.

## Native shared machines

`cna_network_gamer_get_shared_machine_ext` / `cna_network_gamer_set_shared_machine_ext` implement
`CPP-574584031EA4` / `CPP-05AB6F76F931` through native GetSharedMachine/SetSharedMachine. Each
owned machine alias retains the native shared_ptr and independently guards its session. Releasing
the original gamer or machine token does not end another native owner's identity. Native roster
changes are observable through sibling aliases. Reassignment leaves older aliases and their cached
gamer views attached to the original identity; a machine refuses destruction while its own views
are live. Session dispose/destroy refuses while associated aliases remain, including empty aliases.
The native machine holds raw roster pointers; session pins keep those pointers valid without a
session/gamer/machine reference cycle.

Both identity provenance and every roster member must match the target gamer's session.
An unbound standalone identity associates with its first session, including the no-session case;
it cannot later cross that boundary even if empty. Null shared assignment is refused with
CNA_RESULT_INVALID_HANDLE, matching native SetSharedMachine's non-null policy. To detach from an
old identity, assign a newly created empty machine. Assignment does not edit roster membership.
The existing value-copy routes remain independent operations. Release aliases with the existing
typed `cna_network_machine_destroy` route.

`NetworkParentLifetimeSmoke.c::shared_machine_aliases` tests both gamer/alias and sibling alias release orders, two gamers
sharing a native roster, roster addition observed through both aliases, mutation of a cached gamer,
reassignment with old cached identity retained, no-session/session boundary refusal, associated
empty-identity refusal, null/wrong/stale handles and independent session release guards, without
a display. CNA permits only one active session per process;
the strict-C cross-session-boundary fixture uses an associated session versus a no-session gamer,
and a no-session-associated empty machine versus a session gamer.

## Logical-size provider

`cna_game_window_set_logical_size_provider_ext` implements `CPP-30BE369B0F64` through native
SetLogicalSizeProviderEXT. Null removes the provider and restores native client-size fallback.
Registration/replacement/removal requires the game owner thread, even with the optional
foreign-thread service enabled. Invocation is synchronous on that game's thread in callback scope;
recursive driving/destruction is refused. The callback receives previous accepted width/height,
initially client dimensions, and a canonical availability output initialized to false. Success
with false or nonpositive dimensions keeps the previous logical size. These sizes feed native
orientation/fullscreen client bounds; they do not resize graphics render targets.

A stable trampoline snapshots callback/context at invocation start. Replacement/removal from
inside a callback changes later invocations only; nested size queries use the cached size instead
of recurring. Context remains caller-owned until replacement/removal/source destruction **and**
completion of an in-flight invocation. CNA makes no call after source destruction and never frees
caller context. Every callback returns normally; no exception or longjmp may cross into CNA.

Failure mapping follows lifecycle callbacks: `CNA_Result` is the failure channel and
`CNA_CallbackError` carries optional UTF-8 diagnostics. A failing result, or noncanonical availability
boolean, records CNA_RESULT_CALLBACK, requests game exit, preserves the last accepted dimensions
and keeps the first copied diagnostic. The enclosing game drive, client-bounds query or
screen-device-change route reports that failure. A declined size is a successful call.

`ServiceWindowSmoke.c::validate_provider/provider_failure` independently checks initial and
previous dimensions, accepted portrait/landscape orientation, false/nonpositive retention,
callback-time replacement/removal, context retirement, owner thread (also with service enabled),
recursive driving/destruction refusal, source teardown, explicit failure diagnostics and invalid
availability. The strict-C suite is registered as CApi_ServiceWindowSmoke and opens a window, so
run it on the private display:

```sh
tools/platform/run_gpu_tests_private.sh cmake-build-sdlgpu \
  -R '^CApi_(ServiceWindowSmoke|RuntimeGameSmoke|RuntimeComponentsSmoke)$' -V -j4
```

The current coverage counts, the remaining planned rows and the release verdict are in
[README.md](README.md), [COVERAGE.md](COVERAGE.md) and [LIMITATIONS.md](LIMITATIONS.md).
