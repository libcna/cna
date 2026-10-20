// SPDX-License-Identifier: MS-PL

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
            const auto keepAlive = RetainCollectionOwnership();
            enabled_ = value;
            // XNA GameComponent IL dispatches through the virtual hook, not before it.
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
            const auto keepAlive = RetainCollectionOwnership();
            updateOrder_ = value;
            OnUpdateOrderChanged(this, System::EventArgs::Empty);
        }
    }

    void GameComponent::Dispose()
    {
        // Removing this component can release its last collection-owned reference.
        // Keep it alive through the entire virtual call, including a derived epilogue.
        const auto keepAlive = RetainCollectionOwnership();
        Dispose(true);
    }

    std::shared_ptr<IGameComponent> GameComponent::RetainCollectionOwnership()
    {
        const auto& owned = game_->getComponentsProperty().owned_;
        const auto found = owned.find(this);
        return found != owned.end() ? found->second : nullptr;
    }

    void GameComponent::Initialize()
    {
    }

    void GameComponent::Update(GameTime& gameTime)
    {
        (void)gameTime;
    }

    int GameComponent::CompareTo(const GameComponent& other) const
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
        UpdateOrderChanged.Raise(this, args);
    }

    void GameComponent::OnEnabledChanged(System::Object* sender, const System::EventArgs& args)
    {
        (void)sender;
        EnabledChanged.Raise(this, args);
    }

    void GameComponent::Dispose(bool disposing)
    {
        // XNA GameComponent.Dispose(bool) removes membership before every explicit
        // Disposed event. Only the unmanaged/destructor path is idempotent here.
        if (disposed_ && !disposing)
        {
            return;
        }

        disposed_ = true;

        if (disposing)
        {
            (void)game_->getComponentsProperty().Remove(this);
            Disposed.Raise(this, System::EventArgs::Empty);
        }
    }
}
