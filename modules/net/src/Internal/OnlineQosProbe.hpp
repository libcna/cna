// SPDX-License-Identifier: MS-PL
#pragma once
#include "OnlineQosMeasurement.hpp"
#include <functional>
#include <span>
#include <vector>
namespace CNA::Internal::Net {
class OnlineQosProbe final {
public:
  using Clock = std::function<std::int64_t()>;
  using Send = std::function<void(std::vector<unsigned char>)>;
  /**
   * @brief Creates the searcher protocol coordinator.
   * @param probe Bound probe identity.
   * @param generation Job generation.
   * @param clock Monotonic microsecond source.
   * @param send Bounded frame sender.
   */
  OnlineQosProbe(QosProbeId probe, std::uint64_t generation, Clock clock,
                 Send send);
  /**
   * @brief Schedules the first ping and upstream train once.
   */
  void start();
  /**
   * @brief Consumes validated role-specific receiver evidence.
   * @param bytes Complete CNQ frame.
   * @param receiptUs Worker receive timestamp.
   */
  void receive(std::span<const unsigned char> bytes, std::int64_t receiptUs);
  /**
   * @brief Records a ping after its complete transport write.
   * @param bytes Written CNQ frame.
   * @param sentUs Write completion timestamp.
   */
  void sent(std::span<const unsigned char> bytes, std::int64_t sentUs);
  /**
   * @brief Pumps bounded sends and the terminal deadline.
   */
  void tick();
  /**
   * @brief Cancels the job and offers a best-effort Cancel.
   */
  void cancel();
  /**
   * @brief Reads measurement state.
   * @return Pending or terminal state.
   */
  OnlineQosStatus status() const;
  /**
   * @brief Reads published numeric evidence.
   * @return All-zero values until available.
   */
  OnlineQosValues values() const;

private:
  QosProbeId id_;
  std::uint64_t generation_;
  Clock clock_;
  Send send_;
  OnlineQosMeasurement measurement_;
  std::int64_t start_ = 0;
  unsigned nextPing_ = 0;
  bool started_ = false;
};
} // namespace CNA::Internal::Net
