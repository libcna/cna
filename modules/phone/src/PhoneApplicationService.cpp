#include "Microsoft/Phone/Shell/PhoneApplicationService.hpp"

#include "Microsoft/Xna/Framework/Game.hpp"

namespace Microsoft::Phone::Shell {

    PhoneApplicationService::PhoneApplicationService() = default;

    PhoneApplicationService::~PhoneApplicationService()
    {
        DetachEXT();
    }

    PhoneApplicationService& PhoneApplicationService::getCurrentProperty()
    {
        static PhoneApplicationService current;
        return current;
    }

    PhoneApplicationService::StateDictionary& PhoneApplicationService::getStateProperty()
    {
        return state_;
    }

    const PhoneApplicationService::StateDictionary& PhoneApplicationService::getStateProperty() const
    {
        return state_;
    }

    void PhoneApplicationService::AttachEXT(Xna::Framework::Game& game)
    {
        DetachEXT();

        game_ = &game;
        hasBeenDeactivated_ = false;

        deactivatedToken_ = game.Deactivated.Add(
            [this](System::Object*, const System::EventArgs&)
            {
                hasBeenDeactivated_ = true;
                DeactivatedEventArgs args;
                Deactivated.Raise(nullptr, args);
            });

        activatedToken_ = game.Activated.Add(
            [this](System::Object*, const System::EventArgs&)
            {
                // Only a return counts. See hasBeenDeactivated_ in the header for why the
                // first activation of a run is not one.
                if (!hasBeenDeactivated_)
                {
                    return;
                }
                // The process is demonstrably still the one that went away -- this handler is
                // running in it -- so the instance was preserved.
                ActivatedEventArgs args(true);
                Activated.Raise(nullptr, args);
            });

        exitingToken_ = game.Exiting.Add(
            [this](System::Object*, const System::EventArgs&)
            {
                ClosingEventArgs args;
                Closing.Raise(nullptr, args);
            });

        LaunchingEventArgs launching;
        Launching.Raise(nullptr, launching);
    }

    void PhoneApplicationService::DetachEXT()
    {
        if (game_ == nullptr)
        {
            return;
        }

        game_->Activated.Remove(activatedToken_);
        game_->Deactivated.Remove(deactivatedToken_);
        game_->Exiting.Remove(exitingToken_);

        game_ = nullptr;
        activatedToken_ = 0;
        deactivatedToken_ = 0;
        exitingToken_ = 0;
        hasBeenDeactivated_ = false;
    }

} // namespace Microsoft::Phone::Shell
