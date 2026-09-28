// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/GamerServices/AvatarAssets.hpp"
#include "System/Security/Cryptography/SHA256.hpp"
#include <algorithm>
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
std::set<std::string,std::less<>> verified;

const EmbeddedFile* embedded(std::string_view name)
{
    const auto files=embeddedCatalogFiles();
    auto found=std::ranges::find_if(files,[&](const EmbeddedFile& file){return name==file.name;});
    return found==files.end()?nullptr:&*found;
}
}

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
    for(const auto& item:json.at("items")) {
        CatalogItem entry;
        const int id=item.at("id").get<int>();
        if(id<1||id>0xffff||!ids.insert(static_cast<std::uint16_t>(id)).second)malformed("item id");
        entry.id=static_cast<std::uint16_t>(id);
        auto slot=std::ranges::find(SlotNames,item.at("slot").get<std::string>());
        if(slot==SlotNames.end())malformed("item slot");
        entry.slot=static_cast<AvatarItemSlot>(slot-SlotNames.begin());
        entry.name=item.at("name").get<std::string>();
        entry.assets={listed(item.at("assets").at("female")),listed(item.at("assets").at("male"))};
        manifest.items.push_back(std::move(entry));
    }
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
    return manifest;
}
}

const CatalogManifest& embeddedManifest()
{
    static const CatalogManifest manifest=[] {
        const auto* file=embedded("catalog.json");
        if(!file)throw std::runtime_error("the avatar catalog is not compiled into this build");
        return parseManifest(std::string_view(reinterpret_cast<const char*>(file->data),file->size));
    }();
    return manifest;
}

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
    const auto* file=embedded(name);
    if(!file||file->size!=listed->second.size)return std::nullopt;
    // Embedded contents are checked against the manifest once per process.
    std::lock_guard guard(verifiedLock);
    if(!verified.contains(listed->second.sha256)) {
        if(sha256Hex(file->bytes())!=listed->second.sha256)return std::nullopt;
        verified.insert(listed->second.sha256);
    }
    return AssetBytes{nullptr,file->bytes()};
}
}
