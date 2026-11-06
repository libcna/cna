// SPDX-License-Identifier: MS-PL
#pragma once
#include "Microsoft/Xna/Framework/Net/AvailableNetworkSession.hpp"
#include <functional>
#include <mutex>
#include <optional>

namespace CNA::Internal::Net {
// Shared by listing copies; workers hold weak_ptrs, never raw XNA objects.
class OnlineQosResult final {
public:
  /**
   * @brief Owns one immutable pending/ready publication.
   * @param stop Interruptible cancellation callback; no network wait.
   */
  explicit OnlineQosResult(std::function<void()> stop = {});
  /**
   * @brief Cancels work when the last listing/result owner releases it.
   */
  ~OnlineQosResult();
  /**
   * @brief Shares a result with an internal online listing.
   * @param listing Target listing.
   * @param result Shared immutable publication owner.
   */
  static void
  attach(Microsoft::Xna::Framework::Net::AvailableNetworkSession &listing,
         std::shared_ptr<OnlineQosResult> result);
  /**
   * @brief Publishes one validated complete measurement.
   * @param upstream Received upstream bytes per second.
   * @param downstream Received downstream bytes per second.
   * @param averageMs Mean RTT in milliseconds.
   * @param minimumMs Minimum RTT in milliseconds.
   * @return Whether publication was accepted.
   */
  bool complete(int upstream, int downstream, int averageMs, int minimumMs);
  /**
   * @brief Adopts ready data only for a live parent.
   * @param parentLive Whether the result collection is still usable.
   * @return Stable adopted snapshot, or null while unavailable.
   */
  const Microsoft::Xna::Framework::Net::QualityOfService *poll(bool parentLive);
  /**
   * @brief Cancels pending work without waiting for network completion.
   */
  void cancel() noexcept;

private:
  std::mutex mutex_;
  std::function<void()> stop_;
  std::optional<Microsoft::Xna::Framework::Net::QualityOfService> ready_;
  bool canceled_ = false, adopted_ = false;
};
} // namespace CNA::Internal::Net
