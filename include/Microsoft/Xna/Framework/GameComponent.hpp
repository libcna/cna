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
#include "System/IComparable.hpp"
#include "System/IDisposable.hpp"
#include "System/Object.hpp"

namespace Microsoft::Xna::Framework
{
    class Game;

    /** @brief Base implementation for a game component that participates in the update loop. */
    class GameComponent : public System::Object,
                          public IGameComponent,
                          public IUpdateable,
                          public System::IComparable<GameComponent>,
                          public System::IDisposable
    {
    public:
        /** @brief Raised when the component is disposed. */
        System::EventHandler<System::EventArgs> Disposed;

        /** @brief Raised when the enabled state changes. */
        System::EventHandler<System::EventArgs> EnabledChanged;

        /** @brief Raised when the update order changes. */
        System::EventHandler<System::EventArgs> UpdateOrderChanged;

        /**
         * @brief Creates a component owned by the specified game.
         * @param game The game that owns this component.
         */
        explicit GameComponent(Game& game);

        /** @brief Releases component resources. */
        ~GameComponent() override;

        /**
         * @brief Gets the game that owns this component.
         * @return A reference to the owning game.
         */
        [[nodiscard]] Game& getGameProperty() const;

        /**
         * @brief Gets whether this component should be updated.
         * @return true if the component participates in the update loop.
         */
        [[nodiscard]] bool getEnabledProperty() const override;

        /**
         * @brief Sets whether this component should be updated.
         * @param value true to enable updates; false to disable them.
         */
        void setEnabledProperty(bool value);

        /**
         * @brief Gets the order used to sort updateable components.
         * @return The update order value; lower values update first.
         */
        [[nodiscard]] SharpRuntime::intcs getUpdateOrderProperty() const override;

        /**
         * @brief Sets the order used to sort updateable components.
         * @param value The new update order value.
         */
        void setUpdateOrderProperty(SharpRuntime::intcs value);

        /**
         * @brief Returns the EnabledChanged event.
         * @return A reference to the EnabledChanged event handler.
         */
        [[nodiscard]] System::EventHandler<System::EventArgs>& getEnabledChangedEvent() override;

        /**
         * @brief Returns the UpdateOrderChanged event.
         * @return A reference to the UpdateOrderChanged event handler.
         */
        [[nodiscard]] System::EventHandler<System::EventArgs>& getUpdateOrderChangedEvent() override;

        /** @brief Shuts down the component. */
        void Dispose() override;

        /** @brief Initializes the component. */
        void Initialize() override;

        /**
         * @brief Updates the component.
         * @param gameTime Snapshot of the game timing state.
         */
        void Update(GameTime& gameTime) override;

        /**
         * @brief Compares this component with another component using update order.
         * @param other The other component to compare with.
         * @return A negative value, zero, or positive based on update order comparison.
         */
        [[nodiscard]] int CompareTo(const GameComponent& other) const override;

        /**
         * @brief Returns the fully-qualified .NET type name of this class.
         * @return A const reference to the type name string.
         */
        NOXNA [[nodiscard]] const std::string& GetTypeName() const override;

    protected:
        /**
         * @brief Called after update order changes.
         * @param sender The object that originated the event.
         * @param args Event data.
         */
        virtual void OnUpdateOrderChanged(System::Object* sender, const System::EventArgs& args);

        /**
         * @brief Called after enabled state changes.
         * @param sender The object that originated the event.
         * @param args Event data.
         */
        virtual void OnEnabledChanged(System::Object* sender, const System::EventArgs& args);

        /**
         * @brief Performs disposal. When disposing is true, managed-style events may be raised.
         * @param disposing true when called from Dispose(); false when called from the destructor.
         */
        virtual void Dispose(bool disposing);

    private:
        Game* game_;
        bool enabled_;
        SharpRuntime::intcs updateOrder_;
        bool disposed_;
    };
}
