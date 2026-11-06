// SPDX-License-Identifier: MS-PL
#pragma once
#include "OnlineQosProbe.hpp"
#include <array>
namespace CNA::Internal::Net {
class OnlineQosHost final {
public:
  /**
   * @brief Creates one authenticated host probe coordinator.
   * @param id Service-bound probe identity.
   * @param clock Monotonic microseconds.
   * @param send Bounded frame sender.
   */
  OnlineQosHost(QosProbeId id, OnlineQosProbe::Clock clock,
                OnlineQosProbe::Send send);
  /**
   * @brief Starts one bounded downstream train.
   * @param atUs Begin receipt timestamp.
   */
  void begin(std::int64_t atUs);
  /**
   * @brief Times accepted upstream arrivals and echoes unique pings.
   * @param bytes Complete CNQ frame.
   * @param receiptUs Worker receive timestamp.
   */
  void receive(std::span<const unsigned char> bytes, std::int64_t receiptUs);
  /**
   * @brief Reports partial loss once its observation window closes.
   */
  void tick();

private:
  void report();
  QosProbeId id_;
  OnlineQosProbe::Clock clock_;
  OnlineQosProbe::Send send_;
  std::int64_t started_ = 0, first_ = 0, last_ = 0;
  int lastSequence_ = -1;
  unsigned count_ = 0;
  bool begun_ = false, reported_ = false;
  std::array<bool, 4> pongs_{};
};
} // namespace CNA::Internal::Net
