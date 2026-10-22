// SPDX-License-Identifier: MS-PL
#pragma once

#include "Microsoft/Phone/Notification/ChannelErrorType.hpp"
#include "SharpRuntime/SharpRuntimeHelper.hpp"
#include "System/EventArgs.hpp"

namespace Microsoft::Phone::Notification {

    /** @brief Describes a notification channel failure. */
    class NotificationChannelErrorEventArgs : public System::EventArgs {
    public:
        /** @brief Gets the error category. @return The channel error category. */
        [[nodiscard]] ChannelErrorType getErrorTypeProperty() const;

        /**
         * @brief Gets the error code.
         * @return A failure HRESULT: HRESULT_FROM_WIN32 for Winsock errors, or E_FAIL on POSIX.
         */
        [[nodiscard]] SharpRuntime::intcs getErrorCodeProperty() const;

        /**
         * @brief Gets additional error information.
         * @return The native socket error code (errno or Winsock) for CNA's local listener.
         */
        [[nodiscard]] SharpRuntime::intcs getErrorAdditionalDataProperty() const;

        /** @brief Gets the error description. @return The failed operation and its error. */
        [[nodiscard]] const SharpRuntime::String& getMessageProperty() const;

    private:
        friend class HttpNotificationChannel;
        NotificationChannelErrorEventArgs(ChannelErrorType type, SharpRuntime::intcs code,
                                          SharpRuntime::intcs additionalData, SharpRuntime::String message);
        ChannelErrorType type_;
        SharpRuntime::intcs code_;
        SharpRuntime::intcs additionalData_;
        SharpRuntime::String message_;
    };

} // namespace Microsoft::Phone::Notification
