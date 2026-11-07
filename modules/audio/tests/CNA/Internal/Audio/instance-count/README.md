# SDK instance-count corpus

The synthetic PCM WAVs, authored XAP projects and untouched SDK output banks are
CNA-authored **CC0-1.0** material. Probe/test source is MS-PL. No Microsoft SDK
binary, header, assembly, sample media or decompiled IL is redistributed.

All profiles have one mono PCM16 wave at22050Hz, every sample1024, and two cue
definitions, ClockCue and OtherCue. The long source has176400 frames (eight seconds)
and markers11/22/33/44 at500/1000/2000/3000ms. `short` has2205 frames (100ms), with
no markers. Both cues refer to the same sound; their instance counts remain distinct.

| Profile | ClockCue maximum/behavior | Use |
|---|---|---|
| unlimited | 255/REPLACE_OLDEST | Per-handle initialization, publication and bank/definition scope |
| cue_two | 2/FAIL, authored fade-out50ms | Stopping tail and direct destruction |
| short | 2/FAIL, authored fade-out50ms | Real end-of-wave count publication |
| cue_fail | 1/FAIL | Native failed-play evidence; CNA admission repair remains open |
| cue_oldest | 1/REPLACE_OLDEST | Cue replacement controls and eviction fade regressions |
| cat_oldest | 255/REPLACE_OLDEST; category maximum1 | Category replacement controls and eviction fade regressions |
| cat_fail | 255/REPLACE_OLDEST; category maximum1/FAIL | Prepare-time versus Play-time rejection controls |

Except the category-limit profiles, Default has maximum255/REPLACE_OLDEST.
Its fade-in60/fade-out70ms applies to every profile. ClockCue
has fade-in40/fade-out50ms. XAP Behavior0 means FAIL, Behavior2 REPLACE_OLDEST.
The SDK adds the standard NumCueInstances variable (public/read-only/cue-local).

Generate each WAV with Python `wave`, `struct.pack('<h',1024) * frames`, one channel,
width2 and rate22050. From each profile's directory run the owner's existing
DirectX SDK June2010 XactBld3.exe9.29.1962.0:

```text
mkdir native
XactBld3.exe clock.xap native /WINDOWS /F
```

Retain `native/clock.xgs`, `.xsb` and `.xwb` unchanged. BuildTime can vary; the
manifest identifies the exact retained bytes. Under Wine author/run only through
`tools/platform/run_gpu_tests_private.sh --exec`, setting the profile as working
directory inside the child. No installation or COM registration is needed.

The [lifecycle report](../../../../../../../docs/xact-instance-limits.md) gives
native measurements, the exact XNA Refresh bridge, CNA regressions and limitations.
To reproduce the retained count probes, use the compilation instructions in the
[pause-clock corpus](../pause-clock/README.md), replacing the source with
`tools/xna-oracle/XactInstanceCountScopeProbe.cpp` or
`tools/xna-oracle/XactInstanceCountEndProbe.cpp`. Keep outputs under
`build-probe/xact-microsoft/`. Pass absolute Windows paths:

```text
XactInstanceCountScopeProbe.exe genuine-xactengine3_6.dll unlimited-profile-directory
XactInstanceCountEndProbe.exe genuine-xactengine3_6.dll cue_two-profile-directory destroy
XactInstanceCountEndProbe.exe genuine-xactengine3_6.dll cue_two-profile-directory authored
XactInstanceCountEndProbe.exe genuine-xactengine3_6.dll short-profile-directory natural
```

Use matching x86/x64 genuine DLLs and probes, private display and null output.
These run native Microsoft scheduling under Wine, with no managed-wrapper,
native Windows or PCM-output parity claim. Exact input/output hashes, representative
observations and complete-matrix hashes are in PROVENANCE.json; full raw logs,
additional native admission profiles and initial probe revisions are in campaign evidence.

CNA tests read the untouched banks. Their clock checks derive from authored inputs
and the measured publication rules; expected values do not come from CNA output.
The literal RPC/interactive fixtures separately exercise internal consumers using
FAudio's documented table layout and authored linear pitch equations. They are
regressions for the shared value, without a Microsoft RPC/interactive playback claim.

The private-variable consumer controls use the SDK XACT Variables topic, Variable
Properties/Visibility: privacy limits programmer access while retaining content access.
The exact local documentation hash is recorded in provenance. These synthetic controls
do not establish native Microsoft private-variable or RPC/interactive execution.

TQ-XACT-26 adds `tools/xna-oracle/XactInstanceLimitProbe.cpp`, compiled with the same
SDK/MinGW commands. Pass DLL, profile directory and a mode: `none`, `before`, `after`,
`destroyed`, `paused` or `stopping`. The OLDest profiles use the first five modes;
FAIL profiles use `none`, `before` and `stopping`. Thirty-two x86/x64 cells,
observations, hashes and exact limitations are in INSTANCE_LIMIT_PROVENANCE.json.
The native controls qualify public states/counts and admission outcomes. They do not
measure PCM gain. Deterministic fade/deadline and overlapping-victim guards use the
FAudio update/candidate predicates, while retaining Microsoft's public state bits.
