// SPDX-License-Identifier: MS-PL
// GS-009e: one CNA process against a real CNA service. A Guide-signed-in gamer reads avatars with
// the standard XNA API -- its own, another account's by gamertag, and one without an avatar --
// and renders its own, whose hat exists only in the service's newer catalog: the renderer must
// become Ready with that item downloaded by hash (the driver checks the verified disk cache).
// GSP-I1: it then saves an edit the way the CNA avatar editor does (the editor model against the
// service's newest catalog, avatars.set) and reads the stored avatar back.
// GSX-A: the hat's catalog is installed as one validated pack (the driver checks the installed
// pack and that the later run reuses it); with --view-only (a client declining catalog updates) it
// reads the service's projection and stops before the editor.
// Reads alice's password from stdin; prints avatar-* lines for the driver.
#include "CNA/Internal/GamerServices/AvatarAssets.hpp"
#include "CNA/Internal/GamerServices/AvatarDescriptionCodec.hpp"
#include "CNA/Internal/GamerServices/AvatarEditor.hpp"
#include "CNA/Internal/GamerServices/ServiceInvitations.hpp"
#include "CNA/Internal/GamerServices/ServiceSessionDirectory.hpp"
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
#include <cstdio>
#include <future>
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
// Runs work on the gamer services executor, as the editor's loads and saves do, and waits for it.
template<class T>T onExecutor(std::function<T(CNA::Internal::GamerServices::IGamerServicesBackend&)> work) {
    auto service=CNA::Internal::GamerServices::backend();
    auto promise=std::make_shared<std::promise<T>>();
    auto future=promise->get_future();
    service->submit([promise,service,work] {
        try {promise->set_value(work(*service));} catch(...) {promise->set_exception(std::current_exception());}
    },[]{});
    until([&]{return future.wait_for(std::chrono::seconds(0))==std::future_status::ready;});
    return future.get();
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
int main(int argc,char** argv) {
    try {
        const bool viewOnly=argc>1&&std::string(argv[1])=="--view-only";
        std::string available;
        for(auto version:Avatars::availableCatalogVersions())available+=(available.empty()?"":",")+std::to_string(version);
        std::cout<<"avatar-catalogs available="<<available<<"\n"<<std::flush;
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
        // GSP-H1: load times -- embedded catalog cold (first in the process), another avatar, the
        // same avatar again (model cache); then the service-only item (download on the driver's
        // first run, disk cache on its second).
        auto readyMs=[&](AvatarDescription& value) {
            const auto start=Clock::now();
            AvatarRenderer timed(&value);
            until([&]{return timed.getStateProperty()!=AvatarRendererState::Loading;});
            check(timed.getStateProperty()==AvatarRendererState::Ready,"a timed avatar becomes ready");
            return std::chrono::duration<double,std::milli>(Clock::now()-start).count();
        };
        auto first=AvatarDescription::CreateRandom(AvatarBodyType::Female);
        auto second=AvatarDescription::CreateRandom(AvatarBodyType::Male);
        const double cold=readyMs(first),warm=readyMs(second);
        auto again=AvatarDescription(second.getDescriptionProperty());
        AvatarRenderer keep(&second);
        until([&]{return keep.getStateProperty()!=AvatarRendererState::Loading;});
        const double same=readyMs(again);
        auto description=own;
        const auto serviceStart=Clock::now();
        AvatarRenderer renderer(&description);
        until([&]{return renderer.getStateProperty()!=AvatarRendererState::Loading;});
        check(renderer.getStateProperty()==AvatarRendererState::Ready,"the service avatar becomes ready");
        const double service=std::chrono::duration<double,std::milli>(Clock::now()-serviceStart).count();
        std::printf("avatar-timing embedded-cold=%.1fms embedded-warm=%.1fms embedded-same=%.1fms service-item=%.1fms\n",cold,warm,same,service);
        std::fflush(stdout);
        check(renderer.getBindPoseProperty().getCountProperty()==71,"71 bind pose bones");
        // The renderer keeps its model alive, so asking again returns the same assembled model.
        const auto load=Avatars::loadAvatarAsync(*Avatars::decode(description.getDescriptionProperty()));
        std::lock_guard guard(load->lock);
        check(load->done&&load->model,"assembled model");
        std::cout<<"avatar-ready substituted="<<load->model->substitutedItems.size()<<" unavailable="<<load->model->catalogUnavailable<<"\n"<<std::flush;
        if(viewOnly)return 0;

        // The editor: the service's newest catalog, one row changed, saved with avatars.set.
        using Backend=CNA::Internal::GamerServices::IGamerServicesBackend;
        const auto userId=CNA::Internal::GamerServices::GamerAccess::userId(*(*signedIn)[0]);
        const auto version=Avatars::parseManifest(onExecutor<std::string>([](Backend& s){return s.avatarCatalog(0);})).version;
        const auto catalog=Avatars::catalogManifest(version);
        check(catalog!=nullptr,"the service's newest catalog resolves");
        const auto stored=own.getDescriptionProperty();
        Avatars::AvatarEditorModel editor(catalog,std::vector<std::uint8_t>(stored.begin(),stored.end()),1);
        check(!editor.differsFromStored(),"the editor starts from the stored avatar");
        editor.setCategory(Avatars::EditorCategory::Hair);
        editor.select(1);
        check(editor.field(editor.selection()).label=="Hair color","hair category, hair color row");
        check(editor.adjust(1)&&editor.differsFromStored(),"an edit");
        const auto edited=editor.encoded();
        long long revision=0;
        for(int attempt=0;;++attempt) {
            try {
                revision=onExecutor<long long>([&](Backend& s){return s.setAvatar(userId,{edited.begin(),edited.end()});});
                break;
            } catch(const CNA::Internal::GamerServices::ServiceOperationError& error) {
                // The service accepts one avatar change per account every two seconds.
                if(attempt>2||error.code!="RATE_LIMITED")throw;
                std::this_thread::sleep_for(std::chrono::milliseconds(2100));
            }
        }
        const auto readBack=onExecutor<std::vector<CNA::Internal::GamerServices::ServiceAvatarRecord>>([&](Backend& s){return s.avatars({userId});});
        check(readBack.size()==1&&std::vector<std::uint8_t>(readBack[0].description.begin(),readBack[0].description.end())==edited,"the service stores the edit");
        std::cout<<"avatar-edit saved="<<(revision>0)<<" format="<<static_cast<int>(edited[0])<<"\n"<<std::flush;
        return 0;
    } catch(const std::exception& error) {
        std::cerr<<"avatar harness failed: "<<error.what()<<"\n";
        return 1;
    }
}
