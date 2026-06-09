// SPDX-License-Identifier: MS-PL

#pragma once

#include <string>

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/GameTime.hpp"
#include "Microsoft/Xna/Framework/IGameComponent.hpp"
#include "Microsoft/Xna/Framework/IUpdateable.hpp"
#include "SharpRuntime/SharpRuntimeHelper.hpp"
#include "System/EventArgs.hpp"
#include "System/EventHandler.hpp"
#include "System/IDisposable.hpp"
#include "System/Object.hpp"

namespace Microsoft::Xna::Framework
{
    class Game;

    /// Base implementation for a game component that participates in the update loop.
    class GameComponent : public System::Object,
                          public IGameComponent,
                          public IUpdateable,
                          public System::IDisposable
    {
    public:
        /// Raised when the component is disposed.
        System::EventHandler<System::EventArgs> Disposed;

        /// Raised when the enabled state changes.
        System::EventHandler<System::EventArgs> EnabledChanged;

        /// Raised when the update order changes.
        System::EventHandler<System::EventArgs> UpdateOrderChanged;

        /// Creates a component owned by the specified game.
        explicit GameComponent(Game& game);

        /// Releases component resources.
        ~GameComponent() override;

        /// Gets the game that owns this component.
        [[nodiscard]] Game& getGameProperty() const;

        /// Gets whether this component should be updated.
        [[nodiscard]] bool getEnabledProperty() const override;

        /// Sets whether this component should be updated.
        void setEnabledProperty(bool value);

        /// Gets the order used to sort updateable components.
        [[nodiscard]] SharpRuntime::intcs getUpdateOrderProperty() const override;

        /// Sets the order used to sort updateable components.
        void setUpdateOrderProperty(SharpRuntime::intcs value);

        /// Returns the EnabledChanged event.
        [[nodiscard]] System::EventHandler<System::EventArgs>& getEnabledChangedEvent() override;

        /// Returns the UpdateOrderChanged event.
        [[nodiscard]] System::EventHandler<System::EventArgs>& getUpdateOrderChangedEvent() override;

        /// Shuts down the component.
        void Dispose() override;

        /// Initializes the component.
        void Initialize() override;

        /// Updates the component.
        void Update(GameTime& gameTime) override;

        /// Compares this component with another component using update order.
        [[nodiscard]] SharpRuntime::intcs CompareTo(const GameComponent& other) const;

        NOXNA [[nodiscard]] const std::string& GetTypeName() const override;

    protected:
        /// Called after update order changes.
        virtual void OnUpdateOrderChanged(System::Object* sender, const System::EventArgs& args);

        /// Called after enabled state changes.
        virtual void OnEnabledChanged(System::Object* sender, const System::EventArgs& args);

        /// Performs disposal. When disposing is true, managed-style events may be raised.
        virtual void Dispose(bool disposing);

    private:
        Game* game_;
        bool enabled_;
        SharpRuntime::intcs updateOrder_;
        bool disposed_;
    };
}
