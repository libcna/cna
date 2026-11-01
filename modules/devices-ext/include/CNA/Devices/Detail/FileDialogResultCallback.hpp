// SPDX-License-Identifier: MS-PL
#pragma once

#ifdef CNA_DEVICES

#include <functional>
#include <string>
#include <vector>

namespace CNA::Devices::Detail
{
    /**
     * @brief Callback type for a file dialog result.
     *
     * In `Detail` rather than nested inside `FileDialog` so the signature can be named without
     * pulling in the whole class, which the platform-facing plumbing needs.
     *
     * Receives the first selected-path result, or an empty list on cancellation, native error,
     * an unavailable service or provider rejection. Unavailability and rejection can complete
     * synchronously; native completion may use a provider thread. Later provider results for
     * an already-completed request are ignored.
     */
    using FileDialogResultCallback = std::function<void(const std::vector<std::string>& files)>;
}

#endif // CNA_DEVICES
