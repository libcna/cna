# XACT runtime parameter curves

Sound-level and track-level volume/pitch curves contribute independently to each active
voice. Volume results sum in centibels before conversion to amplitude; pitch results sum
with authored and event pitch before the final integer conversion. A category-volume
change immediately recomputes each voice with its own track bindings. The same composition
is retained during authored fades and RPC release tails.

Total XACT pitch uses the FACT range of minus2400 through plus2400 cents (two octaves),
with frequency ratio `2^(cents/1200)`, from0.25 through4. The complete base, effect,
sound/track RPC and event sum is truncated toward zero before that bound, including on
tracks without value events. A private Cue-owned pitch setter supports this range while
the public `SoundEffectInstance.Pitch` setter retains its XNA range minus one through
plus one. The reference is `FACT_internal.c:1563-1571`, `FACT.c:2139-2156` and the
`FACTPITCH_MIN_TOTAL`/`MAX_TOTAL` constants. These regressions cover finite sums within
the representable signed16-bit range, without qualifying overflow or non-finite inputs.

Before those float contributions, the combined sound/effect base is a signed16-bit
integer state. Repeated ADD effect draws wrap that state rather than saturating it.
For sound900 and successive300-cent draws, native helper call106 stores32700;
call107 stores-32536, and only then does the total wave bound yield-2400 cents and
ratio0.25. The mirrored negative case changes to ratio4. CNA retains an equivalent
bounded delta modulo65536 and narrows it with the sound base before RPC evaluation.
This also preserves exact integers during long effect sequences:55742 draws of301
with sound900 give wrapped base2026, while an unbounded float delta loses two cents.

An instrumented native GetNextWave probe checks114084 forced helper calls on Linux
x86_64, with actual public SetPitch and captured voice ratios. Wavebank preparation is
wrapped; zeroed audio/master records supply only the sample rate for default filter
math. Min/max draws are equal. Native integer narrowing is measured for this compiler;
the probe makes no portable C, native scheduling, infinite255, device or PCM claim.
Offline regressions render255 finite complete voices through each real mixer, with
NEW/HOLD controls and a16-fold duration distinction at the wrap. A second test advances
real effect state directly, then checks the actual next voice after55742 activations;
it does not render the intermediate voices or qualify their timing.

Pitch composition preserves native float addition order: integer base/effect first,
then the separately accumulated sound RPC, track RPC and event values. Merging the two
RPC scopes before adding the base can change the truncated result. With float inputs
`1200`, `-1199.9f`, `.9f`, native order gives about0.99997556 cents and truncates to0;
merging the RPCs first rounds them to-1199 and incorrectly produces1 cent. The mirrored
negative case also truncates to0. Exact binary inputs `1200`, `-1199.5f`, `.5f` instead
produce1 cent as a control.

A separate native UpdateSound probe checks30 samples through actual `FACTWave_SetPitch`
and its captured voice ratio, with float evaluation method0. Wave state and voice writes
are wrapped; no native playback is measured. It preloads the folded integer base and
event value, so it does not qualify effect construction, event activation or phase timing.
Two offline regressions check both signs, effect/base grouping, event-last order,
sound-only/exact controls, live boundary changes and every fade/dispatch path. Their
one-cent assertions inspect forwarded mixer ratios; they make no PCM-resolution claim.

The native pitch probe calls actual `FACTWave_SetPitch` and captures its forwarded
frequency ratio, including both signs at1500,2400 and3000 cents. It initializes no
engine/device and measures no native PCM. Offline tests check forwarded ratios at
dispatch and during updates, fades and a sloped release curve. A literal441Hz sine also
checks both real mixers' rendered frequency through complete periods and signal energy;
threshold hysteresis ignores small startup ripples without discarding output frames.

Composed XACT voice gain may exceed one. The sound/track/effect base, sound RPC, track
RPC and event volume are added in that order as float centibels, then converted once
to amplitude. Category gain and the current fade multiply that amplitude before the final
wave gain is bounded to zero through `FACTVOLUME_MAX` (16777216). Applying that maximum before a fade
would incorrectly attenuate large gains. A private Cue-owned setter preserves this path;
the public `SoundEffectInstance.Volume` setter still requires the inclusive range zero
through one. The reference is `FACT_internal.c:1551-1562`, `FACT.c`'s
`FACTWave_SetVolume`, and `FACT.h`'s volume bounds. An isolated native call probe measured
1.25 and8 unchanged, 33554432 capped to16777216, and negative gain floored to zero, with
the underlying voice-volume write captured. It opened no device and measured no PCM.

Converting the effect or authored base separately can lose a valid final gain through
float underflow. For sound and track bytes180 and80 successive ADD effect draws of
byte90, native's combined base is-95526.1015625cB. A76000cB RPC and category setter
multiplier100000000 produce category gain100080000 and final voice gain
0.01727016642689705. A separately converted effect becomes zero and silences that voice.
Authored sound/track byte0 stays negative infinity in centibels and remains silent;
the retained amplitude fields are metadata projections, not runtime composition inputs.

A native probe executes80 forced GetNextWave calls with active/upcoming state carry,
then UpdateSound before and after the live RPC change, capturing the real public
FACTWave_SetVolume voice write. It also calls the public category setter. Wave preparation
and state are wrapped; included internal code is instrumented, while public entry points
come from the uninstrumented reference archive. This qualifies finite arithmetic, not
native asset parsing, complete repeat scheduling, fades, a device or PCM. Three offline
regressions check both real mixers' gain and actual small PCM markers across complete
waves, dispatch, live RPC/category writes and every fade branch. Two direct controls cover
nonzero authored bases under HOLD/NEW/ADD and silent sound/track bases.

Four offline regressions check positive gain at dispatch, updates, category changes,
fade-in/settling, authored fade-out and RPC release. Small literal PCM markers and, for
the maximum-gain case, a small master gain keep the output below clipping on both real
mixer adapters. These tests do not establish their clipping parity. Nine production
mutations independently restore each of the seven unity caps, cap before the fade, or
cap the private setter at one; every mutation fails the new tests on both adapters.

Fresh categories start with current gain1. Their authored gain is kept separately and
enters the current gain only through `AudioCategory.SetVolume`, as authored gain times
the requested multiplier. A parent setter recurses using each child's previous current
gain as that child's multiplier; repeated parent calls therefore compound child gain.
This follows the native FAudio parser and setter, independently of the parent's requested
value. Fresh-engine offline PCM and a separate native parser/setter probe cover this
distinction; Microsoft XACT's initial gain has not been measured.

Filter curves overwrite their own axis in list order. Sound filter results are inherited
first, then track curves can replace frequency and Q independently. Curves do not enable a
filter on a track that has no authored filter. Frequency conversion caps both the input
fraction at one half and the resulting coefficient at one; Q becomes its reciprocal.
These rules follow `FACT_internal.c:71-93,1022-1110,1474-1598` in the local
FAudio reference.

Volume, pitch, filter and reverb-send curves share variable resolution.
Internal curve evaluation reads authored variables regardless of their PUBLIC bit. Only
cue-scoped variables named `AttackTime` or `ReleaseTime` select intrinsic clocks; globals
with those names retain their normal values. Track `AttackTime` uses the pause-adjusted cue
elapsed time minus that track's first authored event timestamp, with unsigned 32-bit
subtraction. Sound curves use track zero's mutable first event-instance timestamp, captured
before dispatch can advance it. Repeating first events can put that origin in the future;
the unsigned subtraction then wraps and curves use their upper endpoint. Reconciliation
and getters retain the origin from that dispatch pass; another engine update can capture
a new origin even at the same clock time. Freezing elapsed time
across pause is CNA's existing scheduler policy. A native FAudio pause probe found different
resume accounting; Microsoft XACT's behavior has not been measured. The same section records
the exact probe scope and keeps that discrepancy open.

An RPC-only release duration is the maximum last-point X among track-level volume curves
bound to cue-scoped `ReleaseTime`. PUBLIC is unnecessary. Sound-only, global, differently
named and non-volume bindings do not supply a release duration. An authored fade-out takes
precedence. At the release deadline the cue cancels pending events before dispatch or random
draws. Before stopping, and during an authored fade, the intrinsic `ReleaseTime` value is zero.
The reference is `FACT_internal.c:802-827,1046-1074,1454-1462` and `FACT.c`'s cue-stop path.

Scheduled voices receive their current phase gain before playback under CNA's existing
configuration contract. RPC-only release uses fade multiplier1 even when it interrupted
fade-in; authored fade-out uses its own remaining fraction. An offline dispatch observer
checks voice gain and renders before reconciliation can repair a bad initial write. A
native phase probe executes the transition helpers and UpdateSound with wrapped wave
calls; it qualifies phase arithmetic, not native first-frame timing or playback.

The regressions use literal native-format banks, injected clocks, hand-derived gain/pitch
and filter values, and both real mixer adapters. Production mutations verify that the tests
reject missing track contributions and changes to CNA's pause-clock policy. Composition,
accessibility, release selection and filter rules were checked against the readable FAudio
reference. A separate native UpdateSound probe executes the RPC-before-repeat sequence on
two wave-less tracks and records their RPC results and event-instance timestamps. Offline
PCM uses hand-derived values at injected update times; off-update getter behavior and
native asynchronous scheduling equivalence are not established. A separate native
pause-only probe exercises no sound playback or RPC evaluation;
no native Windows XACT execution is claimed. Reverb routing and its reference boundary are
described in [the reverb guide](xact-reverb.md) and `handoff.md` §5.1.
Prepared-cue counting and its native qualification are described in
[the instance-limit guide](xact-instance-limits.md).
