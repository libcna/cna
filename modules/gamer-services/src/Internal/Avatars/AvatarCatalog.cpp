// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/GamerServices/AvatarAssets.hpp"
#include "CNA/Internal/GamerServices/IGamerServicesBackend.hpp"
#include <chrono>
#include <future>
#include "System/Security/Cryptography/SHA256.hpp"
#include <algorithm>
#include <cmath>
#include <mutex>
#include <nlohmann/json.hpp>
#include <set>
#include <stdexcept>

namespace CNA::Internal::GamerServices::Avatars {
namespace {
// Exactly the table read from the XNA reference assembly.
constexpr std::array<int,BoneCount> Parents{
    -1, 0, 0, 0, 0, 1, 2, 2, 3, 3, 1, 6, 5, 6, 5, 8, 5, 8, 5, 14, 12, 11, 16, 15, 14, 20, 20, 20, 22, 22, 22,
    25, 25, 25, 28, 28, 28, 33, 33, 33, 33, 33, 33, 33, 36, 36, 36, 36, 36, 36, 36, 37, 38, 39, 40, 43, 44,
    45, 46, 47, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60};

// AvatarBone names; the slots XNA leaves unnamed are CNA helper joints.
constexpr std::array<std::string_view,BoneCount> Names{
    "Root", "BackLower", "HipLeft", "HipRight", "Slot04", "BackUpper", "KneeLeft", "Slot07", "KneeRight", "Slot09",
    "Slot10", "AnkleLeft", "CollarLeft", "Slot13", "Neck", "AnkleRight", "CollarRight", "Slot17", "Slot18", "Head",
    "ShoulderLeft", "ToeLeft", "ShoulderRight", "ToeRight", "Slot24", "ElbowLeft", "Slot26", "Slot27", "ElbowRight",
    "Slot29", "Slot30", "Slot31", "Slot32", "WristLeft", "Slot34", "Slot35", "WristRight", "FingerIndexLeft",
    "FingerMiddleLeft", "FingerRingLeft", "FingerSmallLeft", "PropLeft", "SpecialLeft", "FingerThumbLeft",
    "FingerIndexRight", "FingerMiddleRight", "FingerRingRight", "FingerSmallRight", "PropRight", "SpecialRight",
    "FingerThumbRight", "FingerIndex2Left", "FingerMiddle2Left", "FingerRing2Left", "FingerSmall2Left",
    "FingerThumb2Left", "FingerIndex2Right", "FingerMiddle2Right", "FingerRing2Right", "FingerSmall2Right",
    "FingerThumb2Right", "FingerIndex3Left", "FingerMiddle3Left", "FingerRing3Left", "FingerSmall3Left",
    "FingerThumb3Left", "FingerIndex3Right", "FingerMiddle3Right", "FingerRing3Right", "FingerSmall3Right",
    "FingerThumb3Right"};

constexpr std::array<std::string_view,6> SlotNames{"hair","top","bottom","shoes","glasses","hat"};
constexpr std::array<std::string_view,14> EyeNames{"Neutral","Sad","Angry","Confused","Laughing","Shocked","Happy",
    "Yawning","Sleeping","LookUp","LookDown","LookLeft","LookRight","Blink"};
constexpr std::array<std::string_view,5> EyebrowNames{"Neutral","Sad","Angry","Confused","Raised"};
constexpr std::array<std::string_view,14> MouthNames{"Neutral","Sad","Angry","Confused","Laughing","Shocked","Happy",
    "PhoneticO","PhoneticAi","PhoneticEe","PhoneticFv","PhoneticW","PhoneticL","PhoneticDth"};
constexpr std::size_t MaximumAssetBytes=8u<<20;

[[noreturn]] void malformed(const std::string& what){throw std::runtime_error("avatar catalog manifest: "+what);}

bool safeName(std::string_view name)
{
    // Asset names are plain file names; never paths.
    return !name.empty()&&name.size()<=64&&name.front()!='.'&&
        std::ranges::all_of(name,[](char c){return (c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='_'||c=='.'||c=='-';});
}

int tile(const nlohmann::json& value,int count)
{
    if(!value.is_number_integer())malformed("tile index");
    const int index=value.get<int>();
    if(index<0||index>=count)malformed("tile index out of range");
    return index;
}

// Namespace scope so the avatar loader thread can use them until it is joined at exit.
std::mutex verifiedLock;
std::map<const unsigned char*,std::string> embeddedHashes;
}

template<typename T>
std::optional<T> onServiceExecutor(std::function<T(IGamerServicesBackend&)> work)
{
    try {
        auto service=backend();
        if(!service->serviceEnabled())return std::nullopt;
        auto promise=std::make_shared<std::promise<T>>();
        auto future=promise->get_future();
        const std::weak_ptr<IGamerServicesBackend> origin=service;
        service->submit([promise,origin,work=std::move(work)] {
            try {
                const auto executor=origin.lock();
                if(!executor)throw std::runtime_error("the service backend was replaced");
                promise->set_value(work(*executor));
            } catch(...) {promise->set_exception(std::current_exception());}
        },[]{});
        if(future.wait_for(std::chrono::seconds(30))!=std::future_status::ready)return std::nullopt;
        return future.get();
    } catch(...) {
        return std::nullopt;
    }
}
template std::optional<std::string> onServiceExecutor(std::function<std::string(IGamerServicesBackend&)>);

const std::array<int,BoneCount>& parentBones(){return Parents;}
std::string_view boneName(int slot){return slot>=0&&slot<BoneCount?Names[slot]:std::string_view{};}
int boneIndex(std::string_view name)
{
    auto found=std::ranges::find(Names,name);
    return found==Names.end()?-1:static_cast<int>(found-Names.begin());
}

const CatalogItem* CatalogManifest::item(std::uint16_t id) const
{
    auto found=std::ranges::find(items,id,&CatalogItem::id);
    return found==items.end()?nullptr:&*found;
}

const CatalogItem* CatalogManifest::featureItem(std::uint16_t id) const
{
    auto found=std::ranges::find(featureItems,id,&CatalogItem::id);
    return found==featureItems.end()?nullptr:&*found;
}

namespace {
CatalogManifest parseManifestJson(std::string_view text);
}

CatalogManifest parseManifest(std::string_view text)
{
    try {
        return parseManifestJson(text);
    } catch(const nlohmann::json::exception& error) {
        malformed(error.what());
    }
}

namespace {
Microsoft::Xna::Framework::Vector3 vector3(const nlohmann::json& value,float bound)
{
    if(!value.is_array()||value.size()!=3)malformed("face control vector");
    std::array<float,3> v{};
    for(int k=0;k<3;++k) {
        v[k]=value[k].get<float>();
        if(!std::isfinite(v[k])||std::fabs(v[k])>bound)malformed("face control value");
    }
    return {v[0],v[1],v[2]};
}

void readFaceControls(const nlohmann::json& json,CatalogManifest& manifest)
{
    // Bounded so that a hostile catalog can neither tear a head apart nor cost real time.
    constexpr std::size_t MaximumControls=32, MaximumDeformers=8;
    if(!json.is_object())malformed("face controls");
    for(int body=0;body<2;++body) {
        const auto& list=json.at(body==0?"female":"male");
        if(!list.is_array()||list.size()>MaximumControls)malformed("face control list");
        for(const auto& entry:list) {
            FaceControl control;
            control.parameter=entry.at("parameter").get<int>();
            if(control.parameter<0||control.parameter>=static_cast<int>(FaceParameterCount))malformed("face control parameter");
            const auto scope=entry.at("scope").get<std::string>();
            if(scope!="face"&&scope!="head")malformed("face control scope");
            control.wholeHead=scope=="head";
            if(entry.contains("name")) {
                control.name=entry["name"].get<std::string>();
                if(control.name.size()>32)malformed("face control name");
            }
            const auto& ops=entry.at("ops");
            if(!ops.is_array()||ops.size()>MaximumDeformers)malformed("face control ops");
            for(const auto& op:ops) {
                FaceDeformer deformer;
                deformer.centre=vector3(op.at("centre"),5.0f);
                deformer.radii=vector3(op.at("radii"),1.0f);
                if(deformer.radii.X<1e-3f||deformer.radii.Y<1e-3f||deformer.radii.Z<1e-3f)malformed("face control radii");
                deformer.inner=op.at("inner").get<float>();
                if(!(deformer.inner>=0.0f&&deformer.inner<1.0f))malformed("face control inner");
                if(op.contains("scale")) {
                    deformer.kind=FaceDeformer::Kind::Scale;
                    deformer.amount=vector3(op["scale"],2.0f);
                    if(deformer.amount.X<0.5f||deformer.amount.Y<0.5f||deformer.amount.Z<0.5f)malformed("face control scale");
                } else if(op.contains("move")) {
                    deformer.kind=FaceDeformer::Kind::Move;
                    deformer.amount=vector3(op["move"],0.1f);
                } else if(op.contains("rotate")) {
                    deformer.kind=FaceDeformer::Kind::Rotate;
                    deformer.amount=vector3(op["rotate"],1.0f);
                    if(deformer.amount.Length()<1e-3f)malformed("face control axis");
                    deformer.amount.Normalize();
                    deformer.degrees=op.at("degrees").get<float>();
                    if(!(std::fabs(deformer.degrees)<=45.0f))malformed("face control angle");
                } else {
                    malformed("face control op");
                }
                control.deformers.push_back(deformer);
            }
            manifest.faceControls[body].push_back(std::move(control));
        }
    }
}

CatalogManifest parseManifestJson(std::string_view text)
{
    const auto json=nlohmann::json::parse(text,nullptr,false);
    if(json.is_discarded()||!json.is_object())malformed("not a JSON object");
    if(json.value("format",0)!=1||json.value("rig",std::string())!="cna-avatar-71")malformed("unknown format or rig");
    CatalogManifest manifest;
    const auto version=json.value("catalogVersion",0);
    if(version<1||version>0xffff)malformed("catalog version");
    manifest.version=static_cast<std::uint16_t>(version);
    if(!json.contains("assets")||!json["assets"].is_array()||json["assets"].size()>256)malformed("asset list");
    for(const auto& asset:json["assets"]) {
        CatalogAsset entry{asset.value("name",std::string()),asset.value("sha256",std::string()),asset.value("size",std::size_t{0})};
        if(!safeName(entry.name)||entry.sha256.size()!=64||entry.size==0||entry.size>MaximumAssetBytes||
           !std::ranges::all_of(entry.sha256,[](char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f');}))
            malformed("asset entry");
        if(!manifest.assets.emplace(entry.name,entry).second)malformed("duplicate asset");
    }
    auto listed=[&](const nlohmann::json& value)->std::string {
        if(!value.is_string()||!manifest.assets.contains(value.get<std::string>()))malformed("unlisted asset reference");
        return value.get<std::string>();
    };
    const auto& bodies=json.at("bodies");
    for(int body=0;body<2;++body) {
        const auto& entry=bodies.at(body==0?"female":"male");
        manifest.bodies[body]=listed(entry.at("asset"));
        const int height=entry.at("authoredHeightMillimeters").get<int>();
        if(height<MinimumHeightMillimeters||height>MaximumHeightMillimeters)malformed("authored height");
        manifest.authoredHeightMillimeters[body]=static_cast<std::uint16_t>(height);
    }
    std::set<std::uint16_t> ids;
    auto readItem=[&](const nlohmann::json& item,bool feature) {
        CatalogItem entry;
        const int id=item.at("id").get<int>();
        if(id<1||id>0xffff||!ids.insert(static_cast<std::uint16_t>(id)).second)malformed("item id");
        entry.id=static_cast<std::uint16_t>(id);
        const auto slotName=item.at("slot").get<std::string>();
        if(feature) {
            if(slotName!="facialHair")malformed("feature item slot");
            entry.slot=AvatarItemSlot::FacialHair;
        } else {
            auto slot=std::ranges::find(SlotNames,slotName);
            if(slot==SlotNames.end())malformed("item slot");
            entry.slot=static_cast<AvatarItemSlot>(slot-SlotNames.begin());
        }
        entry.name=item.at("name").get<std::string>();
        entry.assets={listed(item.at("assets").at("female")),listed(item.at("assets").at("male"))};
        if(item.contains("random")) {
            for(int body=0;body<2;++body) {
                const float weight=item["random"].at(body==0?"female":"male").get<float>();
                if(!(weight>=0.0f&&weight<=1000.0f))malformed("item random weight");
                entry.randomWeight[body]=weight;
            }
        }
        if(item.contains("hatAssets")) {
            if(entry.slot!=AvatarItemSlot::Hair)malformed("hat assets on a non-hair item");
            entry.hatAssets={listed(item["hatAssets"].at("female")),listed(item["hatAssets"].at("male"))};
        }
        if(item.contains("coversHair")) {
            if(entry.slot!=AvatarItemSlot::Hat)malformed("coversHair on a non-hat item");
            entry.coversHair=item["coversHair"].get<bool>();
        }
        return entry;
    };
    for(const auto& item:json.at("items"))manifest.items.push_back(readItem(item,false));
    if(json.contains("featureItems")) {
        if(!json["featureItems"].is_array()||json["featureItems"].size()>256)malformed("feature items");
        for(const auto& item:json["featureItems"])manifest.featureItems.push_back(readItem(item,true));
    }
    if(json.contains("faceControls"))readFaceControls(json["faceControls"],manifest);
    const auto& face=json.at("face");
    manifest.faceAsset=listed(face.at("asset"));
    const auto& layout=face.at("layout");
    manifest.face.tileSize=layout.at("tileSize").get<int>();
    manifest.face.columns=layout.at("columns").get<int>();
    const int width=layout.at("width").get<int>(), height=layout.at("height").get<int>();
    if(manifest.face.tileSize<8||manifest.face.tileSize>256||manifest.face.columns<1||width!=manifest.face.columns*manifest.face.tileSize||
       height<manifest.face.tileSize||height>4096||height%manifest.face.tileSize)malformed("face layout");
    const int tiles=manifest.face.columns*(height/manifest.face.tileSize);
    for(std::size_t state=0;state<EyeNames.size();++state)
        for(int side=0;side<2;++side) {
            const auto& pair=layout.at("eyes").at(std::string(EyeNames[state])).at(side==0?"left":"right");
            if(!pair.is_array()||pair.size()!=2)malformed("eye tiles");
            manifest.face.eyes[state][side]={tile(pair[0],tiles),tile(pair[1],tiles)};
        }
    for(std::size_t state=0;state<EyebrowNames.size();++state)
        for(int side=0;side<2;++side)
            manifest.face.eyebrows[state][side]=tile(layout.at("eyebrows").at(std::string(EyebrowNames[state])).at(side==0?"left":"right"),tiles);
    for(std::size_t state=0;state<MouthNames.size();++state)
        manifest.face.mouths[state]=tile(layout.at("mouths").at(std::string(MouthNames[state])),tiles);
    manifest.animationsAsset=listed(json.at("animations").at("asset"));
    // Required slots need an item to fall back to and to draw at random.
    for(auto required:{AvatarItemSlot::Hair,AvatarItemSlot::Top,AvatarItemSlot::Bottom,AvatarItemSlot::Shoes})
        for(int body=0;body<2;++body)
            if(std::ranges::none_of(manifest.items,[&](const CatalogItem& item){return item.slot==required&&item.randomWeight[body]>0;}))
                malformed("a required slot has no item");
    return manifest;
}
}

const std::vector<std::shared_ptr<const CatalogManifest>>& embeddedCatalogs()
{
    static const std::vector<std::shared_ptr<const CatalogManifest>> catalogs=[] {
        std::vector<std::shared_ptr<const CatalogManifest>> out;
        for(const auto& file:embeddedCatalogFiles()) {
            const std::string_view name(file.name);
            if(!name.starts_with("v")||!name.ends_with("/catalog.json"))continue;
            auto manifest=std::make_shared<const CatalogManifest>(
                parseManifest(std::string_view(reinterpret_cast<const char*>(file.data),file.size)));
            if(name!="v"+std::to_string(manifest->version)+"/catalog.json")
                throw std::runtime_error("avatar catalog: "+std::string(name)+" describes another version");
            out.push_back(std::move(manifest));
        }
        if(out.empty())throw std::runtime_error("the avatar catalog is not compiled into this build");
        std::ranges::sort(out,{},[](const auto& manifest){return manifest->version;});
        return out;
    }();
    return catalogs;
}

std::shared_ptr<const CatalogManifest> embeddedManifest(std::uint16_t version)
{
    const auto& catalogs=embeddedCatalogs();
    auto found=std::ranges::find(catalogs,version,[](const auto& manifest){return manifest->version;});
    return found==catalogs.end()?nullptr:*found;
}

const CatalogManifest& newestEmbeddedManifest(){return *embeddedCatalogs().back();}

std::string sha256Hex(std::span<const std::uint8_t> bytes)
{
    System::Security::Cryptography::SHA256 hash;
    const auto digest=hash.ComputeHash(std::vector<SharpRuntime::bytecs>(bytes.begin(),bytes.end()));
    static constexpr char Hex[]="0123456789abcdef";
    std::string text;
    for(auto byte:digest){text+=Hex[byte>>4];text+=Hex[byte&15];}
    return text;
}

std::optional<AssetBytes> resolveAsset(const CatalogManifest& manifest,std::string_view name)
{
    auto listed=manifest.assets.find(name);
    if(listed==manifest.assets.end())return std::nullopt;
    const auto& expected=listed->second;
    // Compiled-in contents are found by size and hash, never by name: two catalog versions may
    // hold different files under one name. Each file is hashed at most once per process.
    for(const auto& file:embeddedCatalogFiles()) {
        if(file.size!=expected.size)continue;
        std::lock_guard guard(verifiedLock);
        auto hash=embeddedHashes.find(file.data);
        if(hash==embeddedHashes.end())hash=embeddedHashes.emplace(file.data,sha256Hex(file.bytes())).first;
        if(hash->second==expected.sha256)return AssetBytes{nullptr,file.bytes()};
    }
    // Not compiled in (a newer catalog): the service's immutable, hash-addressed copy, which the
    // backend caches on disk and verifies; checked again here against this manifest.
    auto bytes=onServiceExecutor<std::vector<unsigned char>>([hash=expected.sha256](IGamerServicesBackend& service) {
        return service.asset(hash);
    });
    if(!bytes||bytes->size()!=expected.size||sha256Hex(*bytes)!=expected.sha256)return std::nullopt;
    auto owned=std::make_shared<const std::vector<std::uint8_t>>(std::move(*bytes));
    return AssetBytes{owned,std::span<const std::uint8_t>(*owned)};
}
}
