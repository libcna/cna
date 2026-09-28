// SPDX-License-Identifier: MS-PL
#pragma once
#include "CNA/Internal/GamerServices/IGamerServicesBackend.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GamerServicesDispatcher.hpp"
#include "System/IAsyncResult.hpp"
#include "System/AsyncCallback.hpp"
#include "System/ArgumentException.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/TimeoutException.hpp"
#include "System/Threading/EventWaitHandle.hpp"
#include <atomic>
#include <chrono>
#include <thread>

namespace CNA::Internal::GamerServices {
/** @brief Caller-owned async operation whose completion is published only during Update. */
class ServiceAsyncResult final : public System::IAsyncResult {
public:
    /** @brief Shared operation storage, independent of caller result lifetime. */
    struct State {
        /** @brief Logical result. */
        std::any value;
        /** @brief Deferred operation exception. */
        std::exception_ptr error;
        /** @brief Completion published at Update. */
        std::atomic<bool> complete=false;
        /** @brief Callback target on dispatcher thread. */
        ServiceAsyncResult* target=nullptr;
        /** @brief Completion signal. */
        mutable System::Threading::EventWaitHandle wait{false,System::Threading::EventResetMode::ManualReset};
    };
    /** @brief Constructs a pending operation. @param operation Operation family. @param owner Object identity.
     * @param callback Completion callback. @param userState Caller state. @param executor Owning backend. */
    ServiceAsyncResult(std::string operation,const void* owner,System::AsyncCallback callback,std::any userState,std::shared_ptr<IGamerServicesBackend> executor)
        : operation_(std::move(operation)),owner_(owner),callback_(std::move(callback)),userState_(std::move(userState)),executor_(std::move(executor)),state_(std::make_shared<State>()) {state_->target=this;}
    /** @brief Cancels callback delivery if the caller releases the pending result. */
    ~ServiceAsyncResult() override {state_->target=nullptr;}
    /** @brief Gets published completion. @return Completion flag. */
    bool getIsCompletedProperty() const override{return state_->complete.load();}
    /** @brief Gets whether completion occurred during Begin. @return False for queued work. */
    bool getCompletedSynchronouslyProperty() const override{return false;}
    /** @brief Gets caller state. @return State reference. */
    const std::any& getAsyncStateProperty() const override{return userState_;}
    /** @brief Gets completion signal. @return Initially unsignaled handle. */
    System::Threading::WaitHandle& getAsyncWaitHandleProperty() const override{return state_->wait;}
    /** @brief Queues an operation. @param operation Family. @param owner Object. @param work Logical work.
     * @param callback Update-thread callback. @param userState Caller state. @param executor Selected backend.
     * @return Caller-owned result. */
    static System::IAsyncResult* begin(std::string operation,const void* owner,std::function<std::any(IGamerServicesBackend&)> work,System::AsyncCallback callback,std::any userState,
        std::shared_ptr<IGamerServicesBackend> executor=backend()) {
        auto result=std::make_unique<ServiceAsyncResult>(std::move(operation),owner,std::move(callback),std::move(userState),std::move(executor));
        const auto state=result->state_;
        result->scheduler_=backend();
        auto* worker=result->executor_.get();
        const bool different=result->executor_.get()!=result->scheduler_.get();
        const std::weak_ptr<IGamerServicesBackend> origin=result->executor_;
        // The result retains the backend; its queued job must not retain its own executor.
        result->scheduler_->submit([state,worker,different,origin,work=std::move(work)] {
            try {
                // Paging may retain an older title context; that is a different executor, so
                // a temporary origin lease cannot retain the job's own scheduling backend.
                auto lease=different ? origin.lock() : std::shared_ptr<IGamerServicesBackend>{};
                if(different && !lease) throw System::InvalidOperationException("Origin service backend was released.");
                state->value=work(different ? *lease : *worker);
            } catch(...) {state->error=std::current_exception();}
        },[state] {
            state->complete=true;state->wait.Set();
            auto* target=state->target;
            if(target&&target->callback_) {auto callback=target->callback_;callback(*target);}
        });
        return result.release();
    }
    /** @brief Gets retained backend ownership for a materialized reader. @return Origin backend. */
    std::shared_ptr<IGamerServicesBackend> executor() const {return executor_;}
    /** @brief Validates and consumes an operation. @param result Begin result. @param operation Family.
     * @param owner Origin object. @return Logical result, or rethrows its exception. */
    static std::any end(System::IAsyncResult* result,const std::string& operation,const void* owner) {
        auto* action=dynamic_cast<ServiceAsyncResult*>(result);
        if(!action||action->operation_!=operation||action->owner_!=owner)throw System::ArgumentException("Invalid asynchronous result.","result");
        if(action->ended_)throw System::InvalidOperationException("End was already called.");
        // Claim before pumping; the callback can reenter End while this call waits.
        action->ended_=true;
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(30);
        while(!action->getIsCompletedProperty()) {
            Microsoft::Xna::Framework::GamerServices::GamerServicesDispatcher::Update();
            pumpRetainedCompletions(action->scheduler_);
            if(std::chrono::steady_clock::now()>=deadline)throw System::TimeoutException("CNA service completion timed out.");
            if(!action->getIsCompletedProperty())std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        if(action->state_->error)std::rethrow_exception(action->state_->error);
        return std::move(action->state_->value);
    }
private:
    std::string operation_;
    const void* owner_;
    System::AsyncCallback callback_;
    std::any userState_;
    std::shared_ptr<IGamerServicesBackend> executor_;
    std::shared_ptr<IGamerServicesBackend> scheduler_;
    std::shared_ptr<State> state_;
    bool ended_=false;
};
}
