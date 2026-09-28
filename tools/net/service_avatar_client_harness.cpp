// SPDX-License-Identifier: MS-PL
// GS-009e: one CNA process against a real CNA service. A Guide-signed-in gamer reads avatars with
// the standard XNA API -- its own, another account's by gamertag, and one without an avatar --
// and renders its own, whose hat exists only in the service's newer catalog: the renderer must
// become Ready with that item downloaded by hash (the driver checks the verified disk cache).
// Reads alice's password from stdin; prints avatar-* lines for the driver.
#include "CNA/Internal/GamerServices/AvatarAssets.hpp"
#include "CNA/Internal/GamerServices/AvatarDescriptionCodec.hpp"
#include "Microsoft/Xna/Framework/GamerServices/AvatarDescription.hpp"
#include "Microsoft/Xna/Framework/GamerServices/AvatarRenderer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/Gamer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GamerServicesDispatcher.hpp"
#include "Microsoft/Xna/Framework/GamerServices/Guide.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamerCollection.hpp"
#include "Microsoft/Xna/Framework/Input/TextInputEXT.hpp"
#include "System/IServiceProvider.hpp"
#include <chrono>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

using namespace Microsoft::Xna::Framework::GamerServices;
namespace Avatars = CNA::Internal::GamerServices::Avatars;
namespace {
using Clock=std::chrono::steady_clock;
void check(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}
class Provider : public System::IServiceProvider {public:void* GetService(const std::type_info&)const override{return nullptr;}};
void enter(const std::string& value) {
    for(unsigned char character:value)Microsoft::Xna::Framework::Input::TextInputEXT::INTERNAL_OnTextInput(character);
    Microsoft::Xna::Framework::Input::TextInputEXT::INTERNAL_OnTextInput(u'\r');
}
template<class Work>void until(Work work,int seconds=30) {
    const auto deadline=Clock::now()+std::chrono::seconds(seconds);
    while(!work()) {
        check(Clock::now()<deadline,"deadline");GamerServicesDispatcher::Update();
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
}
AvatarDescription avatarOf(Gamer* gamer) {
    std::unique_ptr<System::IAsyncResult> result(AvatarDescription::BeginGetFromGamer(gamer,{},{}));
    check(!result->getCompletedSynchronouslyProperty(),"a service-backed avatar read is asynchronous");
    until([&]{return result->getIsCompletedProperty();});
    return AvatarDescription::EndGetFromGamer(result.get());
}
std::string summary(const AvatarDescription& description) {
    const auto bytes=description.getDescriptionProperty();
    const auto decoded=Avatars::decode(bytes);
    return "valid="+std::to_string(description.getIsValidProperty())+" body="+std::to_string(static_cast<int>(description.getBodyTypeProperty()))+
        " height="+std::to_string(static_cast<int>(description.getHeightProperty()*1000.0f+0.5f))+
        " catalog="+std::to_string(decoded?decoded->catalogVersion:0);
}
}
int main() {
    try {
        Provider provider;GamerServicesDispatcher::Initialize(provider);auto* signedIn=Gamer::getSignedInGamersProperty();
        Guide::ShowSignIn(1,true);
        std::string password;std::getline(std::cin,password);enter("alice");enter(password);
        until([&]{return signedIn->getCountProperty()==1;},15);
        const auto own=avatarOf((*signedIn)[0]);
        std::cout<<"avatar-own "<<summary(own)<<"\n"<<std::flush;
        std::unique_ptr<Gamer> bob(Gamer::GetFromGamertag("Bob"));
        std::cout<<"avatar-lookup "<<summary(avatarOf(bob.get()))<<"\n"<<std::flush;
        std::unique_ptr<Gamer> charlie(Gamer::GetFromGamertag("Charlie"));
        std::cout<<"avatar-none "<<summary(avatarOf(charlie.get()))<<"\n"<<std::flush;
        auto description=own;
        AvatarRenderer renderer(&description);
        until([&]{return renderer.getStateProperty()!=AvatarRendererState::Loading;});
        check(renderer.getStateProperty()==AvatarRendererState::Ready,"the service avatar becomes ready");
        check(renderer.getBindPoseProperty().getCountProperty()==71,"71 bind pose bones");
        // The renderer keeps its model alive, so asking again returns the same assembled model.
        const auto load=Avatars::loadAvatarAsync(*Avatars::decode(description.getDescriptionProperty()));
        std::lock_guard guard(load->lock);
        check(load->done&&load->model,"assembled model");
        std::cout<<"avatar-ready substituted="<<load->model->substitutedItems.size()<<"\n"<<std::flush;
        return 0;
    } catch(const std::exception& error) {
        std::cerr<<"avatar harness failed: "<<error.what()<<"\n";
        return 1;
    }
}
