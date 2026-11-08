// SPDX-License-Identifier: MS-PL
#include "CNA/GamerServices/Configuration.hpp"
#include "CNA/Internal/GamerServices/IGamerServicesBackend.hpp"
#include "System/Environment.hpp"
#include <gtest/gtest.h>
#include <curl/curl.h>
#include <openssl/crypto.h>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

namespace {
namespace Service = CNA::Internal::GamerServices;
std::weak_ptr<Service::IGamerServicesBackend> observedBackend;
std::atomic<bool> workerStopped{false};
bool transportUnavailable=false;

void cryptoCleanup() {
    // Oracle: OpenSSL's process cleanup must follow destruction of its users.
    // A weak reference proves backend destruction; the TLS destructor proves
    // the worker was joined. This check runs after Google Test returns.
    if(!observedBackend.expired() || !workerStopped.load()) {
        std::fputs("OpenSSL cleanup preceded backend/thread destruction\n",stderr);
        std::abort();
    }
    std::fputs("backend and worker stopped before OpenSSL cleanup\n",stderr);
}

TEST(ServiceTransportLifetime, BackendStopsBeforeOpenSslProcessCleanup) {
    CNA::GamerServices::Configuration configuration;
    if(const auto* endpoint=std::getenv("CNA_GAMER_SERVICES_ENDPOINT");endpoint&&*endpoint) {
        ASSERT_TRUE(std::string(endpoint).starts_with("https://localhost:"));
        configuration.endpoint=endpoint;
        configuration.gameId="net-lsan";
        const auto* ca=std::getenv("CNA_GAMER_SERVICES_CA_BUNDLE");
        ASSERT_NE(nullptr,ca);
        configuration.caBundle=ca;
    }
    // This dedicated process never reads a user's deployment/account settings.
    CNA::GamerServices::setConfigurationOverride(configuration);
    System::Environment::SetEnvironmentVariable("CNA_GAMER_SERVICES_CREDENTIALS_DIR",std::string("0"));
    auto backend = Service::backend();
    const auto* tls=curl_version_info(CURLVERSION_NOW)->ssl_version;
    if(!tls || !std::string(tls).starts_with("OpenSSL/")) {
        transportUnavailable=true;
        GTEST_SKIP()<<"service transport lifetime requires libcurl with OpenSSL TLS";
    }
    observedBackend = backend;
    std::atomic<bool> initialized{false};
    std::atomic<int> initializationResult{-1};
    backend->submit([&] {
        struct WorkerLifetime {~WorkerLifetime(){workerStopped=true;}};
        thread_local WorkerLifetime lifetime;
        (void)lifetime;
        // Reproduce initialization on the backend worker without a socket.
        // With the old namespace-static registry, OpenSSL's atexit handler is
        // registered later and runs while this worker and its handles live.
        const auto code = curl_global_init(CURL_GLOBAL_DEFAULT);
        initializationResult = code == CURLE_OK ? OPENSSL_atexit(cryptoCleanup) : 0;
        if(code == CURLE_OK)curl_global_cleanup();
        initialized=true;
    },[]{});
    auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
    while(!initialized && std::chrono::steady_clock::now()<deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    // Keep captured state alive until the worker completes, even on failure.
    if(!initialized)std::abort();
    ASSERT_EQ(1,initializationResult.load());

    // Optional real transport mode: a local fake TLS service supplies the
    // events capability. The parent waits for the event connection before
    // allowing process exit, covering connected and interrupted handshakes.
    if(!configuration.endpoint.empty()) {
        backend->signIn(0,"alice","alice-password");
        bool signedIn=false;
        deadline=std::chrono::steady_clock::now()+std::chrono::seconds(15);
        while(!signedIn && std::chrono::steady_clock::now()<deadline) {
            for(const auto& event:backend->pump()) {
                ASSERT_NE(Service::BackendEvent::Type::Failed,event.type);
                signedIn |= event.type==Service::BackendEvent::Type::SignedIn;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        ASSERT_TRUE(signedIn);
        (void)backend->pump();
        std::cout<<"signed-in\n"<<std::flush;
        std::string command;
        ASSERT_TRUE(static_cast<bool>(std::getline(std::cin,command)));
        ASSERT_EQ("stop",command);
    }
    // Leave the registry as the final owner, as a game returning from main does.
}
}

// CTest's skip code describes a missing transport prerequisite, never a failed
// shutdown assertion. The Python fixture probes this before opening any sockets.
int main(int argc, char** argv) {
    if(argc==2 && std::string(argv[1])=="--check-tls-prerequisites") {
        const auto* version=curl_version_info(CURLVERSION_NOW);
        if(!version || !version->ssl_version || !std::string(version->ssl_version).starts_with("OpenSSL/")) {
            std::fputs("SKIP: service transport lifetime requires libcurl with OpenSSL TLS\n",stderr);
            return 77;
        }
        bool https=false, wss=false;
        for(const char* const* protocol=version->protocols;protocol && *protocol;++protocol) {
            https |= std::string(*protocol)=="https";
            wss |= std::string(*protocol)=="wss";
        }
        if(!https || !wss) {
            std::fputs("SKIP: service transport TLS scenarios require libcurl HTTPS and WSS support\n",stderr);
            return 77;
        }
        return 0;
    }
    // Preserve the lifetime oracle's initialization order: the test creates the
    // backend registry before querying curl or initializing OpenSSL on its worker.
    ::testing::InitGoogleTest(&argc,argv);
    const int result=RUN_ALL_TESTS();
    return result==0 && transportUnavailable ? 77 : result;
}
