# SDK pause-clock corpus

The synthetic WAV, authored XAP and untouched SDK-built banks in this folder are
CNA-authored **CC0-1.0** material. No Microsoft SDK binary, header, sample media,
assembly or decompiled IL is redistributed. Test/probe source remains MS-PL.

`clock.wav` has 176400 mono PCM16 samples at 22050 Hz (eight seconds), each equal
to1024. Python's `wave` writes its header; `struct.pack('<h',1024) * 176400`
writes its body. `clock.xap` authors Default, PauseWaves/ClockWave, and
PauseSounds/ClockCue. Track markers 11/22/33/44 are at 500/1000/2000/3000ms.
XactBld3.exe 9.29.1962.0 from DirectX SDK June 2010 builds these outputs:

```text
mkdir native
XactBld3.exe clock.xap native /WINDOWS /F
```

Copy the three banks from `native/`. SDK BuildTime can vary; PROVENANCE.json and
SHA256SUMS identify the retained bytes and tools. Under Wine, author and run only
through `tools/platform/run_gpu_tests_private.sh --exec`; its subprocess must set
this corpus as working directory. No SDK installation or COM registration is
required. Native playback uses Wine's XAudio2 and null audio output.

The permanent [pause-clock report](../../../../../../../docs/xact-pause-clock.md)
describes the ten-cell genuine XACT 3.6 measurement and static XNA Refresh bridge.
The probe takes absolute Windows DLL and corpus paths plus one scenario name:
`control`, `pause`, `unbalanced`, `between` or `twice`. `unbalanced` means
Pause 200, another Pause 600, then only one Resume 1400. It deliberately distinguishes
boolean pause from a nested counter.

From the repository root, compile the own
`tools/xna-oracle/XactPauseClockProbe.cpp` using clang++ targeting
`i686-w64-windows-gnu` or `x86_64-w64-windows-gnu`, with `-std=c++23
-fms-extensions -Wno-invalid-token-paste` and the externally supplied June 2010 SDK
include directory. For a case-sensitive host, copy its required unchanged headers
to the stable ignored `build-probe/xact-microsoft/sdk/` and additionally copy
`XAudio2.h` as `xaudio2.h`. Link with the matching MinGW g++, `-municode
-static-libgcc -static-libstdc++ -lole32 -luuid -ladvapi32`. Keep all build products
under `build-probe/xact-microsoft/`. Explicitly load the authentic February 2010
`xactengine3_6.dll`, not a Wine builtin or3.7. SHA256s and compilation details are
recorded in provenance/campaign evidence; binaries and SDK headers stay external.

CNA's deterministic tests consume these same banks. Their exact logical clock
assertions derive from authored marker times and measured pause exclusion, not
from copying CNA output or fitting native callback jitter. The native probe has
real scheduling/playback through null output, while the CNA tests observe event
dispatch. Neither establishes decoded PCM output or a public marker API.
