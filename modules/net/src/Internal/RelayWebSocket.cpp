// SPDX-License-Identifier: MS-PL
#include "RelayWebSocket.hpp"
#include "CnaService/Protocol.hpp"
#ifdef CNA_SERVICE_TLS_TRANSPORT
#include <curl/curl.h>
#endif
#include <array>
#include <chrono>
#include <mutex>
#include <thread>

#ifdef CNA_SERVICE_TLS_TRANSPORT
namespace CNA::Internal::Net {
namespace {
using Clock=std::chrono::steady_clock;
void check(CURLcode code) {if(code!=CURLE_OK)throw CnaService::RelayError("RELAY_TRANSPORT_UNAVAILABLE");}
void erase(std::string& value) noexcept {std::fill(value.begin(),value.end(),'\0');value.clear();}
struct Secret {
    std::string value;
    ~Secret(){erase(value);}
};
bool hex(std::string_view value,std::size_t length) {
    if(value.size()!=length)return false;
    for(char c:value)if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')))return false;
    return true;
}
bool protocol(const char* const* names,std::string_view required) {
    if(names)for(;*names;++names)if(required==*names)return true;
    return false;
}
RelayMessageKind kind(int flags) {
    constexpr int allowed=CURLWS_TEXT|CURLWS_BINARY|CURLWS_CONT|CURLWS_CLOSE|CURLWS_PING|CURLWS_PONG;
    if(flags&~allowed)throw CnaService::RelayError("RELAY_MESSAGE_TYPE");
    switch(flags&~CURLWS_CONT) {
        case CURLWS_TEXT:return RelayMessageKind::Text;
        case CURLWS_BINARY:return RelayMessageKind::Binary;
        case CURLWS_CLOSE:return RelayMessageKind::Close;
        case CURLWS_PING:return RelayMessageKind::Ping;
        case CURLWS_PONG:return RelayMessageKind::Pong;
        case 0:return RelayMessageKind::Continuation;
        default:throw CnaService::RelayError("RELAY_MESSAGE_TYPE");
    }
}
}
std::string relayEndpoint(const CNA::GamerServices::Configuration& configuration) {
    CNA::GamerServices::validateConfiguration(configuration);
    if(configuration.endpoint.empty())throw CnaService::RelayError("RELAY_NOT_CONFIGURED");
    std::unique_ptr<CURLU,decltype(&curl_url_cleanup)> url(curl_url(),curl_url_cleanup);
    if(!url||curl_url_set(url.get(),CURLUPART_URL,configuration.endpoint.c_str(),0)!=CURLUE_OK)
        throw CnaService::RelayError("RELAY_CONFIGURATION");
    char* raw=nullptr;
    if(curl_url_get(url.get(),CURLUPART_SCHEME,&raw,0)!=CURLUE_OK)throw CnaService::RelayError("RELAY_CONFIGURATION");
    const bool secure=std::string_view(raw)=="https";curl_free(raw);
    if(curl_url_set(url.get(),CURLUPART_SCHEME,secure?"wss":"ws",0)!=CURLUE_OK||
       curl_url_set(url.get(),CURLUPART_PATH,CnaService::RelayPath.data(),0)!=CURLUE_OK||
       curl_url_get(url.get(),CURLUPART_URL,&raw,0)!=CURLUE_OK)throw CnaService::RelayError("RELAY_CONFIGURATION");
    std::string result=raw;curl_free(raw);return result;
}
struct RelayWebSocket::Impl {
    std::unique_ptr<CURL,decltype(&curl_easy_cleanup)> handle{nullptr,curl_easy_cleanup};
    std::stop_token stop;
    RelayMessageAssembler receiver;
    std::optional<Clock::time_point> partialSince;
    static int progress(void* context,curl_off_t,curl_off_t,curl_off_t,curl_off_t) {
        return static_cast<Impl*>(context)->stop.stop_requested()?1:0;
    }
    bool send(std::span<const unsigned char> bytes,std::size_t& offset,unsigned flags) {
        if(stop.stop_requested())throw CnaService::RelayError("RELAY_CANCELLED");
        if(offset>bytes.size())throw CnaService::RelayError("RELAY_SEND_ORDER");
        std::size_t sent=0;
        const auto code=curl_ws_send(handle.get(),bytes.data()+offset,bytes.size()-offset,&sent,0,flags);
        if(sent>bytes.size()-offset)throw CnaService::RelayError("RELAY_SEND_ORDER");
        offset+=sent;
        if(code!=CURLE_OK&&code!=CURLE_AGAIN)throw CnaService::RelayError("RELAY_SEND_FAILED");
        return offset==bytes.size();
    }
    std::optional<RelayMessage> receive(bool& progressed) {
        progressed=false;
        if(stop.stop_requested())throw CnaService::RelayError("RELAY_CANCELLED");
        if(partialSince&&Clock::now()-*partialSince>std::chrono::seconds(5))throw CnaService::RelayError("RELAY_ASSEMBLY_TIMEOUT");
        std::array<unsigned char,1024> scratch{};std::size_t count=0;const curl_ws_frame* metadata=nullptr;
        const auto code=curl_ws_recv(handle.get(),scratch.data(),scratch.size(),&count,&metadata);
        if(code==CURLE_AGAIN)return {};
        if(code!=CURLE_OK)throw CnaService::RelayError("RELAY_RECEIVE_FAILED");
        progressed=true;
        if(!metadata||count>scratch.size()||metadata->offset<0||metadata->bytesleft<0||
           metadata->offset>static_cast<curl_off_t>(CnaService::MaxRelayFrameBytes)||
           metadata->bytesleft>static_cast<curl_off_t>(CnaService::MaxRelayFrameBytes))
            throw CnaService::RelayError("RELAY_RECEIVE_METADATA");
        const auto type=kind(metadata->flags);
        const bool data=type==RelayMessageKind::Text||type==RelayMessageKind::Binary||type==RelayMessageKind::Continuation;
        if(data&&!partialSince)partialSince=Clock::now();
        auto message=receiver.push({type,(metadata->flags&CURLWS_CONT)!=0,
            static_cast<std::size_t>(metadata->offset),static_cast<std::size_t>(metadata->bytesleft),
            std::span(scratch).first(count)});
        if(message&&data)partialSince.reset();
        if(message&&message->kind==RelayMessageKind::Close)throw CnaService::RelayError("RELAY_CLOSED");
        return message;
    }
};
RelayWebSocket::RelayWebSocket(const CNA::GamerServices::Configuration& configuration,
    GamerServices::ServiceRelayTicket ticket,std::stop_token stop):impl_(std::make_unique<Impl>()) {
    Secret secret{std::move(ticket.ticket)};
    if(!hex(secret.value,64)||!hex(ticket.session,32)||!hex(ticket.machine,32))throw CnaService::RelayError("RELAY_TICKET_INVALID");
    (void)CnaService::relayMachineId(ticket.machine);
    const auto endpoint=relayEndpoint(configuration);
    static std::once_flag initialized;
    std::call_once(initialized,[]{check(curl_global_init(CURL_GLOBAL_DEFAULT));});
    const auto* version=curl_version_info(CURLVERSION_NOW);
    if(!version||version->version_num<0x075600||!protocol(version->protocols,"ws")||
       !protocol(version->protocols,"wss")||!(version->features&CURL_VERSION_SSL))
        throw CnaService::RelayError("RELAY_SECURE_WEBSOCKET_UNAVAILABLE");
    impl_->handle.reset(curl_easy_init());if(!impl_->handle)throw CnaService::RelayError("RELAY_TRANSPORT_UNAVAILABLE");
    impl_->stop=stop;auto* handle=impl_->handle.get();
    check(curl_easy_setopt(handle,CURLOPT_URL,endpoint.c_str()));
    check(curl_easy_setopt(handle,CURLOPT_PROTOCOLS_STR,configuration.insecureLoopback?"wss,ws":"wss"));
    check(curl_easy_setopt(handle,CURLOPT_CONNECT_ONLY,2L));
    check(curl_easy_setopt(handle,CURLOPT_HTTP_VERSION,CURL_HTTP_VERSION_1_1));
    check(curl_easy_setopt(handle,CURLOPT_FOLLOWLOCATION,0L));check(curl_easy_setopt(handle,CURLOPT_PROXY,""));
    check(curl_easy_setopt(handle,CURLOPT_SSL_VERIFYPEER,1L));check(curl_easy_setopt(handle,CURLOPT_SSL_VERIFYHOST,2L));
    check(curl_easy_setopt(handle,CURLOPT_SSLVERSION,CURL_SSLVERSION_TLSv1_2));
    if(!configuration.caBundle.empty())check(curl_easy_setopt(handle,CURLOPT_CAINFO,configuration.caBundle.c_str()));
    check(curl_easy_setopt(handle,CURLOPT_NOSIGNAL,1L));check(curl_easy_setopt(handle,CURLOPT_CONNECTTIMEOUT_MS,3000L));
    check(curl_easy_setopt(handle,CURLOPT_TIMEOUT_MS,5000L));check(curl_easy_setopt(handle,CURLOPT_NOPROGRESS,0L));
    check(curl_easy_setopt(handle,CURLOPT_XFERINFOFUNCTION,&Impl::progress));check(curl_easy_setopt(handle,CURLOPT_XFERINFODATA,impl_.get()));
    check(curl_easy_perform(handle));long status=0;check(curl_easy_getinfo(handle,CURLINFO_RESPONSE_CODE,&status));
    if(status!=101)throw CnaService::RelayError("RELAY_UPGRADE_REFUSED");
    constexpr auto request="relay-connect";
    CnaService::Json hello={{"v",CnaService::RelayVersion},{"id",request},{"game",configuration.gameId},{"ticket",secret.value}};
    Secret encoded{hello.dump()};erase(hello["ticket"].get_ref<std::string&>());
    const auto deadline=Clock::now()+std::chrono::seconds(5);std::size_t offset=0;
    const auto helloBytes=std::span(reinterpret_cast<const unsigned char*>(encoded.value.data()),encoded.value.size());
    while(!impl_->send(helloBytes,offset,CURLWS_TEXT)) {
        if(Clock::now()>=deadline)throw CnaService::RelayError("RELAY_HELLO_TIMEOUT");
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    erase(encoded.value);erase(secret.value);
    for(;;) {
        if(Clock::now()>=deadline)throw CnaService::RelayError("RELAY_HELLO_TIMEOUT");
        bool progressed=false;auto message=impl_->receive(progressed);
        if(message&&(message->kind==RelayMessageKind::Text||message->kind==RelayMessageKind::Binary)) {
            if(message->kind!=RelayMessageKind::Text)throw CnaService::RelayError("RELAY_WELCOME_MISMATCH");
            validateRelayWelcome(std::string_view(reinterpret_cast<const char*>(message->bytes.data()),message->bytes.size()),
                request,ticket.session,ticket.machine);break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
}
RelayWebSocket::~RelayWebSocket()=default;
std::optional<std::vector<unsigned char>> RelayWebSocket::receive(bool& progressed) {
    auto message=impl_->receive(progressed);
    if(!message)return {};
    if(message->kind==RelayMessageKind::Text)throw CnaService::RelayError("RELAY_MESSAGE_TYPE");
    if(message->kind==RelayMessageKind::Binary)return std::move(message->bytes);
    return {};
}
bool RelayWebSocket::send(std::span<const unsigned char> frame,std::size_t& offset) {
    (void)CnaService::parseRelayFrame(frame);
    return impl_->send(frame,offset,CURLWS_BINARY);
}
}

#else
// Preserve the private transport contract while refusing I/O this build cannot do -- a browser, or
// a native build without libcurl. No peer, welcome, packet or connection is fabricated.
namespace CNA::Internal::Net {
#ifdef __EMSCRIPTEN__
constexpr const char* NoRelayTransport = "BROWSER_RELAY_TRANSPORT_UNAVAILABLE";
#else
constexpr const char* NoRelayTransport = "RELAY_TRANSPORT_UNAVAILABLE";
#endif
struct RelayWebSocket::Impl {};
std::string relayEndpoint(const CNA::GamerServices::Configuration&) {
    throw CnaService::RelayError(NoRelayTransport);
}
RelayWebSocket::RelayWebSocket(const CNA::GamerServices::Configuration&,
    GamerServices::ServiceRelayTicket, std::stop_token) {
    throw CnaService::RelayError(NoRelayTransport);
}
RelayWebSocket::~RelayWebSocket() = default;
std::optional<std::vector<unsigned char>> RelayWebSocket::receive(bool& progressed) {
    progressed = false;
    throw CnaService::RelayError(NoRelayTransport);
}
bool RelayWebSocket::send(std::span<const unsigned char>, std::size_t&) {
    throw CnaService::RelayError(NoRelayTransport);
}
}
#endif
