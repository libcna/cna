// SPDX-License-Identifier: MS-PL
#include "RelayTransport.hpp"
#include "RelayWebSocket.hpp"
#include "RelayEnetPolicy.hpp"
#include "CNA/Internal/Net/ENetLibrary.hpp"
#include "CNA/Internal/Net/ENetHostHandle.hpp"
#include <array>
#include <chrono>
#include <deque>
#include <map>
#include <mutex>
#include <set>
#include <thread>

namespace CNA::Internal::Net {
namespace {
using Clock=std::chrono::steady_clock;
struct Route {
    ENetSocket socket=ENET_SOCKET_NULL;
    std::uint16_t port=0;
    explicit Route(enet_uint32 loopback) {
        socket=enet_socket_create(ENET_SOCKET_TYPE_DATAGRAM);
        if(socket==ENET_SOCKET_NULL)throw CnaService::RelayError("RELAY_SOCKET_UNAVAILABLE");
        ENetAddress bind{loopback,0},address{};
        if(enet_socket_set_option(socket,ENET_SOCKOPT_NONBLOCK,1)!=0||enet_socket_bind(socket,&bind)!=0||
           enet_socket_get_address(socket,&address)!=0||address.port==0) {
            enet_socket_destroy(socket);socket=ENET_SOCKET_NULL;throw CnaService::RelayError("RELAY_SOCKET_UNAVAILABLE");
        }
        port=address.port;
    }
    ~Route(){if(socket!=ENET_SOCKET_NULL)enet_socket_destroy(socket);}
};
std::set<CnaService::RelayMachineId> routes(const std::vector<std::string>& machines,const CnaService::RelayMachineId& local) {
    if(machines.size()>30)throw CnaService::RelayError("RELAY_ROUTE_LIMIT");
    std::set<CnaService::RelayMachineId> result;
    for(const auto& name:machines) {
        const auto id=CnaService::relayMachineId(name);
        if(id==local||!result.insert(id).second)throw CnaService::RelayError("RELAY_ROUTE_INVALID");
    }
    return result;
}
}
struct RelayTransport::Impl {
    mutable std::mutex mutex;
    std::map<CnaService::RelayMachineId,std::unique_ptr<Route>> sockets;
    CnaService::RelayMachineId local{};
    ENetAddress endpoint{};
    RelayTransportStatus observation;
    std::jthread worker;
    void setRoutes(const std::vector<std::string>& machines) {
        const auto desired=routes(machines,local);
        std::lock_guard lock(mutex);
        if(observation.state==RelayTransportState::Stopped)throw CnaService::RelayError("RELAY_STOPPED");
        std::map<CnaService::RelayMachineId,std::unique_ptr<Route>> additions;
        for(const auto& id:desired)if(!sockets.contains(id))additions.emplace(id,std::make_unique<Route>(endpoint.host));
        // Allocate the entire replacement first, preserving old routes if any allocation fails.
        for(auto it=sockets.begin();it!=sockets.end();)if(!desired.contains(it->first))it=sockets.erase(it);else++it;
        sockets.merge(additions);
    }
    void run(const CNA::GamerServices::Configuration& configuration,GamerServices::ServiceRelayTicket ticket,std::stop_token stop) noexcept {
        try {
            RelayWebSocket connection(configuration,std::move(ticket),stop);
            {std::lock_guard lock(mutex);observation.state=RelayTransportState::Ready;}
            struct Pending {std::vector<unsigned char> bytes;std::size_t offset=0;std::optional<Clock::time_point> started;};
            std::deque<Pending> pending;
            std::array<unsigned char,65536> scratch{};
            struct Sent {Clock::time_point time;std::size_t bytes;};
            std::deque<Sent> recent;std::size_t windowBytes=0;
            std::optional<CnaService::RelayMachineId> lastRoute;
            while(!stop.stop_requested()) {
                {std::lock_guard lock(mutex);std::size_t consumed=0;
                    auto routeIt=lastRoute?sockets.upper_bound(*lastRoute):sockets.begin();
                    if(routeIt==sockets.end())routeIt=sockets.begin();
                    for(std::size_t visited=0;visited<sockets.size()&&consumed<64;++visited) {
                        const auto& [machine,route]=*routeIt;lastRoute=machine;
                        for(int iteration=0;iteration<8&&consumed<64;++iteration) {
                            ENetAddress sender{};ENetBuffer buffer{};buffer.data=scratch.data();buffer.dataLength=scratch.size();
                            const int count=enet_socket_receive(route->socket,&sender,&buffer,1);
                            if(count<0)throw CnaService::RelayError("RELAY_SOCKET_RECEIVE_FAILED");
                            if(count==0)break;
                            ++consumed;
                            if(sender.host!=endpoint.host||sender.port!=endpoint.port||static_cast<std::size_t>(count)>CnaService::MaxRelayDatagramBytes||
                               pending.size()>=CnaService::MaxRelayQueuedFrames) {++observation.dropped;continue;}
                            pending.push_back({CnaService::encodeRelayFrame(machine,std::span(scratch).first(static_cast<std::size_t>(count))),0,{}});
                        }
                        if(++routeIt==sockets.end())routeIt=sockets.begin();
                    }
                    observation.queued=pending.size();
                }
                for(int iteration=0;iteration<8&&!pending.empty();++iteration) {
                    const auto now=Clock::now();
                    while(!recent.empty()&&now-recent.front().time>=std::chrono::seconds(1)) {
                        windowBytes-=recent.front().bytes;recent.pop_front();
                    }
                    auto& frame=pending.front();
                    if(!frame.started) {
                        {
                            const auto destination=CnaService::parseRelayFrame(frame.bytes).machine;
                            std::lock_guard lock(mutex);
                            if(!sockets.contains(destination)){pending.pop_front();++observation.dropped;observation.queued=pending.size();continue;}
                        }
                        if(recent.size()>=CnaService::MaxRelayMessagesPerSecond-8||frame.bytes.size()>CnaService::MaxRelayBytesPerSecond-windowBytes)break;
                        recent.push_back({now,frame.bytes.size()});windowBytes+=frame.bytes.size();frame.started=now;
                    }
                    if(now-*frame.started>std::chrono::seconds(5))throw CnaService::RelayError("RELAY_SEND_TIMEOUT");
                    if(!connection.send(frame.bytes,frame.offset))break;
                    pending.pop_front();std::lock_guard lock(mutex);++observation.sent;observation.queued=pending.size();
                }
                for(int iteration=0;iteration<64;++iteration) {
                    bool progressed=false;auto frame=connection.receive(progressed);
                    if(frame) {
                        const auto decoded=CnaService::parseRelayFrame(*frame);std::lock_guard lock(mutex);
                        const auto route=sockets.find(decoded.machine);
                        if(route==sockets.end()){++observation.dropped;continue;}
                        if(!validateRelayEnetDatagram(decoded.datagram)){++observation.dropped;++observation.rejectedEnet;continue;}
                        ENetBuffer buffer{};buffer.data=const_cast<unsigned char*>(decoded.datagram.data());buffer.dataLength=decoded.datagram.size();
                        const int sent=enet_socket_send(route->second->socket,&endpoint,&buffer,1);
                        if(sent==static_cast<int>(decoded.datagram.size()))++observation.received;
                        else ++observation.dropped;
                    }
                    if(!progressed)break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
            std::lock_guard lock(mutex);observation.state=RelayTransportState::Stopped;observation.queued=0;
        }catch(const CnaService::RelayError& error) {
            std::lock_guard lock(mutex);observation.error=error.code();observation.state=stop.stop_requested()?RelayTransportState::Stopped:RelayTransportState::Failed;observation.queued=0;
        }catch(...) {
            std::lock_guard lock(mutex);observation.error="RELAY_INTERNAL_FAILURE";observation.state=stop.stop_requested()?RelayTransportState::Stopped:RelayTransportState::Failed;observation.queued=0;
        }
    }
};
RelayTransport::RelayTransport(const CNA::GamerServices::Configuration& configuration,
    GamerServices::ServiceRelayTicket ticket,std::uint16_t localPort,const std::vector<std::string>& machines):impl_(std::make_unique<Impl>()) {
    (void)relayEndpoint(configuration);
    impl_->local=CnaService::relayMachineId(ticket.machine);
    if(localPort==0)throw CnaService::RelayError("RELAY_LOCAL_PORT_INVALID");
    ENetLibrary::EnsureInitialized();
    if(enet_address_set_host_ip(&impl_->endpoint,"127.0.0.1")!=0)throw CnaService::RelayError("RELAY_SOCKET_UNAVAILABLE");
    impl_->endpoint.port=localPort;impl_->setRoutes(machines);
    impl_->worker=std::jthread([implementation=impl_.get(),configuration,ticket=std::move(ticket)](std::stop_token stop)mutable {
        implementation->run(configuration,std::move(ticket),stop);
    });
}
RelayTransport::~RelayTransport(){stop();}
void RelayTransport::setRoutes(const std::vector<std::string>& machines){impl_->setRoutes(machines);}
std::uint16_t RelayTransport::routePort(const std::string& machine) const {
    const auto id=CnaService::relayMachineId(machine);std::lock_guard lock(impl_->mutex);
    const auto found=impl_->sockets.find(id);return found==impl_->sockets.end()?0:found->second->port;
}
RelayTransportStatus RelayTransport::status() const {std::lock_guard lock(impl_->mutex);return impl_->observation;}
void RelayTransport::stop() noexcept {
    if(impl_&&impl_->worker.joinable()){impl_->worker.request_stop();impl_->worker.join();}
    if(impl_){std::lock_guard lock(impl_->mutex);impl_->observation.state=RelayTransportState::Stopped;impl_->sockets.clear();}
}
}
