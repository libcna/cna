// SPDX-License-Identifier: MS-PL
#include "OnlineQosMeasurement.hpp"
#include <algorithm>
#include <limits>
namespace CNA::Internal::Net {
namespace {
int rate(unsigned count, std::int64_t span) {
  if (count < 4 || span <= 0)
    return 0;
  // CNQ v1: delivered frames after the baseline, 1200 application bytes each.
  const auto value = static_cast<std::uint64_t>(count - 1) * 1200 * 1000000 /
                     static_cast<std::uint64_t>(span);
  return static_cast<int>(std::min(
      value, static_cast<std::uint64_t>(std::numeric_limits<int>::max())));
}
} // namespace
OnlineQosMeasurement::OnlineQosMeasurement(QosProbeId probe,
                                           std::uint64_t generation,
                                           std::int64_t startUs)
    : probe_(probe), generation_(generation), start_(startUs),
      deadline_(startUs + 3000000) {}
bool OnlineQosMeasurement::sentPing(unsigned sequence, std::uint64_t nonce,
                                    std::int64_t atUs) {
  if (status_ != OnlineQosStatus::Pending || sequence >= 4 || !nonce ||
      pings_[sequence] || atUs < start_ || atUs >= deadline_)
    return false;
  for (const auto &ping : pings_)
    if (ping && ping->nonce == nonce)
      return false;
  pings_[sequence] = Ping{nonce, atUs, {}};
  return true;
}
bool OnlineQosMeasurement::pong(QosProbeId probe, std::uint64_t generation,
                                unsigned sequence, std::uint64_t nonce,
                                std::int64_t atUs) {
  if (status_ != OnlineQosStatus::Pending || probe != probe_ ||
      generation != generation_ || sequence >= 4 || atUs >= deadline_)
    return false;
  auto &ping = pings_[sequence];
  if (!ping || ping->rtt || ping->nonce != nonce || atUs < ping->sent)
    return false;
  ping->rtt = atUs - ping->sent;
  ++received_;
  return true;
}
bool OnlineQosMeasurement::downstream(unsigned sequence, std::int64_t atUs) {
  if (status_ != OnlineQosStatus::Pending || sequence >= 8 ||
      static_cast<int>(sequence) <= lastSequence_ || atUs < start_ ||
      atUs >= deadline_ || (downCount_ && atUs < lastDown_))
    return false;
  lastSequence_ = static_cast<int>(sequence);
  if (!downCount_)
    firstDown_ = atUs;
  lastDown_ = atUs;
  ++downCount_;
  return true;
}
bool OnlineQosMeasurement::upstreamReport(int count, std::int64_t elapsedUs,
                                          std::int64_t atUs) {
  if (status_ != OnlineQosStatus::Pending || atUs < start_ ||
      atUs >= deadline_ || upstream_ || count < 0 || count > 8 ||
      elapsedUs < 0 || elapsedUs > 3000000 || (count < 2 && elapsedUs != 0) ||
      (count >= 2 && elapsedUs == 0))
    return false;
  upstream_ = rate(static_cast<unsigned>(count), elapsedUs);
  return true;
}
void OnlineQosMeasurement::finish(std::int64_t atUs) {
  if (status_ != OnlineQosStatus::Pending)
    return;
  if (atUs < deadline_ && (received_ != 4 || downCount_ != 8 || !upstream_))
    return;
  const int down = rate(downCount_, lastDown_ - firstDown_);
  if (received_ < 3 || !upstream_ || !*upstream_ || !down) {
    status_ = OnlineQosStatus::Failed;
    return;
  }
  std::int64_t sum = 0, minimum = 3000000;
  for (const auto &ping : pings_)
    if (ping && ping->rtt) {
      sum += *ping->rtt;
      minimum = std::min(minimum, *ping->rtt);
    }
  // CNA policy: round integer milliseconds only after averaging microseconds.
  values_ = {*upstream_, down,
             static_cast<int>((sum + received_ * 500) / (received_ * 1000)),
             static_cast<int>((minimum + 500) / 1000)};
  status_ = OnlineQosStatus::Available;
}
void OnlineQosMeasurement::cancel() {
  if (status_ == OnlineQosStatus::Pending)
    status_ = OnlineQosStatus::Canceled;
}
OnlineQosStatus OnlineQosMeasurement::status() const { return status_; }
OnlineQosValues OnlineQosMeasurement::values() const { return values_; }
} // namespace CNA::Internal::Net
