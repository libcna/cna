# Native preparation controls — HND-XACT-01

These own XAP/WAV inputs and untouched June2010 SDK banks are CC0-1.0. Probe/test
source is MS-PL. No SDK executable/header, Microsoft runtime DLL, assembly or IL
is redistributed. PROVENANCE.json pins every input/output and the exact native
probe/DLL identities, plus the104-cell matrix hash and archive destination.

Question1 measures resident, weighted, interactive and streaming preparation.
Native controls run on genuine Refresh-selected XACT3.6, x86/x64, through the
private Wine/WoW64 wrapper and null audio. Both retained TQ-XACT-31 controls and
both new resident notification controls pass before shapes. The group contains
86 complete cells and18 expected observed Prepare8ac70013 refusals (probe rc4).
A successful capture does not mean every native operation succeeded.

Cell IDs are raw-log stems:
`native-v3-preparation-<arch>-<profile>-<mode>-250-io<0|1>-<repeat>`.
Every normal/no-properties/missing weighted mode repeats0..7 on both architectures.
Other modes use repeat0. Read matrix-preparation.json in the archive for exact
HRESULTs, independent bank/cue/instance states, call intervals, pointer identities,
public notifications and terminal statuses; do not infer playback from INUSE.

| Cells (both architectures) | Measured behavior used by CNA regressions |
| --- | --- |
| resident/normal; streaming/normal,no-props (io0/1) | Prepare returns cue4, SoundBank128, selected A132 and unused B4; no WAVEPLAY until Play. Natural cue retirement reaches32; stopped handle retains SoundBank128. Used bank remains132 through cue destruction and2000ms, clears only on SoundBank destruction. |
| weighted/normal,no-props, repeats0..7 | Both sound alternatives observed. Exactly the chosen wave bank becomes132 before Play; later WAVEPLAY and marker101/102 identify the same choice. Unchosen loaded bank stays4. No native RNG sequence/distribution claim. |
| weighted/missingA,missingB, repeats0..7 | Chosen missing bank gives Prepare8ac70013/no cue/SoundBank0; loaded alternative can succeed and play without the other bank. Selection does not fall back after a failed draw. |
| streaming/missingA,missingB | Required A absent fails Prepare8ac70013 with no cue/use; unused B may be absent. |
| interactive/normal,no-props,AtoB,BtoA | Prepare cue4/SoundBank128 with both wave banks4. The variable set before Play determines the subsequently playing bank (132) and marker101/102. After native wave STOP/DESTROY, the cue remains8/count1 through4500ms, then explicit Stop settles32/count0. |
| interactive/missingA,missingB | Prepare and Play succeed. Cue remains8/count1 with no wave or marker dispatch through4500ms. Unused bank stays4. |
| interactive/lateB | Loading B after Prepare but before Play permits actual B playback/use. |
| interactive/lateB-playing | Loading B100ms after Play does not retry the failed resolution or produce wave/marker dispatch through4500ms; cue stays8/count1. |
| interactive/streaming unplayed-stop,unplayed-destroy | No playback. A streaming prepared cue retains A132 through handle destruction; interactive never resolves a bank. SoundBank use follows handle lifetime. Stop settles32 by100ms; transient16 is not an exact synchronous contract. |

One second of own mono S16/22050 PCM: A0=1024, A1=3072, B0=2048 for every sample.
SDK WaveBank streaming flags are retained. Weighted/interactive sounds have a
250ms marker to preserve an actual sound table; cue properties confirm two
variations and the expected interactive flag. Native stream creation waits for
PREPARED; optional IO callbacks forward real ReadFile/GetOverlappedResult.

The offline tests consume these exact SDK banks through the real CNA controller
and CnaMixer/manual device. They check preparation/actual voice/state/ownership
separately and consume real PCM EOF. They do not fit native callbacks' arrival
jitter, native rendering speed or gains. The test RNG seeds merely exercise both
supported branches and cached choices; they do not reproduce Microsoft's RNG.

Verify captured evidence with:

```text
python3 tools/xna-oracle/check-xact-preparation-shapes-oracle.py <archive>/matrix-preparation.json
```

The checker verifies all104 cell identities/outcomes, raw stdout/stderr, actual
native LOAD traces, fixture hashes and stable preparation/lifetime/selection
observations. Seven semantic altered-evidence controls are rejected independently
of the raw-hash guard. Full receipts live under
`cnaplan/evidence/rev002/hnd-xact-01-preparation/` (integrator archive).

Scope: native Microsoft interfaces under Wine/null output, static Refresh bridges;
no managed execution, native Windows, gain/PCM or exact hidden prefetch/RNG instant.
The matrix uses one global interactive variable and one wave per selected sound.
Live variable changes after Play, multi-track interactive failure rollback,
category admission for these shapes and exact Stop notification quiescence need
separate controls. Ordered-wave prefetch is the next group, not part of these104
preparation cells. Native notifications observe playback/resource transitions;
no public CNA notification API is added.
