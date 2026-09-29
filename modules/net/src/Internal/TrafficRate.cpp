// SPDX-License-Identifier: MS-PL
#include "TrafficRate.hpp"
#include <algorithm>
#include <limits>

namespace CNA::Internal::Net {
namespace {
int perSecond(std::uint32_t bytes,double seconds) {
    return static_cast<int>(std::min<double>(bytes/seconds,std::numeric_limits<int>::max()));
}
}
bool TrafficRate::sample(std::chrono::steady_clock::time_point now,std::uint32_t sent,std::uint32_t received) {
    if(!start_){start_=now;sent_=sent;received_=received;return false;}
    const auto elapsed=now-*start_;
    if(elapsed<std::chrono::seconds(1))return false;
    const double seconds=std::chrono::duration<double>(elapsed).count();
    // Unsigned differences stay right across a total that wrapped past 2^32.
    sentPerSecond_=perSecond(sent-sent_,seconds);
    receivedPerSecond_=perSecond(received-received_,seconds);
    start_=now;sent_=sent;received_=received;
    return true;
}
}
