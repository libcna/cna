#pragma once

#include "Microsoft/Xna/Framework/GameTime.hpp"
#include "SharpRuntime/SharpRuntimeHelper.hpp"
#include "System/EventArgs.hpp"
#include "System/EventHandler.hpp"

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

        /// Returns the EnabledChanged event.
        [[nodiscard]] virtual System::EventHandler<System::EventArgs>& getEnabledChangedEvent() = 0;

        /// Returns the UpdateOrderChanged event.
        [[nodiscard]] virtual System::EventHandler<System::EventArgs>& getUpdateOrderChangedEvent() = 0;

        /// Updates the component.
        virtual void Update(GameTime& gameTime) = 0;
    };
}
