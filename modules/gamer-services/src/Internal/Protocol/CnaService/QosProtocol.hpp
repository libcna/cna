// SPDX-License-Identifier: MIT
#pragma once
#include <array>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
namespace CnaService {
/** @brief Independent pre-membership probe endpoint, preserving CNR v1. */
inline constexpr std::string_view QosPath = "/cna/qos/v1";
/** @brief CNQ v1 typed probe traffic; no game payload is accepted. */
enum class QosType : unsigned char {
  Ping = 1,
  Pong = 2,
  Up = 3,
  Down = 4,
  Report = 5,
  Cancel = 6,
  Begin = 7
};
/** @brief Nonzero opaque probe identity, independent of machine identity. */
using QosProbeId = std::array<unsigned char, 16>;
/** @brief Safe credential-free parser refusal. */
class QosError : public std::runtime_error {
public:
  /** @brief Constructs a refusal. @param code Stable safe code. */
  explicit QosError(std::string code);
  /** @brief Reads the refusal. @return Stable safe code. */
  const std::string &code() const noexcept;

private:
  std::string code_;
};
/** @brief Validated borrowed CNQ view; payload lifetime is the input's
 * lifetime. */
struct QosFrameView {
  QosType type = QosType::Ping;
  unsigned sequence = 0, count = 4;
  QosProbeId probe{};
  std::uint64_t nonce = 0;
  std::span<const unsigned char> payload;
  std::uint32_t acceptedCount = 0;
  std::uint64_t elapsedUs = 0;
};
/** @brief Validates before allocating. @param bytes Complete message. @return
 * Borrowed view. */
QosFrameView parseQosFrame(std::span<const unsigned char> bytes);
/** @brief Encodes canonical zero-padded data. @param frame Valid fields.
 * @return Complete frame. */
std::vector<unsigned char> encodeQosFrame(const QosFrameView &frame);
/** @brief Builds a typed frame. @param type Kind. @param probe Identity. @param
 * sequence Index.
 * @param nonce Ping challenge. @param accepted Report count. @param span Report
 * receiver span.
 * @return Validated canonical bytes. */
std::vector<unsigned char> qosFrame(QosType type, const QosProbeId &probe,
                                    unsigned sequence = 0,
                                    std::uint64_t nonce = 0,
                                    unsigned accepted = 0,
                                    std::uint64_t span = 0);
} // namespace CnaService
