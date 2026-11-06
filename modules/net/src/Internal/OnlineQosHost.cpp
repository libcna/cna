// SPDX-License-Identifier: MS-PL
#include "OnlineQosHost.hpp"
#include "CnaService/QosProtocol.hpp"
namespace CNA::Internal::Net {
OnlineQosHost::OnlineQosHost(QosProbeId id, OnlineQosProbe::Clock clock,
                             OnlineQosProbe::Send send)
    : id_(id), clock_(std::move(clock)), send_(std::move(send)) {}
void OnlineQosHost::begin(std::int64_t at) {
  if (begun_)
    return;
  begun_ = true;
  started_ = at;
  for (unsigned i = 0; i < 8; ++i)
    send_(CnaService::qosFrame(CnaService::QosType::Down, id_, i));
}
void OnlineQosHost::receive(std::span<const unsigned char> bytes,
                            std::int64_t at) {
  const auto f = CnaService::parseQosFrame(bytes);
  if (!begun_ || f.probe != id_ || at - started_ >= 3000000 || at < started_)
    return;
  if (f.type == CnaService::QosType::Ping) {
    if (pongs_[f.sequence])
      return;
    pongs_[f.sequence] = true;
    send_(CnaService::qosFrame(CnaService::QosType::Pong, id_, f.sequence,
                               f.nonce));
  } else if (f.type == CnaService::QosType::Up && !reported_ &&
             static_cast<int>(f.sequence) > lastSequence_) {
    lastSequence_ = static_cast<int>(f.sequence);
    if (count_ == 0)
      first_ = at;
    last_ = at;
    ++count_;
    if (count_ == 8)
      report();
  } else if (f.type != CnaService::QosType::Up)
    throw CnaService::QosError("QOS_ROLE");
}
void OnlineQosHost::report() {
  if (reported_)
    return;
  reported_ = true;
  const auto span = count_ >= 2 ? last_ - first_ : 0;
  // Same-timestamp arrivals provide no bandwidth evidence; never divide by
  // zero/invent a rate.
  send_(CnaService::qosFrame(CnaService::QosType::Report, id_, 0, 0,
                             span > 0 ? count_ : 0,
                             static_cast<std::uint64_t>(span)));
}
void OnlineQosHost::tick() {
  if (begun_ && !reported_ && clock_() - started_ >= 1000000)
    report();
}
} // namespace CNA::Internal::Net
