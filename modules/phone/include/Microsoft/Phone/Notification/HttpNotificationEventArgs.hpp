#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "System/EventArgs.hpp"

namespace Microsoft::Phone::Notification {

    /**
     * @brief Arguments for the event raised when a raw notification arrives.
     */
    class HttpNotificationEventArgs : public System::EventArgs {
    public:
        /** @brief Constructs empty arguments. */
        HttpNotificationEventArgs() = default;

        /**
         * @brief Constructs the arguments.
         *
         * @param body The notification's body, exactly as it arrived.
         */
        explicit HttpNotificationEventArgs(std::vector<std::uint8_t> body)
            : body_(std::move(body))
        {
        }

        /**
         * @brief The notification's body.
         *
         * @return The bytes the sender posted.
         *
         * @note .NET exposes this as `Notification.Body`, a `Stream`. It is the bytes here,
         * because every use of it reads the whole thing at once and a stream over a buffer
         * already in memory would only add a step.
         */
        [[nodiscard]] const std::vector<std::uint8_t>& getBodyProperty() const { return body_; }

        /**
         * @brief The notification's body as text.
         *
         * @return The bytes decoded as UTF-8.
         */
        [[nodiscard]] std::string getBodyAsStringProperty() const
        {
            return std::string(body_.begin(), body_.end());
        }

    private:
        std::vector<std::uint8_t> body_;
    };

} // namespace Microsoft::Phone::Notification
