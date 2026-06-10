// SPDX-License-Identifier: MS-PL

#pragma once

#include "Microsoft/Xna/Framework/GameTime.hpp"
#include "SharpRuntime/SharpRuntimeHelper.hpp"
#include "System/EventArgs.hpp"
#include "System/EventHandler.hpp"

namespace Microsoft::Xna::Framework
{
    /// Provides draw behavior and draw ordering for a drawable game object.
    class IDrawable
    {
    public:
        /// Virtual destructor.
        virtual ~IDrawable() = default;

        /// Gets the order used to sort drawable components.
        [[nodiscard]] virtual SharpRuntime::intcs getDrawOrderProperty() const = 0;

        /// Gets whether this object should be drawn.
        [[nodiscard]] virtual bool getVisibleProperty() const = 0;

        /// Returns the DrawOrderChanged event.
        [[nodiscard]] virtual System::EventHandler<System::EventArgs>& getDrawOrderChangedEvent() = 0;

        /// Returns the VisibleChanged event.
        [[nodiscard]] virtual System::EventHandler<System::EventArgs>& getVisibleChangedEvent() = 0;

        /// Draws the object.
        virtual void Draw(const GameTime& gameTime) = 0;
    };
}
