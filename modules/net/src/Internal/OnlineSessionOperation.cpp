// SPDX-License-Identifier: MS-PL
#include "OnlineSessionOperation.hpp"
#include "RelayEnetPolicy.hpp"
#include "System/InvalidOperationException.hpp"
#include <set>
#include <thread>

namespace CNA::Internal::Net {
struct OnlineSessionOperation::State {
    std::shared_ptr<GamerServices::IGamerServicesBackend> origin;
    std::unique_ptr<OnlineSessionPreparation> preparation;
    std::unique_ptr<ServiceENetSession> engine;
    std::vector<std::string> locals;
    ServiceENetDependencies dependencies;
    std::vector<ServiceENetObservation> observations;
    std::function<void()> completion;
    std::exception_ptr error;
    std::thread::id owner=std::this_thread::get_id();
    std::size_t dataCount=0,dataBytes=0;
    bool completed=false,notified=false,canceled=false,consumed=false,executing=false;

    void checkOwner()const {
        if(owner!=std::this_thread::get_id())throw System::InvalidOperationException("Online operations require their owner thread.");
    }
    void retain(ServiceENetObservation observation) {
        if(observation.data) {
            if(dataCount>=128||dataBytes+observation.data->Payload.size()>MaxRelayWaitingBytes)return;
            ++dataCount;dataBytes+=observation.data->Payload.size();
        }
        if(observations.size()>=256)throw GamerServices::ServiceOperationError("LIMIT_EXCEEDED");
        observations.push_back(std::move(observation));
    }
    void update() {
        checkOwner();if(canceled||consumed||executing||error)return;
        struct Guard {bool& flag;Guard(bool& value):flag(value){flag=true;}~Guard(){flag=false;}} guard(executing);
        // Unrelated completion callback failures propagate through the dispatcher, not this result.
        GamerServices::pumpRetainedCompletions(origin);
        try {
            if(preparation&&preparation->complete()) {
                engine=std::make_unique<ServiceENetSession>(preparation->take(),locals,std::move(dependencies));
                preparation.reset();
            }
            if(engine)for(auto& observation:engine->update()) {
                if(observation.type==ServiceENetObservation::Type::Failed)
                    throw GamerServices::ServiceOperationError(observation.failure);
                if(observation.type==ServiceENetObservation::Type::Ready)completed=true;
                retain(std::move(observation));
            }
        }catch(...) {
            error=std::current_exception();completed=true;preparation.reset();engine.reset();observations.clear();
        }
        if(completed&&!notified) {
            notified=true;auto callback=std::move(completion);
            if(callback)callback();
        }
    }
    void cancel() noexcept {
        if(canceled||consumed)return;canceled=true;completed=true;completion={};
        preparation.reset();engine.reset();observations.clear();
    }
};
OnlineSessionOperation::OnlineSessionOperation(std::shared_ptr<GamerServices::IGamerServicesBackend> origin,
    OnlineSessionRequest request,std::vector<std::string> localNames,std::function<void()> completion,
    OnlinePreparationDependencies preparation,ServiceENetDependencies realtime):state_(std::make_shared<State>()) {
    if(localNames.size()!=request.users.size()||localNames.empty()||localNames.size()>4
        ||std::set<std::string>(localNames.begin(),localNames.end()).size()!=localNames.size())
        throw GamerServices::ServiceOperationError("INVALID_ARGUMENT");
    state_->origin=std::move(origin);state_->locals=std::move(localNames);state_->dependencies=std::move(realtime);
    state_->completion=std::move(completion);
    state_->preparation=std::make_unique<OnlineSessionPreparation>(state_->origin,std::move(request),std::function<void()>{},std::move(preparation));
    std::weak_ptr<State> weak=state_;
    subscription_=std::make_unique<GamerServices::ServiceUpdateSubscription>([weak]{if(auto state=weak.lock())state->update();});
}
OnlineSessionOperation::~OnlineSessionOperation(){cancel();}
void OnlineSessionOperation::update(){auto state=state_;state->update();}
bool OnlineSessionOperation::complete()const{state_->checkOwner();return state_->completed;}
EstablishedOnlineSession OnlineSessionOperation::take() {
    auto state=state_;state->checkOwner();
    if(!state->completed||state->consumed)throw System::InvalidOperationException("Online operation cannot be consumed.");
    state->consumed=true;subscription_->cancel();
    if(state->canceled)throw GamerServices::ServiceOperationError("OPERATION_CANCELED");
    if(state->error)std::rethrow_exception(state->error);
    if(!state->engine||!state->engine->ready())throw GamerServices::ServiceOperationError("INVALID_RESPONSE");
    return {std::move(state->engine),std::move(state->observations)};
}
void OnlineSessionOperation::cancel()noexcept{if(subscription_)subscription_->cancel();if(state_)state_->cancel();}
}
