// SPDX-License-Identifier: MS-PL
#include "ServiceSessionPump.hpp"
#include "ServiceRoster.hpp"
#include "CnaService/Protocol.hpp"
#include "System/InvalidOperationException.hpp"
#include <mutex>
#include <set>

namespace CNA::Internal::Net {
namespace {
using namespace GamerServices;
std::set<std::string> localUsers(const ServiceSessionSnapshot& value) {
    std::set<std::string> result;
    for(const auto& member:value.members)if(member.machine==value.machine)result.insert(member.userId);
    return result;
}
void validate(const ServiceSessionSnapshot& value,const ServiceSessionSnapshot& initial,int revision) {
    try {ServiceRoster authority(value);}catch(const CnaService::Error&){throw ServiceOperationError("INVALID_RESPONSE");}
    if(value.session!=initial.session||value.machine!=initial.machine||value.kind!=initial.kind
        ||value.revision<revision||localUsers(value)!=localUsers(initial))throw ServiceOperationError("INVALID_RESPONSE");
}
std::string safeFailure(const std::string& code) {
    for(const char* allowed:{"NOT_FOUND","NOT_AUTHORIZED","UNAUTHENTICATED","INVALID_RESPONSE","INVALID_STATE",
        "CONFLICT","NOT_SUPPORTED","LIMIT_EXCEEDED","RATE_LIMITED"})if(code==allowed)return allowed;
    return "SESSION_SERVICE_UNAVAILABLE";
}
}
struct ServiceSessionPump::State {
    std::mutex mutex;
    bool canceled=false;
    std::optional<ServiceSessionObservation> ready;
};
ServiceSessionPump::ServiceSessionPump(std::shared_ptr<IGamerServicesBackend> backend,std::string owner,
    const ServiceSessionSnapshot& initial,std::function<Time()> clock)
    :backend_(std::move(backend)),state_(std::make_shared<State>()),owner_(std::move(owner)),initial_(initial),clock_(std::move(clock)) {
    if(!backend_)throw ServiceOperationError("INVALID_ARGUMENT");
    validate(initial,initial,initial.revision);
    if(!localUsers(initial).contains(owner_))throw ServiceOperationError("INVALID_ARGUMENT");
    if(!clock_)clock_=[] {return std::chrono::steady_clock::now();};
    nextRead_=clock_();nextRenew_=nextRead_+std::chrono::seconds(30);revision_=initial.revision;
}
ServiceSessionPump::~ServiceSessionPump(){cancel();}
std::optional<ServiceSessionObservation> ServiceSessionPump::update() {
    if(canceled_)return {};
    std::optional<ServiceSessionObservation> result;
    {std::lock_guard lock(state_->mutex);result=std::move(state_->ready);state_->ready.reset();}
    if(result) {
        busy_=false;
        if(!result->failure.empty())stopped_=true;
        else {revision_=result->snapshot->revision;if(result->renewed)nextRenew_=clock_()+std::chrono::seconds(30);}
    }
    const auto now=clock_();
    if(stopped_||busy_||now<nextRead_)return result;
    const bool renew=now>=nextRenew_;auto state=state_;auto* executor=backend_.get();busy_=true;
    nextRead_=now+std::chrono::seconds(1);
    try {
        backend_->submit([state,executor,owner=owner_,initial=initial_,revision=revision_,renew] {
            {std::lock_guard lock(state->mutex);if(state->canceled)return;}
            ServiceSessionObservation observation;
            try {
                auto value=renew?executor->sessionDirectory().touch(owner,initial.session)
                    :executor->sessionDirectory().get(owner,initial.session);
                validate(value,initial,revision);observation.snapshot=std::move(value);observation.renewed=renew;
            }catch(const ServiceOperationError& error){observation.failure=safeFailure(error.code);}
            catch(...){observation.failure="SESSION_SERVICE_UNAVAILABLE";}
            std::lock_guard lock(state->mutex);if(!state->canceled)state->ready=std::move(observation);
        },{});
    }catch(...) {
        busy_=false;stopped_=true;
        if(!result)result=ServiceSessionObservation{};
        result->snapshot.reset();result->renewed=false;result->failure="SESSION_SERVICE_UNAVAILABLE";
    }
    return result;
}
void ServiceSessionPump::retry() {
    if(canceled_||busy_)throw System::InvalidOperationException("Session observation cannot be retried.");
    stopped_=false;nextRead_=nextRenew_=clock_();
}
void ServiceSessionPump::cancel() noexcept {
    canceled_=true;std::lock_guard lock(state_->mutex);state_->canceled=true;state_->ready.reset();
}
}
