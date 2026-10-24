// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/GamerServices/AvatarAssets.hpp"
#include "CNA/Internal/GamerServices/IGamerServicesBackend.hpp"
#include <chrono>
#include <future>
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

// Kind names by AvatarItemSlot, and which manifest list holds each kind.
constexpr std::array<std::string_view,AvatarItemKindCount> SlotNames{"hair","top","bottom","shoes","glasses","hat","facialHair","ears",
    "earrings","wristwear","gloves","rings","eyebrows","eyeShadow","eyeliner","blush","lips"};
enum class ListKind : std::uint8_t { Items, Features, Styles };
ListKind listOf(AvatarItemSlot slot)
{
    if(slot==AvatarItemSlot::FacialHair||slot==AvatarItemSlot::Ears)return ListKind::Features;
    return slot>=AvatarItemSlot::Eyebrows?ListKind::Styles:ListKind::Items;
}
// Tiles a face style names: eyebrows a tile per state and side, lips one per mouth state,
// eyeliner one per side for an open and a closed eye, the other makeup one per side.
std::size_t styleTiles(AvatarItemSlot slot)
{
    switch(slot) {
    case AvatarItemSlot::Eyebrows: return 10;
    case AvatarItemSlot::Lips: return 14;
    case AvatarItemSlot::Eyeliner: return 4;
    default: return 2;
    }
}
constexpr std::array<std::string_view,AvatarColorSlotCount> ColorNames{"skin","hair","eyes","top","bottom","shoes","accessory","lips",
    "eyeShadow","blush","jewelry","gloves"};
constexpr std::array<std::string_view,14> EyeNames{"Neutral","Sad","Angry","Confused","Laughing","Shocked","Happy",
    "Yawning","Sleeping","LookUp","LookDown","LookLeft","LookRight","Blink"};
constexpr std::array<std::string_view,5> EyebrowNames{"Neutral","Sad","Angry","Confused","Raised"};
constexpr std::array<std::string_view,14> MouthNames{"Neutral","Sad","Angry","Confused","Laughing","Shocked","Happy",
    "PhoneticO","PhoneticAi","PhoneticEe","PhoneticFv","PhoneticW","PhoneticL","PhoneticDth"};
constexpr std::size_t MaximumAssetBytes=8u<<20, MaximumAssets=1024, MaximumEntries=1024;

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
template std::optional<std::vector<unsigned char>> onServiceExecutor(std::function<std::vector<unsigned char>(IGamerServicesBackend&)>);

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

const CatalogItem* CatalogManifest::faceStyle(std::uint16_t id) const
{
    auto found=std::ranges::find(faceStyles,id,&CatalogItem::id);
    return found==faceStyles.end()?nullptr:&*found;
}

const CatalogItem* CatalogManifest::entry(std::uint16_t id) const
{
    if(const auto* found=item(id))return found;
    if(const auto* found=featureItem(id))return found;
    return faceStyle(id);
}

const CatalogItem* CatalogManifest::item(std::uint16_t id,AvatarItemSlot slot) const
{
    const auto* found=entry(id);
    return found&&found->slot==slot?found:nullptr;
}

std::vector<const CatalogItem*> CatalogManifest::itemsOf(AvatarItemSlot slot) const
{
    std::vector<const CatalogItem*> out;
    for(const auto* list:{&items,&featureItems,&faceStyles})
        for(const auto& entry:*list)
            if(entry.slot==slot)out.push_back(&entry);
    return out;
}

int CatalogManifest::faceParameter(int body,std::string_view name) const
{
    for(const auto& control:faceControls[body==1?1:0])
        if(control.name==name)return control.parameter;
    return -1;
}

float HeadSurface::radius(float theta,float phi) const
{
    if(rings<1||segments<3||radii.size()!=static_cast<std::size_t>((rings+1)*segments))return 0.0f;
    constexpr float Pi=3.14159265358979f;
    const float row=std::clamp(theta/Pi,0.0f,1.0f)*static_cast<float>(rings);
    float column=(phi+Pi)/(2.0f*Pi)*static_cast<float>(segments);
    column-=std::floor(column/static_cast<float>(segments))*static_cast<float>(segments);
    const int r0=std::min(static_cast<int>(row),rings-1), c0=std::min(static_cast<int>(column),segments-1), c1=(c0+1)%segments;
    const float fr=row-static_cast<float>(r0), fc=column-static_cast<float>(c0);
    auto at=[&](int r,int c){return radii[static_cast<std::size_t>(r*segments+c)];};
    const float top=at(r0,c0)+(at(r0,c1)-at(r0,c0))*fc, bottom=at(r0+1,c0)+(at(r0+1,c1)-at(r0+1,c0))*fc;
    return top+(bottom-top)*fr;
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
    constexpr std::size_t MaximumControls=2*FaceParameterCount, MaximumDeformers=8;
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

// Anchors, the head surface, outfits and presets: what the assembly and the editor read beyond items.
void readFaceData(const nlohmann::json& json,CatalogManifest& manifest)
{
    const char* bodies[]{"female","male"};
    if(json.contains("faceAnchors"))
        for(int body=0;body<2;++body) {
            const auto& anchors=json["faceAnchors"].at(bodies[body]);
            auto& out=manifest.faceAnchors[body];
            out.eyeLeft=vector3(anchors.at("eyeLeft"),5.0f);
            out.eyeRight=vector3(anchors.at("eyeRight"),5.0f);
            out.earLeft=vector3(anchors.at("earLeft"),5.0f);
            out.earRight=vector3(anchors.at("earRight"),5.0f);
        }
    if(json.contains("headSurface"))
        for(int body=0;body<2;++body) {
            const auto& entry=json["headSurface"].at(bodies[body]);
            auto& surface=manifest.headSurface[body];
            surface.centre=vector3(entry.at("centre"),5.0f);
            surface.rings=entry.at("rings").get<int>();
            surface.segments=entry.at("segments").get<int>();
            if(surface.rings<2||surface.rings>128||surface.segments<4||surface.segments>256)malformed("head surface size");
            const auto& radii=entry.at("radii");
            if(!radii.is_array()||radii.size()!=static_cast<std::size_t>((surface.rings+1)*surface.segments))malformed("head surface radii");
            for(const auto& radius:radii) {
                const float value=radius.get<float>();
                if(!(value>0.01f&&value<1.0f))malformed("head surface radius");
                surface.radii.push_back(value);
            }
        }
    if(json.contains("outfits")) {
        if(!json["outfits"].is_array()||json["outfits"].size()>256)malformed("outfits");
        for(const auto& entry:json["outfits"]) {
            CatalogOutfit outfit;
            outfit.name=entry.at("name").get<std::string>();
            if(outfit.name.empty()||outfit.name.size()>64)malformed("outfit name");
            for(const auto& [kind,id]:entry.at("items").items()) {
                auto slot=std::ranges::find(SlotNames,kind);
                if(slot==SlotNames.end())malformed("outfit item kind");
                const auto value=id.get<int>();
                const auto itemSlot=static_cast<AvatarItemSlot>(slot-SlotNames.begin());
                if(value<0||value>0xffff||(value!=0&&!manifest.item(static_cast<std::uint16_t>(value),itemSlot)))malformed("outfit item");
                outfit.items.emplace_back(itemSlot,static_cast<std::uint16_t>(value));
            }
            if(entry.contains("colors"))
                for(const auto& [name,rgb]:entry["colors"].items()) {
                    auto slot=std::ranges::find(ColorNames,name);
                    if(slot==ColorNames.end()||!rgb.is_array()||rgb.size()!=3)malformed("outfit color");
                    AvatarColor color;
                    std::array<std::uint8_t*,3> channels{&color.r,&color.g,&color.b};
                    for(std::size_t k=0;k<3;++k) {
                        const int value=rgb[k].get<int>();
                        if(value<0||value>255)malformed("outfit color");
                        *channels[k]=static_cast<std::uint8_t>(value);
                    }
                    outfit.colors.emplace_back(static_cast<AvatarColorSlot>(slot-ColorNames.begin()),color);
                }
            manifest.outfits.push_back(std::move(outfit));
        }
    }
    if(json.contains("facePresets")) {
        if(!json["facePresets"].is_array()||json["facePresets"].size()>256)malformed("face presets");
        for(const auto& entry:json["facePresets"]) {
            FacePreset preset{entry.at("group").get<std::string>(),entry.at("name").get<std::string>(),{}};
            if(preset.group.empty()||preset.group.size()>16||preset.name.empty()||preset.name.size()>32)malformed("face preset name");
            for(const auto& [name,value]:entry.at("values").items()) {
                const float amount=value.get<float>();
                if(!(amount>=-1.0f&&amount<=1.0f)||name.size()>32)malformed("face preset value");
                preset.values.emplace_back(name,amount);
            }
            manifest.facePresets.push_back(std::move(preset));
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
    if(!isSupportedCatalogVersion(version))malformed("catalog version "+std::to_string(version)+" is a development catalog below the baseline");
    manifest.version=static_cast<std::uint16_t>(version);
    const auto revision=json.value("revision",1);
    if(revision<1||revision>0xffff)malformed("catalog revision");
    manifest.revision=static_cast<std::uint16_t>(revision);
    if(json.value("reader",1)>CatalogReaderLevel)malformed("the catalog needs a newer reader");
    if(!json.contains("assets")||!json["assets"].is_array()||json["assets"].size()>MaximumAssets)malformed("asset list");
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
    // The face atlas first: face styles name its tiles.
    const auto& face=json.at("face");
    manifest.faceAsset=listed(face.at("asset"));
    const auto& layout=face.at("layout");
    manifest.face.tileSize=layout.at("tileSize").get<int>();
    manifest.face.columns=layout.at("columns").get<int>();
    const int width=layout.at("width").get<int>(), height=layout.at("height").get<int>();
    if(manifest.face.tileSize<8||manifest.face.tileSize>256||manifest.face.columns<1||width!=manifest.face.columns*manifest.face.tileSize||
       height<manifest.face.tileSize||height>4096||height%manifest.face.tileSize)malformed("face layout");
    const int tileCount=manifest.face.columns*(height/manifest.face.tileSize);
    manifest.face.tiles=tileCount;
    std::set<std::uint16_t> ids;
    auto readItem=[&](const nlohmann::json& item,ListKind list) {
        CatalogItem entry;
        const int id=item.at("id").get<int>();
        if(id<1||id>0xffff||!ids.insert(static_cast<std::uint16_t>(id)).second)malformed("item id");
        entry.id=static_cast<std::uint16_t>(id);
        auto slot=std::ranges::find(SlotNames,item.at("slot").get<std::string>());
        if(slot==SlotNames.end())malformed("item slot");
        entry.slot=static_cast<AvatarItemSlot>(slot-SlotNames.begin());
        if(listOf(entry.slot)!=list)malformed("item slot in the wrong list");
        entry.name=item.at("name").get<std::string>();
        if(entry.name.empty()||entry.name.size()>64)malformed("item name");
        entry.label=item.value("label",std::string());
        if(entry.label.size()>40)malformed("item label");
        if(list==ListKind::Styles) {
            const auto& tiles=item.at("tiles");
            if(!tiles.is_array()||tiles.size()!=styleTiles(entry.slot))malformed("face style tiles");
            for(const auto& value:tiles)entry.tiles.push_back(tile(value,tileCount));
            entry.opacity=item.value("opacity",1.0f);
            if(!(entry.opacity>0.0f&&entry.opacity<=1.0f))malformed("face style opacity");
        } else {
            entry.assets={listed(item.at("assets").at("female")),listed(item.at("assets").at("male"))};
        }
        if(item.contains("random")) {
            for(int body=0;body<2;++body) {
                const float weight=item["random"].at(body==0?"female":"male").get<float>();
                if(!(weight>=0.0f&&weight<=1000.0f))malformed("item random weight");
                entry.randomWeight[body]=weight;
            }
        }
        const int format=item.value("format",1);
        if(format<FormatVersion||format>ExtendedFormatVersion)malformed("item format");
        // A kind format 3 introduced cannot be named by an older format.
        const bool extendedKind=entry.slot==AvatarItemSlot::Ears||entry.slot>=AvatarItemSlot::Earrings;
        entry.format=static_cast<std::uint8_t>(std::max<int>(format,extendedKind?ExtendedFormatVersion:FormatVersion));
        if(item.contains("hairCover")) {
            if(entry.slot!=AvatarItemSlot::Hat)malformed("hairCover on a non-hat item");
            const auto& cover=item["hairCover"];
            const auto& line=cover.at("line");
            if(!line.is_array()||line.size()!=4)malformed("hairCover line");
            HairCover parsed;
            for(std::size_t k=0;k<4;++k) {
                parsed.line[k]=line[k].get<float>();
                if(!(parsed.line[k]>0.0f&&parsed.line[k]<1.0f))malformed("hairCover line");
            }
            parsed.lift=cover.at("lift").get<float>();
            if(!(parsed.lift>=0.0f&&parsed.lift<=0.1f))malformed("hairCover lift");
            entry.hairCover=parsed;
        }
        if(item.contains("follow")) {
            const auto follow=item["follow"].get<std::string>();
            if(follow=="eyes"&&entry.slot==AvatarItemSlot::Glasses)entry.follow=ItemFollow::Eyes;
            else if(follow=="ears"&&entry.slot==AvatarItemSlot::Earrings)entry.follow=ItemFollow::Ears;
            else malformed("item follow");
        }
        if(item.contains("lobes")) {
            if(entry.slot!=AvatarItemSlot::Ears)malformed("lobes on a non-ear item");
            std::array<std::array<Microsoft::Xna::Framework::Vector3,2>,2> lobes;
            for(int body=0;body<2;++body) {
                const auto& sides=item["lobes"].at(body==0?"female":"male");
                if(!sides.is_array()||sides.size()!=2)malformed("ear lobes");
                for(int side=0;side<2;++side)lobes[body][side]=vector3(sides[side],5.0f);
            }
            entry.lobes=lobes;
        }
        if(item.contains("hides")) {
            if(!item["hides"].is_array()||item["hides"].size()>AvatarItemKindCount)malformed("item hides");
            for(const auto& hidden:item["hides"]) {
                auto kind=std::ranges::find(SlotNames,hidden.get<std::string>());
                if(kind==SlotNames.end()||listOf(static_cast<AvatarItemSlot>(kind-SlotNames.begin()))!=ListKind::Items)malformed("item hides");
                entry.hides.push_back(static_cast<AvatarItemSlot>(kind-SlotNames.begin()));
            }
        }
        return entry;
    };
    auto readList=[&](const char* key,ListKind list,std::vector<CatalogItem>& out) {
        if(!json.contains(key))return;
        if(!json[key].is_array()||json[key].size()>MaximumEntries)malformed(std::string(key)+" list");
        for(const auto& item:json[key])out.push_back(readItem(item,list));
    };
    readList("items",ListKind::Items,manifest.items);
    if(!json.contains("items"))malformed("items");
    readList("featureItems",ListKind::Features,manifest.featureItems);
    readList("faceStyles",ListKind::Styles,manifest.faceStyles);
    if(json.contains("faceControls"))readFaceControls(json["faceControls"],manifest);
    for(std::size_t state=0;state<EyeNames.size();++state)
        for(int side=0;side<2;++side) {
            const auto& pair=layout.at("eyes").at(std::string(EyeNames[state])).at(side==0?"left":"right");
            if(!pair.is_array()||pair.size()!=2)malformed("eye tiles");
            manifest.face.eyes[state][side]={tile(pair[0],tileCount),tile(pair[1],tileCount)};
        }
    for(std::size_t state=0;state<EyebrowNames.size();++state)
        for(int side=0;side<2;++side)
            manifest.face.eyebrows[state][side]=tile(layout.at("eyebrows").at(std::string(EyebrowNames[state])).at(side==0?"left":"right"),tileCount);
    for(std::size_t state=0;state<MouthNames.size();++state)
        manifest.face.mouths[state]=tile(layout.at("mouths").at(std::string(MouthNames[state])),tileCount);
    manifest.animationsAsset=listed(json.at("animations").at("asset"));
    readFaceData(json,manifest);
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
            const auto text=inflateEmbedded(file);
            auto manifest=std::make_shared<const CatalogManifest>(
                parseManifest(std::string_view(reinterpret_cast<const char*>(text.data()),text.size())));
            if(name!="v"+std::to_string(manifest->version)+"/catalog.json")
                throw std::runtime_error("avatar catalog: "+std::string(name)+" describes another version");
            out.push_back(std::move(manifest));
        }
        if(out.empty())throw std::runtime_error("the avatar catalog is not compiled into this build");
        std::ranges::sort(out,{},[](const auto& manifest){return manifest->version;});
        // Every build draws baseline descriptions offline; later catalogs come after it.
        if(out.front()->version!=BaseCatalogVersion)
            throw std::runtime_error("avatar catalog: the baseline catalog v"+std::to_string(BaseCatalogVersion)+" is not compiled into this build");
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

std::optional<AssetBytes> embeddedFileByContent(const CatalogAsset& asset)
{
    // Each compiled-in file is inflated and hashed at most once per process to learn its identity.
    for(const auto& file:embeddedCatalogFiles()) {
        if((file.inflatedSize?file.inflatedSize:file.size)!=asset.size)continue;
        std::shared_ptr<const std::vector<std::uint8_t>> contents;
        {
            std::lock_guard guard(verifiedLock);
            if(auto hash=embeddedHashes.find(file.data);hash!=embeddedHashes.end()&&hash->second!=asset.sha256)continue;
        }
        contents=std::make_shared<const std::vector<std::uint8_t>>(inflateEmbedded(file));
        std::lock_guard guard(verifiedLock);
        auto hash=embeddedHashes.find(file.data);
        if(hash==embeddedHashes.end())hash=embeddedHashes.emplace(file.data,sha256Hex(*contents)).first;
        if(hash->second==asset.sha256)return AssetBytes{contents,std::span<const std::uint8_t>(*contents)};
    }
    return std::nullopt;
}

std::vector<std::uint8_t> embeddedCatalogFile(std::string_view name)
{
    for(const auto& file:embeddedCatalogFiles())
        if(name==file.name)return inflateEmbedded(file);
    return {};
}

int maximumFaceTile(const FaceLayout& layout)
{
    int highest=0;
    for(const auto& state:layout.eyes)for(const auto& side:state)highest=std::max({highest,side[0],side[1]});
    for(const auto& state:layout.eyebrows)highest=std::max({highest,state[0],state[1]});
    for(auto mouth:layout.mouths)highest=std::max(highest,mouth);
    return highest;
}

std::optional<AssetBytes> resolveAsset(const CatalogManifest& manifest,std::string_view name)
{
    auto listed=manifest.assets.find(name);
    if(listed==manifest.assets.end())return std::nullopt;
    // Compiled-in contents are found by size and hash, never by name: two catalog versions may
    // hold different files under one name.
    if(auto embedded=embeddedFileByContent(listed->second))return embedded;
    // Otherwise the installed pack of exactly this version, verified again on every read. Assets
    // are never downloaded here: a missing catalog is installed as a whole (catalogManifest).
    auto bytes=installedCatalogFile(manifest.version,listed->second);
    if(!bytes)return std::nullopt;
    auto owned=std::make_shared<const std::vector<std::uint8_t>>(std::move(*bytes));
    return AssetBytes{owned,std::span<const std::uint8_t>(*owned)};
}
}
