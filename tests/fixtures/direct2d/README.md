# Direct2D log fixtures

Inputs for `scripts/verify-direct2d-debug-log.py --self-test` (plans/plan_direct2d.md D2D-113/D2D-124).

Every file matching `debug-log-*.log` is classified by its name:

| Prefix | Meaning |
|---|---|
| `debug-log-clean-` | The gate MUST accept this log. |
| `debug-log-dirty-` | The gate MUST reject this log. |

The lines are the exact format `Direct2DRenderer::ReportLiveDeviceObjectsNoThrow` writes to
stderr, so a change to that emitter that breaks the gate breaks the self-test as well.
