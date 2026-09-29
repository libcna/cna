// SPDX-License-Identifier: MS-PL
#pragma once
#include <chrono>
#include <cstdint>
#include <optional>

namespace CNA::Internal::Net {
/** @brief Turns a transport's cumulative byte totals into the per-second rates of
 * NetworkSession.BytesPerSecondSent/Received. Totals are sampled at every update; the rates are
 * recomputed once at least a second has passed since the last recomputation. */
class TrafficRate {
public:
    /** @brief Records the transport's totals.
     * @param now Sample time. @param sent Cumulative bytes sent. @param received Cumulative bytes received.
     * @return Whether the rates were recomputed. */
    bool sample(std::chrono::steady_clock::time_point now,std::uint32_t sent,std::uint32_t received);
    /** @brief Gets the latest outbound rate. @return Bytes per second. */
    int sentPerSecond() const{return sentPerSecond_;}
    /** @brief Gets the latest inbound rate. @return Bytes per second. */
    int receivedPerSecond() const{return receivedPerSecond_;}
private:
    std::optional<std::chrono::steady_clock::time_point> start_;
    std::uint32_t sent_=0,received_=0;
    int sentPerSecond_=0,receivedPerSecond_=0;
};
}
