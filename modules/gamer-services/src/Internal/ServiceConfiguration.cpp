// SPDX-License-Identifier: MS-PL
#include "CNA/GamerServices/Configuration.hpp"
#include "CnaService/Protocol.hpp"
#ifndef __EMSCRIPTEN__
#include <curl/curl.h>
#endif
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <memory>

namespace CNA::GamerServices {
namespace {
std::mutex mutex;
std::optional<Configuration> override;
std::string env(const char* name) { const auto* value=std::getenv(name);return value?value:""; }
void read(Configuration& config,const std::filesystem::path& path,bool required) {
    if(!std::filesystem::exists(path)) { if(required)throw CnaService::Error("CONFIGURATION_NOT_FOUND");return; }
    if(std::filesystem::file_size(path)>16384)throw CnaService::Error("INVALID_CONFIGURATION");
    std::ifstream input(path);std::string bytes((std::istreambuf_iterator<char>(input)),{});
    const auto j=CnaService::parse(bytes);
    if(!j.is_object())throw CnaService::Error("INVALID_CONFIGURATION");
    for(auto it=j.begin();it!=j.end();++it)
        if(it.key()!="endpoint"&&it.key()!="gameId"&&it.key()!="caBundle"&&it.key()!="insecureLoopback"&&
           it.key()!="avatarCatalogUpdates"&&it.key()!="maxAvatarCatalogBytes"&&it.key()!="titleVersion")throw CnaService::Error("INVALID_CONFIGURATION");
    if(j.contains("endpoint"))config.endpoint=CnaService::stringField(j,"endpoint",2048);
    if(j.contains("gameId"))config.gameId=CnaService::stringField(j,"gameId",64);
    if(j.contains("caBundle"))config.caBundle=CnaService::stringField(j,"caBundle",4096);
    if(j.contains("insecureLoopback")) {
        if(!j["insecureLoopback"].is_boolean())throw CnaService::Error("INVALID_CONFIGURATION");
        config.insecureLoopback=j["insecureLoopback"].get<bool>();
    }
    if(j.contains("avatarCatalogUpdates")) {
        if(!j["avatarCatalogUpdates"].is_boolean())throw CnaService::Error("INVALID_CONFIGURATION");
        config.avatarCatalogUpdates=j["avatarCatalogUpdates"].get<bool>();
    }
    if(j.contains("maxAvatarCatalogBytes")) {
        if(!j["maxAvatarCatalogBytes"].is_number_unsigned())throw CnaService::Error("INVALID_CONFIGURATION");
        config.maxAvatarCatalogBytes=j["maxAvatarCatalogBytes"].get<std::uint64_t>();
    }
    if(j.contains("titleVersion"))config.titleVersion=CnaService::stringField(j,"titleVersion",32);
}
#ifndef __EMSCRIPTEN__
std::string part(CURLU* url,CURLUPart field) {
    char* value=nullptr;
    if(curl_url_get(url,field,&value,0)!=CURLUE_OK)return {};
    std::string result=value;curl_free(value);return result;
}
#endif
}
void setConfigurationOverride(std::optional<Configuration> config) {
    if(config)validateConfiguration(*config);
    std::lock_guard lock(mutex);override=std::move(config);
}
void validateConfiguration(const Configuration& config) {
    if(!config.titleVersion.empty()&&!CnaService::validVersion(config.titleVersion))throw CnaService::Error("INVALID_CONFIGURATION");
    if(config.endpoint.empty())return;
#ifdef __EMSCRIPTEN__
    // Browser deployment/authentication is not implemented yet. Do not accept a
    // configuration that this target cannot use or weaken native URL validation.
    throw CnaService::Error("BROWSER_SERVICE_TRANSPORT_UNAVAILABLE");
#else
    if(!CnaService::identifier(config.gameId)||config.endpoint.size()>2048)throw CnaService::Error("INVALID_CONFIGURATION");
    std::unique_ptr<CURLU,decltype(&curl_url_cleanup)> url(curl_url(),curl_url_cleanup);
    if(!url||curl_url_set(url.get(),CURLUPART_URL,config.endpoint.c_str(),0)!=CURLUE_OK)throw CnaService::Error("INVALID_CONFIGURATION");
    const auto scheme=part(url.get(),CURLUPART_SCHEME),host=part(url.get(),CURLUPART_HOST);
    if(host.empty()||part(url.get(),CURLUPART_PATH)!="/cna/v1"||!part(url.get(),CURLUPART_USER).empty()||
       !part(url.get(),CURLUPART_PASSWORD).empty()||!part(url.get(),CURLUPART_QUERY).empty()||!part(url.get(),CURLUPART_FRAGMENT).empty())
        throw CnaService::Error("INVALID_CONFIGURATION");
    if(scheme!="https" && !(scheme=="http"&&config.insecureLoopback&&(host=="127.0.0.1"||host=="[::1]")))
        throw CnaService::Error("SECURE_TRANSPORT_REQUIRED");
#endif
}
Configuration resolveConfiguration() {
    {std::lock_guard lock(mutex);if(override)return *override;}
    Configuration config;
    auto base=env("XDG_CONFIG_HOME");if(base.empty()) {base=env("HOME");if(!base.empty())base+="/.config";}
    if(!base.empty())read(config,std::filesystem::path(base)/"cna/gamer-services.json",false);
    const auto manifest=env("CNA_GAMER_SERVICES_MANIFEST");
    read(config,manifest.empty()?std::filesystem::path("cna-title.json"):std::filesystem::path(manifest),!manifest.empty());
    for(auto [name,field]:{std::pair{"CNA_GAMER_SERVICES_ENDPOINT",&config.endpoint},std::pair{"CNA_GAME_ID",&config.gameId},
                           std::pair{"CNA_GAMER_SERVICES_CA_BUNDLE",&config.caBundle},std::pair{"CNA_GAME_VERSION",&config.titleVersion}}) {
        const auto value=env(name);if(!value.empty())*field=value;
    }
    const auto insecure=env("CNA_GAMER_SERVICES_INSECURE_LOOPBACK");
    if(!insecure.empty()) {if(insecure!="0"&&insecure!="1")throw CnaService::Error("INVALID_CONFIGURATION");config.insecureLoopback=insecure=="1";}
    const auto updates=env("CNA_AVATAR_CATALOG_UPDATES");
    if(!updates.empty()) {if(updates!="0"&&updates!="1")throw CnaService::Error("INVALID_CONFIGURATION");config.avatarCatalogUpdates=updates=="1";}
    validateConfiguration(config);return config;
}
}
