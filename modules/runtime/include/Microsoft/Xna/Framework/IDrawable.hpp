// SPDX-License-Identifier: MS-PL

#pragma once

#include "Microsoft/Xna/Framework/GameTime.hpp"
#include "SharpRuntime/SharpRuntimeHelper.hpp"
#include "System/EventArgs.hpp"
#include "System/EventHandler.hpp"

namespace Microsoft::Xna::Framework
{
    /** @brief Provides draw behavior and draw ordering for a drawable game object. */
    class IDrawable
    {
    public:
        /** @brief Virtual destructor. */
        virtual ~IDrawable() = default;

        /**
         * @brief Gets the order used to sort drawable components.
         * @return The draw order value; lower values are drawn first.
         */
        [[nodiscard]] virtual SharpRuntime::intcs getDrawOrderProperty() const = 0;

        /**
         * @brief Gets whether this object should be drawn.
         * @return true if this object is visible and should be drawn.
         */
        [[nodiscard]] virtual bool getVisibleProperty() const = 0;

        /**
         * @brief Returns the DrawOrderChanged event.
         * @return A reference to the DrawOrderChanged event handler.
         */
        [[nodiscard]] virtual System::EventHandler<System::EventArgs>& getDrawOrderChangedEvent() = 0;

        /**
         * @brief Returns the VisibleChanged event.
         * @return A reference to the VisibleChanged event handler.
         */
        [[nodiscard]] virtual System::EventHandler<System::EventArgs>& getVisibleChangedEvent() = 0;

        /**
         * @brief Draws the object.
         * @param gameTime Snapshot of the game timing state.
         */
        virtual void Draw(const GameTime& gameTime) = 0;
    };
}
