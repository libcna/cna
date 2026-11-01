# CNA C API Storage Contract

## Scope

The storage surface covers the whole canonical `Microsoft::Xna::Framework::Storage` module:
`StorageDevice`, `StorageContainer` and `StorageDeviceNotConnectedException`. It is independent of
the graphics device and of the `Game` lifecycle — no storage call needs a game handle, and storage
handles are not game children.

Three owned handle families exist:

| Handle | Created by | Released by |
|---|---|---|
| `CNA_StorageDeviceHandle` | any `cna_storage_device_show_selector*` route | `cna_storage_device_destroy` |
| `CNA_StorageContainerHandle` | `cna_storage_container_open` | `cna_storage_container_destroy` |
| `CNA_StorageStreamHandle` | `cna_storage_container_create_file` / `cna_storage_container_open_file*` | `cna_storage_stream_close` |

Ownership nests strictly. A container cannot be destroyed while it still has an open stream, and a
device cannot be destroyed while it still owns a container; both refuse with
`CNA_RESULT_INVALID_STATE` rather than invalidating a child behind the caller's back. All three
handle kinds use the ordinary creation-thread rule, including the documented opt-in dispatch
exception in [CALLBACKS_AND_THREADING.md](CALLBACKS_AND_THREADING.md).

`cna_storage_container_get_storage_device` returns the original protected device token, not a new
owned alias. A content reader can borrow a storage stream; close returns
`CNA_RESULT_INVALID_STATE` until every such reader releases it.

## Completion routes are synchronous

XNA models device selection and container opening with the `BeginXxx`/`EndXxx` pattern, and CNA
completes both before `Begin` returns. The C API states that fact instead of reproducing the shape:

| Canonical pair | C route |
|---|---|
| `BeginShowSelector(callback, state)` / `EndShowSelector` | `cna_storage_device_show_selector` |
| `BeginShowSelector(player, …)` / `EndShowSelector` | `cna_storage_device_show_selector_for_player` |
| `BeginShowSelector(size, directories, …)` / `EndShowSelector` | `cna_storage_device_show_selector_with_space` |
| `BeginShowSelector(player, size, directories, …)` / `EndShowSelector` | `cna_storage_device_show_selector_for_player_with_space` |
| `BeginOpenContainer` / `EndOpenContainer` | `cna_storage_container_open` |

Each route takes a completion callback and context and invokes the callback before returning.
The callback receives only its context, not the eventual output handle; native Begin completion
can precede End construction or C handle publication, so notification is not proof of final success.
No `System::IAsyncResult`, task, future or operation handle is invented for work that never pends.
A null callback means "no notification wanted", not an error.

## Text, listings and buffers

Container display names, type names, the storage root and every directory or file name use the
standard UTF-8 count/copy protocol: a `_size`/`_count` call, then a `_copy` call that either writes
the full value or returns `CNA_RESULT_BUFFER_TOO_SMALL` with the required byte count. No terminator
is appended and no partial value is ever written.

Directory and file listings are exposed as a count plus an indexed copy rather than as a retained
collection handle, because the canonical `GetDirectoryNames`/`GetFileNames` rebuild an unordered
vector on every call. There is no retained snapshot shared by count and copy, and ordering is not
guaranteed even across immediately consecutive calls. An index addresses the freshly enumerated
result; filesystem changes can change its meaning or make it out of range, which returns
`CNA_RESULT_INVALID_ARGUMENT`. An empty C search pattern selects the no-argument native overload;
a non-empty pattern filters basenames using `*` and `?`, without recursive traversal.

## Streams never expose Sharp Runtime

`System::IO::Stream` is a native implementation dependency and never becomes a C type. A
`CNA_StorageStreamHandle` exposes only `read`, `write`, `seek`, `get_position`, `get_length`,
`set_length`, `flush`, the three capability queries and `close`.

The canonical stream contract is expressed in .NET `Int32` offsets and lengths. A wider C value is
rejected with `CNA_RESULT_OVERFLOW` up front rather than silently truncated, and a negative length
is rejected with `CNA_RESULT_INVALID_ARGUMENT`. A zero-length read or write is a documented no-op
that touches no buffer, so a null pointer is permitted only in that case. Handle/thread validation
still runs, but a zero-length operation does not test native read/write capability. Reads may
return fewer bytes than requested and return zero at EOF; this is not the all-or-nothing array-copy
protocol. Read initializes its byte count to zero before count/handle validation.

Capabilities are queried, not inferred from the mode that opened the stream: a stream opened for
reading answers `CNA_FALSE` to `cna_storage_stream_get_can_write`, and writing to it returns
`CNA_RESULT_NOT_SUPPORTED` instead of dropping the bytes.

`CNA_FILE_MODE_*`, `CNA_FILE_ACCESS_*`, `CNA_FILE_SHARE_*` and `CNA_SEEK_ORIGIN_*` carry the exact
canonical ordinals, pinned by static assertions in the adapter. `CNA_FILE_SHARE_*` is a bit set;
unknown bits are rejected. `cna_storage_container_open_file_share` forwards the selection to
the canonical native stream. Windows enforces the access/share masks. POSIX follows .NET's
advisory policy: None takes an exclusive lock, other values a shared lock; unsupported locking
is best-effort. A sharing conflict returns `CNA_RESULT_IO`, leaves the output handle invalid,
and is checked before truncation. It does not impose mandatory locks on unrelated Unix tools.

`cna_storage_container_create_file`, `cna_storage_container_open_file` and
`cna_storage_container_open_file_access` default to exclusive sharing (None).
The mode-only open chooses write access for Append and read/write otherwise, matching XNA.

## Application name and storage root

`cna_storage_set_app_name_ext` maps the `SetAppNameEXT` extension and must be called before any
storage access, as in the canonical API; it resets the cached root. `cna_storage_get_root_size_ext`
and `cna_storage_copy_root_ext` map `GetStorageRootEXT`.

The root uses CNA's storage policy independently of the windowing platform. On ordinary desktop
builds it tries `XDG_DATA_HOME`, then `LOCALAPPDATA`, then the home directory's application-data
location, and finally the working directory, with the configured application name (`game` by
default) below it. Android uses private application storage. Browser builds use `/cna-storage`
backed by the IDBFS startup hook and refuse storage when that persistent mount is unavailable.
See [filesystem-path-model.md](../filesystem-path-model.md) for the cross-platform path policy.

On Linux, pin `XDG_DATA_HOME` to a disposable directory for tests; there is no C absolute-root
setter. The two root query/copy extensions may create directories as part of lazy resolution.
Application-name configuration is startup policy, not a way to relocate already-open containers.

Both container names and every file/directory path are confined to their owning storage root.
Absolute paths, lexical parent traversal outside the container, and existing symlinks that resolve
outside it return `CNA_RESULT_INVALID_ARGUMENT`. Normalized paths such as
`profiles/../save.bin` remain valid when the final path stays inside the same container. This is a
deliberate CNA security hardening over FNA's unchecked `Path.Combine` behavior.

## Events

`StorageDevice::DeviceChanged` is a canonical *static* event, so
`cna_storage_device_subscribe_device_changed` takes no device handle: the subscription belongs to
the process, not to any one handle. `StorageContainer::Disposing` is per-instance and is subscribed
through the container handle. Both return an owned registration handle whose release unsubscribes;
releasing twice returns `CNA_RESULT_INVALID_HANDLE`. Disposal is idempotent and raises `Disposing`
exactly once, after setting the disposed flag while the container token is still queryable.
Registrations do not block container destruction and remain caller-owned until unsubscribed;
unsubscribing after source destruction succeeds.

`cna_storage_container_dispose` leaves the handle, device dependency and existing streams alive;
it does not delete backing files. Display-name, device and filesystem operations then refuse with
`CNA_RESULT_INVALID_STATE`; disposal-state/type queries and final destroy remain available.
Existing streams continue to use their own native lifetime and must be closed before container
destroy. Final destroy also releases the container's device dependency.

## Failures

| Canonical failure | C result |
|---|---|
| `StorageDeviceNotConnectedException` | `CNA_RESULT_INVALID_STATE` / `CNA_ERROR_CATEGORY_STATE` |
| `System::IO::IOException` and subclasses, `std::filesystem::filesystem_error` | `CNA_RESULT_IO` / `CNA_ERROR_CATEGORY_IO` |
| `System::UnauthorizedAccessException` (including DeleteFile on a directory) | `CNA_RESULT_IO` / `CNA_ERROR_CATEGORY_IO` |
| `System::ArgumentException`, empty relative paths, unknown identities | `CNA_RESULT_INVALID_ARGUMENT` |
| `System::NotSupportedException` (unwritable or unreadable stream) | `CNA_RESULT_NOT_SUPPORTED` |
| `System::ObjectDisposedException` | `CNA_RESULT_INVALID_STATE` |

`cna_storage_container_open` creates the container's directories, the storage root included, as
XNA's `EndOpenContainer` does: a file occupying any of them, or a directory that cannot be written,
is `CNA_RESULT_IO`, and the next open succeeds once the obstruction is gone. Root-resolution
extensions (`cna_storage_get_root_size_ext`, `cna_storage_copy_root_ext`) report an unusable root as
a disconnected device; native free/total-space queries can report that state too. The connectivity
query instead converts its root-resolution failure into a successful false value.

`cna_storage_device_delete_container` removes what XNA's `DeleteContainer` removes: the device's
own player folder of the container (`AllPlayers` or `Player1`..`Player4`), never another player's
folder or the named folder; a missing container or a file of that name is left alone and nothing
is created. It keeps the containment guard: a title name that is absolute or escapes the storage
root is refused with `CNA_RESULT_INVALID_ARGUMENT` instead of being resolved, and so is an empty
title name, which the C++ call would apply to the player folder at the storage root.
The same result protects container opening and all paths used through a container. No native
exception object, type name or C++ throw crosses the ABI; messages reach the caller through the
per-thread UTF-8 diagnostic.

## Verification

`CApi_StorageSmoke` exercises selectors/callbacks, names, streams, access/share refusals, path
containment, parent ordering and disposal events. `CApi_ContentReaderSmoke` covers the stream-borrow
dependency. Their CTest environments pin `XDG_DATA_HOME` inside the build tree. These Linux runs
do not replace native Windows access/share qualification or browser persistence/reload checks.
