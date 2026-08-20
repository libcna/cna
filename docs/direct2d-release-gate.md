# Direct2D release gate

plans/plan_direct2d.md D2D-133. "Is the Direct2D backend releasable?" has exactly one answer, and a
program produces it:

```bash
python3 scripts/validate_direct2d_plan.py --release-gate
```

The command prints every criterion as PASS or BLOCKED and exits nonzero while any of them is
blocked. It is deliberately not satisfiable by editing prose.

## Criteria

1. **Every plan row is closed.** No `🟨` and no `⬜` remains in `plans/plan_direct2d.md`. The checker's
   ordinary mode already refuses a `✅` that cites no evidence, so closing a row requires naming the
   test, gate, document, or delegated task that covers it.
2. **The five CTest gates are registered.** `Direct2D_Smoke`, `Direct2D_2DParity`,
   `Direct2D_Lifetime`, `Direct2D_Unit`, and `Direct2D_Soak` must all exist, so the label cannot
   silently shrink.
3. **The debug-layer/live-object gate exists and is self-tested.**
   `scripts/verify-direct2d-debug-log.py --self-test` classifies its committed positive and
   negative fixtures, and the workflow runs the gate over both native logs. Its whitelist is the
   only permitted set of live objects; anything else fails the run.
4. **A recorded native x64 Windows run exists** (`docs/direct2d-native-evidence.md`). It must name
   the Windows build, the `d2d1.dll`/`d3d11.dll`/`dxgi.dll` versions, the adapter and driver
   version, and the result of the branches Wine cannot exercise: `ColorMatrix`, `Premultiply`,
   `BOUNDED_SOURCE_COPY`, and the Porter-Duff composite modes. This is D2D-22's evidence.
5. **A recorded physical presentation/DPI capture exists**
   (`docs/direct2d-physical-presentation-evidence.md`). It must name the monitor layout and DPI
   scaling used, and show the captured physical pixels for the letterbox bars, the overscan crop,
   and a non-96-DPI window. This is D2D-126's evidence, and it is the one thing an
   Xvfb/Wine/hosted-CI run structurally cannot supply.

## What blocks the gate right now

Everything below criterion 3. Criterion 1 lists its open rows explicitly when it fails; criteria 4
and 5 remain blocked until a real machine produces those two artifacts. The evidence files must be
written from an actual recorded run -- creating them empty to unblock the gate would be the exact
dishonesty the gate exists to prevent, and their required contents above are what review checks.

## Non-criteria

Performance, soak, mutation, differential, and refactor work does not block the release unless it
uncovers a concrete defect on a supported path. That is the same rule as
[`plans/plan_direct2d.md`](../plans/plan_direct2d.md)'s closing verification rules, and the release gate
deliberately does not check benchmark thresholds: a timing gate on shared CI hardware would either
be so loose it proves nothing or so tight it fails at random.
