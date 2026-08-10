// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/GamerServices/GamerServicesDispatcher.hpp"
#include "Microsoft/Xna/Framework/GamerServices/Gamer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamerCollection.hpp"
#include "Microsoft/Xna/Framework/PlayerIndex.hpp"
#include <vector>

namespace Microsoft::Xna::Framework::GamerServices
{
    bool GamerServicesDispatcher::isInitialized_ = false;
    SharpRuntime::IntPtr GamerServicesDispatcher::windowHandle_ = 0;
    std::size_t GamerServicesDispatcher::freedGamerCount_ = 0;
    System::EventHandler<System::EventArgs> GamerServicesDispatcher::InstallingTitleUpdate;

    bool GamerServicesDispatcher::getIsInitializedProperty() { return isInitialized_; }
    SharpRuntime::IntPtr GamerServicesDispatcher::getWindowHandleProperty() { return windowHandle_; }
    void GamerServicesDispatcher::setWindowHandleProperty(SharpRuntime::IntPtr value) { windowHandle_ = value; }

    void GamerServicesDispatcher::Initialize(System::IServiceProvider& /*serviceProvider*/)
    {
        isInitialized_ = true;

        // FNA also hooks AppDomain.CurrentDomain.ProcessExit to reset IsInitialized to false
        // on process exit. There is no equivalent hook in C++; intentionally omitted.

        // Task 7.5/10.2: GamerCollection<T> holds non-owning raw pointers (see its doc comment for
        // the canonical ownership contract), and Gamer::setSignedInGamersProperty() below only
        // deletes the previous SignedInGamerCollection wrapper itself, not its contents - this
        // loop is this Initialize() call's own ownership registry for the SignedInGamer objects
        // it creates, per that contract. A second Initialize() call previously leaked the first
        // call's 4 SignedInGamer objects permanently before this existed. Free them explicitly
        // first (a harmless no-op the first time this runs, since getSignedInGamersProperty()
        // lazily returns an empty collection until Initialize() has run at least once).
        for (SignedInGamer* previous : *Gamer::getSignedInGamersProperty())
        {
            delete previous;
            ++freedGamerCount_;
        }

        std::vector<SignedInGamer*> startGamers;
        startGamers.push_back(new SignedInGamer(SignedInGamer::CreateInternal(
            "Stub Gamer", isInitialized_
        )));
        // FNA marks these three as "FIXME: This is stupid" — kept as-is for fidelity.
        startGamers.push_back(new SignedInGamer(SignedInGamer::CreateInternal(
            "Stub Gamer (1)", isInitialized_, true, Microsoft::Xna::Framework::PlayerIndex::Two
        )));
        startGamers.push_back(new SignedInGamer(SignedInGamer::CreateInternal(
            "Stub Gamer (2)", isInitialized_, true, Microsoft::Xna::Framework::PlayerIndex::Three
        )));
        startGamers.push_back(new SignedInGamer(SignedInGamer::CreateInternal(
            "Stub Gamer (3)", isInitialized_, true, Microsoft::Xna::Framework::PlayerIndex::Four
        )));

        Gamer::setSignedInGamersProperty(new SignedInGamerCollection(
            SignedInGamerCollection::CreateInternal(startGamers)
        ));

        for (SignedInGamer* gamer : *Gamer::getSignedInGamersProperty())
        {
            SignedInGamer::OnSignIn(gamer);
        }
    }

    void GamerServicesDispatcher::Update()
    {
    }

    bool GamerServicesDispatcher::UpdateAsync()
    {
        if (isInitialized_)
        {
            Update();
        }
        return isInitialized_;
    }

    std::size_t GamerServicesDispatcher::GetFreedGamerCountForTesting()
    {
        return freedGamerCount_;
    }
}
