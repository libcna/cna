#pragma once

#include <memory>
#include <string>

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Phone/Shell/ActivatedEventArgs.hpp"
#include "Microsoft/Phone/Shell/ClosingEventArgs.hpp"
#include "Microsoft/Phone/Shell/DeactivatedEventArgs.hpp"
#include "Microsoft/Phone/Shell/LaunchingEventArgs.hpp"
#include "System/Collections/Generic/Dictionary.hpp"
#include "System/EventHandler.hpp"
#include "System/Object.hpp"

namespace Microsoft::Xna::Framework {
    class Game;
}

namespace Microsoft::Phone::Shell {

    /**
     * @brief The application-lifecycle service a Windows Phone application talks to.
     *
     * A phone game does not own its own lifetime. The operating system moves it out of the
     * foreground when the user takes a call, may kill the process while it is away, and starts
     * it again later expecting it to look as though it never stopped. This service is where an
     * application is told about those transitions and where it keeps the state that has to
     * survive them.
     *
     * The four events describe one run: exactly one of `Launching` (a fresh start) or
     * `Activated` (a return) opens it, and `Deactivated` (going to the background, possibly
     * never to return) or `Closing` (the user closed the application) ends it. A game saves
     * from `Deactivated`, because that is the last event guaranteed to arrive before the
     * process can be taken away.
     *
     * @note **CNA has no operating-system shell above the game, so the transitions come from
     * the game.** `Microsoft::Xna::Framework::Game` already receives the platform layer's
     * application-lifecycle events and turns them into its own `Activated`, `Deactivated` and
     * `Exiting`; `AttachEXT` subscribes this service to those, so the phone events are the real
     * platform transitions rather than a simulation. An application therefore attaches the
     * service once, which is the one line a phone application does not have to write.
     */
    class PhoneApplicationService {
    public:
        /** @brief The dictionary type `State` exposes. */
        using StateDictionary =
            System::Collections::Generic::Dictionary<std::string, std::shared_ptr<System::Object>>;

        /**
         * @brief The application's single lifecycle service.
         *
         * @return The process-wide instance.
         */
        [[nodiscard]] static PhoneApplicationService& getCurrentProperty();

        /** @brief Raised when the application starts without preserved state. */
        System::EventHandler<LaunchingEventArgs> Launching;

        /** @brief Raised when the application returns to the foreground. */
        System::EventHandler<ActivatedEventArgs> Activated;

        /** @brief Raised when the application moves to the background. */
        System::EventHandler<DeactivatedEventArgs> Deactivated;

        /** @brief Raised when the application is closing and will not return. */
        System::EventHandler<ClosingEventArgs> Closing;

        /**
         * @brief State that survives the application being moved to the background.
         *
         * Whatever an application puts here is still there when it is activated again, provided
         * the process itself was preserved. It is deliberately not persistent storage: if the
         * process was killed the dictionary comes back empty, which is why an application that
         * must survive that writes to isolated storage from `Deactivated` as well.
         *
         * @return The state dictionary, keyed by the application's own names.
         */
        [[nodiscard]] StateDictionary& getStateProperty();

        /**
         * @brief The state dictionary of a service that must not be modified.
         *
         * @return The state dictionary.
         */
        [[nodiscard]] const StateDictionary& getStateProperty() const;

        /**
         * @brief Drives this service from a game's lifecycle, and reports the fresh start.
         *
         * Subscribes to @p game's `Activated`, `Deactivated` and `Exiting` events and raises the
         * phone events from them, then raises `Launching`, because a process that has just
         * reached this point started without preserved state.
         *
         * Attaching a second time detaches the first game first, so a service is never driven by
         * two games at once.
         *
         * @param game The game whose lifecycle drives this service.
         */
        CNAEXT void AttachEXT(Xna::Framework::Game& game);

        /**
         * @brief Stops driving this service from the attached game.
         *
         * Safe to call when nothing is attached. Called by the destructor, so a game that
         * outlives nothing in particular still leaves no dangling subscription behind.
         */
        CNAEXT void DetachEXT();

        /** @brief Constructs a service that is not attached to anything. */
        PhoneApplicationService();

        /** @brief Detaches from the game, if any, and destroys the service. */
        ~PhoneApplicationService();

        PhoneApplicationService(const PhoneApplicationService&) = delete;
        PhoneApplicationService& operator=(const PhoneApplicationService&) = delete;

    private:
        StateDictionary state_;

        Xna::Framework::Game* game_ = nullptr;
        System::EventHandler<System::EventArgs>::Token activatedToken_ = 0;
        System::EventHandler<System::EventArgs>::Token deactivatedToken_ = 0;
        System::EventHandler<System::EventArgs>::Token exitingToken_ = 0;

        // A game raises Activated when its window first takes focus, which on a phone is part of
        // starting up rather than a return from the background. Without this, every run would
        // report Launching and then immediately Activated, and an application that reloads its
        // saved game from Activated would reload it on a fresh start too.
        bool hasBeenDeactivated_ = false;
    };

} // namespace Microsoft::Phone::Shell
