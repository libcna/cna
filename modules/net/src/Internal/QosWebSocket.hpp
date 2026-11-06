// SPDX-License-Identifier: MS-PL
#pragma once
#include "CNA/GamerServices/Configuration.hpp"
#include "CNA/Internal/GamerServices/ServiceSessionDirectory.hpp"
#include "RelayMessageAssembler.hpp"
#include <chrono>
#include <memory>
#include <stop_token>

namespace CNA::Internal::Net {
/** @brief One worker-owned verified WebSocket; never calls XNA objects or
 * callbacks. */
class QosWebSocket {
public:
  /** @brief Establishes bounded TLS/upgrade/hello and validates authority
   * before returning.
   * @param configuration Deployment trust settings. @param ticket One-use
   * authority, consumed.
   * @param stop Cancellation for connect/hello.
     * @param deadline Whole-job connection/hello deadline. */
  QosWebSocket(const CNA::GamerServices::Configuration &configuration,
               GamerServices::ServiceQosTicket ticket, std::stop_token stop,
               std::chrono::steady_clock::time_point deadline);
  /** @brief Closes the owned transport without detached work. */
  ~QosWebSocket();
  QosWebSocket(const QosWebSocket &) = delete;
  QosWebSocket &operator=(const QosWebSocket &) = delete;
  /** @brief Attempts bounded nonblocking receipt; controls are validated and
   * skipped.
   * @param progressed Whether a transport chunk was consumed.
   * @return Owned binary message, if complete. */
  std::optional<std::vector<unsigned char>> receive(bool &progressed);
  /** @brief Resumes one owned binary frame; caller keeps its bytes stable until
   * completion.
   * @param frame Complete validated envelope. @param offset Updated consumed
   * count.
   * @return Whether the entire frame was consumed. */
  bool send(std::span<const unsigned char> frame, std::size_t &offset);

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
} // namespace CNA::Internal::Net
