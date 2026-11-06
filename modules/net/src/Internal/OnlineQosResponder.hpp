// SPDX-License-Identifier: MS-PL
#pragma once
#include "CNA/Internal/GamerServices/IGamerServicesBackend.hpp"
#include "OnlineQosResult.hpp"
#include <atomic>
#include <memory>
#include <thread>
namespace CNA::Internal::Net {
class OnlineQosResponder final {
public:
  /**
   * @brief Owns the current host probe connection worker.
   * @param origin Retained backend authority.
   * @param actor Host owner account.
   * @param users Exact local group.
   * @param session Host session.
   */
  OnlineQosResponder(
      std::shared_ptr<GamerServices::IGamerServicesBackend> origin,
      std::string actor, std::vector<std::string> users, std::string session);
  /**
   * @brief Stops and joins the bounded host worker.
   */
  ~OnlineQosResponder();

private:
  std::jthread worker_;
};
/**
 * @brief Reads native responder evidence for process tests.
 * @return Completed native Pong writes.
 */
std::uint64_t onlineQosPongsSentForTesting();
/**
 * @brief Reads the acceptance-harness readiness barrier.
 * @return Whether the native responder has a verified channel.
 */
bool onlineQosResponderReadyForTesting();
/**
 * @brief Injects responder absence in a dedicated acceptance process.
 * @param enabled Whether future host responders may start.
 */
void setOnlineQosResponderEnabledForTesting(bool enabled);
/**
 * @brief Queues a bounded job using original search authority.
 * @param origin Retained search backend.
 * @param actor Original searching account.
 * @param listing Private listing permit and deadline.
 * @return Shared publication state, or null without transport support.
 */
std::shared_ptr<OnlineQosResult>
startOnlineQos(std::shared_ptr<GamerServices::IGamerServicesBackend> origin,
               std::string actor,
               const GamerServices::ServiceSessionSnapshot &listing);
} // namespace CNA::Internal::Net
