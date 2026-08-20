# GPS / Location — Future Plan (Not Implemented)

## This does not belong in `Microsoft.Devices.Sensors`

GPS/location is **not part of the real `Microsoft.Devices.Sensors` API** on Windows
Phone 7. It lives in a separate WP7 namespace/assembly: `System.Device.Location`
(`Microsoft.Devices.dll` vs. `System.Device.dll` are distinct real WP7 assemblies).
`Microsoft::Devices::Sensors` in this codebase (`Accelerometer`, `Compass`, `Gyroscope`,
`Motion`) and `Microsoft::Devices::VibrateController` correctly have no location
member, property, or class — this is not an omission, it is the accurate API surface.

**This document exists so a future session doesn't accidentally "complete" location
support by bolting it onto `Microsoft::Devices::Sensors`** (an easy, plausible-looking
mistake — location conceptually feels like "another sensor") — and so there's a
concrete starting sketch if/when location support is ever actually scoped as its own
task. **Nothing in this document is implemented. No code exists for any of this yet.**

---

## If ever implemented: where it belongs

A future compatibility layer for this should live under a `System::Device::Location`
namespace (mirroring the real WP7 assembly/namespace split), e.g.:

```text
include/System/Device/Location/GeoCoordinateWatcher.hpp
include/System/Device/Location/GeoCoordinate.hpp
include/System/Device/Location/GeoPositionChangedEventArgs.hpp
include/System/Device/Location/GeoPositionStatusChangedEventArgs.hpp
include/System/Device/Location/GeoPositionStatus.hpp
include/System/Device/Location/GeoPositionAccuracy.hpp
```

Likely real API members (from the documented WP7 `System.Device.Location` surface —
not independently re-verified against an archived MSDN page the way
`Microsoft::Devices::Sensors` was in `plans/plan_devices_phase2.md` Task P2-2; treat these as
a starting sketch, re-verify before actually implementing):

- **`GeoCoordinateWatcher`** — the main entry point, roughly mirroring this codebase's
  existing sensor-class shape (`Start()`, `Stop()`, a `PositionChanged` event,
  a `Status`/`StatusChanged` pair) but for location specifically. Constructor takes a
  `GeoPositionAccuracy` (`Default`/`High`) desired-accuracy hint.
- **`GeoCoordinate`** — `Latitude`, `Longitude`, `Altitude`, `HorizontalAccuracy`,
  `VerticalAccuracy`, `Speed`, `Course`, plus `IsUnknown`/`Unknown`.
- **`GeoPositionChangedEventArgs<GeoCoordinate>`** — generic over the position type in
  real WP7 (`GeoPosition<T>`), carrying `.Position.Location` (the `GeoCoordinate`) and
  `.Position.Timestamp`.
- **`GeoPositionStatusChangedEventArgs`** / **`GeoPositionStatus`** enum
  (`Disabled`/`Initializing`/`NoData`/`Ready`) — mirrors this codebase's existing
  `SensorState` shape closely enough that `SensorState`-adjacent naming conventions
  already established for `Microsoft::Devices::Sensors` would translate directly.

## Android path (if ever implemented)

- **AOSP / no-GMS baseline:** `android.location.LocationManager`
  (`requestLocationUpdates()`, `GPS_PROVIDER`/`NETWORK_PROVIDER`) — works on any Android
  device, no Google Play Services dependency. This should be the default/required
  renderer, matching this project's general "don't require proprietary vendor services
  for a core feature" posture (see e.g. `VibrateController`'s SDL3-only approach, no
  vendor-specific rumble APIs).
- **Optional, `CNAEXT`/opt-in enhancement:** Google's Fused Location Provider API
  (`com.google.android.gms.location.FusedLocationProviderClient`) — better accuracy/
  battery behavior, but requires Google Play Services, so it must be a strictly
  optional, separately-buildable renderer, never the only path, to keep AOSP/no-GMS
  devices fully supported.
- Requires `ACCESS_FINE_LOCATION`/`ACCESS_COARSE_LOCATION` runtime permission
  (Android 6.0+) — a JNI/Kotlin bridge would need to surface permission-denied as a
  `GeoPositionStatus` value (likely `Disabled` or a new status), not a silent failure.

## iOS path (if ever implemented)

- `CoreLocation` (`CLLocationManager`, `CLLocationManagerDelegate.locationManager(_:didUpdateLocations:)`),
  requesting `requestWhenInUseAuthorization()` (or `requestAlwaysAuthorization()` if
  background location is ever needed, which XNA/WP7 parity would not require).
- `CLLocation` → `GeoCoordinate` is a near-direct field mapping (`coordinate.latitude`/
  `.longitude`, `altitude`, `horizontalAccuracy`, `verticalAccuracy`, `speed`, `course`).

## How `Compass::TrueHeading` would consume this, if ever built (Task COMPASS-003, 2026-07-06)

Re-confirmed 2026-07-06 that this document's core reasoning still applies specifically
to `Compass::TrueHeading` — no change to the decision, just re-checked rather than
silently re-asserted. `Detail::AndroidCompassBackend::PublishReading()` currently sets
`TrueHeading = MagneticHeading` (`COMPASS-002`, confirmed reasonable — the real API
never documents a "declination unknown" fallback because it assumes location is always
available). If a future task ever implements the `System::Device::Location` layer
sketched above, `Compass`'s consumption of it should be:

- An **optional, separately-injected dependency** passed into `AndroidCompassBackend`
  (or a successor), never a hard requirement `Compass::Start()` fails without — a game
  that never touches location should see identical behavior to today (`TrueHeading ==
  MagneticHeading`), not a new failure mode.
- The dependency should be expressed as a small, narrow interface (e.g. something like
  `Detail::IDeclinationSource` returning the current magnetic declination for a given
  coordinate/date, using WMM/IGRF-style declination models — not the full
  `GeoCoordinateWatcher` surface directly), so `AndroidCompassBackend` depends on
  "give me a declination value" rather than the entire location subsystem's shape —
  mirroring this codebase's existing `Detail::ICompassBackend`/`IMotionBackend`
  seam-injection pattern (`SetRendererForTesting()`) rather than inventing a new
  dependency-injection style.
- This keeps the strict XNA `Compass` public surface completely unpolluted — no new
  public method/property/constructor parameter on `Compass` itself; the injection point
  would live entirely in `Detail::`, exactly like `Compass::SetRendererForTesting()`
  already does for its main renderer.
- **Still not implemented, still not scheduled** — this is a plan for *if* it's ever
  built, not a commitment that it will be. The answer to "should CNA implement real
  declination right now" remains **no**, per this document's existing reasoning
  (`System::Device::Location` doesn't exist yet at all, and building it is a
  substantially separate, unscoped effort from anything in `plans/plan_devices.md`).

## Explicitly not doing right now

- No `GeoCoordinateWatcher`/`GeoCoordinate`/any `System::Device::Location` type exists
  in this codebase yet. This document is a placeholder for a future, separately-scoped
  task — not a task itself.
- No GPS/location member should be added to any `Microsoft::Devices::Sensors` class
  under any circumstances, including as a `CNAEXT` extension — the real WP7 API draws
  this line clearly (separate assembly), and this codebase should preserve that
  separation even where CNA takes liberties elsewhere.
