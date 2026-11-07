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
| cat_fail | 255/REPLACE_OLDEST; category maximum1/FAIL | Prepare-time versus Play-time rejection controls; SDK emits a simple cue |
| cat_fail_tail | 2/REPLACE_OLDEST; category maximum1/FAIL | Stored50ms cue tail under category FAIL; authored-stop admission regression |
| cue_oldest_two | 2/REPLACE_OLDEST | Prepare order versus reversed Play order |
| cat_oldest_two | 255/REPLACE_OLDEST; category maximum2 | Prepare order versus reversed Play order |

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

TQ-XACT-27's `cat_fail_tail` deliberately gives ClockCue maximum2 while Default
has maximum1/FAIL. This preserves a complex cue's stored50ms fade; the old
category-only maximum255 fixture is optimized by the SDK into a simple cue and
cannot qualify that tail. The same retained native probe runs `none` and `stopping`
on this profile, and `stopping` on cue_fail, both architectures (six cells).
ADMISSION_TAIL_PROVENANCE.json records the source/engine references, matrix/input
hashes and outcomes. In all four tail cells L remains state16/count0 when I is
admitted, while both full-capacity category controls reject Prepare.

TQ-XACT-28 retains `XactPreparedCueProbe.cpp`, compiled by the same commands.
It accepts the prior modes plus `evicted-only`, `replay-order`, `reverse-play`,
and `no-wave`. Preparation/bank ownership and ordering use20 x86/x64 cells:
18 successful positives and two expected no-wave Prepare errors (8ac70013,
probe exit2). The no-wave mode deliberately omits wave creation; the other modes
must load it. PREPARED_CUE_PROVENANCE.json records outcomes and hashes.
`reverse-play` uses the two new maximum2 profiles; `replay-order` uses maximum1.
Bank-state bit0x80 is the measured managed IsInUse bridge on both banks.
These are reference measurements for the upcoming CNA preparation repair, not
claims that its existing lazy GetCue implementation already conforms.

Validate the retained matrix and exact corpus/probe identities from the repository
root with `python3 tools/xna-oracle/check-xact-prepared-oracle.py <matrix.json>`.
It distinguishes the two intentional missing-wave errors from positive cases and
checks prepared ownership, selection/replay order and pinned engine/input hashes.
The capture driver and raw outputs are retained in the campaign evidence directory.
Four altered-evidence controls verify rejection of lost ownership, wrong selection,
an accepted missing-wave case and different bank bytes.

TQ-XACT-29 uses the unchanged prepared-cue probe's `stopping` mode on `cue_fail`
and `cat_fail_tail`, both architectures. Before I is prepared, L alone remains
Stopping16/count0 after20ms while both banks retain INUSE (128/132). The four-cell
matrix and exact inputs are identified in TAIL_BANK_PROVENANCE.json. Verify with
`python3 tools/xna-oracle/check-xact-tail-bank-oracle.py <matrix.json>` from the repo
root. Four changed-evidence controls reject either lost bank flag, an absent tail
or different input bytes. These qualify authored-tail ownership independently of
counts; the separate prepared-cue runtime repair remains open.
