# Pre-SDL_GPU consolidation

The owner's brief of 2026-09-23: leave `next` clean, fully integrated and reproducibly tested
**before** the SDL_GPU modern-graphics workstream begins. This plan implements no SDL_GPU
functionality and is not an SDL_GPU implementation plan; it measures one, so the next agent starts
from a number rather than a guess.

Task IDs `PSG-0001`, `PSG-0002`, … . Every measurement below was taken on the private compositor
(`tools/platform/run_gpu_tests_private.sh`), never on the live desktop, on the machine's real
Radeon 780M.

## Status

| ID | Task | Status |
|---|---|---|
| PSG-0001 | Integrate `webgpu-modern-graphics` into `next` | 🟩 |
| PSG-0002 | A bounded-memory GoogleTest runner, because the suite outgrew the machine | 🟩 |
| PSG-0003 | The memory measurement that sets the runner's default | 🟩 |
| PSG-0004 | The full `CnaTests` measurement the WebGPU closeout could not take | 🟦 |
| PSG-0005 | The runner on a second renderer, so it is not WebGPU-shaped | 🟦 |
| PSG-0006 | `CNAEXT_LeakLoop`: reproduce, minimise, classify | 🟦 |
| PSG-0007 | SDL_GPU: what exists today | 🟦 |
| PSG-0008 | SDL_GPU: the three formerly dead tests, executed for the first time | 🟦 |
| PSG-0009 | SDL_GPU: modern, classic and CNAEXT-example baselines | 🟦 |
| PSG-0010 | SDL_GPU: backend, build size, warnings, test count | 🟦 |
| PSG-0011 | The handoff matrix, and the questions it cannot answer | 🟦 |

---

## PSG-0001 — the WebGPU integration

`origin/next` had not moved since the WebGPU branch was cut, and `next` was an ancestor of it, so
nothing had to be reconciled or rebased. Verified rather than assumed: `git merge-base --is-ancestor`,
and a `git diff` between the merged tree and the validated feature tip that comes back empty.

| | |
|---|---|
| pre-merge `next` | `c576b5d25` |
| WebGPU feature | `7dd39a8cc` (34 commits, WMG-0001..0028) |
| merge commit | `8c8e5bdb6` — `merge(WebGPUModernGraphics): integrate webgpu-modern-graphics into next` |
| post-merge `next` | `8c8e5bdb6`, pushed |

Authorship audited across all 34 commits before the merge: author **and** committer are
`Robert Vokac <robertvokac@robertvokac.com>` on every one, with zero deviations, and a scan for
AI-attribution markers returns nothing. The merge style follows the convention already in `next`'s
history (`merge(VulkanModernGraphics)`, `merge(GpuTestIsolation)`, …): an explicit `--no-ff` commit
that keeps the feature history.

### Pre-merge regression

| suite | result |
|---|---|
| `CnaGraphicsExtTests` WEBGPU | **933 / 0 / 29** |
| `CnaGraphicsExtTests` VULKAN | **930 / 0 / 32** |
| `CnaGraphicsExtTests` OPENGLES3 | **954 / 0 / 8** |
| classic WebGPU, `ctest -R '^WebGPU'` | 12 failures of 201 |
| classic Vulkan, `ctest -R '^Vulkan_'` | 1 failure of 371 |
| CNAEXT examples, `-L CnaExt` | 2 failures of 32 |
| SDL-free Wayland surface (`cna_test_cnaext_ibl`) | 8/8 |
| SDL-free X11 surface (same) | 8/8 |
| `profile_dead_tests.py` | no test died on a profile refusal |

Each of the three modern rows is exactly its pre-closeout figure, and each of the failure counts is
the one `plans/plan_webgpu_modern_graphics.md` already accounts for. No new failure.

### Post-merge

Shards 0 and 1 of the modern WEBGPU suite returned **232** and **231** passes, identical to the same
shards pre-merge, before the machine's low-memory reaper stopped the remaining shards — the very
problem PSG-0002 exists to remove. Combined with the byte-identical tree, that is what the push
rests on, and it is stated here rather than dressed up as a completed rerun. The full post-merge
number is in PSG-0004, taken with the bounded runner.

---

## PSG-0002 — `tools/tests/run_gtest_bounded.sh`

The WebGPU closeout could not measure `CnaTests` on WEBGPU at all. Every attempt — four shards, then
twelve — was killed by this machine's low-memory reaper, and the closeout recorded the number as
owed rather than answered. This is the tool that pays it.

**The lever is how many tests share one process, not how many processes run at once.** That is a
measurement, not a preference: a two-shard run of the 962-test engine-layer suite was killed at test
351, while a five-shard run of the same suite on the same binary completed. A CNA test builds a
`GraphicsDevice`, and the WebGPU provider keeps per-device state that is not returned when the
device is destroyed, so cost accumulates *inside* a process and raising parallelism alone makes it
worse.

The runner therefore bounds both, and `--tests-per-shard` is the one with the load-bearing default:

```
tools/tests/run_gtest_bounded.sh [options] <gtest-binary> [-- <extra gtest args>]
    --tests-per-shard N   how many test cases share one process   (default 200)
    --shards N            exact shard count instead
    --max-parallel N      how many shards run at once             (default 1)
    --filter F            gtest filter, applied before sharding
    --out DIR             logs, XML and per-shard state
    --resume              skip shards that already completed
    --stop-on-fail        stop launching after a failure
    --allow-live-display  permit :0 / wayland-0
```

It creates no display. It composes with the existing private compositor, so **one** compositor
serves every shard rather than one per shard:

```
tools/platform/run_gpu_tests_private.sh --exec \
    tools/tests/run_gtest_bounded.sh --max-parallel 2 ./cmake-build-webgpu/CnaTests
```

Properties that are there because their absence is a way to report a wrong number:

* **A signal-killed shard fails the run and is named.** The reaper's kill is exactly the case where
  a naive aggregator reports a confident partial pass; here the summary says `KILLED` and calls the
  counts a lower bound.
* **Skips are counted per testcase, not from the XML root.** GoogleTest records a `GTEST_SKIP` as
  `result="skipped"` on the case and puts no `skipped` attribute on `<testsuites>` at all. The first
  version of this aggregator trusted the root and turned a true **933 / 0 / 29** into a confident
  **962 / 0 / 0** — every skip silently promoted to a pass. Caught by comparing against the
  text-mode run, which is why that comparison is recorded here.
* **XML first, text second.** The text summary is the fallback for a shard that died before gtest
  could write its XML — precisely the shard worth not losing.
* **Its own `TMPDIR` per shard**, so two concurrent shards cannot collide in scratch files.
* **It refuses `:0` and `wayland-0` by name** unless `--allow-live-display` is passed, because the
  runner opens no display and the only way it reaches the owner's desktop is by inheriting one.
* `--resume` re-aggregates from existing XML and skips completed shards.

Renderer-agnostic by construction: it reads nothing about the renderer and passes the environment
through. PSG-0005 proves that on a second one.

## PSG-0003 — the measurement behind the default

`CnaGraphicsExtTests`, same binary, private compositor:

| | WEBGPU | VULKAN |
|---|---|---|
| peak RSS over the same ~200 tests | **1115 MB** | **167 MB** |

and the full 962-test suite through the bounded runner at 200 tests per shard, one at a time:

| | |
|---|---|
| shards | 5 |
| peak summed RSS of test processes | **1144 MB** |
| wall clock | 253 s |
| result | **933 passed / 0 failed / 29 skipped**, matching the text-mode sharded run exactly |

So 200 tests per process holds WEBGPU — the most expensive renderer measured — to about 1.1 GB,
against the >30 GB an unsharded run reaches. The default is deliberately the *safe* number rather
than the fastest one, and `--tests-per-shard` raises it for a renderer that costs less.
