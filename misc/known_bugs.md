# Known bugs — measurement records

**This is not a bug list.** The one authoritative list of current known bugs and limitations is
[`handoff.md`](../handoff.md) §5, and only that list says whether something is open. This file keeps the
measurements behind some of its entries: what was measured, against which reference, and what the
correction is. When the §5 entry is closed, delete its record here in the same commit. Section
numbers are stable references, so a removed record leaves a gap.

Capabilities CNA lacks by design are recorded the same way in [`known_gaps.md`](known_gaps.md).

Removed on 2026-10-10 because they were found fixed: §2 EasyGL letterbox viewport and readback
(`7bcf3f496`, `1dbc94f28`), §3 parity fixtures on GL ES profiles (`570d76677`), §4
`Window.CurrentOrientation` following the OS window (`0285061fa`). Removed on 2026-10-13: §1
`RasterizerState.DepthBias` scaling, whose last open renderer, `VULKAN`, was retired. Their records are
in Git history.

---


## 5. The XACT audio suite's failures rotate, so a stable failure COUNT hides a changing set

**Found:** 2026-09-11, while comparing two full-suite runs across the `vulkan` merge.
**Open in:** `modules/audio` — `CueTest`, `SoundBankTest`, `WaveBankTest`.
**Not caused by any renderer work:** neither the `dx` nor the `vulkan` branch changed a single file
under `modules/audio/`, verified by diff against the merge base.

### What was measured

Three full runs of the same 10 563-test suite, on three commits, each reporting **32 failures** —
and each time a *different* subset of the audio family was among them:

| Run | Audio cases that failed |
|---|---|
| after the `dx` merge (`c1c017cd7`) | `CueTest.PlayingCueNaturallyTransitionsToStoppedAfterPlaybackFinishes`, `CueTest.PauseAfterNaturalCompletionIsANoOp` |
| during the `vulkan` merge | the two above, plus `SoundBankTest.IsInUseFalseSoonAfterFireAndForgetCueNaturallyFinishes` and `WaveBankTest.IsInUseFalseSoonAfterCueNaturallyFinishesWithoutExplicitStop` |
| after the `vulkan` merge (`93ca4ffdf`) | `CueTest.PauseAfterDisposeIsANoOp` — a case that had passed in both earlier runs — plus the same `SoundBankTest` and `WaveBankTest` |

Every one of them passes when run alone or in a small serial set, and fails under load. They assert
that a cue has *naturally finished* after a wall-clock wait, so they measure the machine's audio
timing as much as CNA's bookkeeping.

### Why this is written down rather than left as "flaky"

The failure count is stable while the membership is not. A summary that says *"32 failures, same as
the baseline"* is therefore true and useless at the same time: it reads as "nothing changed" for as
long as nobody diffs the NAMES. This merge's real regressions were found only because both
directions of the comparison were taken — what appeared **and** what disappeared — and the same
diff is what exposed this.

**So: never conclude "unchanged" from a matching failure count.** Compare the sets.

### What the correction would be

Not a retry loop, which would hide it again. Either drive the audio clock deterministically in
these tests so "naturally finished" is a fact rather than a race, or mark the family as requiring
an exclusive machine the way `TwoProcessLoopbackTest` and the `ENet*` suites already are, so a
loaded run does not report a product defect. The first is better; the second is honest.
