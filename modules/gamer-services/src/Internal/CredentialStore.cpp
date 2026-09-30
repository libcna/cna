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
#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dpapi.h>
#elif defined(__unix__) || defined(__APPLE__)
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#endif
#if defined(__linux__)
#include <dlfcn.h>
#include <chrono>
#include <future>
#include <memory>
#include <mutex>
#include <thread>
#endif

namespace CNA::Internal::GamerServices {
namespace {
bool tokenValid(const std::string& value) {
    return value.size()==64&&value.find_first_not_of("0123456789abcdef")==std::string::npos;
}
constexpr std::size_t MaxRecordBytes=16384;
std::string encode(const StoredCredential& value) {
    return CnaService::Json{{"v",1},{"refreshToken",value.refreshToken},{"expires",value.expires}}.dump();
}
std::optional<StoredCredential> decode(const std::string& bytes) {
    try {
        if(bytes.empty()||bytes.size()>MaxRecordBytes)return {};
        const auto object=CnaService::parse(bytes);
        if(!object.is_object()||object.size()!=3||!object.contains("v")||!object["v"].is_number_integer()||object["v"]!=1||
            !object.contains("expires")||!object["expires"].is_number_integer()||object["expires"]<0||
            object["expires"]>std::numeric_limits<long long>::max())return {};
        StoredCredential result{CnaService::stringField(object,"refreshToken",64),object["expires"].get<long long>()};
        if(!tokenValid(result.refreshToken))return {};
        return result;
    }catch(...){return {};}
}
#if defined(__linux__)
bool keyringWanted() {
    const auto* value=std::getenv("CNA_GAMER_SERVICES_KEYRING");
    return !(value&&std::string(value)=="0");
}
// The freedesktop Secret Service through libsecret, loaded at run time: no build dependency, and a
// machine without it (headless, no session bus, no keyring daemon) keeps the private files. The
// declarations mirror libsecret-1's stable C ABI (SecretSchema is public API since 0.18).
struct SecretSchemaAttribute {const char* name;int type;};
struct SecretSchema {
    const char* name;int flags;SecretSchemaAttribute attributes[32];
    int reserved;void* reserved1;void* reserved2;void* reserved3;void* reserved4;void* reserved5;void* reserved6;void* reserved7;
};
struct GError {unsigned int domain;int code;char* message;};
class SecretService {
public:
    static SecretService& instance() {static SecretService value;return value;}
    bool available() const {return available_;}
    std::optional<std::string> lookup(const std::string& binding,int slot) {
        std::lock_guard lock(mutex_);GError* error=nullptr;
        const auto slotText=std::to_string(slot);
        char* secret=lookup_(&schema_,nullptr,&error,"binding",binding.c_str(),"slot",slotText.c_str(),nullptr);
        if(error){freeError_(error);return {};}
        if(!secret)return {};
        std::string value(secret);free_(secret);return value;
    }
    bool store(const std::string& binding,int slot,const std::string& value) {
        std::lock_guard lock(mutex_);GError* error=nullptr;
        const auto slotText=std::to_string(slot);
        const bool stored=store_(&schema_,nullptr,"CNA Gamer Services sign-in",value.c_str(),nullptr,&error,
            "binding",binding.c_str(),"slot",slotText.c_str(),nullptr)!=0;
        if(error){freeError_(error);return false;}
        return stored;
    }
    void clear(const std::string& binding,int slot) {
        std::lock_guard lock(mutex_);GError* error=nullptr;
        const auto slotText=std::to_string(slot);
        (void)clear_(&schema_,nullptr,&error,"binding",binding.c_str(),"slot",slotText.c_str(),nullptr);
        if(error)freeError_(error);
    }
private:
    SecretService() {
        schema_.name="com.libcna.GamerServices.Credential";schema_.flags=0;
        schema_.attributes[0]={"binding",0};schema_.attributes[1]={"slot",0};schema_.attributes[2]={nullptr,0};
        // Only with a session bus: libsecret would otherwise try to start one, which can hang.
        const auto* bus=std::getenv("DBUS_SESSION_BUS_ADDRESS");
        if(!bus||!*bus)return;
        handle_=dlopen("libsecret-1.so.0",RTLD_NOW|RTLD_LOCAL);
        if(!handle_)return;
        lookup_=reinterpret_cast<Lookup>(dlsym(handle_,"secret_password_lookup_sync"));
        store_=reinterpret_cast<Store>(dlsym(handle_,"secret_password_store_sync"));
        clear_=reinterpret_cast<Clear>(dlsym(handle_,"secret_password_clear_sync"));
        free_=reinterpret_cast<Free>(dlsym(handle_,"secret_password_free"));
        freeError_=reinterpret_cast<FreeError>(dlsym(handle_,"g_error_free"));
        if(!lookup_||!store_||!clear_||!free_||!freeError_)return;
        // A service that answers a lookup within two seconds is there and unlocked enough to use. A
        // broken one would hold every call for the bus timeout (25 s), so it counts as absent for
        // this process and the probe is left to finish on its own.
        auto answered=std::make_shared<std::promise<bool>>();auto result=answered->get_future();
        std::thread([this,answered] {
            GError* error=nullptr;
            char* probe=lookup_(&schema_,nullptr,&error,"binding","probe","slot","-1",nullptr);
            if(probe)free_(probe);
            const bool ok=error==nullptr;if(error)freeError_(error);
            answered->set_value(ok);
        }).detach();
        available_=result.wait_for(std::chrono::seconds(2))==std::future_status::ready&&result.get();
    }
    using Lookup=char*(*)(const SecretSchema*,void*,GError**,...);
    using Store=int(*)(const SecretSchema*,const char*,const char*,const char*,void*,GError**,...);
    using Clear=int(*)(const SecretSchema*,void*,GError**,...);
    using Free=void(*)(char*);
    using FreeError=void(*)(GError*);
    std::mutex mutex_;SecretSchema schema_{};void* handle_=nullptr;bool available_=false;
    Lookup lookup_=nullptr;Store store_=nullptr;Clear clear_=nullptr;Free free_=nullptr;FreeError freeError_=nullptr;
};
#endif

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

#if defined(_WIN32)
std::wstring wide(const std::filesystem::path& path) {return path.wstring();}
// DPAPI seals a record to this Windows user; the binding is the extra entropy, so a record copied
// to another title's name does not open.
std::optional<std::string> protect(const std::string& bytes,const std::string& binding,bool seal) {
    DATA_BLOB input{static_cast<DWORD>(bytes.size()),reinterpret_cast<BYTE*>(const_cast<char*>(bytes.data()))};
    DATA_BLOB entropy{static_cast<DWORD>(binding.size()),reinterpret_cast<BYTE*>(const_cast<char*>(binding.data()))};
    DATA_BLOB output{};
    const BOOL done=seal
        ?CryptProtectData(&input,L"CNA Gamer Services sign-in",&entropy,nullptr,nullptr,CRYPTPROTECT_UI_FORBIDDEN,&output)
        :CryptUnprotectData(&input,nullptr,&entropy,nullptr,nullptr,CRYPTPROTECT_UI_FORBIDDEN,&output);
    if(!done)return {};
    std::string result(reinterpret_cast<const char*>(output.pbData),output.cbData);
    SecureZeroMemory(output.pbData,output.cbData);LocalFree(output.pbData);
    return result;
}
std::optional<std::string> readFile(const std::filesystem::path& path) {
    const HANDLE file=CreateFileW(wide(path).c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE)return {};
    LARGE_INTEGER size{};std::string bytes;
    if(GetFileSizeEx(file,&size)&&size.QuadPart>0&&size.QuadPart<=static_cast<LONGLONG>(2*MaxRecordBytes)) {
        bytes.resize(static_cast<std::size_t>(size.QuadPart));DWORD read=0;
        if(!ReadFile(file,bytes.data(),static_cast<DWORD>(bytes.size()),&read,nullptr)||read!=bytes.size())bytes.clear();
    }
    CloseHandle(file);
    if(bytes.empty())return {};
    return bytes;
}
#endif
}

CredentialStore::CredentialStore(const CNA::GamerServices::Configuration& config) {
    if(config.endpoint.empty())return;
    const auto* selected=std::getenv("CNA_GAMER_SERVICES_CREDENTIALS_DIR");
    if(selected&&std::string(selected)=="0")return;
    try {
        const auto binding=config.endpoint+"\n"+config.gameId;
        System::Security::Cryptography::SHA256 hash;
        const auto digest=hash.ComputeHash(std::vector<unsigned char>(binding.begin(),binding.end()));
        constexpr char hex[]="0123456789abcdef";
        for(auto byte:digest){key_+=hex[byte>>4];key_+=hex[byte&15];}
    }catch(...){key_.clear();return;}
#if defined(_WIN32)
    try {
        std::filesystem::path root;
        if(selected&&*selected)root=selected;
        else if(const auto* local=std::getenv("LOCALAPPDATA");local&&*local)root=std::filesystem::path(local)/"cna/gamer-services/credentials";
        else return;
        std::filesystem::create_directories(root);
        // One process per endpoint and title: the lease is a file opened with no sharing, which
        // Windows closes when the process ends.
        const HANDLE lease=CreateFileW(wide(root/(key_+".lock")).c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_ALWAYS,
            FILE_ATTRIBUTE_NORMAL,nullptr);
        if(lease==INVALID_HANDLE_VALUE)return;
        lease_=lease;root_=root;
    }catch(...){}
#elif defined(__unix__) || defined(__APPLE__)
    try {
        std::filesystem::path root;
        if(selected&&*selected)root=selected;
        else if(const auto* state=std::getenv("XDG_STATE_HOME");state&&*state)root=std::filesystem::path(state)/"cna/gamer-services/credentials";
        else if(const auto* home=std::getenv("HOME");home&&*home)root=std::filesystem::path(home)/".local/state/cna/gamer-services/credentials";
        else return;
        const bool created=std::filesystem::create_directories(root);
        directory_=open(root.c_str(),O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW);
        if(directory_<0)return;
        if(created&&fchmod(directory_,0700)!=0){close(directory_);directory_=-1;return;}
        struct stat directory{};
        if(fstat(directory_,&directory)!=0||directory.st_uid!=geteuid()||(directory.st_mode&077)!=0){close(directory_);directory_=-1;return;}
        lease_=openat(directory_,(key_+".lock").c_str(),O_RDWR|O_CREAT|O_CLOEXEC|O_NOFOLLOW,0600);
        if(lease_<0||!privateFile(lease_)||flock(lease_,LOCK_EX|LOCK_NB)!=0) {
            if(lease_>=0)close(lease_);close(directory_);lease_=-1;directory_=-1;
            return;
        }
#if defined(__linux__)
        // A directory named in the environment is where records go (tests and operators choose
        // it); the keyring serves the default location.
        keyring_=!(selected&&*selected)&&keyringWanted()&&SecretService::instance().available();
#endif
    }catch(...) {
        if(lease_>=0)close(lease_);if(directory_>=0)close(directory_);lease_=-1;directory_=-1;
    }
#endif
}
CredentialStore::~CredentialStore() {
#if defined(_WIN32)
    if(lease_)CloseHandle(static_cast<HANDLE>(lease_));
#elif defined(__unix__) || defined(__APPLE__)
    if(lease_>=0)close(lease_);if(directory_>=0)close(directory_);
#endif
}
std::string CredentialStore::name(int slot) const {return key_+"-"+std::to_string(slot)+".json";}
const char* CredentialStore::protection() const {
#if defined(_WIN32)
    return lease_?"dpapi":"none";
#else
    return directory_<0?"none":keyring_?"secret-service":"private-file";
#endif
}
std::optional<StoredCredential> CredentialStore::load(int slot) const {
    if(slot<0||slot>3)return {};
#if defined(_WIN32)
    if(!lease_)return {};
    const auto sealed=readFile(root_/name(slot));
    if(!sealed)return {};
    const auto bytes=protect(*sealed,key_,false);
    if(!bytes)return {};
    return decode(*bytes);
#elif defined(__unix__) || defined(__APPLE__)
    if(directory_<0)return {};
    std::optional<StoredCredential> result;
#if defined(__linux__)
    if(keyring_) {
        if(const auto secret=SecretService::instance().lookup(key_,slot))return decode(*secret);
    }
#endif
    try {
        File file{openat(directory_,name(slot).c_str(),O_RDONLY|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK)};
        if(file.value<0||!privateFile(file.value))return {};
        struct stat info{};if(fstat(file.value,&info)!=0||info.st_size<1||info.st_size>static_cast<off_t>(MaxRecordBytes))return {};
        std::string bytes;char chunk[1024];
        for(;;) {
            const auto count=read(file.value,chunk,sizeof(chunk));
            if(count<0){if(errno==EINTR)continue;return {};}
            if(count==0)break;
            if(bytes.size()+static_cast<std::size_t>(count)>MaxRecordBytes)return {};
            bytes.append(chunk,static_cast<std::size_t>(count));
        }
        result=decode(bytes);
    }catch(...){return {};}
#if defined(__linux__)
    // A record kept in a file before the keyring was there moves into it.
    if(result&&keyring_&&SecretService::instance().store(key_,slot,encode(*result))) {
        (void)unlinkat(directory_,name(slot).c_str(),0);(void)fsync(directory_);
    }
#endif
    return result;
#else
    return {};
#endif
}
bool CredentialStore::save(int slot,const StoredCredential& value) const {
    if(slot<0||slot>3||!tokenValid(value.refreshToken)||value.expires<0)return false;
    const auto bytes=encode(value);
#if defined(_WIN32)
    if(!lease_)return false;
    const auto sealed=protect(bytes,key_,true);
    if(!sealed)return false;
    static std::atomic<unsigned long long> counter=0;
    const auto target=root_/name(slot);
    const auto temporary=root_/(name(slot)+"."+std::to_string(GetCurrentProcessId())+"."+std::to_string(++counter)+".tmp");
    const HANDLE file=CreateFileW(wide(temporary).c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE)return false;
    DWORD written=0;
    const bool whole=WriteFile(file,sealed->data(),static_cast<DWORD>(sealed->size()),&written,nullptr)&&written==sealed->size()&&FlushFileBuffers(file);
    CloseHandle(file);
    if(!whole||!MoveFileExW(wide(temporary).c_str(),wide(target).c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(wide(temporary).c_str());return false;
    }
    return true;
#elif defined(__unix__) || defined(__APPLE__)
    if(directory_<0)return false;
#if defined(__linux__)
    // A keyring that refuses the write (locked, no default collection) leaves the private file.
    if(keyring_&&SecretService::instance().store(key_,slot,bytes)) {
        (void)unlinkat(directory_,name(slot).c_str(),0);
        return true;
    }
#endif
    static std::atomic<unsigned long long> counter=0;
    const auto temporary=name(slot)+"."+std::to_string(getpid())+"."+std::to_string(++counter)+".tmp";
    try {
        File file{openat(directory_,temporary.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC|O_NOFOLLOW,0600)};
        if(file.value<0)return false;
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
    (void)bytes;return false;
#endif
}
void CredentialStore::remove(int slot) const {
    if(slot<0||slot>3)return;
#if defined(_WIN32)
    if(lease_)DeleteFileW(wide(root_/name(slot)).c_str());
#elif defined(__unix__) || defined(__APPLE__)
    if(directory_<0)return;
#if defined(__linux__)
    if(keyring_)SecretService::instance().clear(key_,slot);
#endif
    (void)unlinkat(directory_,name(slot).c_str(),0);(void)fsync(directory_);
#endif
}
}
