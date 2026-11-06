// SPDX-License-Identifier: MS-PL
#include "OnlineQosResult.hpp"
#include <utility>

namespace CNA::Internal::Net {
using Microsoft::Xna::Framework::Net::QualityOfService;
OnlineQosResult::OnlineQosResult(std::function<void()> stop)
    : stop_(std::move(stop)) {}
OnlineQosResult::~OnlineQosResult() { cancel(); }
void OnlineQosResult::attach(
    Microsoft::Xna::Framework::Net::AvailableNetworkSession &listing,
    std::shared_ptr<OnlineQosResult> result) {
  listing.onlineQos_ = std::move(result);
}
bool OnlineQosResult::complete(int upstream, int downstream, int averageMs,
                               int minimumMs) {
  std::lock_guard lock(mutex_);
  if (canceled_ || ready_ || upstream <= 0 || downstream <= 0 ||
      minimumMs < 0 || averageMs < minimumMs)
    return false;
  // Refresh QualityOfService::.ctor(int,int,TimeSpan,TimeSpan) IL_0006-0025:
  // separate directional rates and average/minimum, available only on completed
  // measurement.
  auto value = QualityOfService::CreateInternal(
      System::TimeSpan::FromMilliseconds(averageMs), downstream);
  value.bytesPerSecondUpstream_ = upstream;
  value.minimumRoundtripTime_ = System::TimeSpan::FromMilliseconds(minimumMs);
  ready_.emplace(std::move(value));
  return true;
}
const QualityOfService *OnlineQosResult::poll(bool parentLive) {
  std::lock_guard lock(mutex_);
  // Refresh listing getter IL_000b-0023 and replacement IL_00cb-00d0.
  if (parentLive && !canceled_ && ready_)
    adopted_ = true;
  return adopted_ ? &*ready_ : nullptr;
}
void OnlineQosResult::cancel() noexcept {
  std::function<void()> stop;
  {
    std::lock_guard lock(mutex_);
    if (canceled_)
      return;
    canceled_ = true;
    stop = std::move(stop_);
  }
  // Stop must not synchronously wait for network completion or reenter a held
  // result lock.
  if (stop) {
    try {
      stop();
    } catch (...) {
    }
  }
}
} // namespace CNA::Internal::Net
