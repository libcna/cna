// SPDX-License-Identifier: MS-PL
#include "ServiceSessionPump.hpp"
#include "ServiceRoster.hpp"
#include "CnaService/Protocol.hpp"
#include "System/InvalidOperationException.hpp"
#include <algorithm>
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
bool sameSettings(const ServiceSessionSettings& left,const ServiceSessionSettings& right) {
    return left.maxGamers==right.maxGamers&&left.privateSlots==right.privateSlots&&left.state==right.state
        &&left.allowJoinInProgress==right.allowJoinInProgress&&left.allowHostMigration==right.allowHostMigration
        &&left.properties==right.properties;
}
constexpr auto RequestSpacing=std::chrono::milliseconds(100);
std::string safeFailure(const std::string& code) {
    for(const char* allowed:{"NOT_FOUND","NOT_AUTHORIZED","UNAUTHENTICATED","INVALID_RESPONSE","INVALID_STATE",
        "CONFLICT","NOT_SUPPORTED","LIMIT_EXCEEDED","RATE_LIMITED","REMOVED_BY_HOST"})if(code==allowed)return allowed;
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
    nextRead_=clock_();nextRenew_=nextRead_+std::chrono::seconds(30);lastRequest_=nextRead_-std::chrono::seconds(1);
    revision_=initial.revision;
}
ServiceSessionPump::~ServiceSessionPump(){cancel();}
std::optional<ServiceSessionObservation> ServiceSessionPump::update() {
    if(canceled_)return {};
    std::optional<ServiceSessionObservation> result;
    {std::lock_guard lock(state_->mutex);result=std::move(state_->ready);state_->ready.reset();}
    if(result) {
        busy_=false;
        if(!result->failure.empty())stopped_=true;
        else {
            revision_=result->snapshot->revision;if(result->renewed)nextRenew_=clock_()+std::chrono::seconds(30);
            // A newer desire queued while this one was in flight must still be published.
            if(result->published&&desired_&&sending_&&sameSettings(*desired_,*sending_))desired_.reset();
            if(removing_)std::erase(removals_,*removing_);
        }
        sending_.reset();removing_.reset();
        if(again_){again_=false;nextRead_=std::min(nextRead_,lastRequest_+RequestSpacing);}
    }
    const auto now=clock_();
    if(stopped_||busy_)return result;
    const bool publishing=desired_.has_value()&&now>=lastRequest_+RequestSpacing;
    const bool removing=!publishing&&!removals_.empty()&&now>=lastRequest_+RequestSpacing;
    if(!publishing&&!removing&&now<nextRead_)return result;
    const bool renew=!publishing&&!removing&&now>=nextRenew_;auto state=state_;auto* executor=backend_.get();busy_=true;
    lastRequest_=now;nextRead_=now+std::chrono::seconds(1);
    if(publishing)sending_=desired_;
    if(removing)removing_=removals_.front();
    try {
        backend_->submit([state,executor,owner=owner_,initial=initial_,revision=revision_,renew,settings=sending_,machine=removing_] {
            {std::lock_guard lock(state->mutex);if(state->canceled)return;}
            ServiceSessionObservation observation;
            try {
                std::optional<ServiceSessionSnapshot> value;
                if(settings) {
                    // Membership changes advance the revision independently; re-read and retry a bounded number of times.
                    int expected=revision;
                    for(int attempt=0;!value;++attempt) {
                        try {value=executor->sessionDirectory().update(owner,initial.session,expected,*settings);}
                        catch(const ServiceOperationError& error) {
                            if(error.code!="CONFLICT"||attempt==2)throw;
                            expected=executor->sessionDirectory().get(owner,initial.session).revision;
                        }
                    }
                }else if(machine) {
                    // A machine that already left needs no removal; the read reports the departure.
                    try{value=executor->sessionDirectory().remove(owner,initial.session,*machine);}
                    catch(const ServiceOperationError& error) {
                        if(error.code!="NOT_FOUND")throw;
                        value=executor->sessionDirectory().get(owner,initial.session);
                    }
                }else value=renew?executor->sessionDirectory().touch(owner,initial.session)
                    :executor->sessionDirectory().get(owner,initial.session);
                validate(*value,initial,revision);observation.snapshot=std::move(*value);
                observation.renewed=renew||settings.has_value();observation.published=settings.has_value();
            }catch(const ServiceOperationError& error){observation.failure=safeFailure(error.code);}
            catch(...){observation.failure="SESSION_SERVICE_UNAVAILABLE";}
            std::lock_guard lock(state->mutex);if(!state->canceled)state->ready=std::move(observation);
        },{});
    }catch(...) {
        busy_=false;stopped_=true;sending_.reset();
        if(!result)result=ServiceSessionObservation{};
        result->snapshot.reset();result->renewed=false;result->published=false;result->failure="SESSION_SERVICE_UNAVAILABLE";
    }
    return result;
}
void ServiceSessionPump::retry() {
    if(canceled_||busy_)throw System::InvalidOperationException("Session observation cannot be retried.");
    stopped_=false;nextRead_=nextRenew_=clock_();
}
void ServiceSessionPump::publish(ServiceSessionSettings settings) {
    if(canceled_)throw System::InvalidOperationException("Session observation was canceled.");
    desired_=std::move(settings);
}
void ServiceSessionPump::remove(std::string machine) {
    if(canceled_)throw System::InvalidOperationException("Session observation was canceled.");
    if(std::find(removals_.begin(),removals_.end(),machine)==removals_.end())removals_.push_back(std::move(machine));
}
void ServiceSessionPump::expedite() {
    if(canceled_||stopped_)return;
    if(busy_){again_=true;return;}
    nextRead_=std::min(nextRead_,lastRequest_+RequestSpacing);
}
void ServiceSessionPump::cancel() noexcept {
    canceled_=true;std::lock_guard lock(state_->mutex);state_->canceled=true;state_->ready.reset();
}
}
