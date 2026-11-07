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

CNA currently retains a different contract: finite positive bytes mean additional
repetitions. Varied events therefore activate `byte+1` voices; plain events pass that
byte to the mixer's buffer-repeat path. That path can repeat an authored loop region
while playing the prefix and suffix once, whereas finite native event repetition prepares
each wave with wave-level loop0. Existing offline finite-loop tests qualify CNA's current
buffer behavior, not native event cardinality or prefix/suffix parity. CNA also keeps255
unbounded for varied events under the existing owner-approved policy.

Correcting finite events must cover both plain and varied events, remaining budgets,
pause/stop/release, live parameters and actual PCM across prefix, region and suffix.
The wave-level SoundEffectInstance and mixer contracts still use additional buffer repeats;
changing those lower-level contracts would not implement the native event count.
Current defects are tracked in `handoff.md` §5.1. Probe sources, all twelve outcomes and
hashes are archived under `../cnaplan/evidence/rev002/xact-finite-cardinality/`.
