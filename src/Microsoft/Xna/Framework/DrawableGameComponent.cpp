#include "Microsoft/Xna/Framework/DrawableGameComponent.hpp"

namespace Microsoft::Xna::Framework
{
    DrawableGameComponent::DrawableGameComponent(Game& game)
        : GameComponent(game),
          initialized_(false),
          drawOrder_(0),
          visible_(true)
    {
    }

    Graphics::GraphicsDevice& DrawableGameComponent::getGraphicsDeviceProperty()
    {
        return getGameProperty().getGraphicsDeviceProperty();
    }

    SharpRuntime::Int32 DrawableGameComponent::getDrawOrderProperty() const
    {
        return drawOrder_;
    }

    void DrawableGameComponent::setDrawOrderProperty(SharpRuntime::Int32 value)
    {
        if (drawOrder_ != value)
        {
            drawOrder_ = value;
            DrawOrderChanged.Raise(this, System::EventArgs::Empty);
            OnDrawOrderChanged(this, System::EventArgs::Empty);
        }
    }

    bool DrawableGameComponent::getVisibleProperty() const
    {
        return visible_;
    }

    void DrawableGameComponent::setVisibleProperty(bool value)
    {
        if (visible_ != value)
        {
            visible_ = value;
            VisibleChanged.Raise(this, System::EventArgs::Empty);
            OnVisibleChanged(this, System::EventArgs::Empty);
        }
    }

    void DrawableGameComponent::Initialize()
    {
        if (!initialized_)
        {
            initialized_ = true;

            // Full device-service event hookup belongs to IGraphicsDeviceService.
            // Until that service exists, loading immediately matches the common initialized-device path.
            LoadContent();
        }
    }

    void DrawableGameComponent::Dispose(bool disposing)
    {
        if (initialized_)
        {
            UnloadContent();
            initialized_ = false;
        }

        GameComponent::Dispose(disposing);
    }

    void DrawableGameComponent::OnDeviceCreated(System::Object* sender, const System::EventArgs& args)
    {
        (void) sender;
        (void) args;
        LoadContent();
    }

    void DrawableGameComponent::Draw(const GameTime& gameTime)
    {
        (void) gameTime;
    }

    void DrawableGameComponent::LoadContent()
    {
    }

    void DrawableGameComponent::UnloadContent()
    {
    }

    void DrawableGameComponent::OnVisibleChanged(System::Object* sender, const System::EventArgs& args)
    {
        (void) sender;
        (void) args;
    }

    void DrawableGameComponent::OnDrawOrderChanged(System::Object* sender, const System::EventArgs& args)
    {
        (void) sender;
        (void) args;
    }
}
