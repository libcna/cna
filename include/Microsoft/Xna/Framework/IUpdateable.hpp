#pragma once

#include "Microsoft/Xna/Framework/GameTime.hpp"
#include "SharpRuntime/SharpRuntimeHelper.hpp"

namespace Microsoft::Xna::Framework
{
    /// Provides update behavior and update ordering for a game component.
    class IUpdateable
    {
    public:
        virtual ~IUpdateable() = default;

        /// Gets whether this component should be updated.
        [[nodiscard]] virtual bool getEnabledProperty() const = 0;

        /// Gets the order used to sort updateable components.
        [[nodiscard]] virtual SharpRuntime::intcs getUpdateOrderProperty() const = 0;

        /// Updates the component.
        virtual void Update(GameTime& gameTime) = 0;
    };
}
