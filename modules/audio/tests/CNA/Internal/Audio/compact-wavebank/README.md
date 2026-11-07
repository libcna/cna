# Compact PCM wave-bank corpus

These synthetic WAVs, XAP projects and SDK-built banks are CNA-authored
**CC0-1.0** material. No SDK binary, header, sample media or game recording is
redistributed. Framework/test source remains MS-PL.

Each source is mono PCM16 at22050Hz: `a.wav` has8 samples of4096, `b.wav` has7
of8192 and `c.wav` has33 of12288. Python's `wave` module writes each header and
`struct.pack('<h', marker) * frames` writes its body. The two projects reference
these same files. The normal project has Streaming=0/Compact=0; the compact
project has Streaming=1/Compact=1. Outputs are untouched Windows banks from
DirectX SDK June2010 XactBld3.exe9.29.1962.0. From this directory, with an
existing `native` output directory, run the SDK tool:

```text
XactBld3.exe normal.xap native /WINDOWS /F
XactBld3.exe compact.xap native /WINDOWS /F
```

Copy the resulting banks from `native/`. SDK BuildTime can change on a rebuild;
`PROVENANCE.json` and `SHA256SUMS` identify the retained exact outputs. Under
Wine run the tool through `tools/platform/run_gpu_tests_private.sh --exec` with
an explicit subprocess working directory; the private runner enters the repo
root. No SDK installation or COM registration is needed for authoring.

| Bank | Alignment | PCM offsets | PCM lengths | Whole file |
|---|---:|---|---|---:|
| normal | 4 | 220,236,252 | 16,14,66 | 320 |
| compact | 2048 | 2048,4096,6144 | 16,14,66 | 8192 |

The compact words are FE000000, FE400001 and F7C00002: offsets0/1/2 sectors
and deviations2032/2034/1982 bytes. Every PCM body is byte-identical to its
source WAV body, and all inter-entry/tail slack is zero. The final span is
6144−4096=2048 bytes; subtracting its1982-byte deviation leaves66 bytes,
exactly33 samples. Ignoring that deviation appends991 silent samples.

Microsoft [DirectXTK's pinned ComputeLocations](https://github.com/microsoft/DirectXTK/blob/861f3e7df437b50d1f28c964769329e9b23a50fb/Audio/WaveBankReader.cpp#L300-L317)
subtracts deviation for the last entry as well as other entries. Its source
hash is retained in the provenance. This is SDK-format evidence, independent
of CNA output. Pinned FAudio instead ignores final deviation and has separate
compact stride/format/non-last-length defects; CNA does not adopt those defects.

Genuine Microsoft XACT3.7 x86/x64 under Wine accepts and prepares the compact
stream with count3, but its bank/instance property calls interpret compact words
as ordinary metadata. They return unusable formats/durations; later entries'
Prepare calls fail8AC70019. Those diagnostics **do not qualify compact duration
or playback**. The same runtime returns the correct8/7/33 durations and shared
format for the normal bank. Probe vtable addresses are checked against the
explicitly loaded genuine DLL; Wine supplies XAudio2. No PCM is played and no
managed XNA or native Windows parity is claimed. Full receipts and probe source
are archived at `../cnaplan/evidence/rev002/xact-compact-tail/` from the repo root.
