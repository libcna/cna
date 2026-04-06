#pragma once

// NOTE:
// In .NET, EventHandler<TEventArgs> is only a delegate type.
// In this C++ port, it also stores subscribed handlers and provides Raise()/Invoke().
// This is intentional, because C++ has no equivalent of the C# "event" keyword.
// The goal is to keep ported XNA/.NET code visually as close as possible to the original C# source.

#include <functional>
#include <vector>
#include <utility>
#include <cstddef>

#include "System/Object.hpp"

namespace System
{
    /**
     * @brief Represents an event-like handler collection for a specific event argument type.
     *
     * In original C#/.NET, EventHandler<TEventArgs> is only a delegate type:
     *
     * @code
     * public delegate void EventHandler<TEventArgs>(object? sender, TEventArgs e);
     * @endcode
     *
     * That means in .NET it represents the signature of a single callback only.
     *
     * In this C++ port, EventHandler<TEventArgs> intentionally combines:
     * - the callback signature
     * - the list of subscribed handlers
     * - the logic for invoking all subscribed handlers
     *
     * This design differs from .NET because C++ does not have the C# keyword
     * @c event. In C#, the usual pattern is:
     *
     * @code
     * public event EventHandler<MyEventArgs> SomethingHappened;
     * @endcode
     *
     * where:
     * - EventHandler<TEventArgs> is the delegate type
     * - event is a language feature that stores and manages subscribed delegates
     *
     * Since standard C++ has no direct equivalent of the @c event keyword,
     * this class absorbs that behavior into a single type so that ported code
     * can stay visually close to the original C# source.
     *
     * Thanks to this design, C++ code can keep declarations such as:
     *
     * @code
     * System::EventHandler<ExitingEventArgs> Exiting;
     * @endcode
     *
     * and later invoke them with:
     *
     * @code
     * Exiting.Raise(this, args);
     * @endcode
     *
     * This is not a perfect architectural match to .NET internals, but it is a
     * practical compatibility layer for source-porting XNA/.NET code to C++.
     *
     * @tparam TEventArgs The type of event arguments passed to subscribed handlers.
     */
    template<typename TEventArgs>
    class EventHandler
    {
    public:
        /**
         * @brief Type of one subscribed event callback.
         *
         * The callback receives:
         * - sender: pointer to the object that raised the event
         * - e: event arguments
         */
        using HandlerType = std::function<void(Object* sender, const TEventArgs& e)>;

    private:
        /**
         * @brief Internal list of subscribed handlers.
         */
        std::vector<HandlerType> handlers;

    public:
        /**
         * @brief Constructs an empty event handler collection.
         */
        EventHandler() = default;

        /**
         * @brief Destroys the event handler collection.
         */
        ~EventHandler() = default;

        /**
         * @brief Adds a new subscribed handler.
         *
         * This operator is used to mimic the common C# event subscription style
         * as closely as possible.
         *
         * @param handler Handler to add.
         * @return Reference to this instance.
         */
        EventHandler& operator+=(HandlerType handler)
        {
            handlers.push_back(std::move(handler));
            return *this;
        }

        /**
         * @brief Adds a new subscribed handler.
         *
         * @param handler Handler to add.
         */
        void Add(HandlerType handler)
        {
            handlers.push_back(std::move(handler));
        }

        /**
         * @brief Removes all subscribed handlers.
         */
        void Clear()
        {
            handlers.clear();
        }

        /**
         * @brief Returns whether no handlers are currently subscribed.
         *
         * @return true if there are no subscribed handlers, otherwise false.
         */
        [[nodiscard]] bool Empty() const
        {
            return handlers.empty();
        }

        /**
         * @brief Returns the number of subscribed handlers.
         *
         * @return Number of handlers currently stored.
         */
        [[nodiscard]] std::size_t Size() const
        {
            return handlers.size();
        }

        /**
         * @brief Raises the event and invokes all subscribed handlers.
         *
         * All handlers are invoked in the order in which they were added.
         *
         * @param sender Object that raised the event.
         * @param e Event arguments.
         */
        void Raise(Object* sender, const TEventArgs& e)
        {
            for (auto& handler : handlers)
            {
                handler(sender, e);
            }
        }

        /**
         * @brief Invokes all subscribed handlers.
         *
         * This is an alias for Raise() and is provided because in C# events and
         * delegates are commonly invoked using the term "Invoke".
         *
         * @param sender Object that raised the event.
         * @param e Event arguments.
         */
        void Invoke(Object* sender, const TEventArgs& e)
        {
            Raise(sender, e);
        }
    };
}