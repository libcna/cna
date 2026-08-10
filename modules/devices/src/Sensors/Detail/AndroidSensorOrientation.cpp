// SPDX-License-Identifier: MS-PL
#include "Microsoft/Devices/Sensors/Detail/AndroidSensorOrientation.hpp"

#include <atomic>

namespace
{
    // Task ACCEL-008: relaxed ordering is sufficient -- this is a coarse-grained,
    // rarely-toggled opt-out flag, not a synchronization mechanism for any other
    // state. See SDL-SENSOR-004's own resolution (sharp-runtime's TimeSpan
    // copy_count/move_count) for what happens when a shared counter like this is
    // left as a plain, non-atomic global instead.
    std::atomic<bool> g_androidLandscapeRemapEnabled{true};
} // namespace

namespace Microsoft::Devices::Sensors::Detail
{
    void SetAndroidLandscapeRemapEnabled(bool enabled)
    {
        g_androidLandscapeRemapEnabled.store(enabled, std::memory_order_relaxed);
    }

    bool IsAndroidLandscapeRemapEnabled()
    {
        return g_androidLandscapeRemapEnabled.load(std::memory_order_relaxed);
    }
} // namespace Microsoft::Devices::Sensors::Detail
