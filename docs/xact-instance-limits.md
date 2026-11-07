# XACT cue lifecycle and instance limits

CNA currently derives instance counts from Playing and Stopping cues. Cue limits count
the same definition in the same sound bank; category limits count across the engine.
Victims come from the incoming cue's sound bank and follow preparation order. Stopping
cues count toward the limit but cannot be selected again. Prepared cues are registered
with their bank for disposal, but CNA does not allocate their playback sound or admit
them as victims until Play. Its exclusive state enum also cannot report Prepared and
Stopped simultaneously. These prepared/count differences from the native reference
remain open in `handoff.md` §5.1.

The normal readable reference is local FAudio
`6839b88e304a046ae1a609ff14a085371e02a4e0`: `FACTSoundBank_Prepare`,
`FACTCue_Play`, `FACTCue_Stop` and `FACTCue_Destroy` in `FACT.c`, and
`handle_instance_limit`, `play_sound` and `FACT_INTERNAL_DestroySound` in
`FACT_internal.c`. A measured Microsoft XNA disagreement would take precedence;
these native lifecycle cases have not been measured on Microsoft XACT.

## Native preparation and continued eviction

For a single-sound cue, native Prepare allocates a sound without incrementing either
counter. Native counters are unsigned bytes. A prepared sound is eligible for OLDEST
replacement, and destroying it decrements both counters even though it never incremented
them. Destruction preserves its Prepared bit alongside Stopped. Destroying that stopped
cue handle later does not decrement again.

The controlled native probe uses one cue definition/category and a limit of1. Here a
counter value denotes both equal stored counters. Playing includes a fading victim whose
Playing bit remains set; it excludes a never-played prepared sound.

| Step, with P prepared before playing L | Stored counter | Playing cues |
|---|---:|---:|
| L plays | 1 | 1 |
| I plays and selects unplayed P | 2 | 2 |
| P's fade expires and its sound is destroyed | 1 | 2 |
| X plays and selects L | 2 | 3 |
| L's fade expires | 1 | 2 |
| Y plays and selects I | 2 | 3 |
| I's fade expires | 1 | 2 |
| Both remaining playing cues are destroyed | 255 | 0 |

The reverse preparation order first selects the playing cue; the next eviction then
selects the remaining prepared cue and reaches the same undercount. Cue-scope victim
fades use50ms and category-scope fades70ms in these fixtures. In a separate continuation,
stopping L after P expires leaves counter0 while I still plays. X then starts with no
victim or fade-in, leaving two playing cues against the limit of1.

## Native wrap and failed-play effects

Destroying a lone never-played prepared cue measures0→255 in both counters. With cue
fade-in40ms and category fade-in60ms, the following sequence is measured:

| Step | Stored counter | Playing cues | Incoming phase |
|---|---:|---:|---|
| First play after the wrap | 0 | 1 | Fade-in60ms, with no victim |
| Second play | 1 | 2 | Playing, no fade-in or victim |
| Third play | 2 | 3 | Selected scope's fade-in; first cue fades out |

At255 even the other scope's maximum255 limit is met. Both checks run, so category's
60ms fade-in overrides cue's40ms on the first play in either fixture. On the third play
only the selected limit1 is met: fade-in40ms/60ms and victim fade-out50ms/70ms remain
distinct. After the first victim expires the counter is1 with two playing cues.

FAIL also exposes the lifecycle mismatch. After L plays (counter1), a new R is rejected
with `FACTENGINE_E_INSTANCELIMITFAILTOPLAY`. Native stops R's already prepared sound,
decrements the counter to0 and leaves R Prepared|Stopped, while L still plays. Destroying
R changes no counter. The next incoming cue bypasses the limit and coexists with L;
the following rejection decrements again. Draining both playing cues after those two
rejections leaves counter254. All these transitions were measured separately for cue
and category limits.

Controls with two ordinarily played cues and limit2 measure exactly one decrement on
Stop, no further decrement on repeated Immediate/AsAuthored Stop or Destroy of the
stopped cue, and final counter0. Destroy during an authored fade also decrements once;
that public Stop uses the cue's50ms fade even in the category-limit fixture.

## Qualification boundary

TQ-XACT-12 and TQ-XACT-15 use real public Prepare/Play/Stop/Destroy, real SDL mutexes and
an injected clock. Unmodified `FACT_internal.c` is included for controlled fade-expiration
UpdateSound/DestroySound calls. The continuation probe checks90 snapshots across14
fresh fixtures with ASan/UBSan/LSan; that included reference translation unit is
instrumented, while public functions come from the uninstrumented reference archive.
The probe avoids reading unused, potentially uninitialized fade fields.

No device, native asset parser, wave playback, PCM or API thread is initialized. Simple
waves, complex variations, interactive cues and asynchronous lifetime effects remain
unqualified. The native effects above are evidence for a complete preparation/count/
destruction regression design, not a completed CNA fix. Adding Prepared to the existing
victim predicate alone would retain incompatible counts and states.

Evidence: `../cnaplan/evidence/rev002/xact-prepared-cue-lifecycle/` and
`../cnaplan/evidence/rev002/xact-prepared-continuation/`. Voice gain, pitch and release
composition are described in [the RPC guide](xact-rpc.md).
