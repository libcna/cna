#include "Microsoft/Xna/Framework/GameComponent.hpp"

#include "Microsoft/Xna/Framework/Game.hpp"

namespace Microsoft::Xna::Framework
{
    GameComponent::GameComponent(Game& game)
        : game_(&game),
          enabled_(true),
          updateOrder_(0),
          disposed_(false)
    {
    }

    GameComponent::~GameComponent()
    {
        Dispose(false);
    }

    Game& GameComponent::getGameProperty() const
    {
        return *game_;
    }

    bool GameComponent::getEnabledProperty() const
    {
        return enabled_;
    }

    void GameComponent::setEnabledProperty(bool value)
    {
        if (enabled_ != value)
        {
            enabled_ = value;
            EnabledChanged.Raise(this, System::EventArgs::Empty);
            OnEnabledChanged(this, System::EventArgs::Empty);
        }
    }

    SharpRuntime::intcs GameComponent::getUpdateOrderProperty() const
    {
        return updateOrder_;
    }

    void GameComponent::setUpdateOrderProperty(SharpRuntime::intcs value)
    {
        if (updateOrder_ != value)
        {
            updateOrder_ = value;
            UpdateOrderChanged.Raise(this, System::EventArgs::Empty);
            OnUpdateOrderChanged(this, System::EventArgs::Empty);
        }
    }

    void GameComponent::Dispose()
    {
        Dispose(true);
    }

    void GameComponent::Initialize()
    {
    }

    void GameComponent::Update(GameTime& gameTime)
    {
        (void)gameTime;
    }

    SharpRuntime::intcs GameComponent::CompareTo(const GameComponent& other) const
    {
        return other.getUpdateOrderProperty() - getUpdateOrderProperty();
    }

    const std::string& GameComponent::GetTypeName() const
    {
        static const std::string typeName = "Microsoft.Xna.Framework.GameComponent";
        return typeName;
    }

    System::EventHandler<System::EventArgs>& GameComponent::getEnabledChangedEvent()
    {
        return EnabledChanged;
    }

    System::EventHandler<System::EventArgs>& GameComponent::getUpdateOrderChangedEvent()
    {
        return UpdateOrderChanged;
    }

    void GameComponent::OnUpdateOrderChanged(System::Object* sender, const System::EventArgs& args)
    {
        (void)sender;
        (void)args;
    }

    void GameComponent::OnEnabledChanged(System::Object* sender, const System::EventArgs& args)
    {
        (void)sender;
        (void)args;
    }

    void GameComponent::Dispose(bool disposing)
    {
        if (disposed_)
        {
            return;
        }

        disposed_ = true;

        if (disposing)
        {
            Disposed.Raise(this, System::EventArgs::Empty);
        }
    }
}
