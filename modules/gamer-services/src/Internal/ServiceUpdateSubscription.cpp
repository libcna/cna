// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/GamerServices/ServiceUpdateSubscription.hpp"
#include "System/ArgumentNullException.hpp"
#include "System/InvalidOperationException.hpp"
#include <exception>
#include <thread>

namespace CNA::Internal::GamerServices {
namespace {
struct Frame {std::exception_ptr error;};
struct Registry {
    System::EventHandler<System::EventArgs> event;
    std::thread::id owner=std::this_thread::get_id();
    Frame* current=nullptr;
    void checkOwner() const {
        if(owner!=std::this_thread::get_id())throw System::InvalidOperationException("Service updates require their owner thread.");
    }
};
Registry& registry(){static Registry value;return value;}
}
struct ServiceUpdateSubscription::State {
    std::function<void()> callback;
    bool active=true,executing=false;
};
ServiceUpdateSubscription::ServiceUpdateSubscription(std::function<void()> callback) {
    auto& updates=registry();updates.checkOwner();
    if(!callback)throw System::ArgumentNullException("callback");
    state_=std::make_shared<State>();state_->callback=std::move(callback);
    const std::weak_ptr<State> weak=state_;
    token_=updates.event.Add([weak](System::Object*,const System::EventArgs&) {
        auto state=weak.lock();if(!state || !state->active || state->executing)return;
        struct Guard {State& state;Guard(State& value):state(value){state.executing=true;}~Guard(){state.executing=false;}} guard(*state);
        try {auto callback=state->callback;callback();}
        catch(...) {auto& frame=*registry().current;if(!frame.error)frame.error=std::current_exception();}
    });
}
ServiceUpdateSubscription::~ServiceUpdateSubscription(){cancel();}
void ServiceUpdateSubscription::cancel() noexcept {
    if(!state_ || !state_->active)return;
    state_->active=false;state_->callback={};registry().event.Remove(token_);state_.reset();
}
void dispatchServiceUpdates() {
    auto& updates=registry();updates.checkOwner();Frame frame;
    struct Guard {
        Registry& updates;Frame* previous;
        Guard(Registry& value,Frame& next):updates(value),previous(value.current){updates.current=&next;}
        ~Guard(){updates.current=previous;}
    } guard(updates,frame);
    updates.event.Raise(nullptr,System::EventArgs::Empty);
    if(frame.error)std::rethrow_exception(frame.error);
}
}
