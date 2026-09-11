#pragma once

#include "System/EventArgs.hpp"

namespace Microsoft::Phone::Shell {

    /**
     * @brief Arguments for the event raised when the application returns to the foreground.
     *
     * @note Whether the process itself survived is the whole question this event exists to
     * answer, which is what `IsApplicationInstancePreserved` reports. A preserved instance still
     * holds everything it had in memory, including `PhoneApplicationService::State`; an instance
     * that was not preserved is a new process that has to read its state back from storage.
     */
    class ActivatedEventArgs : public System::EventArgs {
    public:
        /** @brief Constructs the arguments for an activation of a process that was not preserved. */
        ActivatedEventArgs() = default;

        /**
         * @brief Constructs the arguments.
         *
         * @param isApplicationInstancePreserved True when the process itself stayed alive.
         */
        explicit ActivatedEventArgs(bool isApplicationInstancePreserved)
            : isApplicationInstancePreserved_(isApplicationInstancePreserved)
        {
        }

        /**
         * @brief Whether the application's own process survived the time in the background.
         *
         * @return True when the instance was preserved and its in-memory state is still there.
         */
        [[nodiscard]] bool getIsApplicationInstancePreservedProperty() const
        {
            return isApplicationInstancePreserved_;
        }

    private:
        bool isApplicationInstancePreserved_ = false;
    };

} // namespace Microsoft::Phone::Shell
