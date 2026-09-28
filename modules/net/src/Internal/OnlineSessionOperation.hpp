// SPDX-License-Identifier: MS-PL
#pragma once
#include "ServiceENetSession.hpp"
#include "CNA/Internal/GamerServices/ServiceUpdateSubscription.hpp"

namespace CNA::Internal::Net {
/** @brief Consumed realtime resources and initial owned observations for the XNA owner. */
struct EstablishedOnlineSession {
    /** @brief Independent authenticated realtime owner. */
    std::unique_ptr<ServiceENetSession> engine;
    /** @brief Bounded observations accumulated before End claims the operation. */
    std::vector<ServiceENetObservation> observations;
};
/** @brief Coordinates service preparation and verified ENet readiness at the owner update boundary. */
class OnlineSessionOperation {
public:
    /** @brief Queues logical membership acquisition and registers weak owner-thread progress.
     * @param origin Retained service executor. @param request Owned logical acquisition request.
     * @param localNames Frozen ordered names matching the requested local accounts.
     * @param completion Once-only owner callback after readiness or deferred failure.
     * @param preparation Private preparation fixtures, empty for secure native operation.
     * @param realtime Private route/time fixtures, empty for secure native operation. */
    OnlineSessionOperation(std::shared_ptr<GamerServices::IGamerServicesBackend> origin,
        OnlineSessionRequest request,std::vector<std::string> localNames,std::function<void()> completion={},
        OnlinePreparationDependencies preparation={},ServiceENetDependencies realtime={});
    /** @brief Cancels future callbacks and releases all unconsumed membership/transport. */
    ~OnlineSessionOperation();
    /** @brief Prevents duplicated ownership. */
    OnlineSessionOperation(const OnlineSessionOperation&)=delete;
    /** @brief Prevents duplicated ownership. */
    OnlineSessionOperation& operator=(const OnlineSessionOperation&)=delete;
    /** @brief Progresses bounded preparation/realtime work on the owner, including retained origins. */
    void update();
    /** @brief Gets readiness/failure completion as published on the owner. @return Completed flag. */
    bool complete() const;
    /** @brief Claims resources once or rethrows the deferred error.
     * @return Independent realtime owner plus initial observations. */
    EstablishedOnlineSession take();
    /** @brief Cancels future notification and releases unconsumed resources at most once. */
    void cancel() noexcept;
private:
    struct State;
    std::shared_ptr<State> state_;
    std::unique_ptr<GamerServices::ServiceUpdateSubscription> subscription_;
};
}
