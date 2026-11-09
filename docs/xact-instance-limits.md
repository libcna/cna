# XACT cue lifecycle and instance limits

CNA's public `Cue.GetVariable("NumCueInstances")` uses a per-handle snapshot,
initially zero. A change to the number of Playing cues in one sound-bank object and
cue definition publishes that number to every retained handle of that definition,
including Prepared, Paused and Stopped handles. Paused Playing cues and instance-limit
fade victims count until retirement; authored Stopping tails do not contribute.
Prepare and nonplaying destruction leave a fresh
handle at zero when the definition's count has not changed. RPC and interactive
selection use the same snapshot, rather than a separate authored initial value.

Admission counts Playing cues, including eviction fades; authored Stopping
tails free capacity. Category counts span the engine. Candidate search preserves
its incoming-bank fallback except the HND-XACT-03 measured OLDEST case: when no
local victim exists, exactly one eligible foreign Playing/nonfading victim can be
replaced. Multiple foreign victims and other cross-bank modes remain unqualified.
Cue-definition limits still search their own bank/definition. A victim already fading is
excluded despite its public Playing state. An unplayed Prepared handle can absorb
one replacement without losing its state or resources. A later successful Play
restores its eligibility at its original preparation position. Category FAIL is
checked during noninteractive preparation and rechecked at Play; cue FAIL
waits until Play.

CNA retains sound-bank `IsInUse` while an undisposed cue handle exists, including
Prepared and Stopped handles. Resolved wave-bank use belongs to the sound bank and
survives cue destruction, including references warmed before a later preparation
failure. Sound-bank disposal releases those references. These TQ-XACT-32 lifetimes
follow TQ-XACT-31's Microsoft controls; playback counts remain independent.

## Microsoft XACT 3.6 qualification (TQ-XACT-25)

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
audio output. Loaded paths and method ownership are checked. The measurement covers
admission (six profiles, x86/x64), cache/scope and destruction/tail/EOF cells; both
architectures agree on steady counts and variable values.

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

TQ-XACT-26's controls below explain the last row (preparation-order selection, not
delayed limit enforcement). FAudio's measured underflow remains outside Microsoft's contract.

The stopping decrement is bounded between the Stop call and the20ms snapshot. It
can race the native thread; no exact synchronous transition, DoWork ownership or
callback timing guarantee follows. CNA publishes at its controller state transition
and tests the settled tail observation. This qualifies neither native Windows, managed
execution, decoded PCM output, all cue shapes, variable setters, nor Microsoft
RPC/interactive playback. One run per architecture/scenario does not establish
scheduling variance.

The own [SDK inputs and reproduction instructions](../modules/audio/tests/CNA/Internal/Audio/instance-count/README.md)
and `tools/xna-oracle/XactInstanceCount{Scope,End}Probe.cpp` preserve the count
experiment.

## Instance-limit controls and fade state (TQ-XACT-26)

The retained `XactInstanceLimitProbe.cpp` runs32 x86/x64 cells: cue/category
OLDEST with no extra handle, an older prepared P, a later prepared P, an older P
destroyed before overflow, and a paused L; cue/category FAIL with no P, older P,
and a settled authored stop. The same genuine3.6 engine and untouched SDK banks
are used; [separate provenance](../modules/audio/tests/CNA/Internal/Audio/instance-count/INSTANCE_LIMIT_PROVENANCE.json)
records the exact source/binary/input hashes. Every protocol call succeeds
except the specifically observed8ac70008 rejections.

Without P, with a later P, or after destroying an older P, I replaces L: after
settling L is Stopped and I Playing/count1. Only an older retained P produces
PPrepared/LPlaying/IPlaying/count2. P can still Play afterward: the older-P case
then leaves P/I Playing/count2; the later-P case leaves only P Playing/count1.
These controls support preparation-order selection including unplayed handles;
general one-step-late enforcement cannot explain the difference. Native internal
victim pointers were not inspected.

During an ordinary overflow's20ms snapshot, L remains state8 (Playing), or72 if
previously paused, and counts2 with I. After the fade it is32 and counts1. The
paused victim retires without Resume; a subsequent Resume leaves it stopped.
FAudio's BeginFadeOut instead adds its STOPPING bit while preserving PLAYING.
CNA retains Microsoft's public Playing/Paused state through the instance-limit
fade. Reconciliation, new waves and category-volume changes still apply the fade,
its expiry cancels pending events, and candidate selection excludes the existing
fade marker so another overflow cannot restart that victim's deadline.

Category FAIL can reject Prepare; an older P prepared while capacity was available
still has its later Play rejected when full. Cue FAIL rejects at Play. Refresh public
Cue.Play specially ignores8ac70008 (IL_000c–0012); GetCue instead forwards the failure
to its error-code translator. These are static bridge facts, not managed execution.
Neither failed Play clears an older handle's published cache. After StopAsAuthored and
requested20ms settling, L has count0 (state16 or32 across these coarse snapshots), and
a new I prepares/plays successfully with maximum1 in either scope (TQ-XACT-27 below).

The regressions protect repeated evictions and a new wave during a fade; these are
source-derived guards, not native overlap/gain qualification. This does not qualify
native gain shape, exact asynchronous deadlines, other instance modes or all cue shapes.

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

CNA's cue/category counts include Playing only. This retains the instance-limit
fades of TQ-XACT-26 while releasing capacity during an authored Stop. The shared
offline regression checks both scopes, a real admitted voice, retained tail state,
published counts and later retirement; its clock counterpart also runs with NULL audio.
All-cue-shape native qualification remains open. Native gain/output, managed/native
Windows execution and synchronous Stop/Play racing semantics are not qualified. The
engine uses its logical controller transition; no measured20ms scheduling delay is
built into CNA.

## Prepared resources and ordering reference (TQ-XACT-28)

A separate retained own probe adds native bank GetState queries to the admission
sequences. Twenty x86/x64 cells cover earlier controls, an evicted prepared
handle after all playing handles are destroyed, reversed Play order at maximum2,
a later Play of the selected prepared handle, and absence of a wave bank. Exact
source/binary/input hashes and outcomes are in
[prepared-cue provenance](../modules/audio/tests/CNA/Internal/Audio/instance-count/PREPARED_CUE_PROVENANCE.json).
The two deliberate missing-wave cells return8ac70013 at Prepare and the probe exits2
by design.

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
NoWaveBank InvalidOperationException (static IL only). Weighted/interactive cues,
complex track variation, streaming, native gain/PCM, managed/native Windows execution
and exact asynchronous boundaries are outside this reference set.

## Authored-tail bank ownership (TQ-XACT-29)

Four further native x86/x64 cells run the unchanged prepared-cue probe in
`stopping` mode against `cue_fail` and `cat_fail_tail`. Before I is prepared,
L alone remains Stopping16/count0 after20ms, but SoundBank state is128 and
WaveBank state132. Both retain the INUSE bit, independently of admission capacity.
Initial bank states are0/4. Exact probe, input, matrix and checker hashes are in
[tail-bank provenance](../modules/audio/tests/CNA/Internal/Audio/instance-count/TAIL_BANK_PROVENANCE.json).
The checked bank snapshots cannot be masked by an additional prepared/playing cue.
The native engine runs under Wine/null output/private display; this adds no native
Windows, managed-wrapper, gain/PCM or exact asynchronous-boundary qualification.

CNA's bank getters include Stopping. The regressions on both real offline mixers
check the sole cue, positive voice state/gain, count0, both bank flags during the
tail, and retirement at the authored50ms deadline; a clock regression also runs with
NULL audio. The deadline uses FAudio's inclusive controller predicate; the native20ms
snapshots do not qualify an exact Microsoft transition instant.

Validate the four native cells with
`python3 tools/xna-oracle/check-xact-tail-bank-oracle.py <matrix.json>`.
Four altered-evidence controls reject a lost SoundBank flag, a lost WaveBank flag,
an already-stopped cue and changed input bytes.

## Prepared cue runtime (TQ-XACT-30)

Noninteractive GetCue resolves its sound, RPC/DSP metadata, owned event schedule,
initial candidate/effect choices and selected bank references. It starts no voice
or event. First dispatch reuses those choices; later event iterations advance them.
Play resets the scheduler anchor so time spent Prepared does not count as playback.
Interactive sound selection stays deferred until Play, following the explicit
interactive exception in FAudio FACTSoundBank_Prepare; that branch is not a
Microsoft native qualification.

A prepared candidate's eligibility is consumed independently of its public state
and bank references. It remains Prepared and can Play later. Play does not reorder
the bank's lifetime registry, so its original position still decides replacement.
Stopping an unplayed prepared cue is immediate even when a fade was authored.
Constructor failure unregisters every partial wave-bank reference and never inserts
a dangling sound-bank entry. Missing wave banks fail at GetCue, while full category
FAIL throws InstancePlayLimitException. Cue FAIL and the category Play recheck
silently stop a refused incoming cue and release its preparation resources.

Static Refresh IL also distinguishes SoundBank.PlayCue: both overloads ignore
8ac70008 (IL_002f–0034 /005f–0064), while other preparation failures use CueNotFound.
GetCue retains its NoWaveBank diagnostic. Cue.Play sets the played/Apply3D latch only
after successful deferred preparation or an ignored admission refusal. The C API's
exception barrier maps both preparation refusals to INVALID_STATE / STATE and clears
a failed created handle.

The engine retains every wave bank by identity, resolves the first matching name and
disposes them all, so destroying a second, same-named streaming bank keeps the first
live bank. This uses FAudio's insertion-ordered wbList/forward lookup, including
LinkedList_AddEntry, not a measured Microsoft duplicate-name policy.

SDK regressions cover the measured preparation, later Play, original position,
ownership, fail/retry and error paths on both real offline mixers. Initial-variation
checks verify draws occur before Play and are reused at first dispatch. Invalid
DSP-code refusal occurs at preparation.

Native reference scope remains TQ-XACT-28's plain single-sound resident PCM controls,
with static Refresh bridges. Weighted/interactive/complex-track and streaming
regressions use the normal FNA reference rather than a Microsoft-native claim.
Copying FAudio's per-wave release on promotion (FACT_internal.c:1537) into the bank
flags would contradict TQ-XACT-31's measured ownership boundary. Variation-prefetch
timing still needs separate controls. Native gain/PCM, managed execution, native
Windows and exact asynchronous instants remain outside this qualification.

## Bank use outlives playback (TQ-XACT-31)

Twenty-six genuine Microsoft3.6 x86/x64 cells distinguish cue lifetime from
sound-bank ownership. Twenty positive cells exit0; six intentional missing-wave
cases return8ac70013 at Prepare and exit2. The two own SDK projects use A:100ms at0,
and either B:1000ms at400 on another track, or no B event. Untouched inputs, hashes,
probe and checker are in the [bank-lifetime corpus](../modules/audio/tests/CNA/Internal/Audio/bank-lifetime/README.md)
and [provenance](../modules/audio/tests/CNA/Internal/Audio/bank-lifetime/PROVENANCE.json).

| Observation on the two-track cue | SoundBank state | A/B wave-bank state |
|---|---|---|
| No cue yet | 0 | 4/4 |
| Prepared, count0 | 128 | 132/132 |
| First short wave ended; second pending or playing | 128 | 132/132 |
| Cue Stopped32/count0, by natural end or explicit Stop | 128 | 132/132 |
| Cue destroyed, after50ms and2000ms | 0 | 132/132 |
| SoundBank destroyed | Object absent | 4/4 |

Never-played controls preserve the same boundary: StopPrepared retains sound-bank
use, and destroying the unplayed cue clears it while wave-bank use remains. The
single immediate-wave control retains A alone; loaded but unused B stays4, and
omitting B does not prevent preparation. Omitting wave-property getters also leaves
the results unchanged. These are observed ownership lifetimes, not inspection of
native cache pointers or a PCM-output measurement.

Missing B after A resolves returns no cue and leaves SoundBank0 but A132. Loading B
then retrying Prepare succeeds. Missing A leaves the unreached B unused. Constructor
exception safety must therefore keep cue registrations valid while preserving the
separate, persistent resource ownership of the sound bank.

Destroying A natively while it still reports INUSE crashed the probe process (exit5);
the `destroy-finished` control therefore checks the flag and skips that destruction.
Safe destruction of an in-use bank is not qualified.

The checker validates exact engine/source/input identities and all26 outcomes;
eight altered-evidence controls are rejected. Run
`python3 tools/xna-oracle/check-xact-bank-lifetime-oracle.py <matrix.json>`.
FAudio remains the normal reference for unmeasured behavior, but its per-wave/count-based
bank flags do not override this measured Microsoft boundary.
Weighted/interactive/streaming and prefetch timing, native Windows, managed
execution and native PCM/gain remain outside the matrix.

## Bank ownership runtime (TQ-XACT-32)

SoundBank.IsInUse follows its undisposed cue registry, including Stopped
handles. Resolving a wave registers its wave bank with the sound bank independently
of that cue. The reciprocal registrations are retained through cue disposal and
natural retirement, then detached when the sound bank is disposed. Missing B after
A resolves leaves A in use while failed-constructor cue registrations roll back;
retrying after B loads preserves the same owner. Loaded but unused banks stay unused.
Deferred interactive playback records its bank during selection, and later loop
candidates retain newly resolved banks when dispatched. Those shapes follow the
normal FNA reference and are not Microsoft-qualified.

Fire-and-forget ownership ends when AudioEngine.Update actually sweeps the retired
cue. An authored Stopping tail must remain retained until its real deadline.
IsInUse is an ownership query, so it does not imply a playing voice or successful
decoding. Encoded-refusal tests retain their direct no-voice/silence assertions.

CNA preserves its safe forced-wave-bank disposal cascade. It detaches persistent
sound-bank references before object destruction, including when a Cue.Disposing
callback destroys a wave bank during SoundBank.Dispose. The native in-use
destruction attempt above does not qualify that CNA safety policy.

The `XactBankLifetime.*` regressions run on both real offline mixers; a literal
two-bank loop control distinguishes initial resolution from a later candidate,
without asserting Microsoft prefetch parity. The private C API smoke checks stopped
cue ownership, retained wave use after Cue.Destroy and release after SoundBank.Destroy.

## CNA regressions

The shared offline `XactInstanceCount.*` tests cover change-only publication,
fresh/paused/stopped recipients, bank/definition isolation, nonplaying destruction,
authored tail, real EOF, RPC/interactive readers, variable-domain/disposal validation
and the instance-limit fade state; two clock/lifetime tests also run under NULL audio
(`XactInstanceCountClockTests.cpp`). Private consumer controls use the SDK XACT
Variables Visibility contract, not a Microsoft private-variable run.

## FAudio reference (TQ-XACT-12/15)

The normal readable reference remains local FAudio
`6839b88e304a046ae1a609ff14a085371e02a4e0`: `FACTSoundBank_Prepare`,
`FACTCue_Play`, `FACTCue_Stop` and `FACTCue_Destroy` in `FACT.c`, and
`handle_instance_limit`, `play_sound` and `FACT_INTERNAL_DestroySound` in
`FACT_internal.c`. Its direct stored counter differs from the Microsoft observations
above; CNA follows the measured Microsoft count/cache contract within that scope.

FAudio measured with public Prepare/Play/Stop/Destroy, real SDL mutexes and an injected
clock (no device, asset parser, PCM or API thread): Prepare allocates a sound without
incrementing the unsigned-byte cue and category counters, yet a prepared sound is an
OLDEST victim and its destruction decrements both. Destroying a lone never-played
prepared cue therefore wraps 0→255, at which even a maximum255 limit is met; with
limit1, eviction sequences end with the counters at255 and no playing cue. A FAIL
rejection stops the incoming prepared sound and decrements again, so the next incoming
cue bypasses the limit and coexists with the current one. Ordinary Stop decrements
exactly once. These effects explain the earlier discrepancy investigation; they are not
the Microsoft oracle for a CNA fix. Voice gain, pitch and release composition are
described in [the RPC guide](xact-rpc.md).

## HND-XACT-03 cross-bank native qualification

The22-cell genuine Microsoft3.6 x86/x64 group uses ordinary resident positive
controls and actual PREPARED/PLAY/STOP/DESTROY callbacks, not a FAudio count hook.
Duplicate A names select the first-registered bank, with the second unused.
Resident/ordered/interactive cloned sound banks share the selected A/B resources
but retain independent owners: destroying the first bank leaves INUSE132 until
the second bank is destroyed, then PREPARED4. Ordered clone creation uses fresh
backing bytes; reuse of the original live buffer returns INVALIDDATA.

Category cap1/REPLACE_OLDEST has a same-bank positive and clone comparison. Both
play the incoming wave and retire the sole older victim, including across banks.
This supersedes the FAudio bank-local fallback only for that qualified selection.
The x64 same-bank victim is Stopping16 at200ms and Stopped32 at400ms; no deterministic
native fade delay follows. Existing fade/gain regressions retain their source
arithmetic with cross-bank victim-retirement assertions (`HndXactCrossBank.*`).

Inputs/identities: tests/CNA/Internal/Audio/cross-bank/PROVENANCE.json under the
audio module; checker tools/xna-oracle/check-xact-cross-bank-oracle.py. The ordered
sequences' four waves for raw byte3 follow the n+1 rule that CNA implements
([xact-looping.md](xact-looping.md), HND-XACT-05). Multiple candidate ordering, other
replacement modes, duplicate disposal/reload and exact notification/resource
quiescence await separate measurements.

## HND-XACT-04 sequential resource/notification lifetimes

The74-cell genuine3.6 x86/x64 lifetime group confirms that unplayed noninteractive
prepared waves can be destroyed without ever playing, while interactive prepared
handles own no wave. Destroying a playing cue stops/destroys its wave resources;
resolved bank ownership persists until SoundBank destruction, independently for
cloned weighted/streaming banks. The device-free `HndXactResourceLifetime.*`
regressions qualify these resource/state observations.

Native unregister controls observe no later callbacks after final unregister in
these runs, although playback and ownership continue to settle. This is not a
managed CNA notification API or proof of concurrent callback quiescence. Forced
INUSE-bank destruction/handle violations were not attempted. Exact callback clocks
remain open. See resource-lifetimes/PROVENANCE.json under the audio test corpus and
check-xact-resource-lifetimes-oracle.py.
