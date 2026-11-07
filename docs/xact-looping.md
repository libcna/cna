# XACT wave repetition

Native event repeat counts and mixer buffer repeat counts are different quantities.
The pinned FAudio reference is `6839b88e304a046ae1a609ff14a085371e02a4e0`.
Its `create_sound` copies the authored byte to the event-instance counter, then immediately
calls GetNextWave for the initial prepared wave. That call already consumes one positive
count. Public DoWork prepares further waves only while the remaining counter is positive.
UpdateSound activates those prepared waves as earlier waves finish.

A native probe calls actual public SoundBank Prepare, Cue Play and Engine DoWork, with
unmodified GetNextWave, ActivateEvent and UpdateSound. The measured results are:

| Authored byte | Counter after initial preparation | Plain PlayWave activations | Effect PlayWave activations, flags0x2201 |
|---|---:|---:|---:|
| 0 | 0 | 1 | 1 |
| 1 | 0 | 1 | 1 |
| 2 | 1 | 2 | 2 |
| 80 | 79 | 80 | 80 |
| 254 | 253 | 254 | 254 |
| 255 | plain0, effect254 | 1, with wave-level loop255 | 255, finite |

Thus the finite native count is `max(byte,1)` complete wave activations. The initial
prepare is not an extra activation beyond that budget. For plain255, GetNextWave delegates
loop255 to the wave and clears the event counter. For effect255 with NEW_ON_LOOP enabled,
the loop mask prevents delegation; GetNextWave instead decrements255 normally. This
distinction follows `FACT_internal.c:272-278,444-452,739-783`,
`FACT.c:559-570` and `FACT_internal.h`'s variation flags.

The probe uses pooled wave records and wraps preparation, playback, state, destruction,
volume and pitch. It supplies controlled STOPPED states after each activation and calls
DoWork before promotion, avoiding starvation of the single upcoming slot. All twelve
cases pass with ASan/UBSan/LSan. Internal reference code is instrumented; public functions
come from an uninstrumented archive. A real SDL mutex and injected clock are used, with
zeroed audio/master records supplying only the filter sample rate. No assets are parsed,
no audio device or API thread runs, and no native PCM or Microsoft XACT is measured.
The plain255 observation proves delegation, not an audible infinite-duration run.

CNA now manages every positive finite event on the control thread. Its initial voice
consumes one count, leaving `byte-1` future activations; each voice uses wave-level loop0,
so every activation includes the complete prefix, region and suffix. Raw0 and raw1 both
play once. Promotion needs an engine update after completion; it does not run in the
mixer callback. Offline PCM tests explicitly render the completion guard and service
that update, without discarding guard frames or claiming asynchronous native timing.

The real-mixer regressions cover finite0/1/2/80/254, all four event families, ordered
selection, early updates without completion, and PCM8/PCM16/ADPCM resident/streamed
prefix/region/suffix output. The codec fixtures also check complete decoded WMAv2 waves,
pause/resume, stops, fresh cues and through-EOF regions. Older variation tests retain
their original draw/pitch/filter sequences by authoring the required total count. NULL
tests explicitly arm completed voices; they make no real-EOF claim.

The SoundEffectInstance and mixer contracts still count additional buffer passes. Separate
PCM and decoded WMAv2 tests retain their prefix-once, repeated-region and suffix-once
checks, including through-EOF regions. Event-count conversion belongs to Cue.

CNA retains owner-approved unbounded255 even for varied events. Native nondelegated255
is finite, as measured above; the intentional source comment preserves this distinction.
CNA also draws a new variation on observed completion, whereas native prepares the next
wave earlier in DoWork. The native probe qualifies serviced activation counts; it does
not establish random-draw timing, starvation behavior or asynchronous audio parity.

Probe sources, all twelve outcomes and hashes are archived under
`../cnaplan/evidence/rev002/xact-finite-cardinality/`. Finite-count regression and mutation
evidence is under `../cnaplan/evidence/rev002/xact-finite-event-budget/`.
