// SPDX-License-Identifier: MS-PL
#pragma once
#include <array>
#include <cstdint>
#include <optional>
namespace CNA::Internal::Net {
using QosProbeId = std::array<unsigned char, 16>;
struct OnlineQosValues {
  int upstream = 0, downstream = 0, averageMs = 0, minimumMs = 0;
};
enum class OnlineQosStatus { Pending, Available, Failed, Canceled };
// Pure CNQ v1 arithmetic, fed only validated bound frames by the worker.
class OnlineQosMeasurement final {
public:
  /**
   * @brief Starts a bounded measurement on a normalized monotonic clock.
   * @param probe Opaque probe identity.
   * @param generation Local job generation.
   * @param startUs Normalized start in microseconds.
   */
  OnlineQosMeasurement(QosProbeId probe, std::uint64_t generation,
                       std::int64_t startUs);
  /**
   * @brief Records actual complete-frame send time.
   * @param sequence Ping index.
   * @param nonce Unique challenge.
   * @param atUs Send completion timestamp.
   * @return Whether the tuple was accepted.
   */
  bool sentPing(unsigned sequence, std::uint64_t nonce, std::int64_t atUs);
  /**
   * @brief Accepts a unique matching host response.
   * @param probe Probe identity.
   * @param generation Job generation.
   * @param sequence Ping index.
   * @param nonce Original challenge.
   * @param atUs Receiver timestamp.
   * @return Whether the response is admissible.
   */
  bool pong(QosProbeId probe, std::uint64_t generation, unsigned sequence,
            std::uint64_t nonce, std::int64_t atUs);
  /**
   * @brief Records one increasing downstream train arrival.
   * @param sequence Train index.
   * @param atUs Receiver timestamp.
   * @return Whether the sample was accepted.
   */
  bool downstream(unsigned sequence, std::int64_t atUs);
  /**
   * @brief Accepts a bounded host receiver report.
   * @param count Distinct accepted frames.
   * @param elapsedUs Host receiver span.
   * @param atUs Local report receipt time.
   * @return Whether the report was accepted.
   */
  bool upstreamReport(int count, std::int64_t elapsedUs, std::int64_t atUs);
  /**
   * @brief Finalizes complete evidence or an expired measurement.
   * @param atUs Current local monotonic timestamp.
   */
  void finish(std::int64_t atUs);
  /**
   * @brief Permanently cancels a pending measurement.
   */
  void cancel();
  /**
   * @brief Reads terminal/pending state.
   * @return Measurement state.
   */
  OnlineQosStatus status() const;
  /**
   * @brief Reads numbers, all zero while unavailable.
   * @return Immutable values.
   */
  OnlineQosValues values() const;

private:
  struct Ping {
    std::uint64_t nonce;
    std::int64_t sent;
    std::optional<std::int64_t> rtt;
  };
  QosProbeId probe_;
  std::uint64_t generation_;
  std::int64_t start_, deadline_;
  std::array<std::optional<Ping>, 4> pings_;
  unsigned received_ = 0, downCount_ = 0;
  int lastSequence_ = -1;
  std::int64_t firstDown_ = 0, lastDown_ = 0;
  std::optional<int> upstream_;
  OnlineQosStatus status_ = OnlineQosStatus::Pending;
  OnlineQosValues values_;
};
} // namespace CNA::Internal::Net
