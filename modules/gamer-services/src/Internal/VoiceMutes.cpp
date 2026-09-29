// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/GamerServices/VoiceMutes.hpp"
#include <map>
#include <set>

namespace CNA::Internal::GamerServices {
namespace {
std::map<std::string,std::set<std::string>>& mutes(){static std::map<std::string,std::set<std::string>> value;return value;}
}
bool voiceMuted(const std::string& localGamertag,const std::string& gamertag)
{
    const auto found=mutes().find(localGamertag);
    return found!=mutes().end()&&found->second.contains(gamertag);
}
void setVoiceMuted(const std::string& localGamertag,const std::string& gamertag,bool muted)
{
    if(muted)mutes()[localGamertag].insert(gamertag);
    else if(auto found=mutes().find(localGamertag);found!=mutes().end())found->second.erase(gamertag);
}
void resetVoiceMutesForTesting(){mutes().clear();}
}
