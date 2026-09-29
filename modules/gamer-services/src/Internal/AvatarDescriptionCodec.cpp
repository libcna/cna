// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/GamerServices/AvatarDescriptionCodec.hpp"
#include "CNA/Internal/GamerServices/AvatarAssets.hpp"
#include <algorithm>
#include <stdexcept>

namespace CNA::Internal::GamerServices::Avatars {
namespace {
// Format 1 layout; format 2 keeps it byte for byte and adds the facial-hair id and the face-shape
// block after the item ids. Everything after that up to the checksum is reserved and must be zero,
// so a later format can use it only by changing byte 0.
constexpr std::size_t MagicOffset=1, BodyOffset=4, HeightOffset=5, BuildOffset=7, CatalogOffset=8;
constexpr std::size_t ColorOffset=10, ItemOffset=ColorOffset+AvatarColorSlotCount*3;
constexpr std::size_t ReservedOffset=ItemOffset+AvatarItemSlotCount*2, ChecksumOffset=DescriptionSize-4;
constexpr std::size_t FacialHairOffset=ReservedOffset, FaceOffset=FacialHairOffset+2;
constexpr std::size_t FaceReservedOffset=FaceOffset+FaceParameterCount;
constexpr std::array<std::uint8_t,3> Magic{'C','N','A'};

bool optionalSlot(AvatarItemSlot slot){return slot==AvatarItemSlot::Glasses||slot==AvatarItemSlot::Hat;}

std::uint16_t read16(std::span<const std::uint8_t> bytes,std::size_t at){return static_cast<std::uint16_t>(bytes[at]|bytes[at+1]<<8);}
void write16(std::vector<std::uint8_t>& bytes,std::size_t at,std::uint16_t value){bytes[at]=value&0xff;bytes[at+1]=value>>8;}

// Palettes CreateRandom draws from; a description may carry any RGB value.
constexpr AvatarColor SkinTones[]={{255,224,196},{241,194,160},{224,172,128},{198,134,90},{160,104,68},{120,78,52},{92,58,40},{255,210,180}};
constexpr AvatarColor HairColors[]={{36,28,24},{74,48,30},{120,78,40},{176,122,62},{226,188,116},{160,60,36},{200,200,196},{60,64,120}};
constexpr AvatarColor EyeColors[]={{70,46,30},{110,72,40},{60,110,160},{70,130,90},{120,120,130},{40,40,44}};
constexpr AvatarColor ClothColors[]={{220,60,56},{240,150,40},{250,210,60},{90,180,80},{50,150,200},{60,80,180},{130,80,170},
    {230,120,170},{240,240,236},{60,62,70},{120,90,60},{40,110,110}};
}

std::uint32_t crc32(std::span<const std::uint8_t> bytes)
{
    std::uint32_t crc=0xffffffffu;
    for(auto byte:bytes) {
        crc^=byte;
        for(int bit=0;bit<8;++bit)crc=(crc>>1)^(0xedb88320u&(0u-(crc&1u)));
    }
    return ~crc;
}

bool AvatarDescriptor::usesFaceFormat() const
{
    return facialHair!=0||std::ranges::any_of(face,[](std::uint8_t value){return value!=NeutralFaceParameter;});
}

bool isEncodable(const AvatarDescriptor& descriptor)
{
    if(descriptor.bodyType>1||descriptor.catalogVersion==0)return false;
    if(descriptor.heightMillimeters<MinimumHeightMillimeters||descriptor.heightMillimeters>MaximumHeightMillimeters)return false;
    for(std::size_t slot=0;slot<AvatarItemSlotCount;++slot)
        if(descriptor.items[slot]==0&&!optionalSlot(static_cast<AvatarItemSlot>(slot)))return false;
    // Ids of a catalog this build does not compile in are checked by the service and the resolver.
    const auto manifest=embeddedManifest(descriptor.catalogVersion);
    if(!manifest)return true;
    for(std::size_t slot=0;slot<AvatarItemSlotCount;++slot) {
        const auto id=descriptor.items[slot];
        if(id==0)continue;
        const auto* item=manifest->item(id);
        if(!item||item->slot!=static_cast<AvatarItemSlot>(slot))return false;
    }
    return descriptor.facialHair==0||manifest->featureItem(descriptor.facialHair)!=nullptr;
}

std::vector<std::uint8_t> encode(const AvatarDescriptor& descriptor)
{
    if(!isEncodable(descriptor))throw std::invalid_argument("avatar descriptor outside the CNA encoding");
    std::vector<std::uint8_t> bytes(DescriptionSize,0);
    const bool face=descriptor.usesFaceFormat();
    bytes[0]=face?FaceFormatVersion:FormatVersion;
    std::ranges::copy(Magic,bytes.begin()+MagicOffset);
    bytes[BodyOffset]=descriptor.bodyType;
    write16(bytes,HeightOffset,descriptor.heightMillimeters);
    bytes[BuildOffset]=descriptor.build;
    write16(bytes,CatalogOffset,descriptor.catalogVersion);
    for(std::size_t index=0;index<AvatarColorSlotCount;++index) {
        const auto& color=descriptor.colors[index];
        bytes[ColorOffset+index*3]=color.r;bytes[ColorOffset+index*3+1]=color.g;bytes[ColorOffset+index*3+2]=color.b;
    }
    for(std::size_t index=0;index<AvatarItemSlotCount;++index)write16(bytes,ItemOffset+index*2,descriptor.items[index]);
    if(face) {
        write16(bytes,FacialHairOffset,descriptor.facialHair);
        std::ranges::copy(descriptor.face,bytes.begin()+FaceOffset);
    }
    const auto crc=crc32(std::span(bytes).first(ChecksumOffset));
    for(int shift=0;shift<4;++shift)bytes[ChecksumOffset+shift]=static_cast<std::uint8_t>(crc>>(shift*8));
    return bytes;
}

std::optional<AvatarDescriptor> decode(std::span<const std::uint8_t> bytes)
{
    if(bytes.size()!=static_cast<std::size_t>(DescriptionSize)||(bytes[0]!=FormatVersion&&bytes[0]!=FaceFormatVersion))return std::nullopt;
    const bool face=bytes[0]==FaceFormatVersion;
    if(!std::equal(Magic.begin(),Magic.end(),bytes.begin()+MagicOffset))return std::nullopt;
    std::uint32_t stored=0;
    for(int shift=0;shift<4;++shift)stored|=static_cast<std::uint32_t>(bytes[ChecksumOffset+shift])<<(shift*8);
    if(stored!=crc32(bytes.first(ChecksumOffset)))return std::nullopt;
    if(std::any_of(bytes.begin()+(face?FaceReservedOffset:ReservedOffset),bytes.begin()+ChecksumOffset,[](auto byte){return byte!=0;}))
        return std::nullopt;
    AvatarDescriptor descriptor;
    descriptor.bodyType=bytes[BodyOffset];
    descriptor.heightMillimeters=read16(bytes,HeightOffset);
    descriptor.build=bytes[BuildOffset];
    descriptor.catalogVersion=read16(bytes,CatalogOffset);
    for(std::size_t index=0;index<AvatarColorSlotCount;++index)
        descriptor.colors[index]={bytes[ColorOffset+index*3],bytes[ColorOffset+index*3+1],bytes[ColorOffset+index*3+2]};
    for(std::size_t index=0;index<AvatarItemSlotCount;++index)descriptor.items[index]=read16(bytes,ItemOffset+index*2);
    if(face) {
        descriptor.facialHair=read16(bytes,FacialHairOffset);
        std::copy_n(bytes.begin()+FaceOffset,FaceParameterCount,descriptor.face.begin());
        // A format 2 buffer always has something format 1 cannot say.
        if(!descriptor.usesFaceFormat())return std::nullopt;
    }
    if(!isEncodable(descriptor))return std::nullopt;
    return descriptor;
}

AvatarDescriptor randomDescriptor(std::optional<std::uint8_t> bodyType,std::mt19937& random)
{
    const auto& catalog=newestEmbeddedManifest();
    auto pick=[&](auto& range)->const auto& {
        std::uniform_int_distribution<std::size_t> index(0,std::size(range)-1);
        return range[index(random)];
    };
    AvatarDescriptor descriptor;
    descriptor.catalogVersion=catalog.version;
    descriptor.bodyType=bodyType?*bodyType:static_cast<std::uint8_t>(std::uniform_int_distribution<int>(0,1)(random));
    auto itemFor=[&](AvatarItemSlot slot) {
        std::vector<std::uint16_t> ids;
        std::vector<double> weights;
        for(const auto& item:catalog.items)
            if(item.slot==slot&&item.randomWeight[descriptor.bodyType]>0) {
                ids.push_back(item.id);
                weights.push_back(item.randomWeight[descriptor.bodyType]);
            }
        return ids[std::discrete_distribution<std::size_t>(weights.begin(),weights.end())(random)];
    };
    const int authored=catalog.authoredHeightMillimeters[descriptor.bodyType];
    descriptor.heightMillimeters=static_cast<std::uint16_t>(std::uniform_int_distribution<int>(authored-110,authored+110)(random));
    descriptor.build=static_cast<std::uint8_t>(std::uniform_int_distribution<int>(72,184)(random));
    descriptor.colors[static_cast<std::size_t>(AvatarColorSlot::Skin)]=pick(SkinTones);
    descriptor.colors[static_cast<std::size_t>(AvatarColorSlot::Hair)]=pick(HairColors);
    descriptor.colors[static_cast<std::size_t>(AvatarColorSlot::Eyes)]=pick(EyeColors);
    for(auto slot:{AvatarColorSlot::Top,AvatarColorSlot::Bottom,AvatarColorSlot::Shoes,AvatarColorSlot::Accessory})
        descriptor.colors[static_cast<std::size_t>(slot)]=pick(ClothColors);
    descriptor.items[static_cast<std::size_t>(AvatarItemSlot::Hair)]=itemFor(AvatarItemSlot::Hair);
    descriptor.items[static_cast<std::size_t>(AvatarItemSlot::Top)]=itemFor(AvatarItemSlot::Top);
    descriptor.items[static_cast<std::size_t>(AvatarItemSlot::Bottom)]=itemFor(AvatarItemSlot::Bottom);
    descriptor.items[static_cast<std::size_t>(AvatarItemSlot::Shoes)]=itemFor(AvatarItemSlot::Shoes);
    // Accessories are the exception rather than the rule.
    std::bernoulli_distribution sometimes(0.3);
    descriptor.items[static_cast<std::size_t>(AvatarItemSlot::Glasses)]=sometimes(random)?itemFor(AvatarItemSlot::Glasses):0;
    descriptor.items[static_cast<std::size_t>(AvatarItemSlot::Hat)]=sometimes(random)?itemFor(AvatarItemSlot::Hat):0;
    // A catalog with face controls gets an individual face: each byte drawn around neutral.
    if(!catalog.faceControls[descriptor.bodyType].empty()) {
        std::uniform_real_distribution<double> unit(0.0,1.0);
        for(auto& value:descriptor.face)
            value=static_cast<std::uint8_t>(std::clamp(128+static_cast<int>(std::lround((unit(random)+unit(random)-1.0)*120.0)),0,255));
    }
    std::vector<std::uint16_t> facial;
    std::vector<double> weights;
    for(const auto& item:catalog.featureItems)
        if(item.randomWeight[descriptor.bodyType]>0) {
            facial.push_back(item.id);
            weights.push_back(item.randomWeight[descriptor.bodyType]);
        }
    if(!facial.empty()&&std::bernoulli_distribution(0.35)(random))
        descriptor.facialHair=facial[std::discrete_distribution<std::size_t>(weights.begin(),weights.end())(random)];
    return descriptor;
}
}
