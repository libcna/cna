# Audit: examples/demo_devices/android/.../app/src/main/AndroidManifest.xml

## Metadata
- Source file: `.../app/src/main/AndroidManifest.xml` (126 lines)
- Audit status: AUDITED (full read)
- Subsystem: `examples-demo_devices` shard
- File type: Android application manifest
- XNA/FNA relevance: declares the permissions/features backing
  `Microsoft::Devices::Sensors`/`VibrateController`'s Android implementation
- Main related tests: none

## Purpose
Declares this demo's Android application entry (`DemodevicesActivity`), optional hardware features
(accelerometer/gyroscope/compass/touchscreen/bluetooth/gamepad/USB-host), and the two permissions
this demo's sensor/vibration usage actually needs (`VIBRATE`,
`HIGH_SAMPLING_RATE_SENSORS`).

## Executive Verdict
Correct and carefully reasoned. Two comments stand out for precision: the Task DEVICES-0123 comment
explicitly cites this project's own `NEXT.md` rule ("do not require Android sensors in the manifest
unless the app explicitly needs them") and correctly marks all three sensor `<uses-feature>`
declarations `android:required="false"`; the Task ANDROID-BRIDGE-004 comment for
`HIGH_SAMPLING_RATE_SENSORS` precisely explains why it's needed (Android 12+/API 31+'s ~200Hz
normal-permission sampling-rate cap, vs. this project's 2ms/~500Hz default `TimeBetweenUpdates`) and
correctly notes it is a normal (not runtime-prompted) protection-level permission, safe to declare
unconditionally.

## Checklist Results
- All three sensor `<uses-feature>` entries (accelerometer/gyroscope/compass, lines 60-62) are
  `android:required="false"` — correctly allows install on devices missing any of them, consistent
  with this demo's own manual-verification purpose (showing "unsupported" state gracefully rather
  than refusing to install).
- `HIGH_SAMPLING_RATE_SENSORS` permission's justification is falsifiable and specific (cites the
  exact default rate and the exact Android API level threshold it's needed for) rather than a vague
  "just in case."
- `<activity android:name="DemodevicesActivity" ...>` correctly matches the actual Java class
  (confirmed in `DemodevicesActivity.java.audit.md`).

## Detailed Findings
None.

## Cross-File Observations
Directly and correctly cross-references this project's own `docs/devices-android.md` for further
detail on the `HIGH_SAMPLING_RATE_SENSORS` permission's rationale (a docs-shard file, not
independently re-verified in this pass, but the citation itself is a good practice).

## Missing or Weak Tests
N/A — manifest declarations aren't unit-testable; would require an actual Android device/emulator
run to verify runtime behavior, outside this audit's scope.

## Positive Findings
The Task DEVICES-0123/ANDROID-BRIDGE-004 comments are excellent examples of precisely justifying a
manifest declaration with a specific technical reason and a citation, rather than copy-pasted
boilerplate rationale.

## Final Assessment
No findings.
