// SPDX-License-Identifier: MS-PL

#pragma once

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GameComponent.hpp"
#include "Microsoft/Xna/Framework/GameTime.hpp"
#include "Microsoft/Xna/Framework/IDrawable.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "SharpRuntime/SharpRuntimeHelper.hpp"
#include "System/EventArgs.hpp"
#include "System/EventHandler.hpp"
#include "System/Object.hpp"

namespace Microsoft::Xna::Framework
{
    /** @brief Game component that can draw itself and participates in draw ordering and visibility. */
    class DrawableGameComponent : public GameComponent, public IDrawable
    {
    public:
        /**
         * @brief Creates a drawable component attached to the specified game.
         * @param game The game that owns this component.
         */
        explicit DrawableGameComponent(Game& game);

        /**
         * @brief Gets the graphics device associated with the owning game.
         * @return A reference to the graphics device.
         */
        [[nodiscard]] Graphics::GraphicsDevice& getGraphicsDeviceProperty();

        /**
         * @brief Gets the drawing order value used to sort drawable components.
         * @return The draw order; lower values are drawn first.
         */
        [[nodiscard]] SharpRuntime::Int32 getDrawOrderProperty() const override;

        /**
         * @brief Sets the drawing order and raises the draw-order change event when the value changes.
         * @param value The new draw order value.
         */
        void setDrawOrderProperty(SharpRuntime::Int32 value);

        /**
         * @brief Gets whether this component should be drawn.
         * @return true if the component is visible and should be drawn.
         */
        [[nodiscard]] bool getVisibleProperty() const override;

        /**
         * @brief Sets visibility and raises the visibility change event when the value changes.
         * @param value true to make the component visible; false to hide it.
         */
        void setVisibleProperty(bool value);

        /**
         * @brief Returns the DrawOrderChanged event.
         * @return A reference to the DrawOrderChanged event handler.
         */
        [[nodiscard]] System::EventHandler<System::EventArgs>& getDrawOrderChangedEvent() override;

        /**
         * @brief Returns the VisibleChanged event.
         * @return A reference to the VisibleChanged event handler.
         */
        [[nodiscard]] System::EventHandler<System::EventArgs>& getVisibleChangedEvent() override;

        /** @brief Raised when the draw order changes. */
        System::EventHandler<System::EventArgs> DrawOrderChanged;

        /** @brief Raised when visibility changes. */
        System::EventHandler<System::EventArgs> VisibleChanged;

        /**
         * @brief Returns the fully-qualified .NET type name of this class.
         * @return A const reference to the type name string.
         */
        CNAEXT [[nodiscard]] const std::string& GetTypeName() const override;

        /** @brief Initializes the component and loads content when a graphics device is available. */
        void Initialize() override;

        /**
         * @brief Draws the component.
         * @param gameTime Snapshot of the game timing state.
         */
        void Draw(const GameTime& gameTime) override;

    protected:
        /**
         * @brief Releases component content when the component is disposed.
         * @param disposing true when called from Dispose(); false when called from the destructor.
         */
        void Dispose(bool disposing) override;

        /** @brief Loads graphics resources used by the component. */
        virtual void LoadContent();

        /** @brief Releases graphics resources used by the component. */
        virtual void UnloadContent();

        /**
         * @brief Called after visibility changes.
         * @param sender The object that originated the event.
         * @param args Event data.
         */
        virtual void OnVisibleChanged(System::Object* sender, const System::EventArgs& args);

        /**
         * @brief Called after draw order changes.
         * @param sender The object that originated the event.
         * @param args Event data.
         */
        virtual void OnDrawOrderChanged(System::Object* sender, const System::EventArgs& args);

    private:
        bool initialized_;
        SharpRuntime::Int32 drawOrder_;
        bool visible_;

        void OnDeviceCreated(System::Object* sender, const System::EventArgs& args);
    };
}
