#pragma once

namespace System {

    /**
     * \class EventArgs
     * \brief Represents the base class for classes that contain event data.
     *
     * This class is a C++ reimplementation of the .NET `System::EventArgs` type.
     * It serves as the common base class for all event argument classes and can
     * also be used for events that do not provide any additional event-specific
     * data.
     *
     * The static instance ::System::EventArgs::Empty can be used when an event
     * does not need to pass custom data to its handlers.
     * @note Status: Verified
     */
    class EventArgs {
    public:
        /**
         * \brief A shared empty instance of EventArgs.
         */
        static const EventArgs Empty;

        /**
         * \brief Initializes a new instance of the EventArgs class.
         */
        EventArgs() = default;
    };

}