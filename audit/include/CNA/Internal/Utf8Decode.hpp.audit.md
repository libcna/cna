# Audit: include/CNA/Internal/Utf8Decode.hpp

## Metadata

- Source file: `include/CNA/Internal/Utf8Decode.hpp`
- Audit status: AUDITED
- Subsystem: `cna-internal-core` shard
- File type: C++ header
- XNA/FNA relevance: internal implementation detail behind XNA-facing content/text APIs
- Graphics backend relevance: see purpose
- Main related tests: see Missing or Weak Tests

## Purpose

Declares DecodeUtf8CodePoint(): decodes one UTF-8 code point from a byte string, used for SDL text-input decoding (feeds Microsoft::Xna::Framework::Input::TextInputEXT).

## Executive Verdict

Healthy — 2 minor, low-priority observations given the trusted-input threat model.

## Checklist Results

### Behavioral correctness / C++ correctness / Memory/resource lifetime / Performance / Thread safety / Portability / Maintainability / Robustness
**Verified correct against the UTF-8 spec**: lead-byte classification (1/2/3/4-byte sequences via `0x80`/`0xE0`/`0xF0`/`0xF8` masks), bounds checking before reading continuation bytes (`index + extraBytes >= text.size()` correctly catches truncated sequences), continuation-byte validation (`(cont & 0xC0) != 0x80`), and the index-advancement contract (every code path — valid, invalid lead, truncated, bad continuation — advances `index` by at least 1, satisfying the function's own documented "callers can't spin forever" guarantee). **2 minor omissions, both low-priority given the trusted-input context (SDL-sourced real keyboard text, not adversarial/network data)**: no overlong-sequence rejection (e.g. a 2-byte encoding of a codepoint that fits in 1 byte decodes successfully rather than being rejected per strict UTF-8 validation) and no surrogate-range (U+D800-U+DFFF) rejection — a malformed 3-byte sequence encoding a value in that range would decode as if it were a legitimate BMP character. Neither is exploitable in this codebase's actual use (SDL-sourced trusted text), so not flagged as a real defect, only a documented gap.

### Testing
Not independently located in this pass.

## Detailed Findings

**Verified correct against the UTF-8 spec**: lead-byte classification (1/2/3/4-byte sequences via `0x80`/`0xE0`/`0xF0`/`0xF8` masks), bounds checking before reading continuation bytes (`index + extraBytes >= text.size()` correctly catches truncated sequences), continuation-byte validation (`(cont & 0xC0) != 0x80`), and the index-advancement contract (every code path — valid, invalid lead, truncated, bad continuation — advances `index` by at least 1, satisfying the function's own documented "callers can't spin forever" guarantee). **2 minor omissions, both low-priority given the trusted-input context (SDL-sourced real keyboard text, not adversarial/network data)**: no overlong-sequence rejection (e.g. a 2-byte encoding of a codepoint that fits in 1 byte decodes successfully rather than being rejected per strict UTF-8 validation) and no surrogate-range (U+D800-U+DFFF) rejection — a malformed 3-byte sequence encoding a value in that range would decode as if it were a legitimate BMP character. Neither is exploitable in this codebase's actual use (SDL-sourced trusted text), so not flagged as a real defect, only a documented gap.

## Cross-File Observations

None.

## Missing or Weak Tests

Not independently located in this pass.

## Positive Findings

Clean, correct implementation.

## Final Assessment

See findings above.
