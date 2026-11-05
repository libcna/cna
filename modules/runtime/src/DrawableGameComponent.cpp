// SPDX-License-Identifier: MS-PL

#include "Microsoft/Xna/Framework/DrawableGameComponent.hpp"
#include "System/InvalidOperationException.hpp"
#include <array>

namespace Microsoft::Xna::Framework
{
    // Shared event state makes automatic C++ teardown safe even if the service dies first.
    // Explicit disposal removes one delegate occurrence, as Refresh Dispose IL_0011..0068 does.
    struct DrawableGameComponent::DeviceSubscriptions
    {
        using Event = System::EventHandler<System::EventArgs>;
        Graphics::IGraphicsDeviceService* service = nullptr;
        std::array<Event, 4> events;
        std::array<Event::Token, 4> tokens{};
        std::size_t count = 0;
        ~DeviceSubscriptions()
        {
            for (std::size_t i = 0; i < count; ++i) events[i].Remove(tokens[i]);
        }
    };

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
        // XNA Refresh get_GraphicsDevice IL_0000..001e uses the captured service.
        if (deviceService_ == nullptr)
            throw System::InvalidOperationException("The GraphicsDevice property requires Initialize.");
        auto* device = deviceService_->getGraphicsDeviceProperty();
        // CNA's reference return cannot represent XNA's null GraphicsDevice.
        if (device == nullptr)
            throw System::InvalidOperationException("The graphics device service has no device.");
        return *device;
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
        GameComponent::Initialize();
        if (!initialized_)
        {
            // XNA Refresh Initialize IL_0011..00ba: capture, validate, subscribe,
            // optionally load, and only then mark initialized. FNA marks it first.
            deviceService_ = getGameProperty().getServicesProperty().GetService<Graphics::IGraphicsDeviceService>();
            if (deviceService_ == nullptr)
                throw System::InvalidOperationException("Drawable components require a graphics device service.");

            auto subscriptions = std::make_shared<DeviceSubscriptions>();
            subscriptions->service = deviceService_;
            const std::array<DeviceSubscriptions::Event*, 4> sources{
                &deviceService_->getDeviceCreatedEvent(), &deviceService_->getDeviceResettingEvent(),
                &deviceService_->getDeviceResetEvent(), &deviceService_->getDeviceDisposingEvent()};
            const std::weak_ptr<int> lifetime = deviceCallbackLifetime_;
            deviceSubscriptions_.push_back(subscriptions);
            for (std::size_t i = 0; i < sources.size(); ++i)
            {
                sources[i]->Share();
                subscriptions->events[i] = *sources[i];
                subscriptions->tokens[i] = sources[i]->Add(
                    [this, lifetime, i](System::Object* sender, const System::EventArgs& args)
                    {
                        if (lifetime.expired()) return;
                        const auto keepAlive = RetainCollectionOwnership();
                        if (i == 0) OnDeviceCreated(sender, args);
                        else if (i == 3) UnloadContent();
                        // XNA Refresh DeviceResetting/DeviceReset IL_0000: ret.
                    });
                ++subscriptions->count;
            }
            if (deviceService_->getGraphicsDeviceProperty() != nullptr) LoadContent();
            initialized_ = true;
        }
    }

    void DrawableGameComponent::Dispose(bool disposing)
    {
        // XNA DrawableGameComponent.Dispose(bool) invokes the managed hook on
        // every explicit call, including before Initialize; false never invokes it.
        if (disposing)
        {
            UnloadContent();
            // Refresh Dispose IL_0003..0068 unloads before detaching event handlers.
            for (std::size_t i = deviceSubscriptions_.size(); i != 0; --i)
            {
                if (deviceSubscriptions_[i - 1]->service == deviceService_)
                {
                    deviceSubscriptions_.erase(deviceSubscriptions_.begin() +
                                               static_cast<std::ptrdiff_t>(i - 1));
                    break;
                }
            }
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
