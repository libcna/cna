// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/GamerServices/IGamerServicesBackend.hpp"
#include "../../modules/net/src/Internal/OnlineSessionPreparation.hpp"
#include "../../modules/net/src/Internal/ServiceSessionPump.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GamerServicesDispatcher.hpp"
#include "Microsoft/Xna/Framework/GamerServices/Gamer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamerCollection.hpp"
#include "Microsoft/Xna/Framework/GamerServices/Guide.hpp"
#include "Microsoft/Xna/Framework/Input/TextInputEXT.hpp"
#include "System/IServiceProvider.hpp"
#include "System/InvalidOperationException.hpp"
#include "Microsoft/Xna/Framework/Net/NetworkSession.hpp"
#include <chrono>
#include <iostream>
#include <memory>
#include <thread>
namespace Service=CNA::Internal::GamerServices;
using namespace Microsoft::Xna::Framework::GamerServices;
namespace {
int checks=0;
const char* phase="initial";
void check(bool value,const char* reason){++checks;if(!value)throw std::runtime_error(reason);}
template<class Work> void rejected(Work work,const char* code) {
    try{work();throw std::runtime_error("Expected control refusal");}
    catch(const Service::ServiceOperationError& error){check(error.code==code,"control refusal code");}
}
class Provider : public System::IServiceProvider {public:void* GetService(const std::type_info&)const override{return nullptr;}};
void enter(const std::string& value) {
    for(unsigned char character:value)Microsoft::Xna::Framework::Input::TextInputEXT::INTERNAL_OnTextInput(character);
    Microsoft::Xna::Framework::Input::TextInputEXT::INTERNAL_OnTextInput(u'\r');
}
void advance() {std::string line;check(static_cast<bool>(std::getline(std::cin,line))&&line=="continue","parent control boundary");}
}
int main(int argc,char** argv) {
    try {
        check(argc==3,"arguments");const std::string role=argv[1],kind=argv[2];
        check(role=="host"||role=="join"||role=="search-empty","role");check(kind=="player"||kind=="ranked","kind");
        const bool host=role=="host";
        const auto category=kind=="player"?Service::ServiceSessionKind::PlayerMatch:Service::ServiceSessionKind::Ranked;
        const std::array<std::string,2> accounts=host?std::array<std::string,2>{"alice","charlie"}:std::array<std::string,2>{"bob","dana"};
        Provider provider;GamerServicesDispatcher::Initialize(provider);auto* signedIn=Gamer::getSignedInGamersProperty();
        check(signedIn->getCountProperty()==0,"no fabricated identity");Guide::ShowSignIn(2,true);
        for(int index=0;index<2;++index) {
            std::string password;check(static_cast<bool>(std::getline(std::cin,password)),"credential input");enter(accounts[index]);enter(password);
            const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(15);
            while(signedIn->getCountProperty()!=index+1) {
                GamerServicesDispatcher::Update();if(std::chrono::steady_clock::now()>=deadline)throw std::runtime_error("sign-in timeout");
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
        check(!Guide::getIsVisibleProperty(),"Guide completion");auto backend=Service::backend();auto& directory=backend->sessionDirectory();
        std::vector<std::string> users;for(int index=0;index<2;++index)users.push_back(backend->profile((*signedIn)[index]->getGamertagProperty()).userId);
        Service::ServiceSessionSettings settings;settings.maxGamers=6;settings.privateSlots=2;settings.properties[0]=37;settings.properties[7]=-2147483647-1;
        std::cout<<"directory-signed\n"<<std::flush;advance();
        Microsoft::Xna::Framework::Net::NetworkSessionProperties search;
        for(int index=0;index<8;++index)search.setItem(index,settings.properties[index]);
        const auto sessionType=category==Service::ServiceSessionKind::Ranked
            ? Microsoft::Xna::Framework::Net::NetworkSessionType::Ranked
            : Microsoft::Xna::Framework::Net::NetworkSessionType::PlayerMatch;
        if(argc>1 && std::string(argv[1])=="search-empty") {
            phase="public-cross-title-find";
            auto found=Microsoft::Xna::Framework::Net::NetworkSession::Find(sessionType,2,search);
            check(found.getCountProperty()==0,"other title advertisements escaped service scope");
            std::cout<<"directory-done "<<checks<<" checks\n"<<std::flush;return 0;
        }
        namespace Transport=CNA::Internal::Net;
        std::string lastPreparedSession;
        const auto prepare=[&](Transport::OnlineSessionRequest request,bool fail=false) {
            Transport::OnlinePreparationDependencies dependencies;
            dependencies.transport=[&,fail](const auto& configuration,auto ticket,const auto& machines) -> std::unique_ptr<Transport::IPreparedOnlineTransport> {
                lastPreparedSession=ticket.session;
                if(fail)throw Service::ServiceOperationError("TEST_TRANSPORT_FAILURE");
                return Transport::makeNativePreparedTransport(configuration,std::move(ticket),machines);
            };
            int completions=0;const auto ownerThread=std::this_thread::get_id();
            auto pending=std::make_unique<Transport::OnlineSessionPreparation>(backend,std::move(request),[&] {
                check(std::this_thread::get_id()==ownerThread,"preparation callback owner thread");++completions;
            },std::move(dependencies));
            check(!pending->complete(),"preparation begins pending");
            const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(20);
            while(!pending->complete()) {
                GamerServicesDispatcher::Update();check(std::chrono::steady_clock::now()<deadline,"preparation deadline");
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
            check(completions==1,"preparation completion once");return pending;
        };
        Transport::OnlineSessionRequest request;request.owner=users[0];request.users=users;request.kind=category;request.settings=settings;
        if(host) {
            phase="failed-host-preparation";
            {auto pending=prepare(request,true);rejected([&]{(void)pending->take();},"TEST_TRANSPORT_FAILURE");}
            rejected([&]{(void)directory.get(users[0],lastPreparedSession);},"NOT_FOUND");
            phase="abandoned-host-preparation";
            {auto pending=prepare(request);}
            rejected([&]{(void)directory.get(users[0],lastPreparedSession);},"NOT_FOUND");
            phase="create";auto hostPreparation=prepare(request);auto hostLease=hostPreparation->take();
            auto session=hostLease->snapshot();check(session.currentGamers==2,"host local group");
            const auto authority=directory.issueRelayTicket(users[0],users,session.session);
            check(authority.machine==session.machine&&authority.session==session.session&&authority.ticket.size()==64,"host relay authority");
            phase="send-invite";auto invitation=directory.sendInvite(users[0],session.session,"Bob");
            check(invitation.invite==directory.sendInvite(users[0],session.session,"Bob").invite,"duplicate invite identity");
            rejected([&]{(void)directory.update(users[0],session.session,session.revision+1,settings);},"CONFLICT");
            if(category==Service::ServiceSessionKind::Ranked) {
                phase="ranked-policy";auto invalid=settings;invalid.allowJoinInProgress=true;
                rejected([&]{(void)directory.update(users[0],session.session,session.revision,invalid);},"INVALID_ARGUMENT");
                const auto unchanged=directory.get(users[0],session.session);
                check(unchanged.revision==session.revision&&!unchanged.allowJoinInProgress,"invalid ranked update mutated authority");
            }
            phase="host-update";settings.state=Service::ServiceSessionState::Playing;session=directory.update(users[0],session.session,session.revision,settings);
            if(category==Service::ServiceSessionKind::Ranked) {
                std::cout<<"directory-playing "<<session.session<<' '<<invitation.invite<<'\n'<<std::flush;advance();
                check(directory.get(users[0],session.session).currentGamers==2,"ranked admission mutated membership");
            }
            settings.state=Service::ServiceSessionState::Lobby;session=directory.update(users[0],session.session,session.revision,settings);
            if(category==Service::ServiceSessionKind::Ranked) {
                const auto previous=invitation.invite;invitation=directory.sendInvite(users[0],session.session,"Bob");
                check(invitation.invite!=previous&&invitation.state==Service::ServiceInvitationState::Pending,"new lobby invitation");
            }
            std::cout<<"directory-host "<<session.session<<' '<<invitation.invite<<'\n'<<std::flush;advance();
            Transport::ServiceSessionPump snapshots(backend,users[0],hostLease->snapshot());
            const auto observe=[&] {
                const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(15);
                const auto ownerThread=std::this_thread::get_id();
                for(;;) {
                    GamerServicesDispatcher::Update();auto observation=snapshots.update();
                    if(observation) {
                        if(!observation->failure.empty())throw Service::ServiceOperationError(observation->failure);
                        check(std::this_thread::get_id()==ownerThread&&observation->snapshot.has_value(),"snapshot owner publication");
                        check(observation->renewed,"explicit pump renewal");return *observation->snapshot;
                    }
                    check(std::chrono::steady_clock::now()<deadline,"snapshot pump deadline");
                    std::this_thread::sleep_for(std::chrono::milliseconds(2));
                }
            };
            phase="host-touch";snapshots.retry();session=observe();check(session.currentGamers==2,"remote group left after restart");
            check(directory.get(users[1],session.session).machine==session.machine,"secondary local roster authority");
            // Parent expires both access credentials again: the existing leaderboard path shares
            // the same participant refresh repair as directory creation/join.
            std::cout<<"directory-after-join\n"<<std::flush;advance();
            phase="expired-pump-renewal";snapshots.retry();session=observe();
            phase="leaderboard-begin";const auto gameplay=backend->beginLeaderboardGame(users);backend->abortLeaderboardGame(gameplay,users[0]);
            check(hostLease->release(),"prepared host close");
        }else {
            std::string session,invite;check(static_cast<bool>(std::getline(std::cin,session))&&static_cast<bool>(std::getline(std::cin,invite)),"control identifiers");
            phase="public-find";
            using Microsoft::Xna::Framework::Net::NetworkSession;
            using Microsoft::Xna::Framework::Net::AvailableNetworkSessionCollection;
            int callbacks=0;const auto updateThread=std::this_thread::get_id();
            std::unique_ptr<AvailableNetworkSessionCollection> publicFound;
            std::unique_ptr<System::IAsyncResult> pending(NetworkSession::BeginFind(sessionType,2,search,
                [&](System::IAsyncResult& result) {
                    check(std::this_thread::get_id()==updateThread,"search callback thread");
                    ++callbacks;publicFound=std::make_unique<AvailableNetworkSessionCollection>(NetworkSession::EndFind(&result));
                },37));
            check(!pending->getIsCompletedProperty()&&!pending->getCompletedSynchronouslyProperty(),"public search begins pending");
            check(!pending->getAsyncWaitHandleProperty().WaitOne(0),"search completion signal too early");
            const auto findDeadline=std::chrono::steady_clock::now()+std::chrono::seconds(15);
            while(!publicFound) {
                GamerServicesDispatcher::Update();
                check(std::chrono::steady_clock::now()<findDeadline,"public search deadline");
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            check(callbacks==1&&pending->getAsyncWaitHandleProperty().WaitOne(0),"search completion once");
            check(std::any_cast<int>(pending->getAsyncStateProperty())==37,"metadata survived callback End");
            check(publicFound->getCountProperty()==1,"public matched directory count");
            const auto& listing=static_cast<const AvailableNetworkSessionCollection&>(*publicFound)[0];
            check(listing.getHostGamertagProperty()=="Alice"&&listing.getCurrentGamerCountProperty()==2,"public service identity");
            check(listing.getOpenPublicGamerSlotsProperty()==2&&listing.getOpenPrivateGamerSlotsProperty()==2,"public service capacity");
            check(listing.getSessionPropertiesProperty().getItem(0)==37&&listing.getSessionPropertiesProperty().getItem(7)==-2147483647-1,"public sparse properties");
            check(listing.GetConnectAddress().empty()&&listing.GetConnectPort()==0,"relay listing has no direct peer endpoint");
            try {(void)NetworkSession::EndFind(pending.get());throw std::runtime_error("duplicate End accepted");}
            catch(const System::InvalidOperationException&) {++checks;}
            int retainedCallbacks=0;
            std::unique_ptr<System::IAsyncResult> retained(NetworkSession::BeginFind(sessionType,2,search,
                [&](System::IAsyncResult& result) {
                    ++retainedCallbacks;check(std::this_thread::get_id()==updateThread,"retained search callback thread");
                    check(result.getIsCompletedProperty(),"retained search completed before callback");
                },83));
            Service::setBackendForTesting(Service::makeFakeBackend({}));
            auto retainedFound=NetworkSession::EndFind(retained.get());
            Service::setBackendForTesting(backend);
            check(retainedFound.getCountProperty()==1&&retainedCallbacks==1,"pending search retains real TLS origin");
            check(std::any_cast<int>(retained->getAsyncStateProperty())==83,"retained search metadata");
            check(Gamer::getSignedInGamersProperty()->getCountProperty()==2,"replacement leaves identity events controlled");
            auto wrongFilter=search;wrongFilter[0]=38;
            check(NetworkSession::Find(sessionType,2,wrongFilter).getCountProperty()==0,"public filter mismatch");
            phase="find";const auto page=directory.find(users[0],category,2,settings.properties,0,32);
            check(page.sessions.size()==1&&page.sessions[0].session==session,"service filtering/correlation");
            auto mismatch=settings.properties;mismatch[0]=38;check(directory.find(users[0],category,2,mismatch,0,32).sessions.empty(),"filter mismatch");
            request.operation=Transport::OnlineSessionRequest::Operation::Join;request.session=session;
            phase="failed-join-preparation";
            {auto pending=prepare(request,true);rejected([&]{(void)pending->take();},"TEST_TRANSPORT_FAILURE");}
            check(directory.find(users[0],category,2,settings.properties,0,32).sessions.size()==1,"failed join retained membership");
            phase="ordinary-join";auto joinPreparation=prepare(request);auto joinLease=joinPreparation->take();
            auto joined=joinLease->snapshot();check(joined.currentGamers==4,"ordinary two-local join");
            const auto authority=directory.issueRelayTicket(users[0],users,session);
            check(authority.machine==joined.machine&&authority.session==session&&authority.ticket.size()==64,"remote relay authority");
            check(directory.get(users[1],session).machine==joined.machine,"secondary local membership read");
            rejected([&]{(void)directory.touch(users[1],session);},"NOT_AUTHORIZED");
            check(joinLease->release(),"prepared ordinary group leave");
            const auto inbox=directory.listInvites(users[0],0,32);check(inbox.invites.size()==1&&inbox.invites[0].invite==invite,"persisted recipient inbox");
            rejected([&]{(void)directory.getInvite(users[1],invite);},"NOT_AUTHORIZED");
            rejected([&]{(void)directory.join(users[0],users,session,invite);},"INVALID_STATE");
            const auto accepted=directory.acceptInvite(users[0],invite);check(accepted.state==Service::ServiceInvitationState::Accepted,"explicit consent");
            check(directory.acceptInvite(users[0],invite).acceptedAt==accepted.acceptedAt,"accept idempotence");
            phase="invited-join";request.invite=invite;auto invitedPreparation=prepare(request);auto invitedLease=invitedPreparation->take();
            joined=invitedLease->snapshot();check(joined.currentGamers==4&&joined.openPrivateSlots==0,"private invited local group");
            check(directory.join(users[0],users,session,invite).machine==joined.machine,"same live group retry");
            rejected([&]{(void)directory.join(users[0],{users[0]},session,invite);},"INVALID_STATE");
            check(invitedLease->release(),"prepared invited group leave");
            rejected([&]{(void)directory.join(users[0],users,session,invite);},"INVALID_STATE");
            check(directory.listInvites(users[0],0,32).invites.empty(),"consumed inbox");
            std::cout<<"directory-before-revoke\n"<<std::flush;advance();
            phase="secondary-revocation";
            rejected([&]{(void)directory.join(users[0],users,session);},"UNAUTHENTICATED");
            check(signedIn->getCountProperty()==2,"signout published before Update");
            GamerServicesDispatcher::Update();
            check(signedIn->getCountProperty()==1&&(*signedIn)[0]->getGamertagProperty()=="Bob","secondary revocation removed owner identity");
            check(directory.listInvites(users[0],0,32).invites.empty(),"owner authority lost on secondary revocation");
        }
        std::cout<<"directory-done "<<checks<<" checks\n"<<std::flush;return 0;
    }catch(const Service::ServiceOperationError& error){std::cerr<<"CNA directory control probe failed at "<<phase<<" code "<<error.code<<"\n";return 1;}
    catch(const std::exception&){std::cerr<<"CNA directory control probe failed at "<<phase<<"\n";return 1;}
}
