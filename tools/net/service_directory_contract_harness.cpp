// SPDX-License-Identifier: MS-PL
// Socket-free acceptance: real CNA backend + real Service over byte-only anonymous pipes.
// Oracle: approved online-guests-v1 contract and unchanged legacy directory/invitation schemas.
#include "../../modules/gamer-services/src/Internal/ServiceBackendTestTransport.hpp"
#include "../../modules/net/src/Internal/OnlineSessionPreparation.hpp"
#include "../../modules/gamer-services/src/Internal/Protocol/CnaService/Protocol.hpp"
#include "System/Environment.hpp"
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
#include <cstdio>
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <map>
#include <memory>
#include <cstdlib>

extern char** environ;
namespace GS=CNA::Internal::GamerServices;
namespace Net=CNA::Internal::Net;
using CnaService::Json;
namespace {
void check(bool value,const char* why){if(!value)throw std::runtime_error(why);}
template<class Work>void refused(Work work,const char* code) {
    try{work();throw std::runtime_error("missing expected refusal");}
    catch(const GS::ServiceOperationError& e){if(e.code!=code)throw std::runtime_error(std::string("expected ")+code+", received "+e.code);}
}
struct Fixture {
    std::string path;
    pid_t child=-1;FILE* input=nullptr;FILE* output=nullptr;
    std::string receive() {
        std::string line;for(int c;(c=fgetc(output))!=EOF;) {if(c=='\n')return line;line+=static_cast<char>(c);check(line.size()<2*1024*1024,"bounded fixture reply");}
        throw std::runtime_error("fixture child closed before reply");
    }
    std::string command(const std::string& action,const std::string& value) {
        const auto bytes=Json{{"action",action},{"value",value}}.dump()+"\n";
        check(fwrite(bytes.data(),1,bytes.size(),input)==bytes.size()&&fflush(input)==0,"fixture request pipe");
        const auto reply=CnaService::parse(receive());check(reply["ok"]==true,"fixture command failed");return reply["value"].get<std::string>();
    }
    std::shared_ptr<GS::IGamerServicesBackend> backend;
    std::map<std::string,std::string> user,tokens;
    std::vector<std::string> operations;
    bool legacy;
    unsigned request=0;
    Fixture(const std::string& executable,std::string file,bool old=false):path(std::move(file)),legacy(old) {
        clean();int toChild[2],fromChild[2];check(pipe(toChild)==0&&pipe(fromChild)==0,"anonymous fixture pipes");
        posix_spawn_file_actions_t actions;check(posix_spawn_file_actions_init(&actions)==0,"spawn actions");
        posix_spawn_file_actions_adddup2(&actions,toChild[0],0);posix_spawn_file_actions_adddup2(&actions,fromChild[1],1);
        for(int fd:{toChild[0],toChild[1],fromChild[0],fromChild[1]})posix_spawn_file_actions_addclose(&actions,fd);
        char* args[]{const_cast<char*>(executable.c_str()),path.data(),nullptr};
        const int error=posix_spawn(&child,executable.c_str(),&actions,nullptr,args,environ);posix_spawn_file_actions_destroy(&actions);
        close(toChild[0]);close(fromChild[1]);check(error==0,"spawn real Service fixture");
        input=fdopen(toChild[1],"w");output=fdopen(fromChild[0],"r");check(input&&output&&receive()=="ready","real Service fixture ready");
        backend=GS::makeServiceBackendWithTransportForTesting({"https://in-process.invalid/cna/v1","one"},[this](std::string_view bytes) {
            const auto input=CnaService::parse(bytes);const auto op=input.at("op").get<std::string>();operations.push_back(op);
            if(legacy)check(!op.ends_with("Guests")&&!op.starts_with("guests."),"no guest operation may reach a service without the capability");
            auto output=CnaService::parse(command("handle",std::string(bytes)));
            if(legacy&&op=="hello") {auto& caps=output["result"]["capabilities"];caps.erase(std::remove(caps.begin(),caps.end(),Json("online-guests-v1")),caps.end());}
            if(output["error"]=="OK"&&(op=="auth.login"||op=="auth.refresh")) {
                const auto& result=output["result"];const auto id=result["identity"]["userId"].get<std::string>();
                tokens[id]=result["token"].get<std::string>();
            }
            return output.dump();
        });
    }
    ~Fixture(){backend.reset();if(input)fclose(input);if(output)fclose(output);if(child>0){int status=0;(void)waitpid(child,&status,0);}clean();}
    void clean(){for(const auto* suffix:{"","-wal","-shm"})std::filesystem::remove(path+suffix);}
    std::vector<GS::BackendEvent> pump() {
        auto events=backend->pump();for(auto& event:events)if(event.completion)event.completion();return events;
    }
    void login(int slot,const std::string& name) {
        backend->signIn(slot,name,name+"-password");const auto events=pump();
        const auto found=std::find_if(events.begin(),events.end(),[&](const auto& e){return e.slot==slot&&e.type==GS::BackendEvent::Type::SignedIn;});
        check(found!=events.end(),"real in-process login failed");user[name]=found->identity.userId;
    }
    std::string guest(int slot,int parent) {
        backend->signInGuest(slot,"ignored caller label",parent);const auto events=pump();
        const auto found=std::find_if(events.begin(),events.end(),[&](const auto& e){return e.slot==slot&&e.type==GS::BackendEvent::Type::SignedIn;});
        check(found!=events.end()&&found->identity.userId.starts_with("g:"),"real in-process guest sign-in failed");return found->identity.userId;
    }
    Json raw(const std::string& op,Json args,const std::string& actor) {
        auto response=CnaService::parse(command("handle",Json{{"v",1},{"id","contract-raw-"+std::to_string(++request)},
            {"op",op},{"game","one"},{"token",tokens.at(actor)},{"args",std::move(args)}}.dump()));
        check(response["error"]=="OK","raw legacy fixture request failed");return response["result"];
    }
    long long count(const char* sql){return std::stoll(command("scalar",sql));}
    void expireAccess(){(void)command("exec","UPDATE sessions SET expires=0");}
    GS::IServiceSessionDirectory& directory(){return backend->sessionDirectory();}
};
GS::ServiceSessionSettings settings(){GS::ServiceSessionSettings s;s.maxGamers=6;s.privateSlots=2;s.allowHostMigration=true;return s;}

void hostPreparation(Fixture& f,GS::ServiceSessionKind kind) {
    f.login(0,"alice");f.login(1,"charlie");f.expireAccess();
    Net::OnlineSessionRequest request;request.owner=f.user.at("alice");request.users={request.owner,f.user.at("charlie")};request.kind=kind;request.settings=settings();
    int factory=0,callbacks=0;std::string session;
    Net::OnlinePreparationDependencies dependencies;
    dependencies.transport=[&](const auto&,auto ticket,const auto&) -> std::unique_ptr<Net::IPreparedOnlineTransport> {
        ++factory;session=ticket.session;throw GS::ServiceOperationError("TEST_TRANSPORT_FAILURE");
    };
    // No ENet host is created: the real control pipeline must reach this exact sentinel.
    Net::OnlineSessionPreparation preparation(f.backend,request,[&]{++callbacks;},dependencies);
    check(!preparation.complete(),"preparation begins queued");f.pump();check(preparation.complete()&&callbacks==1,"owner completion once");
    refused([&]{(void)preparation.take();},"TEST_TRANSPORT_FAILURE");check(factory==1,"factory reached after real authenticated admission and relay ticket");
    check(f.count("SELECT COUNT(*) FROM directory_sessions")==0&&f.count("SELECT COUNT(*) FROM directory_members")==0,"failed host preparation must roll back complete membership");
    refused([&]{(void)f.directory().get(request.owner,session);},"NOT_FOUND");
    // A second attempt must not find the old users occupied by failed cleanup.
    auto next=f.directory().create(request.owner,request.users,kind,request.settings);check(next.guestProtocol==0&&next.currentGamers==2,"same accounts can create again in legacy mode");
    // The expired owner credential causes one refused request and one refresh retry,
    // followed by the second create above: all three requests must be legacy.
    check(std::count(f.operations.begin(),f.operations.end(),"sessions.create")==3&&
        std::count(f.operations.begin(),f.operations.end(),"sessions.createGuests")==0,"both account-only attempts and refresh retry use legacy create");
    check(f.count("SELECT guest_protocol FROM directory_sessions")==0,"account-only session persists legacy mode");
    const auto ticket=f.directory().issueRelayTicket(request.owner,request.users,next.session);
    check(f.command("relay-redeem",ticket.ticket)=="true"&&f.command("relay-valid",ticket.ticket)=="true","real machine grant");
    check(f.directory().touch(request.owner,next.session).guestProtocol==next.guestProtocol,"ticket cannot downgrade mode before touch");
    check(f.directory().leave(request.owner,next.session),"host leave cleans up");
}
void invitations(Fixture& f,GS::ServiceSessionKind kind) {
    f.login(0,"alice");f.login(1,"charlie");f.login(2,"bob");f.login(3,"dana");
    const auto a=f.user.at("alice"),b=f.user.at("bob");const auto groupA=std::vector<std::string>{a,f.user.at("charlie")};
    const auto hosted=f.directory().create(a,groupA,kind,settings());const auto invite=f.directory().sendInvite(a,hosted.session,"bob");
    check(f.directory().get(a,hosted.session).guestProtocol==hosted.guestProtocol,"invitation cannot downgrade session mode");
    check(f.directory().getInvite(b,invite.invite).session==hosted.session,"invitation target");f.directory().acceptInvite(b,invite.invite);
    f.expireAccess();const auto joined=f.directory().join(b,{b,f.user.at("dana")},hosted.session,invite.invite);
    check(joined.currentGamers==4&&joined.guestProtocol==hosted.guestProtocol&&joined.openPrivateSlots==0,"invited account group consumes two private slots");
    auto changing=settings();changing.state=GS::ServiceSessionState::Playing;
    auto playing=f.directory().update(a,hosted.session,joined.revision,changing);check(playing.state==GS::ServiceSessionState::Playing,"host update after invitation and repaired credentials");
    changing.state=GS::ServiceSessionState::Lobby;f.directory().update(a,hosted.session,playing.revision,changing);
    const auto ticket=f.directory().issueRelayTicket(b,{b,f.user.at("dana")},hosted.session);check(f.command("relay-redeem",ticket.ticket)=="true","joined relay redeem");
    check(f.directory().get(b,hosted.session).currentGamers==4,"joined ticket retains typed mode");
    check(!f.directory().leave(b,hosted.session),"joiner leaves only its machine");check(f.directory().leave(a,hosted.session),"host leaves");
}
void guests(Fixture& f,GS::ServiceSessionKind kind) {
    f.login(0,"alice");f.login(1,"bob");const auto a=f.user.at("alice"),b=f.user.at("bob");
    const auto ga=f.guest(2,0),gb=f.guest(3,1);const auto hosted=f.directory().create(a,{a,ga},kind,settings());
    check(hosted.guestProtocol==1&&hosted.members[1].isGuest&&hosted.members[1].parentId==a,"service-issued typed guest roster");
    const auto ticket=f.directory().issueRelayTicket(a,{a,ga},hosted.session);check(f.command("relay-redeem",ticket.ticket)=="true","guest relay redeem");
    check(f.directory().get(a,hosted.session).guestProtocol==1,"guest ticket cannot downgrade mode");
    const auto responder=f.directory().issueQosHostTicket(a,{a,ga},hosted.session);check(f.command("qos-redeem",responder.ticket)=="true"&&f.command("qos-valid",responder.ticket)=="true","guest host QoS authority");
    f.expireAccess();const auto joined=f.directory().join(b,{b,gb},hosted.session);
    check(joined.currentGamers==4&&joined.members[3].isGuest&&joined.members[3].parentId==b,"guest join after parent access rotation");
    f.backend->signOutGuest(2);f.pump();check(f.command("relay-valid",ticket.ticket)=="false","closed guest revokes old full-group grant");
    auto current=f.directory().get(a,hosted.session);check(current.currentGamers==3&&current.hostId==a,"guest close retains parent");
    const auto fresh=f.guest(2,0);auto added=f.directory().addMembers(a,{fresh},hosted.session);check(added.currentGamers==4&&added.members.back().isGuest,"fresh guest add");
    f.directory().leave(b,hosted.session);f.directory().leave(a,hosted.session);
}
void legacySession(Fixture& f) {
    f.login(0,"alice");f.login(1,"charlie");f.login(2,"bob");f.login(3,"dana");const auto a=f.user.at("alice"),b=f.user.at("bob");
    Json wild=Json::array({nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr});
    const auto created=f.raw("sessions.create",{{"kind","player"},{"maxGamers",6},{"privateSlots",0},{"properties",wild},
        {"allowJoinInProgress",false},{"participants",Json::array({f.tokens.at(a),f.tokens.at(f.user.at("charlie"))})}},a);
    const auto id=created["session"].get<std::string>();check(!created.contains("guestProtocol"),"strict legacy snapshot has no typed fields");
    const auto page=f.directory().find(b,GS::ServiceSessionKind::PlayerMatch,2,{},0,32);check(page.sessions.size()==1&&page.sessions[0].guestProtocol==0,"new search keeps legacy listing mode");
    const auto joined=f.directory().join(b,{b,f.user.at("dana")},id);check(joined.guestProtocol==0&&joined.currentGamers==4,"new client joins old mode with account tokens");
    (void)f.directory().issueRelayTicket(b,{b,f.user.at("dana")},id);check(f.directory().touch(b,id).guestProtocol==0,"legacy ticket stays legacy");
    f.directory().leave(b,id);f.directory().leave(a,id);
}
}
int main(int argc,char** argv) {
    try {
        check(argc==3||argc==4,"service fixture executable + database prefix [case]");
        System::Environment::SetEnvironmentVariable("CNA_GAMER_SERVICES_CREDENTIALS_DIR","0");
        const auto selected=[&](const std::string& name){return argc==3||std::string(argv[3])==name;};
        int ran=0;
        for(const auto kind:{GS::ServiceSessionKind::PlayerMatch,GS::ServiceSessionKind::Ranked})for(bool old:{false,true}) {
            const auto suffix=std::string(kind==GS::ServiceSessionKind::Ranked?"ranked":"player")+(old?"-legacy":"-typed");
            for(const auto* scenario:{"preparation","invitations","guests"}) {
                if(old&&std::string(scenario)=="guests")continue;
                const auto name=std::string(scenario)+"-"+suffix;if(!selected(name))continue;
                Fixture f(argv[1],std::string(argv[2])+"-"+name+".sqlite3",old);
                if(std::string(scenario)=="preparation")hostPreparation(f,kind);else if(std::string(scenario)=="invitations")invitations(f,kind);else guests(f,kind);
                ++ran;std::cout<<name<<" passed\n";
            }
        }
        if(selected("legacy-session")){Fixture f(argv[1],std::string(argv[2])+"-legacy-session.sqlite3");legacySession(f);++ran;std::cout<<"legacy-session passed\n";}
        check(ran>0,"unknown case");std::cout<<ran<<" real client/service control cases passed without sockets\n";return 0;
    }catch(const GS::ServiceOperationError& e){std::cerr<<"directory/service contract code: "<<e.code<<'\n';return 1;}
    catch(const std::exception& e){std::cerr<<"directory/service contract: "<<e.what()<<'\n';return 1;}
}
