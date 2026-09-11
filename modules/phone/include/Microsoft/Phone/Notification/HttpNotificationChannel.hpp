// SPDX-License-Identifier: MS-PL
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Phone/Notification/HttpNotificationEventArgs.hpp"
#include "Microsoft/Phone/Notification/NotificationChannelUriEventArgs.hpp"
#include "System/EventHandler.hpp"
#include "System/Uri.hpp"

namespace Microsoft::Phone::Notification {

    /**
     * @brief A channel a service pushes notifications to.
     *
     * An application opens a channel, learns its URI from `ChannelUriUpdated`, and gives that
     * URI to whatever service should reach it. The service then posts to the URI and the bytes
     * arrive as `HttpNotificationReceived`.
     *
     * @note **The channel is the endpoint here, not a relay to one.** On Windows Phone the URI
     * pointed at Microsoft's push service, which forwarded to the device because a phone behind
     * a carrier's network had no address anyone could post to. That service is retired, and the
     * reason for it does not apply to a machine that can simply listen: this channel opens its
     * own HTTP listener and hands out its own address. Nothing changes for the sender -- it
     * still posts to the URI it was given -- and nothing changes for the receiver, which still
     * gets the bytes. What is gone is the hop in between, along with the two things that hop
     * provided and this cannot: reaching a device that is not directly addressable, and
     * delivering to an application that is not running.
     *
     * @note **Notifications are queued, not raised where they arrive.** They land on the
     * listener thread; `DispatchPendingNotificationsEXT` raises them on the caller's thread, so
     * a handler never runs while a frame is half drawn. Windows Phone got the same effect from
     * its dispatcher.
     */
    class HttpNotificationChannel {
    public:
        /**
         * @brief Finds an open channel by name.
         *
         * @param channelName The name the channel was created with.
         * @return The channel, or null when no channel of that name is open.
         */
        [[nodiscard]] static HttpNotificationChannel* Find(const std::string& channelName);

        /**
         * @brief Creates a channel.
         *
         * The channel is not listening until `Open` is called.
         *
         * @param channelName The channel's name, unique within the application.
         * @param serviceName The name of the service that will push to it.
         */
        HttpNotificationChannel(std::string channelName, std::string serviceName);

        /** @brief Closes the channel if it is open, and destroys it. */
        ~HttpNotificationChannel();

        HttpNotificationChannel(const HttpNotificationChannel&) = delete;
        HttpNotificationChannel& operator=(const HttpNotificationChannel&) = delete;

        /** @brief Raised once the channel knows its URI. */
        System::EventHandler<NotificationChannelUriEventArgs> ChannelUriUpdated;

        /** @brief Raised for each raw notification that arrives. */
        System::EventHandler<HttpNotificationEventArgs> HttpNotificationReceived;

        /**
         * @brief The channel's name.
         *
         * @return The name it was created with.
         */
        [[nodiscard]] const std::string& getChannelNameProperty() const { return channelName_; }

        /**
         * @brief Where a service should push to reach this channel.
         *
         * @return The URI, or null before the channel is open -- which is what .NET returns
         *         then, and what an application checks for before handing the URI to a service.
         */
        [[nodiscard]] const System::Uri* getChannelUriProperty() const
        {
            return channelUri_.has_value() ? &*channelUri_ : nullptr;
        }

        /**
         * @brief Whether the channel is bound to the shell's toast display.
         *
         * @return True once BindToShellToast has been called.
         */
        [[nodiscard]] bool getIsShellToastBoundProperty() const { return shellToastBound_; }

        /**
         * @brief Starts listening, and raises ChannelUriUpdated with the address.
         *
         * Opening a channel that is already open does nothing.
         */
        void Open();

        /** @brief Stops listening and releases the address. */
        void Close();

        /**
         * @brief Asks the shell to display toast notifications sent through this channel.
         *
         * @note There is no shell here to display them, so this records the binding and
         * nothing more. A toast the service sends still arrives as a notification; what it
         * does not do is appear over other applications, which is not something this runtime
         * can offer and does not pretend to.
         */
        void BindToShellToast();

        /**
         * @brief Raises the events of every notification that has arrived.
         *
         * Called from the thread that should handle them -- for a game, its own, once per
         * frame. Nothing is raised anywhere else.
         */
        CNAEXT void DispatchPendingNotificationsEXT();

    private:
        void Listen();

        std::string channelName_;
        std::string serviceName_;
        std::optional<System::Uri> channelUri_;
        bool shellToastBound_ = false;

        std::atomic<bool> listening_{false};
        std::thread listener_;
        int listenSocket_ = -1;

        std::mutex mutex_;
        std::vector<std::vector<std::uint8_t>> pending_;
    };

} // namespace Microsoft::Phone::Notification
