// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/GamerServices/LocalProfiles.hpp"
#include "CNA/Internal/GamerServices/AvatarDescriptionCodec.hpp"
#include "CnaService/Protocol.hpp"
#include "System/ArgumentException.hpp"
#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <random>
#include <system_error>
#if defined(__unix__) || defined(__APPLE__)
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

namespace CNA::Internal::GamerServices {
namespace {
constexpr std::uintmax_t MaxStoreBytes = 256 * 1024;
constexpr std::size_t AvatarBytes = static_cast<std::size_t>(Avatars::DescriptionSize);
std::mutex storeMutex;
// Profiles opened this run, so one the store could not keep still has its avatar.
std::vector<LocalProfile> opened;

std::string env(const char* name) {const auto* value=std::getenv(name);return value?value:"";}

std::string folded(const std::string& value) {
    std::string result=value;
    for(auto& c:result)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return result;
}

std::string hex(const std::vector<unsigned char>& bytes) {
    constexpr char digits[]="0123456789abcdef";
    std::string result;result.reserve(bytes.size()*2);
    for(auto byte:bytes){result+=digits[byte>>4];result+=digits[byte&15];}
    return result;
}

std::vector<unsigned char> avatarFromHex(const std::string& text) {
    if(text.size()!=AvatarBytes*2)return {};
    std::vector<unsigned char> bytes(AvatarBytes);
    auto nibble=[](char c)->int{return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:-1;};
    for(std::size_t i=0;i<bytes.size();++i) {
        const int high=nibble(text[2*i]),low=nibble(text[2*i+1]);
        if(high<0||low<0)return {};
        bytes[i]=static_cast<unsigned char>(high<<4|low);
    }
    return Avatars::decode(bytes)?bytes:std::vector<unsigned char>{};
}

std::vector<unsigned char> randomAvatar() {
    static std::mutex lock;
    static std::mt19937 random{std::random_device{}()};
    std::lock_guard guard(lock);
    return Avatars::encode(Avatars::randomDescriptor(std::nullopt,random));
}

// Each readable field of a stored gameDefaults object; anything else keeps its default.
LocalGameDefaults parseGameDefaults(const CnaService::Json& value) {
    LocalGameDefaults defaults;
    const auto choice=[&](const char* key,std::initializer_list<const char*> names,int& target) {
        if(!value.contains(key)||!value[key].is_string())return;
        int index=0;
        for(const auto* name:names){if(value[key].get<std::string>()==name){target=index;return;}++index;}
    };
    choice("gameDifficulty",{"Easy","Normal","Hard"},defaults.gameDifficulty);
    choice("controllerSensitivity",{"Low","Medium","High"},defaults.controllerSensitivity);
    choice("racingCameraAngle",{"Back","Front","Inside"},defaults.racingCameraAngle);
    for(auto [key,target]:{std::pair{"autoAim",&defaults.autoAim},std::pair{"autoCenter",&defaults.autoCenter},
        std::pair{"moveWithRightThumbStick",&defaults.moveWithRightThumbStick},std::pair{"invertYAxis",&defaults.invertYAxis},
        std::pair{"manualTransmission",&defaults.manualTransmission},std::pair{"accelerateWithButtons",&defaults.accelerateWithButtons},
        std::pair{"brakeWithButtons",&defaults.brakeWithButtons}})
        if(value.contains(key)&&value[key].is_boolean())*target=value[key].get<bool>();
    // Colors are "#rrggbb".
    const auto color=[&](const char* key,std::optional<std::array<unsigned char,3>>& target) {
        if(!value.contains(key)||!value[key].is_string())return;
        const auto text=value[key].get<std::string>();
        if(text.size()!=7||text[0]!='#'||text.find_first_not_of("0123456789abcdefABCDEF",1)!=std::string::npos)return;
        std::array<unsigned char,3> rgb{};
        for(std::size_t i=0;i<3;++i)rgb[i]=static_cast<unsigned char>(std::stoul(text.substr(1+2*i,2),nullptr,16));
        target=rgb;
    };
    color("primaryColor",defaults.primaryColor);
    color("secondaryColor",defaults.secondaryColor);
    return defaults;
}

}

LocalGameDefaults parseGameDefaultsJson(std::string_view json) {
    const auto value=CnaService::Json::parse(json,nullptr,false);
    return value.is_object()?parseGameDefaults(value):LocalGameDefaults{};
}

namespace {
// The whole store, or nullopt when a file exists that is not a store this version can read.
std::optional<std::vector<LocalProfile>> readStore(const std::filesystem::path& path) {
    std::error_code error;
    if(path.empty()||!std::filesystem::exists(path,error))return std::vector<LocalProfile>{};
    const auto size=std::filesystem::file_size(path,error);
    if(error||size>MaxStoreBytes)return std::nullopt;
    std::ifstream input(path,std::ios::binary);
    std::string bytes((std::istreambuf_iterator<char>(input)),{});
    if(!input.good()&&!input.eof())return std::nullopt;
    const auto store=CnaService::Json::parse(bytes,nullptr,false);
    if(store.is_discarded()||!store.is_object()||!store.contains("version")||store["version"]!=1||
       !store.contains("profiles")||!store["profiles"].is_array())return std::nullopt;
    std::vector<LocalProfile> profiles;
    for(const auto& entry:store["profiles"]) {
        if(profiles.size()==MaxLocalProfiles)break;
        if(!entry.is_object()||!entry.contains("gamertag")||!entry["gamertag"].is_string())continue;
        LocalProfile profile;profile.gamertag=entry["gamertag"].get<std::string>();
        if(!isValidLocalGamertag(profile.gamertag))continue;
        if(std::any_of(profiles.begin(),profiles.end(),[&](const auto& p){return folded(p.gamertag)==folded(profile.gamertag);}))continue;
        if(entry.contains("autoSignIn")&&entry["autoSignIn"].is_boolean())profile.autoSignIn=entry["autoSignIn"].get<bool>();
        if(entry.contains("avatar")&&entry["avatar"].is_string())profile.avatar=avatarFromHex(entry["avatar"].get<std::string>());
        if(entry.contains("gameDefaults")&&entry["gameDefaults"].is_object()) {
            profile.gameDefaults=parseGameDefaults(entry["gameDefaults"]);
            profile.gameDefaultsJson=entry["gameDefaults"].dump();
        }
        profiles.push_back(std::move(profile));
    }
    return profiles;
}

bool writeStore(const std::filesystem::path& path,const std::vector<LocalProfile>& profiles) {
    auto entries=CnaService::Json::array();
    for(const auto& profile:profiles) {
        CnaService::Json entry{{"gamertag",profile.gamertag},{"autoSignIn",profile.autoSignIn}};
        if(!profile.avatar.empty())entry["avatar"]=hex(profile.avatar);
        if(!profile.gameDefaultsJson.empty())entry["gameDefaults"]=CnaService::Json::parse(profile.gameDefaultsJson);
        entries.push_back(std::move(entry));
    }
    const auto bytes=CnaService::Json{{"version",1},{"profiles",std::move(entries)}}.dump(1)+"\n";
    static std::atomic<unsigned long long> counter=0;
#if defined(__unix__) || defined(__APPLE__)
    const auto temporary=path.string()+"."+std::to_string(getpid())+"."+std::to_string(++counter)+".tmp";
#else
    const auto temporary=path.string()+"."+std::to_string(++counter)+".tmp";
#endif
    std::error_code error;
    {
        std::ofstream output(temporary,std::ios::binary|std::ios::trunc);
        output<<bytes;output.flush();
        if(!output){std::filesystem::remove(temporary,error);return false;}
    }
    std::filesystem::rename(temporary,path,error);
    if(error){std::filesystem::remove(temporary,error);return false;}
    return true;
}

// Serializes read-modify-write between processes sharing the store (two local players starting
// at once would otherwise each keep only their own new profile).
class StoreLock {
public:
    explicit StoreLock(const std::filesystem::path& path) {
#if defined(__unix__) || defined(__APPLE__)
        if(path.empty())return;
        descriptor_=open((path.string()+".lock").c_str(),O_RDWR|O_CREAT|O_CLOEXEC|O_NOFOLLOW,0600);
        if(descriptor_>=0&&flock(descriptor_,LOCK_EX)!=0){close(descriptor_);descriptor_=-1;}
#else
        (void)path;
#endif
    }
    ~StoreLock() {
#if defined(__unix__) || defined(__APPLE__)
        if(descriptor_>=0)close(descriptor_);
#endif
    }
    StoreLock(const StoreLock&)=delete;
    StoreLock& operator=(const StoreLock&)=delete;
private:
    int descriptor_=-1;
};

bool prepareDirectory(const std::filesystem::path& path) {
    std::error_code error;
    const auto directory=path.parent_path();
    const bool created=std::filesystem::create_directories(directory,error);
    if(error)return false;
    if(created)std::filesystem::permissions(directory,std::filesystem::perms::owner_all,std::filesystem::perm_options::replace,error);
    return std::filesystem::is_directory(directory,error);
}

// Finds or creates the stored profile; the caller holds storeMutex.
LocalProfile openStored(const std::string& gamertag) {
    LocalProfile created;created.gamertag=gamertag;created.avatar=randomAvatar();
    for(const auto& profile:opened)if(folded(profile.gamertag)==folded(gamertag))created=profile;
    try {
        const auto path=localProfilesPath();
        // The directory must exist before the lock file can be taken in it.
        const bool writable=!path.empty()&&prepareDirectory(path);
        StoreLock lock(writable?path:std::filesystem::path{});
        auto stored=readStore(path);
        if(stored)for(const auto& profile:*stored)if(folded(profile.gamertag)==folded(gamertag)) {
            if(!profile.avatar.empty())return profile;
            // A profile whose avatar was lost gets a new one, kept from now on.
            auto repaired=profile;repaired.avatar=created.avatar;
            auto profiles=*stored;
            for(auto& entry:profiles)if(folded(entry.gamertag)==folded(gamertag))entry=repaired;
            if(writable)(void)writeStore(path,profiles);
            return repaired;
        }
        if(stored&&stored->size()<MaxLocalProfiles&&writable) {
            stored->push_back(created);(void)writeStore(path,*stored);
        }
    }catch(...) {}
    return created;
}
}

bool isValidLocalGamertag(const std::string& gamertag) {
    if(gamertag.empty()||gamertag.size()>15||!std::isalpha(static_cast<unsigned char>(gamertag.front()))||gamertag.back()==' ')return false;
    for(std::size_t i=0;i<gamertag.size();++i) {
        const auto c=static_cast<unsigned char>(gamertag[i]);
        if(c==' '){if(gamertag[i-1]==' ')return false;continue;}
        if(!std::isalnum(c)||c>127)return false;
    }
    return true;
}

std::filesystem::path localProfilesPath() {
    if(const auto selected=env("CNA_GAMER_SERVICES_PROFILES_DIR");!selected.empty())return std::filesystem::path(selected)/"profiles.json";
    if(const auto data=env("XDG_DATA_HOME");!data.empty())return std::filesystem::path(data)/"cna/gamer-services/profiles.json";
    if(const auto home=env("HOME");!home.empty())return std::filesystem::path(home)/".local/share/cna/gamer-services/profiles.json";
    if(const auto local=env("LOCALAPPDATA");!local.empty())return std::filesystem::path(local)/"CNA/gamer-services/profiles.json";
    return {};
}

std::vector<LocalProfile> loadLocalProfiles() {
    std::lock_guard guard(storeMutex);
    try {return readStore(localProfilesPath()).value_or(std::vector<LocalProfile>{});}
    catch(...) {return {};}
}

LocalProfile openLocalProfile(const std::string& gamertag) {
    if(!isValidLocalGamertag(gamertag))
        throw System::ArgumentException("Profile names are 1 to 15 letters, digits and single spaces, starting with a letter.","gamertag");
    std::lock_guard guard(storeMutex);
    auto profile=openStored(gamertag);
    std::erase_if(opened,[&](const auto& entry){return folded(entry.gamertag)==folded(profile.gamertag);});
    opened.push_back(profile);
    return profile;
}

std::vector<LocalProfile> autoSignInLocalProfiles() {
    const auto names=env("CNA_GAMER_SERVICES_AUTO_SIGN_IN");
    std::vector<LocalProfile> result;
    if(names.empty()) {
        for(auto& profile:loadLocalProfiles())if(profile.autoSignIn&&result.size()<4)result.push_back(std::move(profile));
        return result;
    }
    std::vector<std::string> requested;
    std::size_t start=0;
    while(start<=names.size()) {
        auto end=names.find(',',start);if(end==std::string::npos)end=names.size();
        auto name=names.substr(start,end-start);
        const auto first=name.find_first_not_of(" \t"),last=name.find_last_not_of(" \t");
        name=first==std::string::npos?std::string{}:name.substr(first,last-first+1);
        if(!isValidLocalGamertag(name)||requested.size()==4||
           std::any_of(requested.begin(),requested.end(),[&](const auto& other){return folded(other)==folded(name);}))
            throw CnaService::Error("INVALID_CONFIGURATION");
        requested.push_back(std::move(name));
        start=end+1;
    }
    for(const auto& name:requested)result.push_back(openLocalProfile(name));
    return result;
}

std::optional<LocalProfile> findLocalProfile(const std::string& gamertag) {
    for(const auto& profile:loadLocalProfiles())if(folded(profile.gamertag)==folded(gamertag))return profile;
    // A profile created this run in a store that could not be written.
    std::lock_guard guard(storeMutex);
    for(const auto& profile:opened)if(folded(profile.gamertag)==folded(gamertag))return profile;
    return std::nullopt;
}

std::vector<unsigned char> localProfileAvatar(const std::string& gamertag) {
    // The store first: another process (the avatar editor) may have changed it.
    for(const auto& profile:loadLocalProfiles())if(folded(profile.gamertag)==folded(gamertag)&&!profile.avatar.empty())return profile.avatar;
    std::lock_guard guard(storeMutex);
    for(const auto& profile:opened)if(folded(profile.gamertag)==folded(gamertag))return profile.avatar;
    return {};
}

bool setLocalProfileAvatar(const std::string& gamertag,const std::vector<unsigned char>& description) {
    if(description.size()!=AvatarBytes||!Avatars::decode(description))return false;
    std::lock_guard guard(storeMutex);
    for(auto& profile:opened)if(folded(profile.gamertag)==folded(gamertag))profile.avatar=description;
    try {
        const auto path=localProfilesPath();
        if(path.empty()||!prepareDirectory(path))return false;
        StoreLock lock(path);
        auto stored=readStore(path);
        if(!stored)return false;
        for(auto& profile:*stored)
            if(folded(profile.gamertag)==folded(gamertag)) {
                profile.avatar=description;
                return writeStore(path,*stored);
            }
    }catch(...) {}
    return false;
}
}
