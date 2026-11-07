# XACT cue lifecycle and instance limits

CNA's public `Cue.GetVariable("NumCueInstances")` now uses a per-handle snapshot,
initially zero. A change to the number of Playing cues in one sound-bank object and
cue definition publishes that number to every retained handle of that definition,
including Prepared, Paused and Stopped handles. Paused Playing cues and instance-limit
fade victims count until retirement; authored Stopping tails no longer contribute.
Prepare and nonplaying destruction leave a fresh
handle at zero when the definition's count has not changed. RPC and interactive
selection use the same snapshot, rather than a separate authored initial value.

Admission now counts Playing cues, including eviction fades; authored Stopping
tails free capacity. Category counts span the engine, while candidate searches stay
in the incoming bank and matching category/definition. A victim already fading is
excluded despite its public Playing state. Prepared-victim eligibility and category
FAIL preparation checks remain separate open work in `handoff.md` §5.1.

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

The last sequence originally left victim identity unresolved. TQ-XACT-26's
controls below distinguish the leading-prepared-handle effect from generally delayed
limit enforcement. FAudio's measured underflow remains outside Microsoft's contract.

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

## Instance-limit controls and fade state (TQ-XACT-26)

The retained `XactInstanceLimitProbe.cpp` runs32 final x86/x64 cells: cue/category
OLDEST with no extra handle, an older prepared P, a later prepared P, an older P
destroyed before overflow, and a paused L; cue/category FAIL with no P, older P,
and a settled authored stop. The same genuine3.6 engine and untouched SDK banks
are used; [separate provenance](../modules/audio/tests/CNA/Internal/Audio/instance-count/INSTANCE_LIMIT_PROVENANCE.json)
records the exact final source/binary/input hashes. Every protocol call succeeds
except the specifically observed8ac70008 rejections. Earlier16/28-cell matrices
are retained as intermediate evidence.

Without P, with a later P, or after destroying an older P, I replaces L: after
settling L is Stopped and I Playing/count1. Only an older retained P produces
PPrepared/LPlaying/IPlaying/count2. P can still Play afterward: the older-P case
then leaves P/I Playing/count2; the later-P case leaves only P Playing/count1.
Together with the earlier continuation, these controls support preparation-order
selection including unplayed handles; general one-step-late enforcement cannot
explain the difference. Native internal victim pointers were not inspected.

During an ordinary overflow's20ms snapshot, L remains state8 (Playing), or72 if
previously paused, and counts2 with I. After the fade it is32 and counts1. The
paused victim retires without Resume; a subsequent Resume leaves it stopped.
FAudio's BeginFadeOut instead adds its STOPPING bit while preserving PLAYING.
CNA now retains Microsoft's public Playing/Paused state through the instance-limit
fade. Reconciliation, new waves and category-volume changes still apply the fade,
its expiry cancels pending events, and candidate selection excludes the existing
fade marker so another overflow cannot restart that victim's deadline.

FAIL controls also show the remaining admission repair precisely. Category FAIL
can reject Prepare; an older P prepared while capacity was available still has its
later Play rejected when full. Cue FAIL rejects at Play. Refresh public Cue.Play
specially ignores8ac70008 (IL_000c–0012); GetCue instead forwards the failure to its
error-code translator. These are static bridge facts, not managed execution.
Neither failed Play clears
an older handle's published cache. After StopAsAuthored and requested20ms settling,
L has count0 (state16 or32 across these coarse snapshots), and a new I prepares/plays
successfully with maximum1 in either scope. TQ-XACT-27 below qualifies a genuine
category cue tail and repairs that capacity difference. Prepared-victim selection
and Prepare-time category FAIL remain open.

Three initial regressions (two offline, one NULL-compatible) fail before the fix.
Additional guards protect repeated evictions and a new wave during a fade; these
are source-derived guards, not native overlap/gain qualification. Existing volume,
victim-order and retirement assertions stay in place; their old Stopping expectations
are replaced by the measured public state. This does not qualify native gain shape,
exact asynchronous deadlines, other instance modes or all cue shapes.

## Authored-tail admission (TQ-XACT-27)

The category-only `cat_fail` project is emitted as a simple cue despite the XAP's
fade fields: maximum255 lets the SDK omit its cue-limit record. It cannot prove
a50ms category admission overlap. The owned `cat_fail_tail` project instead uses
cue maximum2/OLDEST and category maximum1/FAIL, serializing the cue's50ms fade
without reaching the cue limit in this experiment. Its banks remain untouched SDK
outputs. [Dedicated provenance](../modules/audio/tests/CNA/Internal/Audio/instance-count/ADMISSION_TAIL_PROVENANCE.json)
records the six native cells and exact hashes.

Both x86/x64 category positive controls reject Prepare while L is Playing, with
8ac70008 and no handle. In the four authored-stop cells (cue_fail and cat_fail_tail,
both architectures), L is still state16/count0 after the requested20ms settling,
and I.Prepare/Play both succeed before L is retired. This is a public native state/
capacity observation, not a precise asynchronous decrement-instant guarantee.

CNA's cue/category counts now include Playing only. This retains the instance-limit
fades repaired in TQ-XACT-26 while releasing capacity during an authored Stop.
The new shared offline regression checks both scopes, a real admitted voice, retained
tail state, published counts and later retirement; its clock counterpart also runs
with NULL audio. Corrected-fixture red runs restore the two original predicates and
fail in all three target binaries. Reintroducing either predicate separately fails
on both real offline adapters. An initial test attempted the optimized simple cue;
its failed tail precondition is preserved in evidence and replaced by the genuinely
authored-tail fixture, with a fresh causal red run. No oracle or assertion is weakened.

Prepared candidates, category Prepare-time FAIL and all cue-shape qualification
remain open. Native gain/output, managed/native Windows execution and synchronous
Stop/Play racing semantics are not qualified here. The engine uses its logical
controller transition; no measured20ms scheduling delay is built into CNA.

## Prepared resources and ordering reference (TQ-XACT-28)

A separate retained own probe adds native bank GetState queries to the admission
sequences. Twenty final x86/x64 cells cover earlier controls, an evicted prepared
handle after all playing handles are destroyed, reversed Play order at maximum2,
a later Play of the selected prepared handle, and absence of a wave bank. Exact
source/binary/input hashes and outcomes are in
[prepared-cue provenance](../modules/audio/tests/CNA/Internal/Audio/instance-count/PREPARED_CUE_PROVENANCE.json).
There are18 successful positives and two deliberate missing-wave failures. Those
return8ac70013 at Prepare and the probe exits2 by design; they are not failed
positive runs. An earlier probe-mode guard incorrectly omitted wave creation in
a loaded-wave control; it was corrected before the full final matrix.

Preparing a cue without Play changes SoundBank state0→128 and WaveBank4→132,
while the cue is Prepared4 and its count0. Both Refresh IsInUse getters test bit0x80
at IL_000b–0017, also the SDK's XACT_STATE_INUSE. After P absorbs an overflow,
stop/destroy L and I: P alone remains Prepared/count0 and **both banks remain in
use**, even after200ms settling. A prepared candidate's participation in replacement
is therefore distinct from its retained resources; destroying it is an incorrect
way to implement the observed selection. Resource pointers were not inspected.

Maximum2 controls prepare P before L, play L before P, then introduce I. P is still
the retired victim: preparation order wins over Play order. In maximum1 replay
controls, P absorbs the initial overflow, later Play P retires L, then a new J
retires P and leaves I/J Playing/count2. The original preparation position remains
relevant after the earlier prepared selection and later Play. Both architectures
and cue/category scopes agree.

With no wave bank loaded, native Prepare fails8ac70013 before Play. Refresh's
GetCue bridge forwards that code; the Framework error-code switch maps it to the
NoWaveBank InvalidOperationException (static IL only). CNA currently returns a lazy
prepared handle, reports both banks unused before playback, and cannot select the
prepared victim. These measured preparation/resource differences remain open until
the runtime repair and regressions land. Weighted/interactive cues, complex track
variation, streaming, native gain/PCM, managed/native Windows execution and exact
asynchronous boundaries are outside this reference set.

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
