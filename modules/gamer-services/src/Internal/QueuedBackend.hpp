// SPDX-License-Identifier: MS-PL
#pragma once
// The work queue every Gamer Services backend runs on: the production service client
// (GamerServicesBackend.cpp) and the deterministic test fixture (GamerServicesFakeBackend.cpp).
// Shared through this header so the fixture can live in its own translation unit and never be
// linked into a game that does not create it.
#include "CNA/Internal/GamerServices/IGamerServicesBackend.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GamerServicesNotAvailableException.hpp"
#include "System/ArgumentOutOfRangeException.hpp"
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace CNA::Internal::GamerServices {
/** @brief Rejects a player slot outside 0..3. @param slot Slot. */
inline void slotGuard(int slot) {if(slot<0||slot>3)throw System::ArgumentOutOfRangeException("slot");}

/** @brief A backend whose requests run in order on one worker (or, in a browser, at pump()). */
class QueuedBackend : public IGamerServicesBackend {
public:
    explicit QueuedBackend(bool background):background_(background) {
        if(background_)worker_=std::jthread([this](std::stop_token stop){
            while(!stop.stop_requested()) {
                std::function<BackendEvent()> job;
                {std::unique_lock lock(queueMutex_);condition_.wait(lock,[&]{return stop.stop_requested()||!jobs_.empty();});
                 if(stop.stop_requested())return;
                 job=std::move(jobs_.front());jobs_.pop_front();++active_;}
                auto event=execute(job);
                {std::lock_guard lock(queueMutex_);--active_;ready_.push_back(std::move(event));}
            }
        });
    }
    ~QueuedBackend() override {stop();}
    void stop() {if(worker_.joinable()){worker_.request_stop();condition_.notify_all();worker_.join();}}
    void submit(std::function<void()> work,std::function<void()> completion) override {
        queue([work=std::move(work),completion=std::move(completion)]() mutable {
            work();BackendEvent event;event.type=BackendEvent::Type::Completion;event.completion=std::move(completion);return event;
        });
    }
    void signInLocal(int slot,const std::string& gamertag) override {
        slotGuard(slot);
        BackendEvent event;event.type=BackendEvent::Type::SignedIn;event.slot=slot;event.signedInToLive=false;
        event.identity.gamertag=gamertag;event.identity.displayName=gamertag;
        ready(std::move(event));
    }
    void signInGuest(int slot,const std::string& gamertag,int host) override {
        slotGuard(slot);slotGuard(host);
        if(host==slot||gamertag.empty()||gamertag.size()>36)throw Microsoft::Xna::Framework::GamerServices::GamerServicesNotAvailableException("Invalid guest.");
        BackendEvent event;event.type=BackendEvent::Type::SignedIn;event.slot=slot;event.signedInToLive=true;event.guestOf=host;
        event.identity.gamertag=gamertag;event.identity.displayName=gamertag;
        ready(std::move(event));
    }
    void signOutGuest(int slot) override {
        slotGuard(slot);BackendEvent event;event.type=BackendEvent::Type::SignedOut;event.slot=slot;ready(std::move(event));
    }
    std::vector<BackendEvent> pump() override {
        if(!background_) {
            for(int i=0;i<32;++i) {
                std::function<BackendEvent()> job;
                {std::lock_guard lock(queueMutex_);if(jobs_.empty())break;job=std::move(jobs_.front());jobs_.pop_front();}
                auto event=execute(job);{std::lock_guard lock(queueMutex_);ready_.push_back(std::move(event));}
            }
        }
        std::lock_guard lock(queueMutex_);std::vector<BackendEvent> result;
        result.reserve(ready_.size());while(!ready_.empty()){result.push_back(std::move(ready_.front()));ready_.pop_front();}return result;
    }
protected:
    static BackendEvent execute(const std::function<BackendEvent()>& job) {
        try {return job();} catch(...) {
            const auto error=std::current_exception();BackendEvent event;event.type=BackendEvent::Type::Completion;
            event.completion=[error]{std::rethrow_exception(error);};return event;
        }
    }
    void queue(std::function<BackendEvent()> job) {
        std::lock_guard lock(queueMutex_);
        if(jobs_.size()+ready_.size()+active_>=128)throw Microsoft::Xna::Framework::GamerServices::GamerServicesNotAvailableException("CNA service queue limit exceeded.");
        jobs_.push_back(std::move(job));condition_.notify_one();
    }
    void ready(BackendEvent event) {std::lock_guard lock(queueMutex_);if(ready_.size()<128)ready_.push_back(std::move(event));}
private:
    bool background_;
    std::jthread worker_;
    std::mutex queueMutex_;
    std::condition_variable condition_;
    std::deque<std::function<BackendEvent()>> jobs_;
    std::deque<BackendEvent> ready_;
    std::size_t active_=0;
};
}
