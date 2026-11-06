# XACT runtime parameter curves

Sound-level and track-level volume/pitch curves contribute independently to each active
voice. Volume results sum in centibels before conversion to amplitude; pitch results sum
with authored and event pitch before the final integer conversion. A category-volume
change immediately recomputes each voice with its own track bindings. The same composition
is retained during authored fades and RPC release tails.

Filter curves overwrite their own axis in list order. Sound filter results are inherited
first, then track curves can replace frequency and Q independently. Curves do not enable a
filter on a track that has no authored filter. Frequency conversion caps both the input
fraction at one half and the resulting coefficient at one; Q becomes its reciprocal.
These rules follow `FACT_internal.c:71-93,1022-1110,1474-1598` in the local
FAudio reference.

Internal curve evaluation reads authored variables regardless of their PUBLIC bit. Only
cue-scoped variables named `AttackTime` or `ReleaseTime` select intrinsic clocks; globals
with those names retain their normal values. Track `AttackTime` uses the pause-adjusted cue
elapsed time minus that track's first authored event timestamp, with unsigned 32-bit
subtraction. For sound curves the current implementation uses track zero's authored origin;
the repeating-first-event difference is tracked in `handoff.md` §5.1.

An RPC-only release duration is the maximum last-point X among track-level volume curves
bound to cue-scoped `ReleaseTime`. PUBLIC is unnecessary. Sound-only, global, differently
named and non-volume bindings do not supply a release duration. An authored fade-out takes
precedence. At the release deadline the cue cancels pending events before dispatch or random
draws. Before stopping, and during an authored fade, the intrinsic `ReleaseTime` value is zero.
The reference is `FACT_internal.c:802-827,1046-1074,1454-1462` and `FACT.c`'s cue-stop path.

The regressions use literal native-format banks, injected clocks, hand-derived gain/pitch
and filter values, and both real mixer adapters. Production mutations verify that the tests
reject missing track contributions and a clock that includes paused time. This establishes
agreement with the readable FAudio reference; no native Windows XACT or native FAudio binary
execution is claimed. Reverb routing and its remaining intrinsic-clock differences are
described in [the reverb guide](xact-reverb.md) and `handoff.md` §5.1.
