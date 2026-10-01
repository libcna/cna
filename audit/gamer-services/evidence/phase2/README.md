# Phase 2 evidence

Starting native commit: 812db9656d98e34fc9013914d149e390a6059ef3.
Starting server commit: e45049abcbefcc8b31875c24b0f16c55f96def80.

Phase 2 adds failing regression tests before fixes, then records focused/final runs.
The worktree's stable `cmake-build-debug` is configured HEADLESS using shared ccache;
no Phase 1 prebuilt binary is treated as validation of modified production code.
Server builds reuse its existing Debug `build/`.
