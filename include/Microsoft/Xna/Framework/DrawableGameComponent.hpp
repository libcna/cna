#pragma once

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
    /// Game component that can draw itself and participates in draw ordering and visibility.
    class DrawableGameComponent : public GameComponent, public IDrawable
    {
    public:
        /// Creates a drawable component attached to the specified game.
        explicit DrawableGameComponent(Game& game);

        /// Gets the graphics device associated with the owning game.
        [[nodiscard]] Graphics::GraphicsDevice& getGraphicsDeviceProperty();

        /// Gets the drawing order value used to sort drawable components.
        [[nodiscard]] SharpRuntime::Int32 getDrawOrderProperty() const;

        /// Sets the drawing order and raises the draw-order change event when the value changes.
        void setDrawOrderProperty(SharpRuntime::Int32 value);

        /// Gets whether this component should be drawn.
        [[nodiscard]] bool getVisibleProperty() const;

        /// Sets visibility and raises the visibility change event when the value changes.
        void setVisibleProperty(bool value);

        /// Raised when the draw order changes.
        System::EventHandler<System::EventArgs> DrawOrderChanged;

        /// Raised when visibility changes.
        System::EventHandler<System::EventArgs> VisibleChanged;

        /// Initializes the component and loads content when a graphics device is available.
        void Initialize() override;

        /// Draws the component.
        void Draw(const GameTime& gameTime) override;

    protected:
        /// Releases component content when the component is disposed.
        void Dispose(bool disposing) override;

        /// Loads graphics resources used by the component.
        virtual void LoadContent();

        /// Releases graphics resources used by the component.
        virtual void UnloadContent();

        /// Called after visibility changes.
        virtual void OnVisibleChanged(System::Object* sender, const System::EventArgs& args);

        /// Called after draw order changes.
        virtual void OnDrawOrderChanged(System::Object* sender, const System::EventArgs& args);

    private:
        bool initialized_;
        SharpRuntime::Int32 drawOrder_;
        bool visible_;

        void OnDeviceCreated(System::Object* sender, const System::EventArgs& args);
    };
}
