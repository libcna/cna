// SPDX-License-Identifier: MS-PL
//
// Task TEST2-010 (2026-07-18, external audit `audit_devices_2026-07-17.md`): the negative
// counterpart to tools/devices/StrictXnaApiSurfaceCheck.cpp, closing that task's own second
// acceptance criterion ("a deliberately leaked extension fails every check") -- which had no
// coverage at all before this task, only the positive "real API still compiles clean" direction.
//
// This translation unit deliberately calls Accelerometer::InjectSyntheticSensorUpdate(), a
// CNAEXT-tagged member (see Accelerometer.hpp; also explicitly named in
// StrictXnaApiSurfaceCheck.cpp's own "Deliberately NOT calling" list). Compiled with the same
// CNA_STRICT_XNA_API + -Werror=deprecated-declarations flags as that file's own target -- this
// one target is expected, and required, to FAIL TO BUILD. See cmake/Harnesses.cmake's own
// cna_strict_xna_api_leak_check target (EXCLUDE_FROM_ALL, so this expected failure never breaks
// the normal build) and the StrictXnaApiSurfaceLeakCheck_MustFailToCompile ctest (WILL_FAIL
// TRUE) that actually invokes and checks this.
//
// If a future CNAEXT macro change (CNAHelper.hpp) or CNA_STRICT_XNA_API build option ever stops
// actually catching a CNAEXT member call, this file compiling successfully is exactly the signal
// that regression would produce -- the ctest wrapping this target would then unexpectedly PASS
// (WILL_FAIL TRUE flips a successful build into a reported test failure), catching the
// regression instead of silently missing it.
#include "Microsoft/Devices/Sensors/Accelerometer.hpp"

int main()
{
    Microsoft::Devices::Sensors::Accelerometer accelerometer;
    accelerometer.InjectSyntheticSensorUpdate(1.0f, 0.0f, 0.0f); // CNAEXT -- must fail to compile here.
    return 0;
}
