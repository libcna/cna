# XACT cue lifecycle and instance limits

CNA's public `Cue.GetVariable("NumCueInstances")` now uses a per-handle snapshot,
initially zero. A change to the number of Playing cues in one sound-bank object and
cue definition publishes that number to every retained handle of that definition,
including Prepared, Paused and Stopped handles. Paused Playing cues count; authored
Stopping tails no longer contribute. Prepare and nonplaying destruction leave a fresh
handle at zero when the definition's count has not changed. RPC and interactive
selection use the same snapshot, rather than a separate authored initial value.

Admission remains separate: existing CNA cue/category limits still count Playing and
Stopping, exclude Prepared victims and allocate playback only at Play. Those admission
rules are not repaired by the public variable fix. Category counts span the engine;
victims come from the incoming cue's bank in preparation order. See `handoff.md` §5.1.

## Microsoft XACT 3.6 qualification (TQ-XACT-25, 2026-11-07)

The retained XNA Refresh Xact.dll4.0.30901.0 selects retail XACT3.6, not3.7. Its public
Cue.GetVariable validates the name at IL_0000–0017, invokes the native bridge at
IL_0027 and returns its float unchanged at IL_0031–0032. The bridge calls native
GetVariableIndex at IL_0057–006a (slot5, x86 offset20), then GetVariable at
IL_0089–009f (slot7, offset28). Exact assembly, CLSID/IID, genuine x86/x64 DLL hashes,
SDK authoring tool and untouched bank hashes are in the
[corpus provenance](../modules/audio/tests/CNA/Internal/Audio/instance-count/PROVENANCE.json).
The bridge is static IL evidence; the managed wrapper was not executed.

Public native Initialize/Prepare/Play/GetVariable/GetCueProperties/Stop/Destroy and
DoWork run on the authentic Microsoft3.6 engine under Wine, private display and null
audio output. Loaded paths and method ownership are checked. Initial measurement
covers12 admission cells (six profiles, x86/x64,300 snapshots), two cache/scope cells
and six destruction/tail/EOF cells. The two retained own C++ probes repeat eight
cache/scope/end cells against the committed SDK banks after removing unused marker
helpers. Both architectures agree on steady counts and variable values.

| Sequence | Measured result |
|---|---|
| L plays; then fresh P is prepared | Bank count1; L variable1; P variable0 immediately and after150ms DoWork |
| OtherCue or a second bank from identical XSB plays | Independent count1; P stays0 |
| Pause L, then another same-definition cue plays | Bank2; paused L, prepared P and incoming cue all receive2 |
| Stop that incoming cue | Bank1; its stopped handle and other recipients receive1 |
| Stop L, then play a replacement | Bank0 then1; the earlier stopped L receives1 |
| Destroy a never-played or already stopped handle | No decrement, unsigned wrap or unchanged-count publication |
| Destroy the playing handle | Prepared peer1→0 in the immediate post-call snapshot, still0 after150ms |
| StopAsAuthored with50ms tail | Immediately count1/state16; after20ms count0 while state remains16; after170ms state32/count0 |
| Natural end of100ms wave | At30ms count1/state8; by430ms count0/state32 |
| Cue FAIL at maximum1 | Play returns8ac70008; bank stays1, rejected handle stays valid/Stopped with variable0; later count change updates it too |
| Category FAIL at maximum1 | Prepare can return8ac70008 with no cue handle |
| Leading Prepared P, OLDEST maximum1, L then I | P remains Prepared; L/I both Playing, settled bank2. Later X/Y each replace one, settled bank2 |

The last sequence proves a CNA admission difference. It does **not** prove which
handle Microsoft chose as victim; prepared-victim selection is a hypothesis until
an appropriate control distinguishes it from delayed enforcement. In particular,
FAudio's measured underflow is not the Microsoft contract.

The stopping decrement is bounded between the Stop call and the20ms snapshot. It
can race the native thread; no exact synchronous transition, DoWork ownership or
callback timing guarantee follows. CNA publishes at its controller state transition
and tests the settled tail observation. Neither this work nor the earlier FAudio
probe qualifies native Windows, managed execution, decoded PCM output, all cue
shapes, variable setters, or Microsoft RPC/interactive playback. One run per
architecture/scenario does not establish scheduling variance.

The own [SDK inputs and reproduction instructions](../modules/audio/tests/CNA/Internal/Audio/instance-count/README.md)
and `tools/xna-oracle/XactInstanceCount{Scope,End}Probe.cpp` preserve the count
experiment. Full raw logs, initial probes and additional admission projects are in
`../cnaplan/evidence/rev002/xact-instance-counts/`.

## CNA regressions and verification

Five shared offline tests cover change-only publication, fresh/paused/stopped
recipients, bank/definition isolation, nonplaying destruction, authored tail, real
EOF, RPC/interactive readers and variable-domain/disposal validation. Two additional
clock/lifetime tests also run under NULL audio. The first three offline tests fail
on both original adapters; the RPC/interactive addition is independently detected
by consumer-specific production mutations. Validation checks preserve an existing
boundary and are not claimed to have failed before the fix.

Thirteen production mutations plus three private-consumer mutations are detected
on both real offline adapters (32
ordinary assertion failures, all builds successful): constant/live/seeded reads,
unchanged-count publication, omitted Prepared/Paused/Stopped recipients, counting
Stopping, mixing definitions, bypassing public lookup validation, either internal
consumer using initial values, missing natural completion publication, and incorrect
PUBLIC-bit gates on private content consumers. Private consumer controls use the
SDK XACT Variables Visibility contract, not a Microsoft private-variable run. Original
source bytes are restored exactly. Full results and review receipts are in campaign
evidence. No public API/C ABI, stub or dependency is added; the C API inventory has
unchanged symbol counts and a regenerated header fingerprint.

## Historical FAudio reference (TQ-XACT-12/15)

The normal readable reference remains local FAudio
`6839b88e304a046ae1a609ff14a085371e02a4e0`: `FACTSoundBank_Prepare`,
`FACTCue_Play`, `FACTCue_Stop` and `FACTCue_Destroy` in `FACT.c`, and
`handle_instance_limit`, `play_sound` and `FACT_INTERNAL_DestroySound` in
`FACT_internal.c`. The sections below describe **FAudio**, not Microsoft XACT.
Its direct stored counter and underflow differ from the Microsoft observations
above; CNA follows the measured Microsoft count/cache contract within that scope.

### FAudio preparation and continued eviction

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

### FAudio wrap and failed-play effects

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

### FAudio qualification boundary

TQ-XACT-12 and TQ-XACT-15 use real public Prepare/Play/Stop/Destroy, real SDL mutexes and
an injected clock. Unmodified `FACT_internal.c` is included for controlled fade-expiration
UpdateSound/DestroySound calls. The continuation probe checks90 snapshots across14
fresh fixtures with ASan/UBSan/LSan; that included reference translation unit is
instrumented, while public functions come from the uninstrumented reference archive.
The probe avoids reading unused, potentially uninitialized fade fields.

No device, native asset parser, wave playback, PCM or API thread is initialized. Simple
waves, complex variations, interactive cues and asynchronous lifetime effects remain
unqualified. These historical FAudio effects explain the earlier discrepancy investigation; they
are not the Microsoft oracle for a CNA fix. Adding Prepared to the existing victim
predicate alone would still leave unqualified admission and state behavior.

Evidence: `../cnaplan/evidence/rev002/xact-prepared-cue-lifecycle/` and
`../cnaplan/evidence/rev002/xact-prepared-continuation/`. Voice gain, pitch and release
composition are described in [the RPC guide](xact-rpc.md).
