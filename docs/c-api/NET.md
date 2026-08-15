# CNA C API Networking Contract

## Scope

`CNA/C/net.h` covers the network identity enumerations, the quality-of-service value, the
session-property list and both packet buffers. `CNA/C/net_gamers.h` adds gamers, machines and the
event descriptions. Sessions, local gamers and discovery are a later coverage task and have no C
route yet; nothing in either header opens a socket or joins a session.

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

Both canonical factories are mapped. `cna_quality_of_service_init` reproduces the unmeasured one
faithfully, including its quirk of reporting `is_available` as `CNA_TRUE` with every measurement
zero. `cna_quality_of_service_init_measured` takes the single round-trip sample a discovery
exchange yields, which is why the average and the minimum are the same value and the throughput
fields stay zero.

## Session properties

`CNA_NetworkSessionPropertiesHandle` owns the canonical list. Each element is a
`CNA_OptionalInt32`, a fixed POD carrying presence and value, because the canonical element type is
an optional integer.

Two canonical behaviors are preserved rather than tidied up:

- the list reports itself read-only while still accepting add, remove and clear; and
- writing at an index past the end **appends** instead of extending the list to that index.

Two are decided in the C layer, because the canonical implementation does not decide them at all:

- `Insert` and `RemoveAt` forward straight to the backing vector with no bounds check, so an
  out-of-range index there is undefined behavior. The C routes range-check first and return
  `CNA_RESULT_INVALID_ARGUMENT`.
- the canonical enumerator dereferences its backing storage before its first advance, so
  `cna_network_session_property_enumerator_get_current` reports `CNA_RESULT_INVALID_STATE` before
  the first advance and after the last one instead of reading out of bounds.

The canonical non-const indexer returns a proxy whose documented quirk is that even a bare
out-of-range *read* through it appends, because the proxy must bind a slot before it can know
whether the caller will read or write. That proxy has no C counterpart; the C API exposes the
strict read and the appending write as separate routes, which is what the canonical header itself
recommends, and does not simulate the quirk.

`cna_network_session_properties_create_enumerator` returns an owned enumerator that observes the
live list. It must be destroyed before its list, and the list refuses destruction while one is
open.

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

### Two documented extensions

The canonical API hands a writer straight to a send operation and fills a reader through a receive
operation, and never exposes either buffer. That leaves a C consumer with no way to move a packet
across a transport the C API does not own, and no way to observe packet contents at all, so two
extension routes exist:

- `cna_packet_writer_copy_data_ext` copies out the bytes a writer has produced;
- `cna_packet_reader_set_data_ext` replaces a reader's contents and rewinds it.

Both are marked `_ext` because they have no canonical counterpart. Neither exposes a native stream.

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
`cna_network_gamer_create` takes a session handle so its shape is already final; session handles
arrive with the session slice, so for now only `CNA_INVALID_HANDLE` is accepted.

The CNA extension setters keep an `_ext` suffix — `set_has_left_session_ext`, `set_id_ext`,
`set_is_host_ext`, `set_roundtrip_ticks_ext` — so a consumer can see at a glance which state the
canonical API leaves permanently fixed without them.

`cna_network_gamer_copy_machine` hands back an **independent copy** rather than an alias. The
canonical setter already takes its machine by value, and the canonical machine exposes no mutator,
so a copy is observationally identical to the reference the canonical getter returns.

`CNA_NetworkMachineHandle` owns a machine. Its roster is exposed as a count plus indexed access
returning a borrowed gamer view; a view keeps its machine alive and blocks the machine's release.
Only a session populates a roster, so a machine created from C reports none.
`cna_network_machine_remove_from_session` reports the canonical always-throwing placeholder as
`CNA_RESULT_NOT_SUPPORTED` rather than pretending it succeeded.

The seven canonical event-argument types become fixed `CNA_*EventInfo` descriptions with `_init`
routines, delivered by value exactly as every other C API event payload is. A payload gamer is a
validated handle, so a description can never name a handle that was never a gamer.
