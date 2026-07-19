# Audit: include/Microsoft/Devices/Sensors/Detail/SensorOwnerControlBlock.hpp

## Metadata
- Source file: `include/Microsoft/Devices/Sensors/Detail/SensorOwnerControlBlock.hpp` (61 lines)
- Audit status: AUDITED (full read)
- Subsystem: `microsoft-devices` shard
- File type: C++ header (template)
- XNA/FNA relevance: NOXNA internal implementation detail, no FNA/WP7 equivalent
- Main related tests: not independently located in this pass

## Purpose
A minimal, heap-allocated (via `shared_ptr`), shared-ownership control block letting a native backend callback (running on a thread the owner class does not control) safely detect "the owner object is gone" without ever dereferencing it — used by `Compass`/`Motion` (the two sensor classes whose backends are real native implementations capable of independently-scheduled async callbacks, unlike `Accelerometer`/`Gyroscope`'s SDL-event-watch-driven dispatch).

## Executive Verdict
Correct, minimal, and precisely scoped. The design (a `shared_ptr`-held block separate from the owner object itself, carrying `mutex`/`generation`/`owner`) is directly traced to a real found-and-fixed defect (Task LIFE-005, "a `CurrentValueChanged` handler destroying the owner before an already-copied `Calibrate` callback runs" in `AndroidCompassBackend::HandleMagneticFieldSample()`), not a speculative hardening exercise.

## Checklist Results
- The doc comment explicitly documents the correct usage contract: capture a *copy* of the `shared_ptr`, never `this` or a raw `TOwner*`, in every lambda handed to a backend; lock `mutex`, check `generation` and `owner != nullptr`, only then read and call through the pointer, and release the lock before calling any user code — verified this exact sequence is followed in `Motion::Start()`'s reading/calibration lambdas (audited separately).
- `owner` is documented as set to `nullptr` by the owner's own `Dispose(true)`, "as close to the start of teardown as practical" — confirmed this matches `Motion::Dispose(bool)`'s actual sequencing (null `control_->owner` under lock, *then* call `Stop()`).

## Detailed Findings
None. One deliberately-disclosed, accepted design boundary is worth highlighting as a positive finding rather than a defect: the doc comment explicitly states that a callback which has *already* passed its generation/owner check and is *currently*, on another thread, calling into `owner` at the exact instant a *different* thread completes that owner's destruction "remains unsupported/undefined" — this control block closes the far more common "callback arrives after the owner decided to tear down" case, not full concurrent-destruction safety for an already-in-flight callback. This is explicitly framed as consistent with an existing project-wide precedent (`ANDROID-BRIDGE-006`), i.e. a known, accepted, and consistently-applied boundary rather than an isolated gap. Whether any real call site could actually trigger this narrow remaining race (as opposed to it being a purely theoretical boundary) was not independently determined in this pass — worth a note for whichever future pass audits `AndroidCompassBackend`/`AndroidMotionBackend` directly, since only a real native backend's own threading model could confirm or rule out actual exposure.

## Cross-File Observations
See `src/Microsoft/Devices/Sensors/Motion.cpp.audit.md` for confirmation that `Motion`'s own `Start()`/`Stop()`/`Dispose(bool)` correctly follow every contract this control block's doc comment documents.

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
The explicit, precise disclosure of the design's remaining boundary (rather than an implied claim of full safety) is exactly the kind of honest scope statement this audit has come to expect from this codebase's `Devices` subsystem, and is consistent with an already-established project-wide precedent rather than an ad hoc excuse.

## Final Assessment
No findings.
