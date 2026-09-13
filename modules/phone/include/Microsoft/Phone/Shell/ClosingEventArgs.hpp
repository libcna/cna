// SPDX-License-Identifier: MS-PL
#pragma once

#include "System/EventArgs.hpp"

namespace Microsoft::Phone::Shell {

    /**
     * @brief Arguments for the event raised when the application is closing for good.
     *
     * Raised when the user backs out of the application rather than switching away from it. An
     * application that closes this way is not coming back to preserved state, so this is the
     * last point at which anything can be written.
     */
    class ClosingEventArgs : public System::EventArgs {
    public:
        /** @brief Constructs the arguments. */
        ClosingEventArgs() = default;
    };

} // namespace Microsoft::Phone::Shell
