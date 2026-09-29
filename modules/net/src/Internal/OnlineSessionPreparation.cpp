// SPDX-License-Identifier: MS-PL
#include "OnlineSessionPreparation.hpp"
#include "ServiceRoster.hpp"
#include "CNA/Internal/GamerServices/BackendConfiguration.hpp"
#include "System/InvalidOperationException.hpp"
#include <algorithm>
#include <chrono>
#include <mutex>
#include <set>
#include <thread>

namespace CNA::Internal::Net {
namespace {
using namespace GamerServices;
class NativePreparedTransport final : public IPreparedOnlineTransport {
public:
    NativePreparedTransport(const CNA::GamerServices::Configuration& configuration,
        ServiceRelayTicket ticket,const std::vector<std::string>& machines)
        :host_(ENetHostHandle::CreateRelayHost()),relay_(configuration,std::move(ticket),host_.getBoundPortProperty(),machines) {}
    ENetHostHandle& host() override {return host_;}
    RelayTransport* relay() override {return &relay_;}
    RelayTransportStatus status() const override {return relay_.status();}
    void reconnect(ServiceRelayTicket ticket) override {relay_.reconnect(std::move(ticket));}
private:
    ENetHostHandle host_;
    RelayTransport relay_;
};
bool leave(IGamerServicesBackend& backend,const std::string& owner,const std::string& session) noexcept {
    try {(void)backend.sessionDirectory().leave(owner,session);return true;}catch(...){return false;}
}
bool opaque(const std::string& value) {
    return value.size()==32&&std::all_of(value.begin(),value.end(),[](unsigned char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f');});
}
void requestGuard(const OnlineSessionRequest& request) {
    if(request.users.empty()||request.users.size()>4||request.users.front()!=request.owner||request.owner.empty()
        ||request.owner.size()>64||(request.operation!=OnlineSessionRequest::Operation::Create&&request.operation!=OnlineSessionRequest::Operation::Join)
        ||(request.kind!=ServiceSessionKind::PlayerMatch&&request.kind!=ServiceSessionKind::Ranked))
        throw ServiceOperationError("INVALID_ARGUMENT");
    std::set<std::string> users;
    for(const auto& user:request.users)if(user.empty()||user.size()>64||!users.insert(user).second)
        throw ServiceOperationError("INVALID_ARGUMENT");
    if(request.operation==OnlineSessionRequest::Operation::Join
        ?(!opaque(request.session)||(!request.invite.empty()&&!opaque(request.invite)))
        :(!request.session.empty()||!request.invite.empty()))throw ServiceOperationError("INVALID_ARGUMENT");
}
void groupGuard(const OnlineSessionRequest& request,const ServiceSessionSnapshot& snapshot) {
    ServiceRoster authority(snapshot);
    if(snapshot.kind!=request.kind||(request.operation==OnlineSessionRequest::Operation::Join&&snapshot.session!=request.session)
        ||(request.operation==OnlineSessionRequest::Operation::Create
        &&(snapshot.hostId!=request.owner||snapshot.hostMachine!=snapshot.machine)))throw ServiceOperationError("INVALID_RESPONSE");
    std::set<std::string> group;
    for(const auto& member:snapshot.members)if(member.machine==snapshot.machine)group.insert(member.userId);
    if(group!=std::set<std::string>(request.users.begin(),request.users.end()))throw ServiceOperationError("INVALID_RESPONSE");
}
}
std::unique_ptr<IPreparedOnlineTransport> makeNativePreparedTransport(
    const CNA::GamerServices::Configuration& configuration,ServiceRelayTicket ticket,const std::vector<std::string>& machines) {
    return std::make_unique<NativePreparedTransport>(configuration,std::move(ticket),machines);
}
struct OnlineSessionPreparation::State {
    struct Value {ServiceSessionSnapshot snapshot;std::unique_ptr<IPreparedOnlineTransport> transport;};
    mutable std::mutex mutex;
    bool canceled=false,complete=false,consumed=false;
    std::string owner;
    std::exception_ptr error;
    std::unique_ptr<Value> value;
    std::function<void()> completion;
    bool canceledNow() const {std::lock_guard lock(mutex);return canceled;}
};
PreparedOnlineSession::PreparedOnlineSession(std::shared_ptr<IGamerServicesBackend> backend,
    std::string owner,ServiceSessionSnapshot snapshot,std::unique_ptr<IPreparedOnlineTransport> transport)
    :backend_(std::move(backend)),owner_(std::move(owner)),snapshot_(std::move(snapshot)),transport_(std::move(transport)) {}
PreparedOnlineSession::~PreparedOnlineSession(){(void)release();}
const ServiceSessionSnapshot& PreparedOnlineSession::snapshot() const{return snapshot_;}
const std::shared_ptr<IGamerServicesBackend>& PreparedOnlineSession::backend() const{return backend_;}
IPreparedOnlineTransport& PreparedOnlineSession::transport() {
    if(!transport_)throw System::InvalidOperationException("Prepared transport was released.");return *transport_;
}
bool PreparedOnlineSession::release() noexcept {
    if(released_)return releaseSucceeded_;released_=true;transport_.reset();
    releaseSucceeded_=leave(*backend_,owner_,snapshot_.session);return releaseSucceeded_;
}
OnlineSessionPreparation::OnlineSessionPreparation(std::shared_ptr<IGamerServicesBackend> backend,
    OnlineSessionRequest request,std::function<void()> completion,OnlinePreparationDependencies dependencies)
    :backend_(std::move(backend)),state_(std::make_shared<State>()) {
    requestGuard(request);if(!backend_)throw ServiceOperationError("INVALID_ARGUMENT");
    if(!dependencies.configuration)dependencies.configuration=configurationForBackend;
    if(!dependencies.transport)dependencies.transport=makeNativePreparedTransport;
    const auto configuration=dependencies.configuration(*backend_);
    state_->owner=request.owner;state_->completion=std::move(completion);auto state=state_;auto* executor=backend_.get();
    backend_->submit([state,executor,request=std::move(request),configuration,factory=std::move(dependencies.transport)] {
        std::string acquired;
        std::unique_ptr<State::Value> prepared;
        try {
            if(state->canceledNow())return;
            auto& directory=executor->sessionDirectory();
            auto snapshot=request.operation==OnlineSessionRequest::Operation::Create
                ?directory.create(request.owner,request.users,request.kind,request.settings)
                :directory.join(request.owner,request.users,request.session,request.invite);
            acquired=request.operation==OnlineSessionRequest::Operation::Join?request.session:snapshot.session;
            groupGuard(request,snapshot);
            if(!state->canceledNow()) {
                auto ticket=directory.issueRelayTicket(request.owner,request.users,snapshot.session);
                if(ticket.session!=snapshot.session||ticket.machine!=snapshot.machine)throw ServiceOperationError("INVALID_RESPONSE");
                if(!state->canceledNow()) {
                    prepared=std::make_unique<State::Value>();prepared->snapshot=std::move(snapshot);
                    prepared->transport=factory(configuration,std::move(ticket),ServiceRoster(prepared->snapshot).remoteMachines());
                    if(!prepared->transport||!prepared->transport->host().IsValid()
                        ||!prepared->transport->host().getBoundPortProperty())throw ServiceOperationError("RELAY_TRANSPORT_UNAVAILABLE");
                    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
                    while(!state->canceledNow()) {
                        const auto status=prepared->transport->status();
                        if(status.state==RelayTransportState::Ready)break;
                        if(status.state!=RelayTransportState::Connecting||std::chrono::steady_clock::now()>=deadline)
                            throw ServiceOperationError("RELAY_TRANSPORT_UNAVAILABLE");
                        std::this_thread::sleep_for(std::chrono::milliseconds(2));
                    }
                    std::lock_guard lock(state->mutex);
                    if(!state->canceled){state->value=std::move(prepared);acquired.clear();}
                }
            }
        }catch(...) {std::lock_guard lock(state->mutex);state->error=std::current_exception();}
        prepared.reset();if(!acquired.empty())(void)leave(*executor,request.owner,acquired);
    },[state] {
        std::function<void()> completion;
        {std::lock_guard lock(state->mutex);if(state->complete)return;state->complete=true;
            if(!state->canceled)completion=std::move(state->completion);}
        if(completion)completion();
    });
}
OnlineSessionPreparation::~OnlineSessionPreparation(){cancel();}
bool OnlineSessionPreparation::complete() const {std::lock_guard lock(state_->mutex);return state_->complete;}
std::unique_ptr<PreparedOnlineSession> OnlineSessionPreparation::take() {
    std::lock_guard lock(state_->mutex);
    if(!state_->complete||state_->consumed)throw System::InvalidOperationException("Preparation cannot be consumed.");
    state_->consumed=true;
    if(state_->canceled)throw ServiceOperationError("OPERATION_CANCELED");
    if(state_->error)std::rethrow_exception(state_->error);
    if(!state_->value)throw ServiceOperationError("INVALID_RESPONSE");
    auto& value=*state_->value;
    auto result=std::unique_ptr<PreparedOnlineSession>(new PreparedOnlineSession(backend_,
        state_->owner,std::move(value.snapshot),std::move(value.transport)));
    state_->value.reset();return result;
}
void OnlineSessionPreparation::cancel() noexcept {
    std::unique_ptr<State::Value> value;
    {std::lock_guard lock(state_->mutex);state_->canceled=true;state_->completion={};value=std::move(state_->value);}
    if(value){value->transport.reset();(void)leave(*backend_,state_->owner,value->snapshot.session);}
}
}
