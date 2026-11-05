<!-- SPDX-License-Identifier: MS-PL -->
# Intermediate XML malformed UTF-8 regression

`seed-9949.minimized.xml` is the byte-for-byte MTH-SAFE-007 minimized seed 9949.
It is deliberately not UTF-8 text: `<XnaContent><Asset>` + D9 + `</Asset></XnaContent>`.
Length: 41 bytes. SHA-256: b356e0017996a542439ca144e60abdfa153f5e5c96a5805bf4f24564e38f9609.

Oracle: UTF-8 requires a continuation byte after D9. Read must refuse this input;
XNA Refresh `IntermediateSerializer.Deserialize<T>` IL_0081..00a5 wraps the XML error.
This is a hostile-input fixture, separate from the genuine-XNA reference corpus.
The regression test consumes this file and pins the literal bytes.

Replay from the CNA root after rebuilding the existing configured fuzzer target:
`cmake-build-fna3d/cna_xna_intermediate_fuzzer --replay-one tests/fuzz/xna-intermediate/seed-9949.minimized.xml`
Do not regenerate seed 9949 against a changed corpus: replay the retained bytes.
