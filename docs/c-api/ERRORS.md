# CNA C API Errors

## Result model

All fallible functions return `CNA_Result`, a `uint32_t` typedef. The initial stable numeric set is:

| Constant | Value | Meaning |
|---|---:|---|
| `CNA_RESULT_SUCCESS` | 0 | Operation completed. |
| `CNA_RESULT_INVALID_ARGUMENT` | 1 | Nullability, range, struct-prefix or argument invariant failed. |
| `CNA_RESULT_INVALID_HANDLE` | 2 | Handle is null where forbidden, stale, wrong-kind or foreign-runtime. |
| `CNA_RESULT_INVALID_STATE` | 3 | Call is valid in general but not in the object's current state. |
| `CNA_RESULT_OUT_OF_MEMORY` | 4 | Native allocation failed. |
| `CNA_RESULT_IO` | 5 | File, stream or storage operation failed. |
| `CNA_RESULT_NOT_SUPPORTED` | 6 | Selected renderer/platform/API configuration does not support the operation. |
| `CNA_RESULT_PLATFORM` | 7 | Native platform service failed. |
| `CNA_RESULT_THREAD` | 8 | Call occurred on a disallowed thread. |
| `CNA_RESULT_CALLBACK` | 9 | A registered callback returned failure. |
| `CNA_RESULT_OVERFLOW` | 10 | Input/output size or numeric conversion cannot be represented safely. |
| `CNA_RESULT_ENCODING` | 11 | Required UTF-8 or text constraint failed. |
| `CNA_RESULT_INTERNAL` | 12 | A native failure was caught without a more specific public category. |
| `CNA_RESULT_SHUTTING_DOWN` | 13 | Runtime shutdown prevents the operation. |
| `CNA_RESULT_BUFFER_TOO_SMALL` | 14 | Destination capacity is insufficient; required size is reported. |

No function may let a C++ exception, Sharp Runtime exception or platform exception cross the C ABI.
The adapter catches known native exception categories first, maps them to this set, then catches all
remaining failures as `CNA_RESULT_INTERNAL`.

Typed content loads map missing, unreadable and decoder-rejected asset data to `CNA_RESULT_IO`.
They reserve `CNA_RESULT_NOT_SUPPORTED` for a renderer refusal or a decoded resource outside the
currently documented C transfer subset.

## Error information

On failure, CNA records structured error information for the calling thread. The first ABI exposes:

```text
CNA_Result cna_error_get_last_info(CNA_ErrorInfo* out_info);
CNA_Result cna_error_get_last_message_size(uint64_t* out_bytes);
CNA_Result cna_error_copy_last_message(char* destination,
                                       uint64_t capacity,
                                       uint64_t* out_bytes);
```

`CNA_ErrorInfo` contains only `struct_size`, `struct_version`, `CNA_Result`, a fixed-width public
error category and the UTF-8 message byte length. It contains no native exception pointer or message
pointer. Error information is per thread and is overwritten by the next failed CNA call on that
thread. The three `cna_error_*` queries do not overwrite it. A successful ordinary CNA call leaves
the previous diagnostic available until replaced, allowing cleanup after a failure.

`cna_error_copy_last_message` writes the required byte count to `out_bytes`. If capacity is too
small it returns `CNA_RESULT_BUFFER_TOO_SMALL`, performs no partial UTF-8 character write, and
leaves the required count available. Error text is diagnostic only; programs must branch on
`CNA_Result` and documented output state.

## Callback failures

A callback returning a non-success result stops the enclosing operation at its documented safe
point, records `CNA_RESULT_CALLBACK` as the enclosing failure, and preserves callback-provided
diagnostic data only through the explicit callback error channel defined by that API. Callback code
must not use C++ exceptions or nonlocal control flow across CNA frames.
