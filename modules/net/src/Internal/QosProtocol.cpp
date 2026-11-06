// SPDX-License-Identifier: MIT
#include "CnaService/QosProtocol.hpp"
#include <algorithm>
namespace CnaService {
QosError::QosError(std::string code)
    : std::runtime_error("CNA probe frame rejected"), code_(std::move(code)) {}
const std::string &QosError::code() const noexcept { return code_; }
namespace {
std::uint64_t read(std::span<const unsigned char> b, unsigned offset,
                   unsigned size) {
  std::uint64_t v = 0;
  for (unsigned i = 0; i < size; ++i)
    v = (v << 8) | b[offset + i];
  return v;
}
void write(std::span<unsigned char> b, unsigned offset, unsigned size,
           std::uint64_t v) {
  for (unsigned i = 0; i < size; ++i) {
    b[offset + size - i - 1] = static_cast<unsigned char>(v);
    v >>= 8;
  }
}
} // namespace
QosFrameView parseQosFrame(std::span<const unsigned char> b) {
  if (b.size() < 32)
    throw QosError("QOS_TRUNCATED");
  if (b.size() > 1200)
    throw QosError("QOS_TOO_LARGE");
  if (b[0] != 'C' || b[1] != 'N' || b[2] != 'Q')
    throw QosError("QOS_MAGIC");
  if (b[3] != 1)
    throw QosError("QOS_VERSION");
  if (b[4] < 1 || b[4] > 7)
    throw QosError("QOS_TYPE");
  if (b[7])
    throw QosError("QOS_RESERVED");
  QosFrameView f;
  f.type = static_cast<QosType>(b[4]);
  f.sequence = b[5];
  f.count = b[6];
  std::copy_n(b.begin() + 8, 16, f.probe.begin());
  if (std::all_of(f.probe.begin(), f.probe.end(),
                  [](auto x) { return x == 0; }))
    throw QosError("QOS_PROBE");
  f.nonce = read(b, 24, 8);
  const bool ping = f.type == QosType::Ping || f.type == QosType::Pong;
  const bool train = f.type == QosType::Up || f.type == QosType::Down;
  const bool report = f.type == QosType::Report;
  if (b.size() != (train ? 1200U : report ? 48U : 32U))
    throw QosError("QOS_LENGTH");
  f.payload = b.subspan(32);
  if (ping) {
    if (f.sequence >= 4 || f.count != 4 || !f.nonce)
      throw QosError("QOS_FIELDS");
  } else {
    if (f.nonce || f.count != ((train || report) ? 8U : 0U) ||
        f.sequence >= (train ? 8U : 1U))
      throw QosError("QOS_FIELDS");
    if (train && std::any_of(f.payload.begin(), f.payload.end(),
                             [](auto x) { return x != 0; }))
      throw QosError("QOS_FIELDS");
    if (report) {
      if (read(b, 44, 4))
        throw QosError("QOS_RESERVED");
      f.acceptedCount = static_cast<std::uint32_t>(read(b, 32, 4));
      f.elapsedUs = read(b, 36, 8);
      if (f.acceptedCount > 8 || f.elapsedUs > 3000000 ||
          (f.acceptedCount < 2 ? f.elapsedUs != 0 : f.elapsedUs == 0))
        throw QosError("QOS_FIELDS");
    }
  }
  return f;
}
std::vector<unsigned char> encodeQosFrame(const QosFrameView &f) {
  if (f.sequence > 255 || f.count > 255)
    throw QosError("QOS_FIELDS");
  std::array<unsigned char, 1200> b{};
  b[0] = 'C';
  b[1] = 'N';
  b[2] = 'Q';
  b[3] = 1;
  b[4] = static_cast<unsigned char>(f.type);
  b[5] = static_cast<unsigned char>(f.sequence);
  b[6] = static_cast<unsigned char>(f.count);
  std::copy(f.probe.begin(), f.probe.end(), b.begin() + 8);
  write(b, 24, 8, f.nonce);
  const auto size = (f.type == QosType::Up || f.type == QosType::Down) ? 1200U
                    : f.type == QosType::Report                        ? 48U
                                                                       : 32U;
  if (f.type == QosType::Report) {
    write(b, 32, 4, f.acceptedCount);
    write(b, 36, 8, f.elapsedUs);
  }
  (void)parseQosFrame(std::span(b).first(size));
  return {b.begin(), b.begin() + size};
}
std::vector<unsigned char> qosFrame(QosType type, const QosProbeId &probe,
                                    unsigned sequence, std::uint64_t nonce,
                                    unsigned accepted, std::uint64_t span) {
  QosFrameView f;
  f.type = type;
  f.probe = probe;
  f.sequence = sequence;
  f.nonce = nonce;
  f.acceptedCount = accepted;
  f.elapsedUs = span;
  f.count = (type == QosType::Ping || type == QosType::Pong) ? 4
            : (type == QosType::Up || type == QosType::Down ||
               type == QosType::Report)
                ? 8
                : 0;
  return encodeQosFrame(f);
}
} // namespace CnaService
