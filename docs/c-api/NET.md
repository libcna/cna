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

`CNA_SendDataOptions` deserves one note: the canonical enumeration is marked as flags, but its
members use plain sequential values and `ReliableInOrder` is its own discrete member rather than a
composition. The C identities are therefore discrete too and must never be combined bitwise.

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
unspecified. Get/set accept indices 0..7 and return `CNA_RESULT_INVALID_ARGUMENT` outside that range
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

The canonical color asymmetry is preserved and is worth stating plainly: the writer emits four
**bytes** while the reader consumes four **floats**. A color written with
`cna_packet_writer_write_color` therefore cannot be read back with
`cna_packet_reader_read_color` — the reader will run off the end of a four-byte payload. Four
floats do read back into a color through the canonical float constructor.

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
  without moving it -- after `cna_local_network_gamer_receive_data_into_packet_reader`, the whole
  received packet, so a consumer with a reader type of its own (a managed `BinaryReader`) receives
  packets of any length (ABI 0.36.0, `CBIND-130`).

All three are marked `_ext` because they have no canonical counterpart. None exposes a native stream.

## Join failures

A canonical join failure carries a join-error value on the exception object, which never crosses
the ABI. The firewall converts the exception to `CNA_RESULT_INVALID_STATE` with
`CNA_ERROR_CATEGORY_STATE` and the message in the per-thread diagnostic, and records the join error
per thread as well. `cna_net_get_last_join_error` reads it back.

Any later failure on the same thread clears the record, so a stale join error can never be
returned; a caller that needs it must read it immediately after the failing call, exactly as with
the rest of the per-thread error information.

## Gamers, machines and event descriptions

`CNA_NetworkGamerHandle` owns a canonical gamer. Every canonical flag, the session-local
identifier, the round-trip time as 100-nanosecond ticks and the owning session handle are exposed.
`cna_network_gamer_create` takes an optional session handle (`CNA_INVALID_HANDLE` for none); an
empty gamertag gives the canonical factory's placeholder name, which no session gamer reports.

The CNA extension setters keep an `_ext` suffix — `set_has_left_session_ext`, `set_id_ext`,
`set_is_host_ext`, `set_roundtrip_ticks_ext` — so a consumer can see at a glance which state the
canonical API leaves permanently fixed without them.

`cna_network_gamer_copy_machine` hands back an **independent copy** rather than an alias. The
canonical setter already takes its machine by value, and the canonical machine exposes no mutator,
so a copy is observationally identical to the reference the canonical getter returns.

`CNA_NetworkMachineHandle` owns a machine. Its roster is exposed as a count plus indexed access
returning a borrowed gamer view; a view keeps its machine alive and blocks the machine's release.
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
back as an **independent owned list**, so a caller's list cannot alias or outlive the description.

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

All four rosters — all, local, remote and previous — are a roster identity plus a count and an
indexed **borrowed view**. A view keeps its session alive and blocks the session's release, so a
caller releases every view first.

The canonical `SessionProperties` getter returns a mutable list. C does not receive a reference
whose lifetime is tied invisibly to the session: `cna_network_session_copy_session_properties`
creates an owned list, the ordinary property-list routes mutate it, and
`cna_network_session_replace_session_properties` copies those values back into the live session.
The temporary list may be destroyed immediately after replacement.

Two lifetime rules follow from the canonical contract rather than from the binding:

- `cna_network_session_add_remote_gamer_ext` retains the gamer handle's resource for the session's
  lifetime, because the canonical add deliberately does not take ownership. A caller may release
  its own handle immediately without invalidating the roster; `cna_network_session_remove_gamer_ext`
  drops the retention.
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

The three leaderboard events are raised at `EndGame` and when a gamer leaves a
`LocalWithLeaderboards`, `PlayerMatch` or `Ranked` game, as in XNA. `InviteAccepted` is raised when a
player accepts a CNA service invitation in the Guide (or joins a friend's game from it); an
acceptance nobody is subscribed to reaches the first subscriber once, and only while its invitation
can still be joined. Without a configured service there are no invitations, and the `join_invited`
routes, like a `PlayerMatch` or `Ranked` create or find, answer `CNA_RESULT_NOT_SUPPORTED`
(`GamerServicesNotAvailableException`). See `docs/gamer-services-known-limitations.md`.

## Discovery, join and the fake-async pairs

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

`cna_network_session_find` refuses a local-only session type outright, matching the canonical
search; a `SystemLink` search runs LAN discovery and a `PlayerMatch`/`Ranked` search reads the CNA
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
the sender comes back as a **borrowed gamer view** that keeps its session alive exactly like a
roster view — the caller releases it. An empty queue is not a failure: the route succeeds and
reports zero bytes.

The canonical checks come first, in XNA's order: the offset must lie inside the destination (a zero
capacity is refused), and a packet that does not fit after it is refused with
`CNA_RESULT_INVALID_ARGUMENT` and **stays queued**. `cna_local_network_gamer_receive_data_into_packet_reader`
sizes the reader to the packet and reports its length. (The FNA-era port consumed the packet before
checking and reported zero for the reader form.)

### Sending

All six canonical `SendData` overloads are mapped: the plain and offset/length byte forms, each
with and without a recipient, and the two packet-writer forms. A null recipient handle means "every
gamer", matching the canonical null pointer.

`CNA_SendDataOptions` is validated against the canonical identities, because its members are
discrete values rather than composable flags; an unknown value is refused instead of being cast
through.

### The two extensions

`cna_local_network_gamer_clear_packet_queue_ext` and `cna_local_network_gamer_enqueue_packet_ext`
carry an `_ext` suffix because the canonical operations they map are CNA extensions. Enqueuing takes
the same fixed `CNA_NetworkEventInfo` description the session pump uses, so no canonical event
object crosses the ABI.

`EnableSendVoice` runs the canonical checks (a null or foreign remote gamer, either gamer having left)
and then stops or resumes the gamer's voice to that remote gamer. `SendPartyInvites` answers
`CNA_RESULT_INVALID_STATE`, the canonical refusal, while the gamer's CNA service party has fewer
than two people, and otherwise invites the rest of the party to the current online session.
