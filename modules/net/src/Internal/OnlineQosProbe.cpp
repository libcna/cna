// SPDX-License-Identifier: MS-PL
#include "OnlineQosProbe.hpp"
#include "CnaService/QosProtocol.hpp"
namespace CNA::Internal::Net {
OnlineQosProbe::OnlineQosProbe(QosProbeId id, std::uint64_t generation,
                               Clock clock, Send send)
    : id_(id), generation_(generation), clock_(std::move(clock)),
      send_(std::move(send)), measurement_(id, generation, 0) {}
void OnlineQosProbe::start() {
  if (started_)
    return;
  started_ = true;
  start_ = clock_();
  tick();
}
void OnlineQosProbe::sent(std::span<const unsigned char> bytes,
                          std::int64_t at) {
  const auto f = CnaService::parseQosFrame(bytes);
  if (f.probe == id_ && f.type == CnaService::QosType::Ping)
    measurement_.sentPing(f.sequence, f.nonce, at - start_);
}
void OnlineQosProbe::tick() {
  if (!started_ || measurement_.status() != OnlineQosStatus::Pending)
    return;
  const auto elapsed = clock_() - start_;
  if (elapsed < 3000000)
    while (nextPing_ < 4 && elapsed >= nextPing_ * 100000) {
      const auto index = nextPing_++;
      send_(CnaService::qosFrame(CnaService::QosType::Ping, id_, index,
                                 generation_ * 4 + index + 1));
      if (index == 0)
        for (unsigned i = 0; i < 8; ++i)
          send_(CnaService::qosFrame(CnaService::QosType::Up, id_, i));
    }
  measurement_.finish(elapsed);
}
void OnlineQosProbe::receive(std::span<const unsigned char> bytes,
                             std::int64_t at) {
  const auto f = CnaService::parseQosFrame(bytes);
  if (f.probe != id_)
    return;
  if (f.type == CnaService::QosType::Pong)
    measurement_.pong(id_, generation_, f.sequence, f.nonce, at - start_);
  else if (f.type == CnaService::QosType::Down)
    measurement_.downstream(f.sequence, at - start_);
  else if (f.type == CnaService::QosType::Report)
    measurement_.upstreamReport(static_cast<int>(f.acceptedCount),
                                static_cast<std::int64_t>(f.elapsedUs),
                                at - start_);
  else if (f.type == CnaService::QosType::Cancel)
    measurement_.cancel();
  else
    throw CnaService::QosError("QOS_ROLE");
}
void OnlineQosProbe::cancel() {
  if (measurement_.status() == OnlineQosStatus::Pending) {
    measurement_.cancel();
    send_(CnaService::qosFrame(CnaService::QosType::Cancel, id_));
  }
}
OnlineQosStatus OnlineQosProbe::status() const { return measurement_.status(); }
OnlineQosValues OnlineQosProbe::values() const { return measurement_.values(); }
} // namespace CNA::Internal::Net
