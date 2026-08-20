# LZX differential-test reference outputs (plans/plan_xnb.md XNB-30A)

These two files are the exact decompressed bytes produced by **FNA's own, unmodified
`src/Content/LzxDecoder.cs`**, run via Mono against the compressed payloads inside the sibling
`Explosion.xnb` and `FontCalibri14.xnb` fixtures. They exist so `LzxDecoderDifferentialTests.cpp`
can assert CNA's C++ LZX decompressor produces byte-identical output to the actual reference
implementation it was ported from -- not just structurally-plausible output.

## How they were produced (reproducible)

1. Copy FNA's real `src/Content/LzxDecoder.cs` unmodified into a scratch directory.
2. Add a small `Program.cs` driver that replicates FNA's own
   `ContentManager.GetContentReaderFromXnb()` block-framing loop verbatim (2-byte/5-byte block
   headers, 32KB default frame size, `0xFF` frame-size-override signal -- see that method in
   `src/Content/ContentManager.cs`), driving the real `LzxDecoder` class, reading a raw compressed
   payload file and writing the decompressed bytes to an output file.
3. Extract each fixture's raw compressed payload (the bytes after the 14-byte
   header-plus-decompressed-size prefix) with a short Python script.
4. `mcs LzxDecoder.cs Program.cs -out:LzxDiff.exe` (Mono C# compiler).
5. `mono LzxDiff.exe <payload> <decompressedSize> <output>` for each fixture.

Verified 2026-07-16 against CNA's own decompressor (`CNA::Internal::Xnb::DecompressXnbPayload`)
compiled as a standalone tool linked against the real `libCNA.a`/`libSHARP_RUNTIME.a`: both
outputs matched byte-for-byte (SHA-256 identical) before these reference files were vendored here.
`Explosion.xnb` exercises a single 32KB-or-smaller LZX block; `FontCalibri14.xnb` exercises a real
multi-block decode (44032 decompressed bytes, spanning more than one 32KB frame), so together they
cover both the single- and multi-block code paths against the actual reference decoder, not a
re-derivation of it.
