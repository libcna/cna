// SPDX-License-Identifier: MS-PL
#include "CredentialStore.hpp"
#include "CnaService/Protocol.hpp"
#include "System/Security/Cryptography/SHA256.hpp"
#include <atomic>
#include <cerrno>
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <vector>
#if defined(__unix__) || defined(__APPLE__)
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace CNA::Internal::GamerServices {
namespace {
bool tokenValid(const std::string& value) {
    return value.size()==64&&value.find_first_not_of("0123456789abcdef")==std::string::npos;
}
#if defined(__unix__) || defined(__APPLE__)
bool privateFile(int descriptor) {
    struct stat info{};
    return fstat(descriptor,&info)==0&&S_ISREG(info.st_mode)&&info.st_uid==geteuid()&&
        (info.st_mode&077)==0&&info.st_nlink==1;
}
struct File {
    int value;
    ~File(){if(value>=0)close(value);}
};
#endif
}
CredentialStore::CredentialStore(const CNA::GamerServices::Configuration& config) {
#if defined(__unix__) || defined(__APPLE__)
    try {
        if(config.endpoint.empty())return;
        const auto* selected=std::getenv("CNA_GAMER_SERVICES_CREDENTIALS_DIR");
        if(selected&&std::string(selected)=="0")return;
        std::filesystem::path root;
        if(selected&&*selected)root=selected;
        else if(const auto* state=std::getenv("XDG_STATE_HOME");state&&*state)root=std::filesystem::path(state)/"cna/gamer-services/credentials";
        else if(const auto* home=std::getenv("HOME");home&&*home)root=std::filesystem::path(home)/".local/state/cna/gamer-services/credentials";
        else return;
        const auto binding=config.endpoint+"\n"+config.gameId;
        System::Security::Cryptography::SHA256 hash;
        const auto digest=hash.ComputeHash(std::vector<unsigned char>(binding.begin(),binding.end()));
        constexpr char hex[]="0123456789abcdef";
        for(auto byte:digest){key_+=hex[byte>>4];key_+=hex[byte&15];}
        const bool created=std::filesystem::create_directories(root);
        directory_=open(root.c_str(),O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW);
        if(directory_<0)return;
        if(created&&fchmod(directory_,0700)!=0){close(directory_);directory_=-1;return;}
        struct stat directory{};
        if(fstat(directory_,&directory)!=0||directory.st_uid!=geteuid()||(directory.st_mode&077)!=0){close(directory_);directory_=-1;return;}
        lease_=openat(directory_,(key_+".lock").c_str(),O_RDWR|O_CREAT|O_CLOEXEC|O_NOFOLLOW,0600);
        if(lease_<0||!privateFile(lease_)||flock(lease_,LOCK_EX|LOCK_NB)!=0) {
            if(lease_>=0)close(lease_);close(directory_);lease_=-1;directory_=-1;
        }
    }catch(...) {
        if(lease_>=0)close(lease_);if(directory_>=0)close(directory_);lease_=-1;directory_=-1;
    }
#else
    (void)config;
#endif
}
CredentialStore::~CredentialStore() {
#if defined(__unix__) || defined(__APPLE__)
    if(lease_>=0)close(lease_);if(directory_>=0)close(directory_);
#endif
}
std::string CredentialStore::name(int slot) const {return key_+"-"+std::to_string(slot)+".json";}
std::optional<StoredCredential> CredentialStore::load(int slot) const {
#if defined(__unix__) || defined(__APPLE__)
    if(directory_<0||slot<0||slot>3)return {};
    try {
        File file{openat(directory_,name(slot).c_str(),O_RDONLY|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK)};
        if(file.value<0||!privateFile(file.value))return {};
        struct stat info{};if(fstat(file.value,&info)!=0||info.st_size<1||info.st_size>16384)return {};
        std::string bytes;char chunk[1024];
        for(;;) {
            const auto count=read(file.value,chunk,sizeof(chunk));
            if(count<0){if(errno==EINTR)continue;return {};}
            if(count==0)break;
            if(bytes.size()+static_cast<std::size_t>(count)>16384)return {};
            bytes.append(chunk,static_cast<std::size_t>(count));
        }
        const auto object=CnaService::parse(bytes);
        if(!object.is_object()||object.size()!=3||!object.contains("v")||!object["v"].is_number_integer()||object["v"]!=1||
            !object.contains("expires")||!object["expires"].is_number_integer()||object["expires"]<0||
            object["expires"]>std::numeric_limits<long long>::max())return {};
        StoredCredential result{CnaService::stringField(object,"refreshToken",64),object["expires"].get<long long>()};
        if(!tokenValid(result.refreshToken))return {};return result;
    }catch(...){return {};}
#else
    (void)slot;return {};
#endif
}
bool CredentialStore::save(int slot,const StoredCredential& value) const {
#if defined(__unix__) || defined(__APPLE__)
    if(directory_<0||slot<0||slot>3||!tokenValid(value.refreshToken)||value.expires<0)return false;
    static std::atomic<unsigned long long> counter=0;
    const auto temporary=name(slot)+"."+std::to_string(getpid())+"."+std::to_string(++counter)+".tmp";
    try {
        File file{openat(directory_,temporary.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC|O_NOFOLLOW,0600)};
        if(file.value<0)return false;
        const auto bytes=CnaService::Json{{"v",1},{"refreshToken",value.refreshToken},{"expires",value.expires}}.dump();
        std::size_t position=0;
        while(position<bytes.size()) {
            const auto count=write(file.value,bytes.data()+position,bytes.size()-position);
            if(count<0&&errno==EINTR)continue;
            if(count<=0){unlinkat(directory_,temporary.c_str(),0);return false;}
            position+=static_cast<std::size_t>(count);
        }
        if(fsync(file.value)!=0||renameat(directory_,temporary.c_str(),directory_,name(slot).c_str())!=0){unlinkat(directory_,temporary.c_str(),0);return false;}
        return fsync(directory_)==0;
    }catch(...){unlinkat(directory_,temporary.c_str(),0);return false;}
#else
    (void)slot;(void)value;return false;
#endif
}
void CredentialStore::remove(int slot) const {
#if defined(__unix__) || defined(__APPLE__)
    if(directory_>=0&&slot>=0&&slot<4){(void)unlinkat(directory_,name(slot).c_str(),0);(void)fsync(directory_);}
#else
    (void)slot;
#endif
}
}
