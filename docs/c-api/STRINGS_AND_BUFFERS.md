# CNA C API Strings, Buffers and Collections

## UTF-8 strings

All text at the ABI boundary is UTF-8. The public string view is:

```text
typedef struct CNA_StringView {
    const char* data;
    uint64_t byte_length;
} CNA_StringView;
```

`data == NULL` is permitted only with `byte_length == 0`; a non-null empty string is also valid.
The byte sequence is not NUL-terminated by contract and CNA never reads beyond `byte_length`.
Input text must be valid UTF-8 scalar-value encoding. APIs whose semantic value is a path, asset
name, window title or identifier additionally reject embedded U+0000 with
`CNA_RESULT_ENCODING`; consumers must not rely on C-string truncation.

CNA converts UTF-8 to/from its native string representation at the boundary. No `wchar_t`, locale
encoding, `System::String`, `std::string` storage or temporary pointer becomes part of the public
ABI.

## Caller-owned output

Returned variable-size content uses two calls: a size query followed by a copy. For example:

```text
CNA_Result cna_<object>_get_<value>_size(CNA_Handle object, uint64_t* out_bytes);
CNA_Result cna_<object>_copy_<value>(CNA_Handle object,
                                     void* destination,
                                     uint64_t capacity,
                                     uint64_t* out_bytes);
```

The size result counts bytes, not an implied terminator. Text-copy functions may offer a separate
documented NUL-terminating convenience variant only if its required capacity and behavior are
unambiguous. The base copy operation returns raw UTF-8 bytes.

For a null destination with nonzero capacity, CNA returns `CNA_RESULT_INVALID_ARGUMENT`. For a null
destination and zero capacity, the function reports the required count and returns
`CNA_RESULT_BUFFER_TOO_SMALL` unless the required count is zero. On insufficient capacity CNA does
not write a partial logical element and returns the full required count. The API never allocates an
output buffer on behalf of a C caller in the initial ABI.

## Bulk data

Binary resource transfer uses a pointer plus `uint64_t` element/byte count. Typed APIs state both
the element type and whether their count is bytes or elements. Null with a zero count is valid;
null with a nonzero count is `CNA_RESULT_INVALID_ARGUMENT`. All multiplication, offset and native
size conversion is checked; values unrepresentable by the native implementation return
`CNA_RESULT_OVERFLOW`.

Texture uploads, readback, vertices, indices, audio samples and sprite commands transfer ranges in
bulk. The C API must not require a C program to perform one native transition per element.

The initial `Texture2D` transfer route accepts an exact width-times-height array of `CNA_Color`
values for Color-format mip level zero. The adapter converts each independently laid out C value;
it never reinterprets the C array as C++ `Color`. Readback reports the required pixel count, writes
nothing when capacity is insufficient and performs one canonical `Texture2D::GetData` call after
capacity validation. Other surface formats and mip/sub-rectangle transfers remain unavailable in
this initial slice and return the documented argument or not-supported result rather than silently
changing representation.

## Collections

No `std::vector`, Sharp Runtime collection, iterator or container pointer crosses the boundary.
Public collection APIs use one of these patterns:

- count + copy into a caller-owned typed array;
- lookup by UTF-8 name or fixed ID returning an owned/borrowed handle;
- indexed query returning one POD/handle;
- explicit enumeration handle when a live snapshot is too large to copy.

The documentation for every collection states snapshot versus live semantics, mutation behavior,
element ownership and thread affinity. Dictionary-like APIs expose lookup/key enumeration rather
than a native dictionary layout.
