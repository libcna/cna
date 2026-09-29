// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/GamerServices/AvatarDescriptionCodec.hpp"
#include <algorithm>
#include <stdexcept>

namespace CNA::Internal::GamerServices::Avatars {
namespace {
// v1 layout. Everything after the item ids up to the checksum is reserved and must be zero, so a
// later format can use it only by changing byte 0.
constexpr std::size_t MagicOffset=1, BodyOffset=4, HeightOffset=5, BuildOffset=7, CatalogOffset=8;
constexpr std::size_t ColorOffset=10, ItemOffset=ColorOffset+AvatarColorSlotCount*3;
constexpr std::size_t ReservedOffset=ItemOffset+AvatarItemSlotCount*2, ChecksumOffset=DescriptionSize-4;
constexpr std::array<std::uint8_t,3> Magic{'C','N','A'};

constexpr AvatarCatalogItem Items[]={
    {1,AvatarItemSlot::Hair,"hair_short"},{2,AvatarItemSlot::Hair,"hair_spiky"},{3,AvatarItemSlot::Hair,"hair_bob"},
    {4,AvatarItemSlot::Hair,"hair_ponytail"},{5,AvatarItemSlot::Hair,"hair_buzz"},
    {20,AvatarItemSlot::Top,"top_tshirt"},{21,AvatarItemSlot::Top,"top_longsleeve"},{22,AvatarItemSlot::Top,"top_hoodie"},
    {23,AvatarItemSlot::Top,"top_tank"},
    {40,AvatarItemSlot::Bottom,"bottom_jeans"},{41,AvatarItemSlot::Bottom,"bottom_shorts"},{42,AvatarItemSlot::Bottom,"bottom_skirt"},
    {60,AvatarItemSlot::Shoes,"shoes_sneakers"},{61,AvatarItemSlot::Shoes,"shoes_boots"},
    {80,AvatarItemSlot::Glasses,"glasses_round"},{81,AvatarItemSlot::Glasses,"glasses_square"},
    {100,AvatarItemSlot::Hat,"hat_cap"},{101,AvatarItemSlot::Hat,"hat_beanie"},
};

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

std::span<const AvatarCatalogItem> catalogItems(){return Items;}

const AvatarCatalogItem* findCatalogItem(std::uint16_t id)
{
    auto found=std::ranges::find(Items,id,&AvatarCatalogItem::id);
    return found==std::end(Items)?nullptr:&*found;
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

bool isEncodable(const AvatarDescriptor& descriptor)
{
    if(descriptor.bodyType>1||descriptor.catalogVersion==0)return false;
    if(descriptor.heightMillimeters<MinimumHeightMillimeters||descriptor.heightMillimeters>MaximumHeightMillimeters)return false;
    // Ids of a newer catalog cannot be checked here; the asset resolver handles them.
    if(descriptor.catalogVersion>CatalogVersion)return true;
    for(std::size_t slot=0;slot<AvatarItemSlotCount;++slot) {
        const auto id=descriptor.items[slot];
        if(id==0) {if(!optionalSlot(static_cast<AvatarItemSlot>(slot)))return false;continue;}
        const auto* item=findCatalogItem(id);
        if(!item||item->slot!=static_cast<AvatarItemSlot>(slot))return false;
    }
    return true;
}

std::vector<std::uint8_t> encode(const AvatarDescriptor& descriptor)
{
    if(!isEncodable(descriptor))throw std::invalid_argument("avatar descriptor outside the CNA v1 encoding");
    std::vector<std::uint8_t> bytes(DescriptionSize,0);
    bytes[0]=FormatVersion;
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
    const auto crc=crc32(std::span(bytes).first(ChecksumOffset));
    for(int shift=0;shift<4;++shift)bytes[ChecksumOffset+shift]=static_cast<std::uint8_t>(crc>>(shift*8));
    return bytes;
}

std::optional<AvatarDescriptor> decode(std::span<const std::uint8_t> bytes)
{
    if(bytes.size()!=static_cast<std::size_t>(DescriptionSize)||bytes[0]!=FormatVersion)return std::nullopt;
    if(!std::equal(Magic.begin(),Magic.end(),bytes.begin()+MagicOffset))return std::nullopt;
    std::uint32_t stored=0;
    for(int shift=0;shift<4;++shift)stored|=static_cast<std::uint32_t>(bytes[ChecksumOffset+shift])<<(shift*8);
    if(stored!=crc32(bytes.first(ChecksumOffset)))return std::nullopt;
    if(std::any_of(bytes.begin()+ReservedOffset,bytes.begin()+ChecksumOffset,[](auto byte){return byte!=0;}))return std::nullopt;
    AvatarDescriptor descriptor;
    descriptor.bodyType=bytes[BodyOffset];
    descriptor.heightMillimeters=read16(bytes,HeightOffset);
    descriptor.build=bytes[BuildOffset];
    descriptor.catalogVersion=read16(bytes,CatalogOffset);
    for(std::size_t index=0;index<AvatarColorSlotCount;++index)
        descriptor.colors[index]={bytes[ColorOffset+index*3],bytes[ColorOffset+index*3+1],bytes[ColorOffset+index*3+2]};
    for(std::size_t index=0;index<AvatarItemSlotCount;++index)descriptor.items[index]=read16(bytes,ItemOffset+index*2);
    if(!isEncodable(descriptor))return std::nullopt;
    return descriptor;
}

std::uint16_t authoredHeightMillimeters(std::uint8_t bodyType){return bodyType==1?1800:1680;}

AvatarDescriptor randomDescriptor(std::optional<std::uint8_t> bodyType,std::mt19937& random)
{
    auto pick=[&](auto& range)->const auto& {
        std::uniform_int_distribution<std::size_t> index(0,std::size(range)-1);
        return range[index(random)];
    };
    auto itemFor=[&](AvatarItemSlot slot,bool allowNone) {
        std::vector<std::uint16_t> ids;
        if(allowNone)ids.push_back(0);
        for(const auto& item:Items)if(item.slot==slot)ids.push_back(item.id);
        return pick(ids);
    };
    AvatarDescriptor descriptor;
    descriptor.bodyType=bodyType?*bodyType:static_cast<std::uint8_t>(std::uniform_int_distribution<int>(0,1)(random));
    const int authored=authoredHeightMillimeters(descriptor.bodyType);
    descriptor.heightMillimeters=static_cast<std::uint16_t>(std::uniform_int_distribution<int>(authored-110,authored+110)(random));
    descriptor.build=static_cast<std::uint8_t>(std::uniform_int_distribution<int>(72,184)(random));
    descriptor.colors[static_cast<std::size_t>(AvatarColorSlot::Skin)]=pick(SkinTones);
    descriptor.colors[static_cast<std::size_t>(AvatarColorSlot::Hair)]=pick(HairColors);
    descriptor.colors[static_cast<std::size_t>(AvatarColorSlot::Eyes)]=pick(EyeColors);
    for(auto slot:{AvatarColorSlot::Top,AvatarColorSlot::Bottom,AvatarColorSlot::Shoes,AvatarColorSlot::Accessory})
        descriptor.colors[static_cast<std::size_t>(slot)]=pick(ClothColors);
    descriptor.items[static_cast<std::size_t>(AvatarItemSlot::Hair)]=itemFor(AvatarItemSlot::Hair,false);
    descriptor.items[static_cast<std::size_t>(AvatarItemSlot::Top)]=itemFor(AvatarItemSlot::Top,false);
    descriptor.items[static_cast<std::size_t>(AvatarItemSlot::Bottom)]=itemFor(AvatarItemSlot::Bottom,false);
    descriptor.items[static_cast<std::size_t>(AvatarItemSlot::Shoes)]=itemFor(AvatarItemSlot::Shoes,false);
    // Accessories are the exception rather than the rule.
    std::bernoulli_distribution sometimes(0.3);
    descriptor.items[static_cast<std::size_t>(AvatarItemSlot::Glasses)]=sometimes(random)?itemFor(AvatarItemSlot::Glasses,false):0;
    descriptor.items[static_cast<std::size_t>(AvatarItemSlot::Hat)]=sometimes(random)?itemFor(AvatarItemSlot::Hat,false):0;
    return descriptor;
}
}
