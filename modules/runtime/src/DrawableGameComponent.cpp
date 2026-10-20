// SPDX-License-Identifier: MS-PL

#include "Microsoft/Xna/Framework/DrawableGameComponent.hpp"

namespace Microsoft::Xna::Framework
{
    const std::string& DrawableGameComponent::GetTypeName() const
    {
        static const std::string typeName = "Microsoft.Xna.Framework.DrawableGameComponent";
        return typeName;
    }

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
            const auto keepAlive = RetainCollectionOwnership();
            drawOrder_ = value;
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
            const auto keepAlive = RetainCollectionOwnership();
            visible_ = value;
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
        // XNA DrawableGameComponent.Dispose(bool) invokes the managed hook on
        // every explicit call, including before Initialize; false never invokes it.
        if (disposing)
        {
            UnloadContent();
        }

        GameComponent::Dispose(disposing);
    }

    void DrawableGameComponent::OnDeviceCreated(System::Object* sender, const System::EventArgs& args)
    {
        (void)sender;
        (void)args;
        LoadContent();
    }

    void DrawableGameComponent::Draw(const GameTime& gameTime)
    {
        (void)gameTime;
    }

    void DrawableGameComponent::LoadContent()
    {
    }

    void DrawableGameComponent::UnloadContent()
    {
    }

    System::EventHandler<System::EventArgs>& DrawableGameComponent::getDrawOrderChangedEvent()
    {
        return DrawOrderChanged;
    }

    System::EventHandler<System::EventArgs>& DrawableGameComponent::getVisibleChangedEvent()
    {
        return VisibleChanged;
    }

    void DrawableGameComponent::OnVisibleChanged(System::Object* sender, const System::EventArgs& args)
    {
        (void)sender;
        VisibleChanged.Raise(this, args);
    }

    void DrawableGameComponent::OnDrawOrderChanged(System::Object* sender, const System::EventArgs& args)
    {
        (void)sender;
        DrawOrderChanged.Raise(this, args);
    }
}
