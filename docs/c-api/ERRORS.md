# CNA C API error contract

The public declarations in [`abi.h`](../../modules/c-api/include/CNA/C/abi.h) and
[`core.h`](../../modules/c-api/include/CNA/C/core.h) are the source of truth for the fixed-width
result and error-record ABI. This page explains how those values are produced, retained and
consumed across the rest of the C API.

## Results

Every fallible operation returns `CNA_Result`, a `uint32_t` typedef. These numeric values are part
of the ABI:

| Constant | Value | Meaning |
|---|---:|---|
| `CNA_RESULT_SUCCESS` | 0 | Operation completed. |
| `CNA_RESULT_INVALID_ARGUMENT` | 1 | Nullability, range, structure-prefix or argument invariant failed. |
| `CNA_RESULT_INVALID_HANDLE` | 2 | Handle is null where forbidden, stale, the wrong kind or owned by another runtime. |
| `CNA_RESULT_INVALID_STATE` | 3 | Call is valid in general but not in the object's current state. |
| `CNA_RESULT_OUT_OF_MEMORY` | 4 | Native allocation failed. |
| `CNA_RESULT_IO` | 5 | File, stream, storage or content input/output failed. |
| `CNA_RESULT_NOT_SUPPORTED` | 6 | The selected renderer, platform or API configuration cannot perform the operation. |
| `CNA_RESULT_PLATFORM` | 7 | A native platform service failed. |
| `CNA_RESULT_THREAD` | 8 | The call occurred on a disallowed thread. |
| `CNA_RESULT_CALLBACK` | 9 | A registered failure-reporting callback returned non-success. |
| `CNA_RESULT_OVERFLOW` | 10 | A size or numeric conversion cannot be represented safely. |
| `CNA_RESULT_ENCODING` | 11 | Required UTF-8 or another documented text constraint failed. |
| `CNA_RESULT_INTERNAL` | 12 | CNA caught a native failure with no more specific public result. |
| `CNA_RESULT_SHUTTING_DOWN` | 13 | Runtime shutdown prevents the operation. |
| `CNA_RESULT_BUFFER_TOO_SMALL` | 14 | A caller-owned destination cannot hold the complete output. |

No C++ exception, Sharp Runtime exception or platform exception may cross the C ABI. The exception
firewall translates known exception families to the result set above and catches every remaining
failure as `CNA_RESULT_INTERNAL`.

## Error categories

`CNA_ErrorCategory` is a second fixed-width `uint32_t`. It groups results by the kind of recovery a
consumer can attempt; it is not a second result code.

| Category | Value | Results normally assigned to it |
|---|---:|---|
| `CNA_ERROR_CATEGORY_NONE` | 0 | `CNA_RESULT_SUCCESS`; also the initial empty record. |
| `CNA_ERROR_CATEGORY_ARGUMENT` | 1 | `CNA_RESULT_INVALID_ARGUMENT` for ordinary argument-contract failures. |
| `CNA_ERROR_CATEGORY_HANDLE` | 2 | `CNA_RESULT_INVALID_HANDLE`. |
| `CNA_ERROR_CATEGORY_STATE` | 3 | `CNA_RESULT_INVALID_STATE`. |
| `CNA_ERROR_CATEGORY_MEMORY` | 4 | `CNA_RESULT_OUT_OF_MEMORY`. |
| `CNA_ERROR_CATEGORY_IO` | 5 | `CNA_RESULT_IO`. |
| `CNA_ERROR_CATEGORY_NOT_SUPPORTED` | 6 | `CNA_RESULT_NOT_SUPPORTED`. |
| `CNA_ERROR_CATEGORY_PLATFORM` | 7 | `CNA_RESULT_PLATFORM`. |
| `CNA_ERROR_CATEGORY_THREAD` | 8 | `CNA_RESULT_THREAD`. |
| `CNA_ERROR_CATEGORY_CALLBACK` | 9 | `CNA_RESULT_CALLBACK`. |
| `CNA_ERROR_CATEGORY_RANGE` | 10 | `CNA_RESULT_OVERFLOW`, `CNA_RESULT_BUFFER_TOO_SMALL`, and range exceptions translated to `CNA_RESULT_INVALID_ARGUMENT`. |
| `CNA_ERROR_CATEGORY_ENCODING` | 11 | `CNA_RESULT_ENCODING`. |
| `CNA_ERROR_CATEGORY_INTERNAL` | 12 | `CNA_RESULT_INTERNAL` and any otherwise unknown result. |
| `CNA_ERROR_CATEGORY_SHUTTING_DOWN` | 13 | `CNA_RESULT_SHUTTING_DOWN`. |

The category can therefore carry useful information not implied by the result alone. In
particular, a translated `std::out_of_range` is `CNA_RESULT_INVALID_ARGUMENT` with
`CNA_ERROR_CATEGORY_RANGE`, while an ordinary invalid argument uses
`CNA_ERROR_CATEGORY_ARGUMENT`.

## Per-thread error record

Each thread starts with an empty record: success, category `NONE`, zero message bytes and no
supplemental exception data. An ordinary failed C API operation replaces the record on its calling
thread; records on other threads are independent. The next recorded failure replaces the complete
record, including any exception type, parameter name or subsystem payload described below. The core
observation routes described below are the deliberate non-recording exception.

`CNA_ErrorInfo` version 1 contains only:

```text
uint32_t          struct_size
uint32_t          struct_version
CNA_Result        result
CNA_ErrorCategory category
uint64_t          message_byte_length
```

It contains no message pointer, native exception pointer or subsystem-specific payload. Initialize
`struct_size` and `struct_version` before passing it to
`cna_error_get_last_info`. The core observation routes are:

```text
CNA_Result cna_error_get_last_info(CNA_ErrorInfo* out_info);
CNA_Result cna_error_get_last_message_size(uint64_t* out_bytes);
CNA_Result cna_error_copy_last_message(char* destination,
                                       uint64_t capacity,
                                       uint64_t* out_bytes);
```

These `cna_error_*` queries never overwrite the record they inspect, including when the query
itself returns `CNA_RESULT_INVALID_ARGUMENT` or `CNA_RESULT_BUFFER_TOO_SMALL`. A successful
ordinary direct call also leaves the previous failure available, so cleanup can run before the
caller copies the diagnostic.

The one deliberate success exception is opt-in foreign-thread dispatch. If a refused call is
queued through `cna_game_set_foreign_thread_calls_ext`, the serving game thread's failure record is
copied back to the waiting caller; a successfully served call resets that caller's record to the
initial empty state. See [Callbacks and threading](CALLBACKS_AND_THREADING.md#opt-in-foreign-thread-service).

Every error-text copy route reports the exact UTF-8 byte count without a terminator. Insufficient
capacity returns `CNA_RESULT_BUFFER_TOO_SMALL` and writes no partial result. A zero-length value
therefore needs no destination. Messages are diagnostics for people and logs: consumers must never
branch on their wording.

## Exception translation and binding metadata

The firewall preserves the important behavioral distinction between common native exception
families:

| Exception family | Result | Category |
|---|---|---|
| `std::bad_alloc` | `OUT_OF_MEMORY` | `MEMORY` |
| numeric overflow/range error | `OVERFLOW` | `RANGE` |
| `std::out_of_range` | `INVALID_ARGUMENT` | `RANGE` |
| argument or invalid-cast exception | `INVALID_ARGUMENT` | `ARGUMENT` |
| invalid operation, bare logic error, device lost/reset, disconnected storage, sensor or session-join failure | `INVALID_STATE` | `STATE` |
| filesystem, stream, content-load or unauthorized-access failure | `IO` | `IO` |
| unavailable implementation, audio device, microphone, gamer service or suitable graphics device | `NOT_SUPPORTED` | `NOT_SUPPORTED` |
| native platform-service failure | `PLATFORM` | `PLATFORM` |
| any remaining standard or unknown exception | `INTERNAL` | `INTERNAL` |

Subsystem pages document narrower distinctions, such as gamer-services network and privilege
failures. Typed content loads, for example, use `CNA_RESULT_IO` for missing, unreadable or rejected
asset data and reserve `CNA_RESULT_NOT_SUPPORTED` for an unavailable renderer operation or decoded
resource outside the documented C transfer surface.

For a binding that must re-raise the canonical exception, the error record can also expose the
fully qualified .NET-style type name and an argument exception's parameter name:

```text
CNA_Result cna_error_get_last_exception_type_size_ext(uint64_t* out_bytes);
CNA_Result cna_error_copy_last_exception_type_ext(char* destination,
                                                  uint64_t capacity,
                                                  uint64_t* out_bytes);
CNA_Result cna_error_get_last_exception_param_name_size_ext(uint64_t* out_bytes);
CNA_Result cna_error_copy_last_exception_param_name_ext(char* destination,
                                                        uint64_t capacity,
                                                        uint64_t* out_bytes);
```

Examples are `System.ArgumentOutOfRangeException` and
`Microsoft.Xna.Framework.Content.ContentLoadException`. A failure raised by C-layer validation,
such as a null output or stale handle, has no canonical exception behind it, so both sizes are
zero. The same zero value applies when an exception has no parameter name. These routes use the
same exact-size, no-terminator, no-partial-copy and non-overwriting rules as the message routes.
Exception metadata is for language bindings; plain C code still branches on the result, category
and documented output state.

## Subsystem exception payloads

Two canonical exceptions carry values that neither the general result nor diagnostic text can
represent. Their valid query calls observe the same per-thread record without clearing it:

- `cna_net_get_last_join_error` returns the `NetworkSessionJoinError` from the last translated
  `NetworkSessionJoinException`. When absent, its presence flag is `CNA_FALSE` and the value is the
  documented `SESSION_NOT_FOUND` default.
- `cna_sensors_get_last_error_id_ext` returns the signed error identifier from the last translated
  sensor failure. When absent, its presence flag is `CNA_FALSE` and the value is zero.

Read these payloads immediately after the failed operation. Any later recorded failure clears
them. Unlike the core `cna_error_*` observers, these are ordinary generated subsystem routes: an
invalid call to one of them is itself a failure and can replace the record.

## Callback failures

Before invoking a lifecycle or frame callback, CNA initializes its versioned `CNA_CallbackError`.
The callback returns `CNA_RESULT_SUCCESS` to continue. Any non-success return stops the game at the
documented safe point and is reported by the enclosing game route as `CNA_RESULT_CALLBACK` with
category `CALLBACK`; the callback's own result is deliberately not propagated as the outer result.

On failure the callback may place borrowed UTF-8 bytes in `CNA_CallbackError.message`. CNA copies a
valid, non-empty message before the callback returns. An absent, empty, malformed, embedded-NUL or
otherwise invalid message becomes the stable generic diagnostic
`A C game lifecycle callback failed.`

The game retains its first callback failure. Exit and other completion hooks still run, but a later
callback failure or an unrelated cleanup diagnostic does not replace the original. Terminal run
calls and game destruction reproduce that stored `CNA_RESULT_CALLBACK` and diagnostic until the
game is released.

Callback code must return normally. It must not let a C++ exception, `longjmp` or other nonlocal
control transfer cross CNA frames. Callback lifetime, reentrancy and threading rules are defined in
[Callbacks and threading](CALLBACKS_AND_THREADING.md).

## Consumer rules

- Test the returned `CNA_Result` first; outputs are usable only as their route documents.
- Use `CNA_ErrorCategory` for broad recovery policy and the result for the exact public condition.
- Copy diagnostics before another failing call on the same thread.
- Use the size-then-copy protocol and do not expect a trailing NUL byte.
- Never parse message text or treat exception metadata as the plain-C control-flow contract.

The focused regression boundary is covered by `CApi_ErrorSmoke`, `CApi_BoundaryDetail`,
`CApi_ContentExceptionSmoke`, `CApi_NetSmoke`, `CApi_SensorDeviceSmoke`,
`CApi_SensorEventsSmoke`, `CApi_HostDrivenRunSmoke` and the generated route-validation test.
