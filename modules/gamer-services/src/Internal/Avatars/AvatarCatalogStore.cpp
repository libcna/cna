// SPDX-License-Identifier: MS-PL
// Installed avatar catalog packs (Layer B of docs/avatars.md): catalogs this release does not
// compile in, installed whole, validated before use, and kept.
#include "CNA/Internal/GamerServices/AvatarAssets.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <map>
#include <mutex>
#include <random>
#include <stdexcept>
#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

namespace CNA::Internal::GamerServices::Avatars {
namespace {
namespace fs = std::filesystem;

// Far above any real catalog (v2 is 14 MB), so a hostile descriptor cannot fill a disk.
constexpr std::uint64_t HardMaximumPackBytes=std::uint64_t{256}<<20;
// Staging left by an interrupted install is reused by a retry; after a week it is abandoned.
constexpr auto AbandonedStagingAge=std::chrono::hours(24*7);

std::mutex storeLock;
bool rootOverridden=false;
fs::path rootOverride;
std::map<std::uint16_t,std::shared_ptr<const CatalogManifest>> loaded;

[[noreturn]] void invalid(const std::string& what){throw std::runtime_error("avatar catalog pack: "+what);}

// One installer per catalog version at a time, across threads and processes: without it, the
// installer that activates a pack removes the staging of another still writing the same pack.
// The operating system releases the lock when its holder ends. Best effort: a lock file that
// cannot be opened leaves the install unserialized.
class InstallLock {
public:
    InstallLock(const fs::path& root,std::uint16_t version) {
        const auto path=root/(".install-v"+std::to_string(version)+".lock");
#if defined(_WIN32)
        handle_=CreateFileW(path.wstring().c_str(),GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
        OVERLAPPED whole{};
        if(handle_!=INVALID_HANDLE_VALUE&&!LockFileEx(handle_,LOCKFILE_EXCLUSIVE_LOCK,0,1,0,&whole)){CloseHandle(handle_);handle_=INVALID_HANDLE_VALUE;}
#else
        descriptor_=::open(path.c_str(),O_RDWR|O_CREAT|O_CLOEXEC,0600);
        while(descriptor_>=0&&::flock(descriptor_,LOCK_EX)!=0) {
            if(errno!=EINTR){::close(descriptor_);descriptor_=-1;}
        }
#endif
    }
    ~InstallLock() {
#if defined(_WIN32)
        if(handle_!=INVALID_HANDLE_VALUE){OVERLAPPED whole{};UnlockFileEx(handle_,0,1,0,&whole);CloseHandle(handle_);}
#else
        if(descriptor_>=0)::close(descriptor_);
#endif
    }
    InstallLock(const InstallLock&)=delete;
    InstallLock& operator=(const InstallLock&)=delete;
private:
#if defined(_WIN32)
    HANDLE handle_=INVALID_HANDLE_VALUE;
#else
    int descriptor_=-1;
#endif
};

bool isHash(std::string_view text)
{
    return text.size()==64&&text.find_first_not_of("0123456789abcdef")==std::string_view::npos;
}

std::string hashOf(std::string_view text)
{
    return sha256Hex(std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(text.data()),text.size()));
}

std::optional<std::string> readText(const fs::path& path,std::uintmax_t limit)
{
    std::error_code error;
    if(fs::is_symlink(path,error)||!fs::is_regular_file(path,error))return std::nullopt;
    const auto size=fs::file_size(path,error);
    if(error||size>limit)return std::nullopt;
    std::string text(static_cast<std::size_t>(size),'\0');
    std::ifstream stream(path,std::ios::binary);
    if(!stream.read(text.data(),static_cast<std::streamsize>(text.size())))return std::nullopt;
    return text;
}

bool writeFile(const fs::path& path,std::span<const std::uint8_t> bytes)
{
    // Written beside its name and renamed, so a file under its final name is always whole.
    const auto partial=fs::path(path).concat(".part");
    {
        std::ofstream output(partial,std::ios::binary|std::ios::trunc);
        output.write(reinterpret_cast<const char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));
        if(!output.flush())return false;
    }
    std::error_code error;
    fs::rename(partial,path,error);
    return !error;
}

std::optional<std::vector<std::uint8_t>> readVerified(const fs::path& path,const CatalogAsset& asset)
{
    const auto text=readText(path,asset.size);
    if(!text||text->size()!=asset.size||hashOf(*text)!=asset.sha256)return std::nullopt;
    return std::vector<std::uint8_t>(text->begin(),text->end());
}

fs::path versionDirectory(const fs::path& root,std::uint16_t version){return root/("v"+std::to_string(version));}

std::string stagingPrefix(std::uint16_t version,const std::string& manifestSha256)
{
    return ".staging-v"+std::to_string(version)+"-"+manifestSha256.substr(0,16)+"-";
}

std::string uniqueTag()
{
    static std::atomic<unsigned> counter{0};
    std::random_device device;
    return std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+"-"+std::to_string(device())+"-"+
        std::to_string(++counter);
}

// The installed pack's own record, checked against its manifest.
std::shared_ptr<const CatalogManifest> loadInstalled(const fs::path& root,std::uint16_t version)
{
    const auto directory=versionDirectory(root,version);
    const auto record=readText(directory/"pack.json",4096);
    const auto manifestText=readText(directory/"catalog.json",1u<<20);
    if(!record||!manifestText)return nullptr;
    try {
        const auto json=nlohmann::json::parse(*record);
        if(json.at("packFormat")!=CatalogPackFormat||json.at("version")!=version||json.at("reader").get<int>()>CatalogReaderLevel||
           json.at("manifestSha256")!=hashOf(*manifestText))
            return nullptr;
        auto manifest=std::make_shared<const CatalogManifest>(parseManifest(*manifestText));
        return manifest->version==version?manifest:nullptr;
    } catch(const std::exception&) {
        return nullptr;
    }
}
}

CatalogPack parseCatalogPack(std::string_view text)
{
    CatalogPack pack;
    try {
        const auto json=nlohmann::json::parse(text);
        const int version=json.at("version").get<int>();
        if(version<1||version>0xffff)invalid("version");
        pack.version=static_cast<std::uint16_t>(version);
        pack.packFormat=json.at("packFormat").get<int>();
        pack.reader=json.at("reader").get<int>();
        for(const auto& format:json.at("descriptionFormats"))pack.descriptionFormats.push_back(format.get<int>());
        pack.manifestSha256=json.at("manifestSha256").get<std::string>();
        pack.manifestSize=json.at("manifestSize").get<std::size_t>();
        pack.totalBytes=json.at("totalBytes").get<std::uint64_t>();
    } catch(const nlohmann::json::exception&) {
        invalid("malformed descriptor");
    }
    if(!isHash(pack.manifestSha256)||pack.manifestSize==0||pack.manifestSize>(1u<<20))invalid("manifest reference");
    return pack;
}

void attachCatalogManifest(CatalogPack& pack,std::string manifest)
{
    if(manifest.size()!=pack.manifestSize||hashOf(manifest)!=pack.manifestSha256)invalid("manifest does not match its hash");
    const auto parsed=parseManifest(manifest);
    if(parsed.version!=pack.version)invalid("manifest describes another version");
    std::uint64_t total=0;
    for(const auto& [name,asset]:parsed.assets)total+=asset.size;
    if(total!=pack.totalBytes)invalid("total size does not match the manifest");
    pack.manifest=std::move(manifest);
}

void validateCatalog(const CatalogManifest& manifest,const std::function<std::span<const std::uint8_t>(const CatalogAsset&)>& file)
{
    std::map<std::string,AvatarGlb,std::less<>> models;
    int atlasWidth=0,atlasHeight=0;
    for(const auto& [name,asset]:manifest.assets) {
        const auto bytes=file(asset);
        if(name.ends_with(".glb")) {
            auto glb=parseAvatarGlb(bytes);
            for(const auto& primitive:glb.primitives)
                if(!primitive.texturePng.empty())(void)decodeAvatarImage(primitive.texturePng);
            models.emplace(name,std::move(glb));
        } else if(name.ends_with(".png")) {
            const auto image=decodeAvatarImage(bytes);
            if(name==manifest.faceAsset){atlasWidth=image.width;atlasHeight=image.height;}
        } else {
            invalid("unsupported file "+name);
        }
    }
    auto model=[&](const std::string& name)->const AvatarGlb& {
        auto found=models.find(name);
        if(found==models.end())invalid(name+" is not a model");
        return found->second;
    };
    for(int body=0;body<2;++body)
        if(model(manifest.bodies[body]).primitives.empty())invalid("a body has no geometry");
    auto fitted=[&](const std::array<std::string,2>& assets) {
        for(int body=0;body<2;++body) {
            const auto& item=model(assets[body]);
            if(item.primitives.empty())invalid(assets[body]+" has no geometry");
            for(int bone=0;bone<BoneCount;++bone)
                if(Microsoft::Xna::Framework::Vector3::Distance(item.bindTranslations[bone],model(manifest.bodies[body]).bindTranslations[bone])>1e-3f)
                    invalid(assets[body]+" is not fitted to its body");
        }
    };
    for(const auto* list:{&manifest.items,&manifest.featureItems})
        for(const auto& item:*list) {
            fitted(item.assets);
            if(!item.hatAssets[0].empty())fitted(item.hatAssets);
        }
    const auto& face=manifest.face;
    if(atlasWidth!=face.columns*face.tileSize||atlasHeight%face.tileSize||maximumFaceTile(face)>=face.columns*(atlasHeight/face.tileSize))
        invalid("face atlas does not match its layout");
    if(model(manifest.animationsAsset).clips.size()!=31)invalid("animations must have every preset");
}

fs::path installedCatalogRoot()
{
    {
        std::lock_guard guard(storeLock);
        if(rootOverridden)return rootOverride;
    }
    if(const auto* configured=std::getenv("CNA_GAMER_SERVICES_CATALOGS_DIR");configured&&*configured)return configured;
#ifdef _WIN32
    if(const auto* local=std::getenv("LOCALAPPDATA");local&&*local)return fs::path(local)/"cna/avatar-catalogs";
#endif
    if(const auto* xdg=std::getenv("XDG_DATA_HOME");xdg&&*xdg)return fs::path(xdg)/"cna/avatar-catalogs";
    if(const auto* home=std::getenv("HOME");home&&*home)return fs::path(home)/".local/share/cna/avatar-catalogs";
    return {};
}

void setInstalledCatalogRootForTesting(fs::path root)
{
    {
        std::lock_guard guard(storeLock);
        rootOverridden=!root.empty();
        rootOverride=std::move(root);
        loaded.clear();
    }
    forgetCatalogUpdateFailures();
}

std::shared_ptr<const CatalogManifest> installedManifest(std::uint16_t version)
{
    const auto root=installedCatalogRoot();
    if(root.empty())return nullptr;
    {
        std::lock_guard guard(storeLock);
        if(auto found=loaded.find(version);found!=loaded.end())return found->second;
    }
    auto manifest=loadInstalled(root,version);
    if(!manifest)return nullptr;
    std::lock_guard guard(storeLock);
    return loaded.emplace(version,std::move(manifest)).first->second;
}

std::optional<std::vector<std::uint8_t>> installedCatalogFile(std::uint16_t version,const CatalogAsset& asset)
{
    const auto root=installedCatalogRoot();
    if(root.empty()||!isHash(asset.sha256))return std::nullopt;
    return readVerified(versionDirectory(root,version)/asset.sha256,asset);
}

std::vector<std::uint16_t> availableCatalogVersions()
{
    std::vector<std::uint16_t> versions;
    for(const auto& catalog:embeddedCatalogs())versions.push_back(catalog->version);
    const auto root=installedCatalogRoot();
    std::error_code error;
    if(!root.empty())
        for(fs::directory_iterator entry(root,error),end;!error&&entry!=end;entry.increment(error)) {
            const auto name=entry->path().filename().string();
            if(name.size()<2||name[0]!='v'||name.find_first_not_of("0123456789",1)!=std::string::npos||name.size()>6)continue;
            const auto version=std::stoul(name.substr(1));
            if(version<1||version>0xffff||std::ranges::find(versions,version)!=versions.end())continue;
            if(installedManifest(static_cast<std::uint16_t>(version)))versions.push_back(static_cast<std::uint16_t>(version));
        }
    std::ranges::sort(versions);
    return versions;
}

CatalogInstall installCatalogPack(const CatalogPack& pack,std::uint64_t maximumBytes,
    const std::function<std::vector<std::uint8_t>(const CatalogAsset&)>& fetch,std::string* error)
{
    auto fail=[&](CatalogInstall outcome,const std::string& why) {
        if(error)*error=why;
        return outcome;
    };
    const auto root=installedCatalogRoot();
    if(root.empty())return fail(CatalogInstall::Refused,"no catalog directory is configured");
    if(pack.packFormat!=CatalogPackFormat)return fail(CatalogInstall::Refused,"unknown pack format");
    if(pack.reader>CatalogReaderLevel)return fail(CatalogInstall::Refused,"the catalog needs a newer CNA");
    if(pack.totalBytes>std::min(maximumBytes,HardMaximumPackBytes))return fail(CatalogInstall::Refused,"the catalog is larger than allowed");
    CatalogManifest manifest;
    try {
        if(hashOf(pack.manifest)!=pack.manifestSha256)return fail(CatalogInstall::Failed,"manifest does not match its hash");
        manifest=parseManifest(pack.manifest);
    } catch(const std::runtime_error& failure) {
        return fail(CatalogInstall::Failed,failure.what());
    }
    if(manifest.version!=pack.version||manifest.version==0)return fail(CatalogInstall::Failed,"manifest describes another version");
    if(embeddedManifest(pack.version))return CatalogInstall::AlreadyInstalled;
    const auto final=versionDirectory(root,pack.version);
    std::error_code fsError;
    fs::create_directories(root,fsError);
    if(fsError)return fail(CatalogInstall::Failed,"the catalog directory cannot be created");
    const InstallLock lock(root,pack.version);
    // Whether a pack of this version is there is decided under the lock: an installer that waited
    // finds the one another installer just activated.
    if(fs::exists(final,fsError)) {
        // Catalog versions are CNA-wide identities: the pack installed first is that version.
        const auto installed=installedManifest(pack.version);
        if(installed&&hashOf(pack.manifest)==hashOf(readText(final/"catalog.json",1u<<20).value_or(std::string())))
            return CatalogInstall::AlreadyInstalled;
        return fail(CatalogInstall::Failed,installed?"another catalog is installed as this version":"the installed pack is damaged");
    }

    // Earlier interrupted attempts at this exact pack are reused (every file verified again);
    // abandoned staging of anything else is removed.
    const auto prefix=stagingPrefix(pack.version,pack.manifestSha256);
    std::vector<fs::path> previous;
    const auto now=fs::file_time_type::clock::now();
    for(fs::directory_iterator entry(root,fsError),end;!fsError&&entry!=end;entry.increment(fsError)) {
        const auto name=entry->path().filename().string();
        if(!name.starts_with(".staging-"))continue;
        std::error_code itemError;
        if(name.starts_with(prefix))previous.push_back(entry->path());
        else if(now-entry->last_write_time(itemError)>AbandonedStagingAge&&!itemError)fs::remove_all(entry->path(),itemError);
    }
    const auto staging=root/(prefix+uniqueTag());
    fs::create_directory(staging,fsError);
    if(fsError)return fail(CatalogInstall::Failed,"the staging directory cannot be created");
    auto abandon=[&](const std::string& why) {
        // Verified files stay for a retry; nothing under the version's own name exists.
        return fail(CatalogInstall::Failed,why);
    };
    const auto others=availableCatalogVersions();
    try {
        for(const auto& [name,asset]:manifest.assets) {
            const auto target=staging/asset.sha256;
            std::optional<std::vector<std::uint8_t>> bytes;
            if(auto embedded=embeddedFileByContent(asset))bytes.emplace(embedded->begin(),embedded->end());
            for(const auto& candidate:others) {
                if(bytes)break;
                if(!embeddedManifest(candidate))bytes=installedCatalogFile(candidate,asset);
            }
            for(const auto& attempt:previous) {
                if(bytes)break;
                bytes=readVerified(attempt/asset.sha256,asset);
            }
            if(!bytes) {
                bytes=fetch(asset);
                if(bytes->size()!=asset.size||sha256Hex(*bytes)!=asset.sha256)return abandon(name+" does not match the manifest");
            }
            if(!writeFile(target,*bytes))return abandon("the catalog directory is not writable");
        }
    } catch(const std::exception& failure) {
        return abandon(failure.what());
    }
    const auto manifestBytes=std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(pack.manifest.data()),pack.manifest.size());
    if(!writeFile(staging/"catalog.json",manifestBytes))return abandon("the catalog directory is not writable");
    try {
        std::map<std::string,std::vector<std::uint8_t>,std::less<>> read;
        validateCatalog(manifest,[&](const CatalogAsset& asset)->std::span<const std::uint8_t> {
            auto bytes=readVerified(staging/asset.sha256,asset);
            if(!bytes)invalid(asset.name+" changed on disk");
            return read.insert_or_assign(asset.sha256,std::move(*bytes)).first->second;
        });
    } catch(const std::runtime_error& failure) {
        // An invalid pack is not worth resuming.
        fs::remove_all(staging,fsError);
        return fail(CatalogInstall::Failed,failure.what());
    }
    const nlohmann::json record{{"packFormat",CatalogPackFormat},{"version",pack.version},{"reader",pack.reader},
        {"manifestSha256",pack.manifestSha256},{"totalBytes",pack.totalBytes}};
    const auto recordText=record.dump();
    if(!writeFile(staging/"pack.json",std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(recordText.data()),recordText.size())))
        return abandon("the catalog directory is not writable");
    // Activation: one rename. Readers see no pack or the whole pack.
    fs::rename(staging,final,fsError);
    if(fsError) {
        std::error_code ignored;
        fs::remove_all(staging,ignored);
        if(installedManifest(pack.version))return CatalogInstall::AlreadyInstalled;
        return fail(CatalogInstall::Failed,"the pack could not be activated");
    }
    for(const auto& attempt:previous)fs::remove_all(attempt,fsError);
    return CatalogInstall::Installed;
}
}
