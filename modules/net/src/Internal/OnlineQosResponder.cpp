// SPDX-License-Identifier: MS-PL
#include "OnlineQosResponder.hpp"
#include "CNA/Internal/GamerServices/BackendConfiguration.hpp"
#include "CnaService/QosProtocol.hpp"
#include "OnlineQosHost.hpp"
#include "QosWebSocket.hpp"
#include "RelayWebSocket.hpp"
#include <array>
#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
namespace CNA::Internal::Net {
namespace {
using Clock = std::chrono::steady_clock;
std::atomic<std::uint64_t> pongs{0};
std::atomic<bool> responderEnabled{true}, responderReady{false};
std::int64_t micros() {
  return std::chrono::duration_cast<std::chrono::microseconds>(
             Clock::now().time_since_epoch())
      .count();
}
struct Writer {
  std::deque<std::vector<unsigned char>> frames;
  std::size_t offset = 0, bytes = 0;
  void offer(std::vector<unsigned char> frame) {
    if (frames.size() >= 16 || bytes + frame.size() > 19200)
      throw CnaService::QosError("QOS_QUEUE_FULL");
    bytes += frame.size();
    frames.push_back(std::move(frame));
  }
  template <class Completed> bool drain(QosWebSocket &socket, Completed done) {
    bool progress = false;
    while (!frames.empty()) {
      const auto before = offset;
      const bool complete = socket.send(frames.front(), offset);
      progress |= offset != before;
      if (!complete)
        break;
      done(frames.front());
      bytes -= frames.front().size();
      frames.pop_front();
      offset = 0;
    }
    return progress;
  }
};
void pause(std::stop_token stop, int milliseconds) {
  for (int i = 0; i < milliseconds && !stop.stop_requested(); i += 2)
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
}
struct Job {
  std::shared_ptr<GamerServices::IGamerServicesBackend> origin;
  std::string actor, permit;
  Clock::time_point expires;
  std::weak_ptr<OnlineQosResult> result;
  std::shared_ptr<std::stop_source> cancellation;
};
void measure(Job &job) {
  const auto stop = job.cancellation->get_token();
  const auto deadline =
      std::min(job.expires, Clock::now() + std::chrono::seconds(5));
  if (stop.stop_requested() || Clock::now() >= deadline)
    return;
  try {
    const auto config = GamerServices::configurationForBackend(*job.origin);
    auto ticket =
        job.origin->sessionDirectory().issueQosTicket(job.actor, job.permit);
    if (stop.stop_requested() || Clock::now() >= deadline)
      return;
    const auto id = CnaService::relayMachineId(ticket.probe);
    QosWebSocket socket(config, std::move(ticket), stop, deadline);
    Writer writer;
    OnlineQosProbe probe(id, 1, micros,
                         [&](auto bytes) { writer.offer(std::move(bytes)); });
    probe.start();
    while (!stop.stop_requested() && Clock::now() < deadline &&
           probe.status() == OnlineQosStatus::Pending) {
      probe.tick();
      bool progress = writer.drain(
          socket, [&](const auto &bytes) { probe.sent(bytes, micros()); });
      for (int i = 0; i < 32; ++i) {
        bool moved = false;
        auto frame = socket.receive(moved);
        progress |= moved;
        if (frame)
          probe.receive(*frame, micros());
        if (!moved)
          break;
      }
      probe.tick();
      if (!progress)
        pause(stop, 2);
    }
    if (!stop.stop_requested() && Clock::now() < deadline &&
        probe.status() == OnlineQosStatus::Available) {
      const auto v = probe.values();
      if (auto result = job.result.lock())
        result->complete(v.upstream, v.downstream, v.averageMs, v.minimumMs);
    }
  } catch (...) { /* A missing host, transport or authority stays an unmeasured
                     listing. */
  }
}
class Scheduler {
public:
  Scheduler() {
    for (auto &thread : workers_)
      thread = std::jthread([this](std::stop_token stop) { run(stop); });
  }
  ~Scheduler() {
    for (auto &thread : workers_)
      thread.request_stop();
    changed_.notify_all();
  }
  bool submit(Job job) {
    std::lock_guard lock(mutex_);
    if (queue_.size() >= 128 || counts_[job.origin.get()] >= 32)
      return false;
    ++counts_[job.origin.get()];
    queue_.push_back(std::move(job));
    changed_.notify_one();
    return true;
  }

private:
  void run(std::stop_token stop) {
    while (!stop.stop_requested()) {
      std::optional<Job> job;
      {
        std::unique_lock lock(mutex_);
        changed_.wait(lock,stop,[&] { return !queue_.empty(); });
        if (stop.stop_requested())
          return;
        job = std::move(queue_.front());
        queue_.pop_front();
      }
      measure(*job);
      {
        std::lock_guard lock(mutex_);
        auto it = counts_.find(job->origin.get());
        if (--it->second == 0)
          counts_.erase(it);
      }
    }
  }
  std::mutex mutex_;
  std::condition_variable_any changed_;
  std::deque<Job> queue_;
  std::map<GamerServices::IGamerServicesBackend *, unsigned> counts_;
  std::array<std::jthread, 4> workers_;
};
} // namespace
std::shared_ptr<OnlineQosResult>
startOnlineQos(std::shared_ptr<GamerServices::IGamerServicesBackend> origin,
               std::string actor,
               const GamerServices::ServiceSessionSnapshot &listing) {
  if (listing.qosPermit.empty() || relayTransportRefusal()!=nullptr)
    return {};
  auto cancellation = std::make_shared<std::stop_source>();
  auto result = std::make_shared<OnlineQosResult>(
      [cancellation] { cancellation->request_stop(); });
  try {
    static Scheduler scheduler;
    if (!scheduler.submit({std::move(origin), std::move(actor), listing.qosPermit,
                           listing.qosExpires, result, cancellation}))result->cancel();
  } catch (...) {result->cancel();}
  return result;
}
std::uint64_t onlineQosPongsSentForTesting() { return pongs.load(); }
bool onlineQosResponderReadyForTesting() { return responderReady.load(); }
void setOnlineQosResponderEnabledForTesting(bool enabled) {
  responderEnabled = enabled;
}
OnlineQosResponder::OnlineQosResponder(
    std::shared_ptr<GamerServices::IGamerServicesBackend> origin,
    std::string actor, std::vector<std::string> users, std::string session) {
  if (!responderEnabled || relayTransportRefusal()!=nullptr)
    return;
  const auto config = GamerServices::configurationForBackend(*origin);
  pongs = 0;
  responderReady = false;
  worker_ =
      std::jthread([origin = std::move(origin), actor = std::move(actor),
                    users = std::move(users), session = std::move(session),
                    config](std::stop_token stop) {
        while (!stop.stop_requested()) {
          try {
            auto ticket = origin->sessionDirectory().issueQosHostTicket(
                actor, users, session);
            QosWebSocket socket(config, std::move(ticket), stop,
                                Clock::now() + std::chrono::seconds(5));
            Writer writer;
            responderReady = true;
            struct Host {
              std::unique_ptr<OnlineQosHost> core;
              std::int64_t end;
            };
            std::map<QosProbeId, Host> hosts;
            const auto written=[](const auto& bytes) {
              if(CnaService::parseQosFrame(bytes).type==CnaService::QosType::Pong)++pongs;
            };
            while (!stop.stop_requested()) {
              bool progress = writer.drain(socket,written);
              for (int i = 0; i < 32; ++i) {
                bool moved = false;
                auto frame = socket.receive(moved);
                progress |= moved;
                if (frame) {
                  const auto at = micros();
                  const auto f = CnaService::parseQosFrame(*frame);
                  if (f.type == CnaService::QosType::Cancel)
                    hosts.erase(f.probe);
                  else if (f.type == CnaService::QosType::Begin) {
                    if (hosts.size() >= 4 || hosts.contains(f.probe))
                      throw CnaService::QosError("QOS_HOST_FULL");
                    auto core = std::make_unique<OnlineQosHost>(
                        f.probe, micros,
                        [&](auto bytes) { writer.offer(std::move(bytes)); });
                    core->begin(at);
                    hosts.emplace(f.probe, Host{std::move(core), at + 3000000});
                  } else if (auto it = hosts.find(f.probe); it != hosts.end())
                    it->second.core->receive(*frame, at);
                }
                // Drain between received frames so several Begin bursts do not fill the
                // shared bounded queue before the worker offers its first write.
                progress |= writer.drain(socket,written);
                if (!moved)
                  break;
              }
              for (auto &[id, host] : hosts)
                host.core->tick();
              std::erase_if(hosts, [](const auto &p) {
                return micros() >= p.second.end;
              });
              if (!progress)
                pause(stop, 2);
            }
          } catch (const GamerServices::ServiceOperationError &error) {
            if (error.code == "NOT_SUPPORTED")
              return;
          } catch (...) {
          }
          responderReady = false;
          pause(stop, 1000);
        }
      });
}
OnlineQosResponder::~OnlineQosResponder() {
  worker_.request_stop();
  if (worker_.joinable())
    worker_.join();
  responderReady = false;
}
} // namespace CNA::Internal::Net
