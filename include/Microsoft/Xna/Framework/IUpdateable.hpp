// SPDX-License-Identifier: MS-PL

#pragma once

#include "Microsoft/Xna/Framework/GameTime.hpp"
#include "SharpRuntime/SharpRuntimeHelper.hpp"
#include "System/EventArgs.hpp"
#include "System/EventHandler.hpp"

namespace Microsoft::Xna::Framework
{
    /** @brief Provides update behavior and update ordering for a game component. */
    class IUpdateable
    {
    public:
        /** @brief Virtual destructor. */
        virtual ~IUpdateable() = default;

        /**
         * @brief Gets whether this component should be updated.
         * @return true if the component participates in the update loop.
         */
        [[nodiscard]] virtual bool getEnabledProperty() const = 0;

        /**
         * @brief Gets the order used to sort updateable components.
         * @return The update order value; lower values update first.
         */
        [[nodiscard]] virtual SharpRuntime::intcs getUpdateOrderProperty() const = 0;

        /**
         * @brief Returns the EnabledChanged event.
         * @return A reference to the EnabledChanged event handler.
         */
        [[nodiscard]] virtual System::EventHandler<System::EventArgs>& getEnabledChangedEvent() = 0;

        /**
         * @brief Returns the UpdateOrderChanged event.
         * @return A reference to the UpdateOrderChanged event handler.
         */
        [[nodiscard]] virtual System::EventHandler<System::EventArgs>& getUpdateOrderChangedEvent() = 0;

        /**
         * @brief Updates the component.
         * @param gameTime Snapshot of the game timing state.
         */
        virtual void Update(GameTime& gameTime) = 0;
    };
}
