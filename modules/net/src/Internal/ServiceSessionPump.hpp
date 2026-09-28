// SPDX-License-Identifier: MS-PL
#pragma once
#include "CNA/Internal/GamerServices/IGamerServicesBackend.hpp"
#include <chrono>

namespace CNA::Internal::Net {
/** @brief Owned logical observation; XNA event conversion belongs to the session owner. */
struct ServiceSessionObservation {
    /** @brief Latest authenticated authority, absent on failure. */
    std::optional<GamerServices::ServiceSessionSnapshot> snapshot;
    /** @brief Safe stable failure code, never exception/transport text or credentials. */
    std::string failure;
    /** @brief Whether this successful request renewed the machine lease. */
    bool renewed=false;
    /** @brief Whether this snapshot is the result of a host settings publication. */
    bool published=false;
};
/** @brief Bounded asynchronous directory read/lease pump with owner-thread publication.
 * The owner drives Update plus GamerServicesDispatcher.Update; queued work contains logical
 * data only. Construction, cancellation, retry and observation consumption share one owner thread. */
class ServiceSessionPump {
public:
    /** @brief Monotonic clock point used for bounded scheduling. */
    using Time=std::chrono::steady_clock::time_point;
    /** @brief Captures origin and validated initial requesting-machine authority.
     * @param backend Retained origin. @param owner Authenticated machine owner.
     * @param initial Initial full roster. @param clock Explicit test clock, empty for steady_clock. */
    ServiceSessionPump(std::shared_ptr<GamerServices::IGamerServicesBackend> backend,std::string owner,
        const GamerServices::ServiceSessionSnapshot& initial,std::function<Time()> clock={});
    /** @brief Suppresses future publication; membership release belongs to the prepared lease. */
    ~ServiceSessionPump();
    /** @brief Prevents duplicated pump ownership. */
    ServiceSessionPump(const ServiceSessionPump&)=delete;
    /** @brief Prevents duplicated pump ownership. */
    ServiceSessionPump& operator=(const ServiceSessionPump&)=delete;
    /** @brief Consumes one completed observation and schedules at most one due read.
     * @return Owned observation or empty while pending/not due. */
    std::optional<ServiceSessionObservation> update();
    /** @brief Retries the same origin with an immediate lease renewal after failure. */
    void retry();
    /** @brief Queues host-owned settings; the latest desire replaces any unsent one.
     * A stale revision is re-read and retried a bounded number of times in the same worker job.
     * @param settings Complete desired host settings. */
    void publish(GamerServices::ServiceSessionSettings settings);
    /** @brief Schedules an authoritative read as soon as the current request completes,
     * spaced at least 100 ms from the previous request. */
    void expedite();
    /** @brief Cancels scheduling and suppresses queued/active publication. */
    void cancel() noexcept;
private:
    struct State;
    std::shared_ptr<GamerServices::IGamerServicesBackend> backend_;
    std::shared_ptr<State> state_;
    std::string owner_;
    GamerServices::ServiceSessionSnapshot initial_;
    std::function<Time()> clock_;
    Time nextRead_,nextRenew_,lastRequest_;
    std::optional<GamerServices::ServiceSessionSettings> desired_,sending_;
    bool busy_=false,stopped_=false,canceled_=false,again_=false;
    int revision_=0;
};
}
