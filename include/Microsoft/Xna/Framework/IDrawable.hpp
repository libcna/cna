#pragma once

#include "Microsoft/Xna/Framework/GameTime.hpp"
#include "SharpRuntime/SharpRuntimeHelper.hpp"

namespace Microsoft::Xna::Framework
{
    /// Provides draw behavior and draw ordering for a drawable game object.
    class IDrawable
    {
    public:
        virtual ~IDrawable() = default;

        /// Gets the order used to sort drawable components.
        [[nodiscard]] virtual SharpRuntime::intcs getDrawOrderProperty() const = 0;

        /// Gets whether this object should be drawn.
        [[nodiscard]] virtual bool getVisibleProperty() const = 0;

        /// Draws the object.
        virtual void Draw(const GameTime& gameTime) = 0;
    };
}
