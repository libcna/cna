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
handle kinds are thread-affine like every other C API handle.

## The fake-async pairs are single synchronous calls

XNA models device selection and container opening with the `BeginXxx`/`EndXxx` pattern, and CNA
completes both before `Begin` returns. The C API states that fact instead of reproducing the shape:

| Canonical pair | C route |
|---|---|
| `BeginShowSelector(callback, state)` / `EndShowSelector` | `cna_storage_device_show_selector` |
| `BeginShowSelector(player, …)` / `EndShowSelector` | `cna_storage_device_show_selector_for_player` |
| `BeginShowSelector(size, directories, …)` / `EndShowSelector` | `cna_storage_device_show_selector_with_space` |
| `BeginShowSelector(player, size, directories, …)` / `EndShowSelector` | `cna_storage_device_show_selector_for_player_with_space` |
| `BeginOpenContainer` / `EndOpenContainer` | `cna_storage_container_open` |

Each route still takes the canonical completion callback and context and still invokes the callback
before returning, so consumer code written against the canonical completion contract keeps working.
No `System::IAsyncResult`, task, future or operation handle is invented for work that never pends.
A null callback means "no notification wanted", not an error.

## Text, listings and buffers

Container display names, type names, the storage root and every directory or file name use the
standard UTF-8 count/copy protocol: a `_size`/`_count` call, then a `_copy` call that either writes
the full value or returns `CNA_RESULT_BUFFER_TOO_SMALL` with the required byte count. No terminator
is appended and no partial value is ever written.

Directory and file listings are exposed as a count plus an indexed copy rather than as a retained
collection handle, because the canonical `GetDirectoryNames`/`GetFileNames` rebuild an unordered
vector on every call. A name is therefore addressed by the index of the same immediately preceding
count call; an out-of-range index returns `CNA_RESULT_INVALID_ARGUMENT`. An empty search pattern
selects the canonical no-argument overload; a non-empty pattern selects the glob overload.

## Streams never expose Sharp Runtime

`System::IO::Stream` is a native implementation dependency and never becomes a C type. A
`CNA_StorageStreamHandle` exposes only `read`, `write`, `seek`, `get_position`, `get_length`,
`set_length`, `flush`, the three capability queries and `close`.

The canonical stream contract is expressed in .NET `Int32` offsets and lengths. A wider C value is
rejected with `CNA_RESULT_OVERFLOW` up front rather than silently truncated, and a negative length
is rejected with `CNA_RESULT_INVALID_ARGUMENT`. A zero-length read or write is a documented no-op
that touches no buffer, so a null pointer is permitted only in that case.

Capabilities are queried, not inferred from the mode that opened the stream: a stream opened for
reading answers `CNA_FALSE` to `cna_storage_stream_get_can_write`, and writing to it returns
`CNA_RESULT_NOT_SUPPORTED` instead of dropping the bytes.

`CNA_FILE_MODE_*`, `CNA_FILE_ACCESS_*`, `CNA_FILE_SHARE_*` and `CNA_SEEK_ORIGIN_*` carry the exact
canonical ordinals, pinned by static assertions in the adapter. `CNA_FILE_SHARE_*` is a bit set;
unknown bits are rejected. The canonical container currently ignores the share selection, so
`cna_storage_container_open_file_share` differs from `cna_storage_container_open_file_access` only
in which selection the caller states explicitly.

## Application name and storage root

`cna_storage_set_app_name_ext` maps the `SetAppNameEXT` extension and must be called before any
storage access, as in the canonical API; it resets the cached root. `cna_storage_get_root_size_ext`
and `cna_storage_copy_root_ext` map `GetStorageRootEXT`.

The root is derived from the platform preference path, so a test suite or tool that must not write
into the real user data directory should pin the platform's data-home environment variable rather
than expecting the C API to accept an absolute root.

## Events

`StorageDevice::DeviceChanged` is a canonical *static* event, so
`cna_storage_device_subscribe_device_changed` takes no device handle: the subscription belongs to
the process, not to any one handle. `StorageContainer::Disposing` is per-instance and is subscribed
through the container handle. Both return an owned registration handle whose release unsubscribes;
releasing twice returns `CNA_RESULT_INVALID_HANDLE`. Disposal is idempotent and raises `Disposing`
exactly once.

## Failures

| Canonical failure | C result |
|---|---|
| `StorageDeviceNotConnectedException` | `CNA_RESULT_INVALID_STATE` / `CNA_ERROR_CATEGORY_STATE` |
| `System::IO::IOException` and subclasses, `std::filesystem::filesystem_error` | `CNA_RESULT_IO` / `CNA_ERROR_CATEGORY_IO` |
| `System::ArgumentException`, empty relative paths, unknown identities | `CNA_RESULT_INVALID_ARGUMENT` |
| `System::NotSupportedException` (unwritable or unreadable stream) | `CNA_RESULT_NOT_SUPPORTED` |
| `System::ObjectDisposedException` | `CNA_RESULT_INVALID_STATE` |

`cna_storage_device_delete_container` keeps the canonical containment guard: a title name that is
absolute or escapes the storage root is refused with `CNA_RESULT_INVALID_ARGUMENT` instead of being
resolved. No native exception object, type name or C++ throw crosses the ABI; messages reach the
caller through the per-thread UTF-8 diagnostic.
