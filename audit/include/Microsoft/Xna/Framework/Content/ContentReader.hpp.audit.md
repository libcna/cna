# Audit: include/Microsoft/Xna/Framework/Content/ContentReader.hpp

## Metadata
- Source file: `include/Microsoft/Xna/Framework/Content/ContentReader.hpp`
- Audit status: AUDITED (full read, 446 lines)
- Subsystem: `xna-content` shard
- File type: C++ header
- XNA/FNA relevance: Direct XNA type; FNA reference: `src/Content/ContentReader.cs`
- Main related tests: not independently located in this pass

## Purpose
Declares `ContentReader`: the object-graph reader passed to every `.xnb` type reader's `Read()`,
implementing the 1-based type-reader dispatch protocol, shared-resource fixups, and
`ReadExternalReference<T>()`.

## Executive Verdict
Needs attention — a confirmed HIGH-severity finding, detailed fully in the paired `.cpp` report
since the vulnerability lives in the implementation of the path-resolution helper this header only
declares the method signature for: `ReadExternalReference<T>()`'s doc comment (lines 120-152)
explicitly claims a hardening addition beyond FNA — "a reference that resolves above the content
root's own logical space is rejected outright" — but the actual containment check only catches a
relative `..`-style escape, not an absolute-path escape, which `std::filesystem::path`
concatenation does not protect against on its own. The rest of the class (the 1-based dispatch
protocol, `ReadSharedResource<T>()`'s fixup-queueing, the `NOXNA` hardening helpers
`CheckCollectionElementCount`/`CheckDecodedByteSize`/`ReadBytesExactOrThrow`) is well-designed and
clearly disclosed.

## Checklist Results

### HIGH: `ReadExternalReference<T>()`'s documented containment guarantee has an absolute-path bypass
Lines 120-152's doc comment states plainly: "a reference that resolves above the content root's
own logical space is rejected outright rather than attempting to load whatever happens to be
there." The actual check (implemented in the paired `.cpp`, see that report for the full mechanism)
only rejects a resolved path that is exactly `".."` or begins with `"../"` — it does not reject a
resolved path that is *absolute* (e.g. `/etc/passwd`, or a Windows drive-rooted path), which
`std::filesystem::path`'s own concatenation semantics let bypass the base-directory prefix entirely.
This means the documented containment guarantee is incomplete: an absolute-path external reference
in a crafted `.xnb`/`.cnj` file escapes the content root silently, contradicting the very doc
comment that claims otherwise. Full mechanism and file-load-chain detail in
`src/Microsoft/Xna/Framework/Content/ContentReader.cpp.audit.md`.

### Positive: `CheckCollectionElementCount`/`CheckDecodedByteSize`/`ReadBytesExactOrThrow` are well-designed, disclosed hardening additions
Lines 283-341: each is clearly justified (a deliberately *separate* check from the other so a
legitimate large texture isn't rejected by a collection-count-tuned limit; `int64_t`-widening
callers so the multiplication itself can't wrap; an exact-length-or-throw read to prevent a
downstream raw-pointer API from trusting an original requested length instead of the actually-read
length). These are exactly the kind of NOXNA security hardening this project does well elsewhere
(e.g. the `XnbReadLimits`-consuming checks confirmed in the `cna-internal-core` shard).

## Detailed Findings
1. **[HIGH] `ReadExternalReference<T>()`'s documented "rejected outright" containment guarantee has
   an absolute-path bypass** — declared lines 120-152; full mechanism in
   `src/Microsoft/Xna/Framework/Content/ContentReader.cpp.audit.md`.

## Cross-File Observations
- `maxObjectNestingDepth` (from `XnbReadLimits`, audited in the `cna-internal-core` shard as a dead
  limit with zero consumers anywhere) is confirmed to have zero consumers here too:
  `InnerReadObject<T>()`/`ReadObject<T>()` thread no depth counter through the recursive
  object-graph read at all. This reconfirms (does not newly discover) that finding from this
  reader's own recursive-dispatch protocol specifically.
- `maxSharedResourceCount` (also from `XnbReadLimits`, previously only confirmed via grep as "a
  symbol referenced there") is confirmed genuinely enforced here — see the paired `.cpp` report,
  `InitializeTypeReaders()`.

## Missing or Weak Tests
Not independently located in this pass. A test constructing a `.xnb`/`.cnj` file whose external
reference is an absolute path (or a Windows-style rooted path under the Emscripten/native-Windows
build) and asserting the load is rejected would directly catch finding #1.

## Positive Findings
The class-level doc comment (lines 40-53) is a strong disclosure of the deliberate deviation from
FNA's always-non-null `ContentManager` (a null manager is explicitly supported here, for
standalone use ahead of full `.xnb` integration), and the `existingInstance`-as-`std::optional<T>`
design is consistently applied and well-justified throughout.

## Final Assessment
One HIGH finding: a documented containment guarantee ("rejected outright") that has a real,
concrete absolute-path bypass. Recommend escalating to `AUDIT_CROSS_CUTTING_FINDINGS.md` given the
"a security control exists but has an exploitable gap" pattern, similar in shape (though narrower
in practical exploitability, being gated to two loose-file-registered asset types today) to the
`StorageDevice::DeleteContainer()` finding from the `xna-storage` shard.
