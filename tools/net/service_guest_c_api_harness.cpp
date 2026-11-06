// SPDX-License-Identifier: MS-PL
// Real Guide sign-in; all session/roster operations below use the exported C API.
#include "CNA/C/cna.h"
#include "Microsoft/Xna/Framework/GamerServices/Gamer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GamerServicesDispatcher.hpp"
#include "Microsoft/Xna/Framework/GamerServices/Guide.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamerCollection.hpp"
#include "Microsoft/Xna/Framework/Input/TextInputEXT.hpp"
#include "System/IServiceProvider.hpp"
#include <chrono>
#include <iostream>
#include <set>
#include <thread>
#include <poll.h>
#include <unistd.h>
using namespace Microsoft::Xna::Framework::GamerServices;
namespace {
CNA_NetworkSessionHandle session=CNA_INVALID_HANDLE;
void check(bool v,const char* why){if(!v)throw std::runtime_error(why);}
void ok(CNA_Result r){check(r==CNA_RESULT_SUCCESS,"C API result");}
struct Provider:System::IServiceProvider{void* GetService(const std::type_info&)const override{return nullptr;}};
void pump(){GamerServicesDispatcher::Update();if(session!=CNA_INVALID_HANDLE)ok(cna_network_session_update(session));}
template<class Work>void until(Work work){
    const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(30);
    while(!work()){check(std::chrono::steady_clock::now()<end,"C guest deadline");pump();std::this_thread::sleep_for(std::chrono::milliseconds(2));}
}
std::string line(){
    std::string result;const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(60);
    while(true){
        pollfd fd{0,POLLIN,0};if(poll(&fd,1,2)>0){char c=0;check(read(0,&c,1)==1,"parent input");if(c=='\n')return result;result+=c;check(result.size()<256,"input ceiling");}
        pump();check(std::chrono::steady_clock::now()<end,"barrier deadline");
    }
}
void enter(const std::string& text){for(unsigned char c:text)Microsoft::Xna::Framework::Input::TextInputEXT::INTERNAL_OnTextInput(c);Microsoft::Xna::Framework::Input::TextInputEXT::INTERNAL_OnTextInput(u'\r');}
}
int main(int argc,char** argv){
    try {
        check(argc==4&&std::string(argv[3])=="guests-c","C harness arguments");
        const bool host=std::string(argv[1])=="host";check(host||std::string(argv[1])=="join","role");
        const std::string kind=argv[2];check(kind=="player"||kind=="ranked","kind");
        const auto type=kind=="player"?CNA_NETWORK_SESSION_TYPE_PLAYER_MATCH:CNA_NETWORK_SESSION_TYPE_RANKED;
        Provider provider;GamerServicesDispatcher::Initialize(provider);Guide::ShowSignIn(2,true);
        const auto password=line();enter(host?"alice":"bob");enter(password);
        auto* signedIn=Gamer::getSignedInGamersProperty();until([&]{return signedIn->getCountProperty()==1;});enter("Guest");
        until([&]{return signedIn->getCountProperty()==2;});
        CNA_Handle locals[2]{};ok(cna_gamer_get_signed_in_gamer_at(0,&locals[0]));ok(cna_gamer_get_signed_in_gamer_at(1,&locals[1]));
        CNA_Bool guest=CNA_FALSE;ok(cna_signed_in_gamer_get_is_guest(locals[1],&guest));check(guest==CNA_TRUE,"C signed-in guest flag");
        if(host){
            ok(cna_network_session_create_with_local_gamers(type,locals,2,6,0,CNA_INVALID_HANDLE,&session));
            std::cout<<"session-created\n"<<std::flush;
        }else{
            CNA_AvailableNetworkSessionCollectionHandle found=CNA_INVALID_HANDLE;
            ok(cna_network_session_find_with_local_gamers(type,locals,2,CNA_INVALID_HANDLE,&found));
            int32_t count=0;ok(cna_available_network_session_collection_get_count(found,&count));check(count==1,"C guest Find");
            CNA_AvailableNetworkSessionHandle listing=CNA_INVALID_HANDLE;ok(cna_available_network_session_collection_copy_session(found,0,&listing));
            ok(cna_network_session_join(listing,&session));ok(cna_available_network_session_destroy(listing));ok(cna_available_network_session_collection_destroy(found));
            std::cout<<"session-joined\n"<<std::flush;
        }
        until([&]{int32_t n=0;ok(cna_network_session_get_gamer_count(session,CNA_NETWORK_SESSION_ROSTER_ALL,&n));return n==4;});
        // WIRE arithmetic: two accounts + two guests, both local and remote flags preserved.
        int guests=0;for(int i=0;i<4;++i){
            CNA_NetworkGamerHandle g=CNA_INVALID_HANDLE;ok(cna_network_session_get_gamer(session,CNA_NETWORK_SESSION_ROSTER_ALL,i,&g));
            CNA_Bool flag=CNA_FALSE;ok(cna_network_gamer_get_is_guest(g,&flag));guests+=flag==CNA_TRUE;ok(cna_network_gamer_destroy(g));
        }
        check(guests==2,"C roster has exactly two guests");
        for(auto roster:{CNA_NETWORK_SESSION_ROSTER_LOCAL,CNA_NETWORK_SESSION_ROSTER_REMOTE}){
            int32_t n=0;ok(cna_network_session_get_gamer_count(session,roster,&n));check(n==2,"C local/remote groups");
            int groupGuests=0;for(int i=0;i<2;++i){CNA_NetworkGamerHandle g=CNA_INVALID_HANDLE;ok(cna_network_session_get_gamer(session,roster,i,&g));CNA_Bool flag=CNA_FALSE;ok(cna_network_gamer_get_is_guest(g,&flag));groupGuests+=flag==CNA_TRUE;ok(cna_network_gamer_destroy(g));}
            check(groupGuests==1,"one C guest per machine");
        }
        std::cout<<"session-roster\n"<<std::flush;check(line()=="continue","C barrier");
        ok(cna_signed_in_gamer_destroy(locals[0]));ok(cna_signed_in_gamer_destroy(locals[1]));
        ok(cna_network_session_dispose(session));ok(cna_network_session_destroy(session));session=CNA_INVALID_HANDLE;
        std::cout<<"session-done guests-c\n"<<std::flush;return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';if(session!=CNA_INVALID_HANDLE)(void)cna_network_session_destroy(session);return 1;}
}
