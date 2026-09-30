// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/GamerServices/VoiceMutes.hpp"
#include <map>
#include <mutex>
#include <utility>
#include <set>

namespace CNA::Internal::GamerServices {
namespace {
std::map<std::string,std::set<std::string>>& mutes(){static std::map<std::string,std::set<std::string>> value;return value;}
std::map<std::string,std::set<std::string>>& blocks(){static std::map<std::string,std::set<std::string>> value;return value;}
std::mutex& providerMutex(){static std::mutex value;return value;}
std::function<bool()>& provider(){static std::function<bool()> value;return value;}
}
std::function<bool()> setLocalVoiceCapabilityProvider(std::function<bool()> value)
{
    std::lock_guard lock(providerMutex());return std::exchange(provider(),std::move(value));
}
bool localVoiceCapable()
{
    std::function<bool()> test;
    {std::lock_guard lock(providerMutex());test=provider();}
    try{return test&&test();}catch(...){return false;}
}
bool voiceMuted(const std::string& localGamertag,const std::string& gamertag)
{
    const auto found=mutes().find(localGamertag);
    return (found!=mutes().end()&&found->second.contains(gamertag))||playerBlocked(localGamertag,gamertag);
}
bool playerBlocked(const std::string& localGamertag,const std::string& gamertag)
{
    const auto found=blocks().find(localGamertag);
    return found!=blocks().end()&&found->second.contains(gamertag);
}
void setPlayerBlocked(const std::string& localGamertag,const std::string& gamertag,bool blocked)
{
    if(blocked)blocks()[localGamertag].insert(gamertag);
    else if(auto found=blocks().find(localGamertag);found!=blocks().end())found->second.erase(gamertag);
}
void setBlockedPlayers(const std::string& localGamertag,const std::vector<std::string>& gamertags)
{
    blocks()[localGamertag]=std::set<std::string>(gamertags.begin(),gamertags.end());
}
void setVoiceMuted(const std::string& localGamertag,const std::string& gamertag,bool muted)
{
    if(muted)mutes()[localGamertag].insert(gamertag);
    else if(auto found=mutes().find(localGamertag);found!=mutes().end())found->second.erase(gamertag);
}
void resetVoiceMutesForTesting(){mutes().clear();blocks().clear();}
}
