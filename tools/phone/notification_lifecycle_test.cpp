// SPDX-License-Identifier: MS-PL
#include "Microsoft/Phone/Notification/HttpNotificationChannel.hpp"
#if defined(_WIN32)
#include <winsock2.h>
using socket_t = SOCKET;
static constexpr socket_t InvalidSocket = INVALID_SOCKET;
static constexpr int SendFlags = 0;
static constexpr int ShutdownWrite = SD_SEND;
static void CloseSocket(socket_t socket) { closesocket(socket); }
#else
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
using socket_t = int;
static constexpr socket_t InvalidSocket = -1;
static constexpr int SendFlags = MSG_NOSIGNAL;
static constexpr int ShutdownWrite = SHUT_WR;
static void CloseSocket(socket_t socket) { ::close(socket); }
#endif
#include <chrono>
#include <cstdio>
#include <future>
#include <string>
#include <thread>

using Microsoft::Phone::Notification::HttpNotificationChannel;
static const auto ownerThread = std::this_thread::get_id();
static bool partial = false;
static std::promise<void> waitingForMore;
static unsigned receives = 0;

static void ObserveReceive()
{
    // Observe entry into the listener's second receive, not elapsed time or transport ACKs.
    // No CNA implementation is replaced: the actual syscall and parser still run.
    if (partial && std::this_thread::get_id() != ownerThread && ++receives == 2)
#if !defined(_WIN32)
        waitingForMore.set_value();
#else
        (void)0;
#endif
}

#if defined(_WIN32)
using Receive = int (WSAAPI *)(SOCKET, char*, int, int);
using WaitEvents = DWORD (WSAAPI *)(DWORD, const WSAEVENT*, BOOL, DWORD, BOOL);
extern "C" Receive __real___imp_recv;
extern "C" WaitEvents __real___imp_WSAWaitForMultipleEvents;
static int WSAAPI ObservedReceive(SOCKET socket, char* buffer, int size, int flags)
{
    ObserveReceive();
    return __real___imp_recv(socket,buffer,size,flags);
}
static DWORD WSAAPI ObservedWait(DWORD count, const WSAEVENT* events, BOOL all, DWORD timeout, BOOL alertable)
{
    // Windows uses cancellable event waits instead of blocking recv. Observe the
    // first wait after real request bytes were received, then call the actual API.
    static bool announced = false;
    if (partial && std::this_thread::get_id() != ownerThread && receives && !announced) {
        announced = true;
        waitingForMore.set_value();
    }
    return __real___imp_WSAWaitForMultipleEvents(count, events, all, timeout, alertable);
}
extern "C" {
Receive __wrap___imp_recv = ObservedReceive;
WaitEvents __wrap___imp_WSAWaitForMultipleEvents = ObservedWait;
}
#else
extern "C" ssize_t __real_recv(int, void*, size_t, int);
extern "C" ssize_t __wrap_recv(int socket, void* buffer, size_t size, int flags)
{
    ObserveReceive();
    return __real_recv(socket,buffer,size,flags);
}
#endif

static socket_t Connect(const std::string& uri)
{
    const auto at=uri.find(':',uri.find("//")+2);
    const auto end=uri.find('/',at);
    const socket_t client=::socket(AF_INET,SOCK_STREAM,0);
    sockaddr_in address{};
    address.sin_family=AF_INET;
    address.sin_port=htons(static_cast<unsigned short>(std::stoi(uri.substr(at+1,end-at-1))));
    address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    if(client==InvalidSocket||::connect(client,reinterpret_cast<sockaddr*>(&address),sizeof(address))!=0) {
        if(client!=InvalidSocket)CloseSocket(client);
        return InvalidSocket;
    }
    return client;
}

int main(int argc,char** argv)
{
#if defined(_WIN32)
    struct Winsock {
        ~Winsock() { WSACleanup(); }
    } winsock;
    WSADATA data;
    if(WSAStartup(MAKEWORD(2,2), &data))return 10;
#endif
    const std::string mode=argc>1?argv[1]:"--complete";
    partial=mode=="--partial" || mode=="--partial-header";
    const bool truncated=mode=="--truncated";
    if(mode=="--idle") {
        for(int cycle=0;cycle<2000;++cycle) {
            HttpNotificationChannel channel("IdleLifecycle","test");
            channel.Open();
            if(!channel.getChannelUriProperty())return 2;
            std::this_thread::yield();
            channel.Close();
        }
        std::puts("NOTIFICATION_IDLE_LIFECYCLE_PASS");
        return 0;
    }
    for(int cycle=0;cycle<(partial?1:100);++cycle) {
        HttpNotificationChannel channel("Lifecycle","test");
        int notifications=0;
        channel.HttpNotificationReceived += [&](System::Object*, const auto& args) {
            notifications += args.getBodyAsStringProperty()=="x"?1:1000;
        };
        channel.Open();
        if(!channel.getChannelUriProperty())return 2;
        const socket_t client=Connect(channel.getChannelUriProperty()->ToString());
        if(client==InvalidSocket)return 3;
        const std::string request=mode=="--partial-header"?
            "POST /Lifecycle/ HTTP/1.1\r\nContent-Len":
            (partial || truncated?"POST /Lifecycle/ HTTP/1.1\r\nContent-Length: 100\r\n\r\nx":
                                  "POST /Lifecycle/ HTTP/1.1\r\nContent-Length: 1\r\n\r\nx");
        if(::send(client,request.data(),request.size(),SendFlags)!=static_cast<int>(request.size()))return 4;
        if(partial) {
            if(waitingForMore.get_future().wait_for(std::chrono::seconds(5))!=std::future_status::ready)return 5;
            std::puts("LISTENER_WAITING_FOR_INCOMPLETE_REQUEST");std::fflush(stdout);
            channel.Close();
        } else {
            if(truncated)::shutdown(client,ShutdownWrite);
            char response[256];std::string header;
            while(header.find("\r\n\r\n")==std::string::npos) {
                const auto count=::recv(client,response,sizeof(response),0);
                if(count<=0 || header.size()>1024)return 6;
                header.append(response,static_cast<std::size_t>(count));
            }
            if(!header.starts_with(truncated?"HTTP/1.1 400 ":"HTTP/1.1 200 "))return 8;
            channel.Close();
        }
        CloseSocket(client);
        if(channel.getChannelUriProperty()||HttpNotificationChannel::Find("Lifecycle"))return 7;
        channel.DispatchPendingNotificationsEXT();
        if(notifications!=(partial || truncated?0:1))return 9;
        channel.Close(); // idempotent; must not close a reused descriptor.
    }
    std::puts("NOTIFICATION_LIFECYCLE_PASS");
}
