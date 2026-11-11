# XACT wave repetition

Event repeat counts and mixer buffer repeat counts are different quantities. Microsoft XACT3.6
and FAudio disagree on the event count; CNA follows Microsoft, measured for every play-wave
family (HND-XACT-05 and HND-XACT-06, below).

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
raw0..4, one-second waves without a loop region, lookahead250 ms. HND-XACT-06 extends it.

## Effect families, loop regions, random variations and long counts (HND-XACT-06)

The same engines, 136 cells in five groups, each group rerunning the retained TQ-XACT-31, one-wave
resident and HND-XACT-02 samebank controls; every SDK bank is one authored field away from an
HND-XACT-05 bank ([corpus, oracle and provenance](../modules/audio/tests/CNA/Internal/Audio/loop-extensions/README.md),
checker `tools/xna-oracle/check-xact-loop-extensions-oracle.py`, probe
`tools/xna-oracle/XactLoopExtensionProbe.cpp`, archive `cnaplan/evidence/rev002/hnd-xact-06-loop-extensions/`):

| Shape (raw loop byte n) | Native wave lifecycle |
|---|---|
| type4 = PlayWave + Pitch Variation, its NEW_ON_LOOP off or on (0..4) | one A0 wave held n+1 passes |
| type6 = ordered NEW_ON_LOOP + Pitch Variation (0..4) | n+1 one-pass waves A0,A1,A0,... |
| type6nol0 = no track NEW_ON_LOOP, pitch NEW_ON_LOOP on (0..4) | one A0 wave held n+1 passes |
| type1 with a loop region [5512, 16537] of the 1 s wave (0..4) | one wave held 1 + n/2 passes: prefix, n+1 regions, suffix |
| base with that region (0..4) | n+1 waves, each the whole wave once |
| random/shuffle with NEW_ON_LOOP (1, 3) | n+1 one-pass waves; random may repeat a candidate, shuffle alternated |
| random/shuffle without NEW_ON_LOOP (1, 3) | one wave of the drawn candidate held n+1 passes (x86 random raw1 held A1) |
| ordered with / without NEW_ON_LOOP (5, 100, 254) | 6/101/255 one-pass waves / one wave held 6/101/255 passes |

So the HND-XACT-05 rule holds for every family and these counts: **raw n plays n+1 passes, and
only track NEW_ON_LOOP separates them into waves.** The effect NEW_ON_LOOP bit does not; with it
the held wave's pitch still varies over its passes: three x86 and three x64 101-pass holds of
type4nol agree within 7 ms per pass (1 %), those of type4 scatter over 63 ms, as one pitch drawn
per wave in -100..100 cents does. A held wave repeats its loop region, not the whole wave.
XACT_TRACK_PROPERTIES report loopCount n and a duration of the wave plus n region lengths
(1000 + 500n ms with the region, (n+1) x 1000 ms without), also for the separate NEW_ON_LOOP
waves that actually play n+1 whole waves.

Not measured: volume and filter NEW_ON_LOOP bits, effect ADD operators, type6 with both NEW_ON_LOOP
kinds, the pitch values themselves (no PCM), Ordered From Random and Random No Immediate Repeats,
more than two candidates, raw 255 (owner policy below), streaming or cross-bank candidates, native
PCM. A held 101- or 255-pass count is exact only to the hold resolution (about 2 %); the separate
waves of the same counts are exact.

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

For every play-wave family CNA follows the Microsoft rule. With track NEW_ON_LOOP it manages the
n further activations on the control thread; each voice uses wave-level loop0, so every
activation includes the complete prefix, region and suffix. Without any NEW_ON_LOOP the one voice
receives wave-level loop n: prefix, n+1 region passes, suffix (the whole wave when no region is
authored), as for 255; the HND-XACT-06 region and long-count banks confirm both. An effect
NEW_ON_LOOP bit alone is the one deliberate approximation: Microsoft holds one wave and varies
its effect over the passes, but a CNA voice has no per-pass update, so CNA renders each of the
n+1 passes as a complete wave of the first candidate with re-drawn effects (FAudio's
arithmetic). The count and candidate are the measured ones; the native single lifecycle, the
gapless join and a region repeated inside one wave are not. A finite event without track
NEW_ON_LOOP never reselects its candidate. Raw255 keeps FAudio's delegation rule (no NEW_ON_LOOP
of either kind) under the owner's unbounded policy.
Promotion needs an engine update after completion; it does not run in the mixer callback.
Offline PCM tests explicitly render the completion guard and service that update, without
discarding guard frames or claiming asynchronous native timing.

The real-mixer regressions cover finite0/1/2/80/254 for all four event families and both
NEW_ON_LOOP kinds, ordered selection, early updates without
completion, and PCM8/PCM16/ADPCM resident/streamed output for both the delegated wave loop and
NEW_ON_LOOP complete waves. The codec fixtures also check decoded WMAv2 waves, pause/resume,
stops, fresh cues and through-EOF regions. `HndXactCardinality.*` and `HndXactLoopExtension.*`
replay every measured SDK bank through both mixers at its native 22050 Hz, and `HndXactPrefetch.SdkRawThreeProducesFourCompleteWaveActivations`
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
Microsoft's 255 was not measured.
The FAudio probe qualifies serviced activation counts; it does not establish random-draw
timing, starvation behavior or asynchronous audio parity.
