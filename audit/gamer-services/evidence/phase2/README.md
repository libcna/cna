# Phase 2 evidence

Starting native commit: 812db9656d98e34fc9013914d149e390a6059ef3.
Starting server commit: e45049abcbefcc8b31875c24b0f16c55f96def80.

Phase 2 adds failing regression tests before fixes, then records focused/final runs.
The worktree's stable `cmake-build-debug` is configured HEADLESS using shared ccache;
no Phase 1 prebuilt binary is treated as validation of modified production code.
Server builds reuse its existing Debug `build/`.


## Reading the results

- `gs-phase2-offline-before.log`: initial four failing tests (three I/O cases and numeric boundaries); `full-device-before` adds the real failed-write case. `offline-after` shows the four I/O cases passing.
- `gs-phase2-client-tests.log` / `net-tests.log`: final complete freshly built native suites. `native-results.json` contains compact per-suite metadata.
- `gs-phase2-server-final.log`: authoritative final 34-test registration: 27 pass, seven skip. Earlier `server-tests.log` predates the numeric test and uses the existing sibling client harnesses.
- `picture-before` / `picture-after`: real service privacy failure and 42-check success. `privacy.log`: existing 66 checks.
- `numeric-server.log`: 47 storage/response/restart/concurrent award checks; `offset-before` is an overly strict diagnostic error-code expectation, NOT a confirmed production bypass. `offset-qualified` records all three oversized inputs rejected.
- `session-crash-loopback` / `session-add-loopback`: existing non-isolated variants, run with unchanged sibling harness binaries; no NAT claim.
- `protocol.log`: six-copy identity and 5068 protocol harness checks; not exhaustive operation/version certification.
- `numeric-path-scan.txt` preserves the pre-fix source search, including intentional float uses; it is not a list of defects.
- `repositories.txt` records pre-final-document-commit status; the handoff identifies all commits. Binary hashes distinguish fresh final clients from the older extra-probe client.

All logs contain test fixtures only. No generated binary/database is committed. See ../../gamer-services-handoff-phase2.md for commands, limitations and reproduction details.
