# Location scope and compatibility boundary

**Status: not implemented; checked 2026-11-01.** CNA has no IPlatformLocation service or Location
capability, and the local Sharp Runtime checkout has no GeoCoordinate/GeoCoordinateWatcher types.
This is a scope guard, not an implementation checklist.

The owner's decisions of 2026-10-25 and the proposed delivery sequence are in
[location-proposal.md](location-proposal.md). That proposal supersedes this page's older platform
sketch. In particular, Android is AOSP LocationManager only; an optional Google fused provider is
not part of the approved design.

## Namespace and ownership

Location belongs to the separate System.Device.Location API, not Microsoft.Devices.Sensors.
Do not add a Location sensor, LocationReading or GPS property to Accelerometer/Compass/Gyroscope/
Motion. The [Microsoft location namespace reference](https://learn.microsoft.com/en-us/dotnet/api/system.device.location?view=netframework-4.8)
describes the separate watcher/coordinate model.

Under the project rule for System.* types, the proposed compatibility types, watcher logic and
provider seam belong in a new Sharp Runtime device-location component. CNA would provide the
platform service and adapter. The old sketch's CNA-local include/System/Device/Location paths are
obsolete; no such module or headers exist yet.

Before implementation, check the archived Windows Phone surface and behavior separately from
the desktop .NET Framework reference. The proposal's examples are design sketches, not compilable
CNA code or measured WP7 parity.

## Compass true heading

AndroidCompassBackend::PublishReading currently supplies MagneticHeading as TrueHeading.
There is no location or geographic declination integration.

A possible later declination dependency should be narrow and optional, internal to the compass
implementation, without changing Compass's public WP7 surface or making Compass.Start require
location. The earlier IDeclinationSource name is a sketch, not an existing interface. A current
coordinate alone is not a declination model.

Compass::SetBackendForTesting is the existing CNAEXT test seam; SetRendererForTesting, named by
the old text, does not exist. Test injection is not the proposed public location API.

## Document lifecycle

Keep these two files separate while implementation has not started. The owner-approved LOC-11
step merges them into docs/location.md when that work begins. Completed sensor tasks remain in
Git history; this page does not reopen them or authorize language-specific bindings.
