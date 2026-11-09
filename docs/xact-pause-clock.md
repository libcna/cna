# XACT pause clock

CNA excludes paused time from cue scheduling. This matches measured Microsoft
retail XACT 3.6 marker dispatch, rather than pinned FAudio's pause/resume accounting
(TQ-XACT-24, whose regressions use the same untouched SDK banks).

## Reference selection

Retained XNA Refresh `Microsoft.Xna.Framework.Xact.dll`4.0.30901.0 has SHA256
`a14d5364dca7cf49fb90639e87ba04d52b59a700dc9198efa5707ce8eae28f0a`.
Its public Cue.Pause/Resume IL_0000–0011 forward TRUE/FALSE to the native wrapper.
UnsafeNativeMethods.Cue.Pause IL_0033–0044 normalizes the boolean and calls
IXACT3Cue vtable byte offset 32, slot8 on x86. XACT3CreateEngine IL_0070–0083
selects retail CLSID248D8A3B-6256-44D3-A018-2AC96C459F47 and
IID B1EE676A-D9CD-4D2A-89A8-FA53EB9E480B. That is XACT 3.6, **not 3.7**.
No redistributed assembly or decompiled IL is included.

The probe directly loads authentic x86/x64 XACT 3.6 DLLs 9.28.1886.0 extracted from
the owner's SDK's February 2010 redistributable cabinets, without installation or
COM registration. It invokes DllGetClassObject with the Refresh retail CLSID and
uses the official SDK's IXACT3 interfaces. The interface IID is unchanged in the
June 2010 headers used for compilation. Loaded paths and vtable method ownership
are checked against that supplied DLL; every loaded audio module is logged.
Wine supplies `xaudio2_6.dll`, and playback uses private display/null audio output.

## Measurement

The [owned corpus](../modules/audio/tests/CNA/Internal/Audio/pause-clock/README.md)
contains one eight-second mono PCM16 wave at 22050 Hz, one cue, and marker IDs
11/22/33/44 authored at 500/1000/2000/3000ms. Real CreateEngine, Initialize,
CreateInMemoryWaveBank, CreateSoundBank, Prepare, Play, Pause, GetState and DoWork
run. Public MARKER notifications, not a private field expression, provide the
observable. QueryPerformanceCounter records callback arrival and brackets pause
calls before any logging; the harness services DoWork approximately every 1 ms.

| Scenario | Calls in nominal wall milliseconds | Marker arrival pattern |
|---|---|---|
| Control | No pause | 500,1000,2000,3000 plus delivery latency |
| One pause | Pause 200, Resume 1400 | 1700,2200,3200,4200 plus latency |
| Duplicate pause | Pause 200, Pause 600, Resume 1400 | Same as one pause; one Resume suffices |
| Between markers | Pause 700, Resume 1900 | 500,2200,3200,4200 plus latency |
| Two pauses | Pause 200/Resume 1400, Pause 1900/Resume 2500 | 1700,2800,3800,4800 plus latency |

All ten architecture/scenario cells complete with four markers in authored order.
Control latency is about 1–3 ms; paused results follow the listed shifts within a
few milliseconds. The callback log also retains native notification timestamps.
State is 72 (PLAYING|PAUSED) while paused and 8 (PLAYING) after the single Resume.
Exact rows, hashes and call intervals live in the corpus provenance. The later
duplicate Pause runs after a 400 ms threshold within the hold; its individual
call timestamp is not logged. T,T,F is the measured duplicate-call sequence. Earlier 3.7
controls showed the same first-pause shift, but do not establish the Refresh
runtime selection; the 3.6 matrix is the reference for this decision.

Pinned FAudio `6839b88e304a046ae1a609ff14a085371e02a4e0` differs. Its unmodified
FACTCue_Pause measured start 1000/elapsed 500 after pause 1500 and start 1000/elapsed
2000 after resume 2500 (injected clock, minimal records). Evaluating its UpdateSound
expression at 2600 gives 3600, while active elapsed excluding pause is 600. CNA keeps
the measured Microsoft pause exclusion and boolean semantics.

## CNA regressions and limits

A parser test checks all four authored IDs and timestamps. Two shared offline
scheduler tests use these exact SDK banks, an injected clock and the real
Cue/AudioEngine controller. They test before/at marker boundaries, no dispatch
while paused, T,T,F with the second Pause later in the hold, a marker before the
pause, cumulative holds and no duplicate dispatch on repeated updates. Both real
mixer adapters run them. Inclusive deadline checks come from FAudio UpdateSound
(`elapsedCue >= timestamp`); the native callback measurement establishes pause
exclusion, not zero-jitter millisecond boundaries. Their observer measures
internal dispatch, not a public
CNA marker-notification API. No API or C ABI surface is added.

The native probe exercises actual Microsoft scheduling under Wine. It does not
execute the managed XNA wrapper, capture/render PCM, measure AttackTime RPC output,
qualify category pauses, Stop while paused, or all host-update cadences. The
Refresh wrapper relationship is static IL evidence; x64 is an additional native
control, not a claim that the retained x86 XNA assembly ran as x64. No native
Windows timing or sample-accurate output is claimed. All SDK binaries and headers
remain external. The own probe is
[`XactPauseClockProbe.cpp`](../tools/xna-oracle/XactPauseClockProbe.cpp).
