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
    ASSERT_NE(nullptr,tls);
    ASSERT_TRUE(std::string(tls).starts_with("OpenSSL/"))<<"This isolated probe requires libcurl's OpenSSL transport.";
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
