# Audit: examples/demo_devices/android/.../res/values/strings.xml

## Metadata
- Source file: `.../app/src/main/res/values/strings.xml` (4 lines)
- Audit status: AUDITED (full read)
- Subsystem: `examples-demo_devices` shard
- File type: Android string resources
- XNA/FNA relevance: none
- Main related tests: none

## Purpose
Declares `app_name`, referenced by `AndroidManifest.xml`'s `android:label` and the launcher icon
label.

## Executive Verdict
LOW finding: `app_name` is still the generic SDL-Android-project-template placeholder value
(`"Game"`), never customized for this demo. A device's home screen / app switcher would show the
installed app as "Game," not anything identifying it as the CNA Devices demo.

## Checklist Results
- Single string resource, correctly referenced by the manifest — but not customized.

## Detailed Findings

### LOW — `app_name` left as the generic template placeholder ("Game"), not customized for this demo
Cosmetic, not a functional defect. A user installing multiple CNA example demos on the same Android
device (this one and, e.g., `demo_avatar`'s equivalent, if it has an Android build) would be unable
to distinguish them by name on the home screen — each would plausibly also say "Game" if none of
the demo Android projects customized this value (not independently verified across other demos in
this pass, but worth flagging as a template-wide risk).

## Cross-File Observations
None beyond the finding above.

## Missing or Weak Tests
N/A.

## Positive Findings
None specific to this file.

## Final Assessment
One LOW finding: uncustomized template placeholder app name.
