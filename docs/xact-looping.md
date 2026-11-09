# XACT wave repetition

Event repeat counts and mixer buffer repeat counts are different quantities. Microsoft XACT3.6
and FAudio disagree on the event count; CNA follows Microsoft where it was measured
(HND-XACT-05, below) and keeps the FAudio budget where it was not.

## Microsoft XACT3.6 finite cardinality (HND-XACT-05)

Genuine Refresh-selected XACT3.6 x86 and x64 played SDK-built banks that change one field of
HND-XACT-02's samebank shape at a time (56 cells, [corpus, oracle and provenance](../modules/audio/tests/CNA/Internal/Audio/finite-cardinality/README.md),
checker `tools/xna-oracle/check-xact-finite-cardinality-oracle.py`):

| Shape, raw loop byte n = 0..4 | Native wave lifecycle |
|---|---|
| type3 ordered, NEW_ON_LOOP, flags0x0c (the HND-XACT-02 shape) | n+1 PREPARED/PLAY/STOP/DESTROY, candidates A0,A1,A0,...; each one pass |
| same, flags0x00 | identical |
| type3 ordered, NEW_ON_LOOP off | one A0 wave held for n+1 passes |
| type1 PlayWave of A0 | one A0 wave held for n+1 passes |

The rule that explains every cell: **a raw byte n plays the wave n+1 times.** With
NEW_ON_LOOP (variation bit22) each pass is a separate complete wave and the next candidate;
without it the event hands n to one wave, which loops itself (the delegation FAudio applies only
to 255). XACT_TRACK_PROPERTIES report loopCount n and duration (n+1) x 1000 ms throughout. The
flags byte (0x04 Use Center Speaker, 0x08 New Speaker Position On Loop) changes nothing.

Measured on: event types1 and3, two ordered candidates in one resident bank, flags0x0c/0x00,
raw0..4, one-second waves without a loop region, lookahead250 ms. Not measured, and not changed
in CNA: the effect families4/6 and their effect NEW_ON_LOOP bits, waves with a loop region (what
the delegated wave repeats), random/shuffle variations, raw5..254, streaming or cross-bank
candidates, native PCM. 255 keeps the owner-approved policy below.

## FAudio reference budget

The pinned FAudio reference is `6839b88e304a046ae1a609ff14a085371e02a4e0`.
Its `create_sound` copies the authored byte to the event-instance counter, then immediately
calls GetNextWave for the initial prepared wave. That call already consumes one positive
count. Public DoWork prepares further waves only while the remaining counter is positive.
UpdateSound activates those prepared waves as earlier waves finish.

An FAudio probe calls actual public SoundBank Prepare, Cue Play and Engine DoWork, with
unmodified GetNextWave, ActivateEvent and UpdateSound. The measured results are:

| Authored byte | Counter after initial preparation | Plain PlayWave activations | Effect PlayWave activations, flags0x2201 |
|---|---:|---:|---:|
| 0 | 0 | 1 | 1 |
| 1 | 0 | 1 | 1 |
| 2 | 1 | 2 | 2 |
| 80 | 79 | 80 | 80 |
| 254 | 253 | 254 | 254 |
| 255 | plain0, effect254 | 1, with wave-level loop255 | 255, finite |

Thus FAudio's finite count is `max(byte,1)` complete wave activations. The initial
prepare is not an extra activation beyond that budget. For plain255, GetNextWave delegates
loop255 to the wave and clears the event counter. For effect255 with NEW_ON_LOOP enabled,
the loop mask prevents delegation; GetNextWave instead decrements255 normally. This
distinction follows `FACT_internal.c:272-278,444-452,739-783`,
`FACT.c:559-570` and `FACT_internal.h`'s variation flags.

The probe uses pooled wave records and wraps preparation, playback, state, destruction,
volume and pitch. It supplies controlled STOPPED states after each activation and calls
DoWork before promotion, avoiding starvation of the single upcoming slot. It runs under
ASan/UBSan/LSan; internal reference code is instrumented, public functions come from an
uninstrumented archive. A real SDL mutex and injected clock are used, with
zeroed audio/master records supplying only the filter sample rate. No assets are parsed,
no audio device or API thread runs, and no native PCM or Microsoft XACT is measured.
The plain255 observation proves delegation, not an audible infinite-duration run.

## CNA

For PlayWave and PlayWaveTrackVariation CNA follows the Microsoft rule. With NEW_ON_LOOP it
manages the n further activations on the control thread; each voice uses wave-level loop0, so
every activation includes the complete prefix, region and suffix. Without NEW_ON_LOOP the one
voice receives wave-level loop n: prefix, n+1 region passes, suffix (the whole wave when no
region is authored), as for 255. The effect families4/6 keep FAudio's budget: the initial
voice consumes one count, leaving `byte-1` future activations, so raw0 and raw1 both play once.
Promotion needs an engine update after completion; it does not run in the mixer callback.
Offline PCM tests explicitly render the completion guard and service that update, without
discarding guard frames or claiming asynchronous native timing.

The real-mixer regressions cover finite0/1/2/80/254 for all four event families (the measured
n+1 rule for types1/3, the FAudio budget for4/6), ordered selection, early updates without
completion, and PCM8/PCM16/ADPCM resident/streamed output for both the delegated wave loop and
NEW_ON_LOOP complete waves. The codec fixtures also check decoded WMAv2 waves, pause/resume,
stops, fresh cues and through-EOF regions. `HndXactCardinality.*` replays every measured SDK
bank through both mixers at its native 22050 Hz, and `HndXactPrefetch.SdkRawThreeProducesFourCompleteWaveActivations`
plays HND-XACT-02's three SDK profiles (raw byte 3, resident and streamed) to their fourth and
last wave, the count HND-XACT-02's genuine engines played; its prefetch regressions also qualify
early bank ownership and missing-successor termination. Older variation tests
retain their original draw/pitch/filter sequences by authoring the required total count. NULL
tests explicitly arm completed voices; they make no real-EOF claim.

The SoundEffectInstance and mixer contracts still count additional buffer passes. Separate
PCM and decoded WMAv2 tests retain their prefix-once, repeated-region and suffix-once
checks, including through-EOF regions. Event-count conversion belongs to Cue.

CNA retains owner-approved unbounded255 even for varied events. FAudio's nondelegated255
is finite, as its probe shows; the intentional source comment preserves this distinction.
The FAudio probe qualifies serviced activation counts; it does not establish random-draw
timing, starvation behavior or asynchronous audio parity.
