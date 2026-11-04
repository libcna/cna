# CNA C API Networking Contract

## Scope

`CNA/C/net.h` covers the network identity enumerations, the quality-of-service value, the
session-property list and both packet buffers. `CNA/C/net_gamers.h` adds gamers, machines and the
event descriptions, and `CNA/C/net_sessions.h` adds discovered sessions, their
collection, the session object itself and its local gamers. Nothing here opens a socket unless a
`SystemLink` session (direct ENet on the LAN) or a `PlayerMatch`/`Ranked` session (the configured
CNA service and its authenticated relay) is created or searched for; a `Local` session touches no
transport at all.

## Identities

`CNA_NetworkSessionEndReason`, `CNA_NetworkSessionJoinError`, `CNA_NetworkSessionState`,
`CNA_NetworkSessionType` and `CNA_SendDataOptions` carry the canonical ordinals exactly, pinned by
adapter static assertions.

For the six ordinary `SendData` routes, `CNA_SendDataOptions` is a set of flags:
`RELIABLE_IN_ORDER` is
`RELIABLE | IN_ORDER`, and `CHAT` combines with either or both of them (5, 6 and 7). A value with any
other bit set is refused with `CNA_RESULT_INVALID_ARGUMENT`. The two event-injection extensions
have a narrower validation boundary; see [Extension limitations](#extension-limitations).

## Quality of service

`CNA_QualityOfService` is a copied fixed-layout value, not a handle, because the canonical type is
an immutable measurement snapshot. Round-trip times are 100-nanosecond ticks; `System::TimeSpan`
never crosses the boundary.

Both canonical factories are mapped. `cna_quality_of_service_init` reproduces the unmeasured one:
`is_available` is `CNA_FALSE` and every measurement zero (XNA's `internal QualityOfService()`; the
FNA-era port reported it available). `cna_quality_of_service_init_measured` takes the single round-trip sample a discovery
exchange yields, which is why the average and the minimum are the same value and the throughput
fields stay zero.

## Session properties

`CNA_NetworkSessionPropertiesHandle` owns eight fixed slots. Each value is a
`CNA_OptionalInt32` carrying presence and signed value. Count is always eight; initial slots are
absent (`has_value = CNA_FALSE`). Get/set accept indices 0..7 and return `CNA_RESULT_INVALID_ARGUMENT` outside that range
without mutation. Structural add/insert/remove/remove_at/clear return `CNA_RESULT_NOT_SUPPORTED`,
including invalid structural indices, following the reference fixed collection contract.

The XNA collection interface reports IsReadOnly=false even for advertised snapshots. Copies returned
from AvailableNetworkSession retain write refusal; setters return `CNA_RESULT_NOT_SUPPORTED`.
Copies from a live NetworkSession are independent editable values; replacing the live collection
checks that the session is not disposed and that the caller is the current host before any mutation.
No protocol structures cross the C ABI and existing symbols remain available.

CopyTo copies all eight values into existing capacity after its output index; insufficient space
returns `CNA_RESULT_BUFFER_TOO_SMALL` with the required count, and no partial output. The owned
enumerator observes the eight live values; reading before the first advance or after the last one
returns `CNA_RESULT_INVALID_STATE`. Destroy it before its collection; the collection refuses
destruction while an enumerator remains open. Writes cannot invalidate the fixed storage shape.

## Packet buffers

`CNA_PacketWriterHandle` and `CNA_PacketReaderHandle` own the canonical in-memory buffers. Each
canonical `Write` overload and each canonical read gets its own C route, so C never depends on
overload resolution.

`cna_packet_writer_write_color` writes four bytes in R, G, B, A order and
`cna_packet_reader_read_color` reads those same four bytes. They round-trip all four channels;
a payload shorter than four bytes fails with `CNA_RESULT_IO`. Do not encode a color as four floats.
Matrices, quaternions and vectors use their component floats in the native declaration order.

A negative capacity is refused exactly as the canonical constructor refuses it. A non-negative
capacity is a hint the canonical backing buffer does not act on.

### Three documented extensions

The canonical API hands a writer straight to a send operation and fills a reader through a receive
operation, and never exposes either buffer. That leaves a C consumer with no way to move a packet
across a transport the C API does not own, and no way to observe packet contents at all, so three
extension routes exist:

- `cna_packet_writer_copy_data_ext` copies out the bytes a writer has produced;
- `cna_packet_reader_set_data_ext` replaces a reader's contents and rewinds it;
- `cna_packet_reader_copy_data_ext` copies out the bytes a reader holds, whatever its position and
  without moving it, including the whole packet received through
  `cna_local_network_gamer_receive_data_into_packet_reader`.

All three are marked `_ext` because they have no canonical counterpart. None exposes a native stream.

## Join failures

A canonical join failure carries a join-error value on the exception object, which never crosses
the ABI. The firewall converts the exception to `CNA_RESULT_INVALID_STATE` with
`CNA_ERROR_CATEGORY_STATE` and the message in the per-thread diagnostic, and records the join error
per thread as well. `cna_net_get_last_join_error` reads it back.

A later recorded failure on the same thread replaces the record, including the join-error payload.
Successful direct calls and diagnostic reads preserve it; opt-in foreign-thread dispatch has the
success-reset exception described in [ERRORS.md](ERRORS.md). Inspect the join error before another
failure can overwrite it. When absent, `out_has_join_error` is false and the enum output is the
`SESSION_NOT_FOUND` placeholder, not a new failure indication.

## Gamers, machines and event descriptions

`CNA_NetworkGamerHandle` owns a canonical gamer. Every canonical flag, the session-local
identifier, the round-trip time as 100-nanosecond ticks and the owning session handle are exposed.
`cna_network_gamer_create` takes an optional session handle (`CNA_INVALID_HANDLE` for none); an
empty gamertag gives the canonical factory's placeholder name, which no session gamer reports.

The CNA extension setters keep an `_ext` suffix — `set_has_left_session_ext`, `set_id_ext`,
`set_is_host_ext`, `set_roundtrip_ticks_ext` — so a consumer can see at a glance which state the
canonical API leaves permanently fixed without them.

`cna_network_gamer_copy_machine` hands back an owned machine copy rather than an alias. Its roster
contains non-owning gamer pointers, so it is not a deep copy of those gamers. A nonempty copy
retains the source gamer's session dependency; source handles can be released while the copy and
its views remain valid. Empty copies hold no roster dependency. Assigning a nonempty machine
requires its gamers to belong to the target gamer's session; detached/foreign links are refused.

`CNA_NetworkMachineHandle` owns a machine. Its roster is exposed as a count plus indexed access
returning a new owned view handle; a view keeps its machine alive and blocks the machine's release.
Release each view with `cna_network_gamer_destroy`.
Only a session populates a roster, so a machine created from C reports none.
`cna_network_machine_remove_from_session` is the canonical `RemoveFromSession`: only the host may
remove another machine, and the canonical refusals (the local machine, a machine that left, a
caller that is not the host) come back as documented failures.

The seven canonical event-argument types become fixed `CNA_*EventInfo` descriptions with `_init`
routines, delivered by value exactly as every other C API event payload is. A payload gamer is a
validated handle, so a description can never name a handle that was never a gamer.

## Discovered sessions

`CNA_AvailableNetworkSessionHandle` owns one description a discovery backend published. Every
scalar is exposed directly; the host gamertag and connect address use the count/copy protocol; the
quality of service comes back as a copied `CNA_QualityOfService`; and the session properties come
back as an **independent owned list**. It does not alias the description and may outlive it;
the advertised copy still rejects writes. Destroy it through the property-list API.

Both equality operators become explicit routes, because C has no operator overloading.

One canonical limit shapes creation: the quality-of-service type offers exactly two constructions —
unmeasured, and one built from a single round-trip sample. Only that sample can be carried in, so
`cna_available_network_session_create_ext` reads `average_roundtrip_ticks` and ignores the
throughput fields. A description a `SystemLink` search publishes carries measured downstream and
upstream bandwidth as well; one from a `PlayerMatch`/`Ranked` search is unmeasured.

`CNA_AvailableNetworkSessionCollectionHandle` owns the read-only collection. An element is **copied
out** rather than aliased, so it survives the collection it came from; the canonical factory copies
its own input the same way, so the handles a caller passes in stay independently owned and may be
released immediately.

## Network sessions

`CNA_NetworkSessionHandle` owns the session object. The canonical creation routes hand back a
caller-owned raw pointer, so the handle owns the deletion, and only one session exists at a time —
a canonical process-wide restriction, not a C one.

A session cannot exist without at least one **signed-in gamer**: the canonical constructor selects
its host from its local gamers and fails while that list is empty. `CNA/C/gamer_services.h`
provides the signed-in gamers (see `GAMER_SERVICES.md`); a C host can also create one and publish or
clear the process-wide collection itself.

That shapes the three creation routes:

- `cna_network_session_create` and `cna_network_session_create_with_properties` take their local
  gamers from the published collection, so it must already hold one;
- `cna_network_session_create_with_local_gamers` takes an explicit list and ignores the published
  collection entirely.

The C adapter retains canonical owned signed-in inputs through creation (including completion
callbacks) and the session's lifetime. `add_local_gamer` retains an accepted input in the same way.
It only asks for the gamer, as XNA's `AddLocalGamer` does: the gamer is in the local and all
rosters, and `GamerJoined` is raised, from the next `cna_network_session_update` of a `Local` or
`SystemLink` session (an online one adds it when the service answers).
Clearing the published collection and releasing the original handles does not replace these
identities. A signed-in view obtained from a local roster or machine shares the retained native
object and can outlive the session without keeping the session alive. Callback-scoped native
borrows retain their existing scope.

All four rosters — all, local, remote and previous — are a roster identity plus a count and an
indexed **owned view handle** onto borrowed native gamer state. A view keeps its session alive and
blocks the session's first disposal and release, so a caller destroys every view with
`cna_network_gamer_destroy` first. Standalone gamers bound to that session and nonempty machine
copies participate in the same dependency guard. Session-owned native factory values remain alive
for current/previous rosters and queued events until the session ends; retaining these values
separately from the handle records avoids a session/gamer ownership cycle.
This differs from a callback's temporary gamer token, which the caller must not destroy or retain.

The canonical `SessionProperties` getter returns a mutable list. C does not receive a reference
whose lifetime is tied invisibly to the session: `cna_network_session_copy_session_properties`
creates an owned list, the ordinary property-list routes mutate it, and
`cna_network_session_replace_session_properties` copies those values back into the live session.
The temporary list may be destroyed immediately after replacement.

Two lifetime rules follow from the canonical contract rather than from the binding:

- `cna_network_session_add_remote_gamer_ext` retains the canonical native gamer for the session's
  lifetime, because the canonical add deliberately does not take ownership. A caller may release
  its own handle immediately without invalidating the roster; `cna_network_session_remove_gamer_ext`
  preserves that backing for previous rosters and queued leave events.
- `cna_network_session_start_game` and `_end_game` only **queue** a state change. The state moves
  when `cna_network_session_update` pumps the queue, exactly as the canonical implementation does.

A packet event queued on a `Local` session is a deliberate no-op in the canonical pump, so
`cna_network_session_send_network_event_ext` succeeds and delivers nothing there; `SystemLink` and
online sessions deliver it.

## Session events

Each of the nine instance events and the static `InviteAccepted` event has its own
`cna_network_session_subscribe_*` route taking a typed callback, and they share one
`cna_network_session_unsubscribe`.

A payload gamer is handed to the callback as a handle that exists **only for the duration of that
call**. A consumer that needs the gamer afterwards must copy what it needs while the callback runs;
it can never retain a pointer into session-owned state.

An instance registration holds a weak reference to its session, so releasing it after the session
is gone is a no-op rather than a failure. `InviteAccepted` is a static event, so its subscription
belongs to the process and takes no session handle.

`GamerJoined` carries one canonical behavior worth knowing: it replays itself for every gamer
already in the session the instant a handler subscribes, so the callback fires before
`cna_network_session_subscribe_gamer_joined` returns whenever the session is not empty.

A host that leaves takes the session's host with it, as XNA's `ProcessGamerLeft` does:
`cna_network_session_get_host` reports `CNA_INVALID_HANDLE` until a host change names the next one,
and that change's `old_host` is `CNA_INVALID_HANDLE`.

The three leaderboard events are raised at `EndGame` and when a gamer leaves a
`LocalWithLeaderboards`, `PlayerMatch` or `Ranked` game, as in XNA. `InviteAccepted` is raised when a
player accepts a CNA service invitation in the Guide (or joins a friend's game from it); an
acceptance nobody is subscribed to reaches the first subscriber once, and only while its invitation
can still be joined. Without a configured service there are no invitations, and the `join_invited`
routes, like a `PlayerMatch` or `Ranked` create or find, answer `CNA_RESULT_NOT_SUPPORTED`
(`GamerServicesNotAvailableException`). See [gamer-services-known-limitations.md](../gamer-services-known-limitations.md).

## Discovery, join and synchronous completion routes

Every canonical `Begin`/`End` pair collapses into one synchronous C route: offline operations
complete before `Begin` returns, and an online one (`PlayerMatch`/`Ranked` create, find, join,
invited join) completes at a later `GamerServicesDispatcher.Update`, which the canonical `End` wait
pumps. Each `*_async` route still takes the canonical completion callback and invokes it before
returning; the callback receives only the caller's own context, because no operation object may
cross the ABI, and no `std::any` state is exposed.

The three `cna_network_session_create_*_async` routes preserve the requested `max_gamers`, exactly
like their synchronous counterparts. CNA retains the value through its internal Begin/End action
instead of inheriting FNA's stubbed hardcoded value; the resulting limit drives both readback and
the functional session-capacity behavior.

`cna_network_session_find` refuses `Local`; `LocalWithLeaderboards` returns an empty collection.
A `SystemLink` search runs LAN discovery and a `PlayerMatch`/`Ranked` search reads the CNA
service directory. `cna_network_session_join` joins the discovered session, and the invited-join
routes join the invitation the player accepted in the Guide.

Only one session exists at a time, so a caller releases one before creating or joining the next.

## Local gamers

A local gamer **is** a network gamer, so it uses the same owned `CNA_NetworkGamerHandle` rather than
a second handle kind. Every `cna_local_network_gamer_*` route therefore starts by checking that the
handle really names a local gamer and returns `CNA_RESULT_INVALID_HANDLE` when it does not, instead
of reinterpreting a remote gamer as a local one.

The usual way to obtain one is `cna_network_session_get_gamer` with the
`CNA_NETWORK_SESSION_ROSTER_LOCAL` roster. `cna_local_network_gamer_create_ext` maps the canonical
internal factory for the cases where a gamer is needed outside a session; the session handle may be
`CNA_INVALID_HANDLE`, and such a gamer belongs to no roster.

### Receiving

Each canonical `ReceiveData` overload gets its own route, so C never depends on overload
resolution. A payload arrives in a caller-owned buffer described by a pointer and a byte count, and
the sender comes back as an **owned view handle** that keeps its session alive exactly like a
roster view — the caller destroys it with `cna_network_gamer_destroy`. An empty queue is not a failure: the route succeeds and
reports zero bytes.

The canonical checks come first, in XNA's order: the offset must lie inside the destination (a zero
capacity is refused), and a packet that does not fit after it is refused with
`CNA_RESULT_INVALID_ARGUMENT` and **stays queued**. `cna_local_network_gamer_receive_data_into_packet_reader`
sizes the reader to the packet and reports its length. (The FNA-era port consumed the packet before
checking and reported zero for the reader form.)

### Sending

All six canonical `SendData` overloads are mapped: the plain and offset/length byte forms, each
with and without a recipient, and the two packet-writer forms. The routes without a recipient send
to every gamer. The `_to` routes refuse `CNA_INVALID_HANDLE`, the canonical null recipient, with
`CNA_RESULT_INVALID_ARGUMENT` (`System.ArgumentNullException` for `recipient`) before the
canonical call's other checks, as XNA's recipient overloads do. A packet writer is sent whole and
then emptied. An empty writer is refused with `CNA_RESULT_INVALID_ARGUMENT`
(`System.ArgumentOutOfRangeException`, for `offset` while its buffer has never held a byte and for
`count` once it has), and nothing is sent.

All combinations of the three defined option bits are accepted by these routes; unknown bits
return `CNA_RESULT_INVALID_ARGUMENT` before sending.

### The two extensions

`cna_local_network_gamer_clear_packet_queue_ext` and `cna_local_network_gamer_enqueue_packet_ext`
carry an `_ext` suffix because the canonical operations they map are CNA extensions. Enqueuing takes
the same fixed `CNA_NetworkEventInfo` description the session pump uses, so no canonical event
object crosses the ABI.

`EnableSendVoice` runs the canonical checks (a null or foreign remote gamer, either gamer having left)
and then stops or resumes the gamer's voice to that remote gamer. `SendPartyInvites` answers
`CNA_RESULT_INVALID_STATE`, the canonical refusal, while the gamer's CNA service party has fewer
than two people, and otherwise invites the rest of the party to the current online session.

Both event-injection extensions validate `CNA_NetworkEventInfo.reliable` as a SendDataOptions
bitmask, matching the ordinary send routes. All combinations of Reliable, InOrder and Chat
(0..7) are accepted; bits outside that set return `CNA_RESULT_INVALID_ARGUMENT`. CNA-FIX-D13's
strict-C regression covers both extension routes, payload round trips and refusal without queue
mutation for unknown bits.

## Extension limitations

- Standalone `cna_network_gamer_create` and `cna_local_network_gamer_create_ext` retain a supplied
  session and block its disposal/release while the dependent handle is alive. They do not add a
  gamer to a roster; `add_remote_gamer_ext` requires a gamer already bound to that same session.
- An owned signed-in input to `cna_local_network_gamer_create_ext` is retained without copying
  its canonical identity. Its signed-in views share that backing and survive release of the input
  and local gamer handles. `CApi_NetworkParentLifetimeSmoke` verifies identity, shared mutations,
  transitive construction, session creation/add-local, callback release and release ordering.
  Callback-scoped signed-in borrows retain their callback scope.
- Event-injection payload bytes are copied, but optional gamer/sender pointers are borrowed by
  queued native events. Keep those gamers alive until the event has been processed or cleared.

## Verification

The adapter code is in `modules/c-api/src/CnaCApiNet*.cpp`. `CApi_NetSmoke` covers the C packet
round trip, properties, discovery descriptions, local sessions, events, roster lifetime and
send/receive routes. `CApi_BoundaryDetail` checks join-exception translation. These local checks
do not certify LAN discovery, the configured online service, relay connectivity or voice hardware;
see the networking and gamer-service qualification documents for those environments.

## Shared machine identities (ABI 0.60)

`cna_network_gamer_get_shared_machine_ext` returns an owned alias of native `GetSharedMachine`;
`cna_network_gamer_set_shared_machine_ext` assigns that identity without copying. Release each
alias with `cna_network_machine_destroy`. The original token can be released after assignment;
the native gamer still owns its machine. Roster changes are visible through sibling aliases.
Reassigning one gamer leaves older aliases and cached roster views on their original identity.
Every associated alias, even an empty one, guards its session against dispose/destroy; a machine
cannot be destroyed while its own gamer views remain live. Release views, aliases, then session.

Assignments validate both identity provenance and each roster member's session. An unbound
standalone machine becomes associated with the target session, including the no-session case,
and cannot cross that boundary later, even if empty. Null assignment is refused with
CNA_RESULT_INVALID_HANDLE, matching the native non-null requirement. Assign a newly created empty
machine to detach from an old identity. Assignment itself does not edit either roster. The
existing `copy_machine` / `set_machine` routes continue to copy values.

Guest/private-slot flags remain managed by the transport/session. Robert Vokac approved no-C-form
dispositions for their native mutators on 2026-11-03; C callers use the existing getters.
See [CLOSURE_060.md](CLOSURE_060.md) for exact rows and execution evidence.
