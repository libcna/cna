// SPDX-License-Identifier: MS-PL
#pragma once
#include "System/EventHandler.hpp"
#include "System/EventArgs.hpp"
#include <functional>
#include <memory>

namespace CNA::Internal::GamerServices {
/** @brief Owned private update observer; construction, dispatch and cancellation share one thread. */
class ServiceUpdateSubscription {
public:
    /**
     * @brief Registers work at the dispatcher boundary after ready backend events.
     *
     * Callbacks must borrow or weakly capture their owning controller. Nested updates skip the
     * observer already executing, while allowing other pending operations to progress.
     * @param callback Owner-thread progress work, never a background callback.
     */
    explicit ServiceUpdateSubscription(std::function<void()> callback);
    /** @brief Cancels future delivery, including snapshots already being dispatched. */
    ~ServiceUpdateSubscription();
    /** @brief Prevents duplicated registration ownership. */
    ServiceUpdateSubscription(const ServiceUpdateSubscription&)=delete;
    /** @brief Prevents duplicated registration ownership. */
    ServiceUpdateSubscription& operator=(const ServiceUpdateSubscription&)=delete;
    /** @brief Cancels delivery at most once on the owner thread. */
    void cancel() noexcept;
private:
    struct State;
    std::shared_ptr<State> state_;
    System::EventHandler<System::EventArgs>::Token token_{};
};
/** @brief Runs private observers on the owner thread and rethrows the first error after the batch. */
void dispatchServiceUpdates();
}
