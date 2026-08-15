// SPDX-License-Identifier: MS-PL
#pragma once

#ifdef CNA_DEVICES

namespace CNA::Devices
{
    /**
     * @brief Reports basic system hardware information (CPU cores, RAM).
     *
     * CNA extension — no XNA/WP7 equivalent exists. Backed by
     * `IPlatformSystemInfo::GetLogicalCpuCount()`/`GetSystemRamMiB()`, implemented for every
     * platform CNA targets, including Web/Emscripten (approximated from
     * `navigator.hardwareConcurrency` there).
     *
     * Deliberately minimal scope: this does not wrap every SIMD-capability query
     * native libraries may expose — only what a game would
     * plausibly branch on for thread-pool sizing or a "low-end device" quality
     * preset. A future task can add specific SIMD queries if a real consumer need
     * appears.
     *
     * All members are static: this class has no instance state of its own.
     */
    class SystemInfo
    {
    public:
        /**
         * @brief Gets the number of logical CPU cores available.
         *
         * @return The number of logical CPU cores. Always at least 1.
         */
        [[nodiscard]] static int getLogicalCpuCoreCountProperty();

        /**
         * @brief Gets the amount of system RAM, in megabytes.
         *
         * @return The amount of system RAM, in MB.
         */
        [[nodiscard]] static int getSystemRamMegabytesProperty();

    private:
        /** @brief Not instantiable — every member is static. */
        SystemInfo() = delete;
    };
} // namespace CNA::Devices

#endif // CNA_DEVICES
