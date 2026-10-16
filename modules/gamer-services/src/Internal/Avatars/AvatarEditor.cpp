// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/GamerServices/AvatarEditor.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>

namespace CNA::Internal::GamerServices::Avatars {
namespace {
// Build and face-shape bytes move in sixteenths of their range, so every value is reachable and
// eight steps either way cover it.
constexpr int SliderStep=16;
constexpr int HeightStep=10;
// Face shapes read clearly on a small card: each preset's moves are applied a little stronger.
constexpr float PresetStrength=1.35f;
// A body preset's height moves this far from the authored height at either end.
constexpr int PresetHeightRange=250;

std::size_t at(AvatarColorSlot slot){return static_cast<std::size_t>(slot);}
bool optionalEntry(AvatarItemSlot slot)
{
    return slot==AvatarItemSlot::Glasses||slot==AvatarItemSlot::Hat||slot==AvatarItemSlot::FacialHair||
        (slot>=AvatarItemSlot::Earrings&&slot<=AvatarItemSlot::Rings)||slot>=AvatarItemSlot::EyeShadow;
}

std::uint8_t stepSlider(std::uint8_t value,int delta)
{
    const int steps=static_cast<int>(std::lround((value-NeutralFaceParameter)/static_cast<double>(SliderStep)))+delta;
    return static_cast<std::uint8_t>(std::clamp(NeutralFaceParameter+steps*SliderStep,0,255));
}
float sliderPosition(std::uint8_t value){return std::clamp((value-NeutralFaceParameter)/127.0f,-1.0f,1.0f);}
std::uint8_t sliderValue(float position)
{
    const int steps=static_cast<int>(std::lround(position*127.0/SliderStep));
    return static_cast<std::uint8_t>(std::clamp(NeutralFaceParameter+steps*SliderStep,0,255));
}

// "hair_side_part" -> "Side part".
std::string readable(std::string_view name)
{
    std::string text;
    for(char c:name) {
        if(c=='_'||c=='-')text+=' ';
        else if(std::isupper(static_cast<unsigned char>(c))&&!text.empty()) {
            text+=' ';
            text+=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        } else text+=c;
    }
    if(!text.empty())text[0]=static_cast<char>(std::toupper(static_cast<unsigned char>(text[0])));
    return text;
}

// Face features by what a person calls them, with the catalog control each one drives. A slider's
// two ends are named too ("Narrow"/"Wide"), so its value reads as a shape rather than a number.
struct Feature {const char* control;const char* label;const char* less;const char* more;};
constexpr Feature FaceSliders[]{
    {"headWidth","Head width","Narrow","Wide"},{"headHeight","Head length","Short","Long"},{"foreheadHeight","Forehead","Low","High"},
    {"cheekboneWidth","Cheekbones","Narrow","Wide"},{"cheekFullness","Cheeks","Lean","Full"},{"jawWidth","Jaw","Narrow","Broad"},
    {"jawTaper","Jaw taper","Square","Tapered"},{"jawAngle","Jaw corners","Soft","Wide"},{"chinSize","Chin","Small","Strong"},
    {"chinHeight","Chin length","Short","Long"},{"chinProjection","Chin depth","Receding","Forward"}};
constexpr Feature EyeSliders[]{
    {"eyeSize","Eye size","Small","Large"},{"eyeShape","Eye shape","Narrow","Round"},{"eyeSpacing","Eye spacing","Close","Wide"},
    {"eyeHeight","Eye height","Low","High"},{"eyeTilt","Eye tilt","Down","Up"}};
constexpr Feature BrowSliders[]{
    {"browHeight","Height","Low","High"},{"browSpacing","Spacing","Close","Apart"},{"browAngle","Angle","Flat","Lifted"},
    {"browThickness","Thickness","Fine","Bold"}};
constexpr Feature EarSliders[]{{"earSize","Size","Small","Large"},{"earHeight","Height","Low","High"},{"earAngle","Angle","Flat","Out"}};
constexpr Feature NoseSliders[]{
    {"noseSize","Size","Small","Large"},{"noseWidth","Width","Narrow","Wide"},{"noseHeight","Height","Low","High"},
    {"noseBridge","Bridge","Fine","Strong"},{"noseTip","Tip","Down","Up"}};
constexpr Feature MouthSliders[]{
    {"mouthWidth","Width","Narrow","Wide"},{"mouthHeight","Height","Low","High"},{"upperLip","Upper lip","Thin","Full"},
    {"lowerLip","Lower lip","Thin","Full"}};

std::span<const Feature> slidersOf(EditorCategory category)
{
    switch(category) {
    case EditorCategory::Face: return FaceSliders;
    case EditorCategory::Eyes: return EyeSliders;
    case EditorCategory::Eyebrows: return BrowSliders;
    case EditorCategory::Ears: return EarSliders;
    case EditorCategory::Nose: return NoseSliders;
    case EditorCategory::Mouth: return MouthSliders;
    default: return {};
    }
}
// The catalog facePresets group a category's shape row offers, and that row's name.
std::pair<const char*,const char*> presetsOf(EditorCategory category)
{
    switch(category) {
    case EditorCategory::Body: return {"body","Body shape"};
    case EditorCategory::Face: return {"face","Face shape"};
    case EditorCategory::Eyes: return {"eyes","Eye shape"};
    case EditorCategory::Eyebrows: return {"brows","Placement"};
    case EditorCategory::Ears: return {"ears","Placement"};
    case EditorCategory::Nose: return {"nose","Nose shape"};
    case EditorCategory::Mouth: return {"mouth","Mouth shape"};
    default: return {nullptr,nullptr};
    }
}
constexpr const char* SideNames[]{"Both","Left","Right"};
}

const char* editorCategoryName(EditorCategory category)
{
    static const char* const names[EditorCategoryCount]{"Body","Skin","Face","Eyes","Eyebrows","Ears","Nose","Mouth","Hair","Facial hair",
        "Makeup","Outfits","Tops","Bottoms","Shoes","Headwear","Glasses","Earrings","Wristwear","Gloves","Rings"};
    return names[static_cast<int>(category)];
}

const char* editorCategoryGroup(EditorCategory category)
{
    return static_cast<int>(category)<EditorFeatureCategoryCount?"Features":"Style";
}

AvatarEditorModel::AvatarEditorModel(std::shared_ptr<const CatalogManifest> catalog,std::vector<std::uint8_t> current,std::uint32_t seed)
    : catalog_(std::move(catalog)),stored_(std::move(current)),random_(seed)
{
    const auto decoded=decode(stored_);
    start_=decoded?fitted(*decoded):coherentRandom(std::uniform_int_distribution<int>(0,1)(random_)==1?1:0);
    working_=start_;
}

AvatarDescriptor AvatarEditorModel::fitted(const AvatarDescriptor& source) const
{
    // The avatar as the edited catalog can express it: entries it does not have become the kind's
    // first entry (or none), and a catalog without face controls keeps the authored face.
    AvatarDescriptor result=source;
    result.catalogVersion=catalog_->version;
    const int body=result.bodyType==1?1:0;
    for(int kind=0;kind<static_cast<int>(AvatarItemKindCount);++kind) {
        const auto slot=static_cast<AvatarItemSlot>(kind);
        const auto id=entryOf(result,slot);
        if(id==0||catalog_->item(id,slot))continue;
        const auto offered=choices(slot);
        setEntry(result,slot,optionalEntry(slot)||offered.empty()?0:offered.front());
    }
    if(catalog_->faceControls[body].empty()) {
        result.face.fill(NeutralFaceParameter);
        result.proportion=NeutralFaceParameter;
    }
    return result;
}

std::uint16_t AvatarEditorModel::entryOf(const AvatarDescriptor& descriptor,AvatarItemSlot slot) const
{
    if(static_cast<std::size_t>(slot)<AvatarItemSlotCount)return descriptor.items[static_cast<std::size_t>(slot)];
    switch(slot) {
    case AvatarItemSlot::FacialHair: return descriptor.facialHair;
    case AvatarItemSlot::Ears: return descriptor.ears;
    case AvatarItemSlot::Eyebrows: return descriptor.eyebrows;
    default: break;
    }
    if(slot>=AvatarItemSlot::Earrings&&slot<=AvatarItemSlot::Rings)return descriptor.accessories[accessoryIndex(slot)];
    return descriptor.makeup[makeupIndex(slot)];
}

void AvatarEditorModel::setEntry(AvatarDescriptor& descriptor,AvatarItemSlot slot,std::uint16_t id) const
{
    if(static_cast<std::size_t>(slot)<AvatarItemSlotCount) {
        descriptor.items[static_cast<std::size_t>(slot)]=id;
        return;
    }
    switch(slot) {
    case AvatarItemSlot::FacialHair: descriptor.facialHair=id; return;
    case AvatarItemSlot::Ears: {
        // The first ear shape is the catalog's default, which a description names as 0.
        const auto ears=catalog_->itemsOf(AvatarItemSlot::Ears);
        descriptor.ears=!ears.empty()&&ears.front()->id==id?0:id;
        return;
    }
    case AvatarItemSlot::Eyebrows: descriptor.eyebrows=id; return;
    default: break;
    }
    if(slot>=AvatarItemSlot::Earrings&&slot<=AvatarItemSlot::Rings) {
        const auto index=accessoryIndex(slot);
        descriptor.accessories[index]=id;
        if(id==0||slot==AvatarItemSlot::Gloves)descriptor.sides[index]=AvatarSides::Both;
        return;
    }
    descriptor.makeup[makeupIndex(slot)]=id;
}

std::vector<std::uint16_t> AvatarEditorModel::choices(AvatarItemSlot slot) const
{
    std::vector<std::uint16_t> ids;
    // Optional kinds start with none; eyebrows with the classic brows every description without a
    // style has; ears have no "none".
    if(optionalEntry(slot)||slot==AvatarItemSlot::Eyebrows)ids.push_back(0);
    for(const auto* entry:catalog_->itemsOf(slot))ids.push_back(entry->id);
    return ids;
}

std::string AvatarEditorModel::entryName(std::uint16_t id,AvatarItemSlot slot) const
{
    if(id==0) {
        if(slot==AvatarItemSlot::Eyebrows)return "Classic";
        if(slot==AvatarItemSlot::Ears) {
            const auto ears=catalog_->itemsOf(AvatarItemSlot::Ears);
            return ears.empty()?"Default":entryName(ears.front()->id,slot);
        }
        return "None";
    }
    const auto* entry=catalog_->entry(id);
    if(!entry)return "Item "+std::to_string(id);
    if(!entry->label.empty())return entry->label;
    std::string_view name=entry->name;
    // Catalog names carry their kind as a prefix ("top_hoodie", "facial_beard").
    if(const auto underscore=name.find('_');underscore!=std::string_view::npos)name.remove_prefix(underscore+1);
    return readable(name);
}

std::vector<AvatarEditorModel::Row> AvatarEditorModel::rows() const
{
    std::vector<Row> list;
    const int body=working_.bodyType==1?1:0;
    auto entry=[&](AvatarItemSlot slot,const char* label){if(!catalog_->itemsOf(slot).empty())list.push_back({Kind::Entry,static_cast<int>(slot),label});};
    auto color=[&](AvatarColorSlot slot,const char* label){list.push_back({Kind::Color,static_cast<int>(slot),label});};
    auto worn=[&](AvatarItemSlot slot){return entryOf(working_,slot)!=0;};
    auto shape=[&] {
        const auto [group,label]=presetsOf(category_);
        if(group&&std::ranges::any_of(catalog_->facePresets,[&](const FacePreset& preset){return preset.group==group;}))
            list.push_back({Kind::Preset,0,label});
        for(const auto& feature:slidersOf(category_))
            if(const int parameter=catalog_->faceParameter(body,feature.control);parameter>=0)list.push_back({Kind::Face,parameter,feature.label});
    };
    const bool shaped=!catalog_->faceControls[body].empty();
    switch(category_) {
    case EditorCategory::Body:
        list.push_back({Kind::BodyType,0,"Body type"});
        if(shaped)shape();
        list.push_back({Kind::Height,0,"Height"});
        list.push_back({Kind::Build,0,"Build"});
        if(shaped)list.push_back({Kind::Proportion,0,"Proportions"});
        break;
    case EditorCategory::Skin: color(AvatarColorSlot::Skin,"Skin tone"); break;
    case EditorCategory::Face: if(shaped)shape(); break;
    case EditorCategory::Eyes: color(AvatarColorSlot::Eyes,"Eye color"); if(shaped)shape(); break;
    case EditorCategory::Eyebrows: entry(AvatarItemSlot::Eyebrows,"Style"); if(shaped)shape(); break;
    case EditorCategory::Ears: entry(AvatarItemSlot::Ears,"Shape"); if(shaped)shape(); break;
    case EditorCategory::Nose: case EditorCategory::Mouth: if(shaped)shape(); break;
    case EditorCategory::Hair: entry(AvatarItemSlot::Hair,"Hairstyle"); color(AvatarColorSlot::Hair,"Hair color"); break;
    case EditorCategory::FacialHair: entry(AvatarItemSlot::FacialHair,"Style"); color(AvatarColorSlot::Hair,"Color"); break;
    case EditorCategory::Makeup:
        entry(AvatarItemSlot::EyeShadow,"Eye shadow");
        if(worn(AvatarItemSlot::EyeShadow))color(AvatarColorSlot::EyeShadow,"Shadow color");
        entry(AvatarItemSlot::Eyeliner,"Eyeliner");
        entry(AvatarItemSlot::Blush,"Blush");
        if(worn(AvatarItemSlot::Blush))color(AvatarColorSlot::Blush,"Blush color");
        entry(AvatarItemSlot::Lips,"Lips");
        if(worn(AvatarItemSlot::Lips))color(AvatarColorSlot::Lips,"Lip color");
        break;
    case EditorCategory::Outfits: if(!catalog_->outfits.empty())list.push_back({Kind::Outfit,0,"Outfit"}); break;
    case EditorCategory::Tops: entry(AvatarItemSlot::Top,"Top"); color(AvatarColorSlot::Top,"Color"); break;
    case EditorCategory::Bottoms: entry(AvatarItemSlot::Bottom,"Bottoms"); color(AvatarColorSlot::Bottom,"Color"); break;
    case EditorCategory::Shoes: entry(AvatarItemSlot::Shoes,"Shoes"); color(AvatarColorSlot::Shoes,"Color"); break;
    case EditorCategory::Headwear: entry(AvatarItemSlot::Hat,"Headwear"); color(AvatarColorSlot::Accessory,"Color"); break;
    case EditorCategory::Glasses: entry(AvatarItemSlot::Glasses,"Glasses"); color(AvatarColorSlot::Accessory,"Frame color"); break;
    case EditorCategory::Earrings: case EditorCategory::Wristwear: case EditorCategory::Gloves: case EditorCategory::Rings: {
        const auto slot=category_==EditorCategory::Earrings?AvatarItemSlot::Earrings:category_==EditorCategory::Wristwear?
            AvatarItemSlot::Wristwear:category_==EditorCategory::Gloves?AvatarItemSlot::Gloves:AvatarItemSlot::Rings;
        entry(slot,editorCategoryName(category_));
        if(!worn(slot))break;
        if(slot!=AvatarItemSlot::Gloves)list.push_back({Kind::Sides,static_cast<int>(accessoryIndex(slot)),"Worn on"});
        if(slot==AvatarItemSlot::Gloves)color(AvatarColorSlot::Gloves,"Color");
        else color(AvatarColorSlot::Jewelry,"Metal");
        if(slot==AvatarItemSlot::Wristwear)color(AvatarColorSlot::Accessory,"Band color");
        break;
    }
    }
    return list;
}

void AvatarEditorModel::setCategory(EditorCategory category)
{
    category_=category;
    selection_=0;
}

void AvatarEditorModel::turnPage(int delta)
{
    setCategory(static_cast<EditorCategory>(((static_cast<int>(category_)+delta)%EditorCategoryCount+EditorCategoryCount)%EditorCategoryCount));
}

EditorView AvatarEditorModel::view() const
{
    switch(category_) {
    case EditorCategory::Face: case EditorCategory::Eyes: case EditorCategory::Eyebrows: case EditorCategory::Nose:
    case EditorCategory::Mouth: case EditorCategory::Makeup:
        return EditorView::Face;
    case EditorCategory::Hair: case EditorCategory::FacialHair: case EditorCategory::Glasses: case EditorCategory::Headwear:
        return EditorView::Head;
    case EditorCategory::Ears: case EditorCategory::Earrings: return EditorView::Profile;
    case EditorCategory::Tops: return EditorView::Upper;
    case EditorCategory::Bottoms: return EditorView::Lower;
    case EditorCategory::Shoes: return EditorView::Feet;
    case EditorCategory::Wristwear: case EditorCategory::Gloves: case EditorCategory::Rings: return EditorView::Hands;
    default: return EditorView::Body;
    }
}

void AvatarEditorModel::select(int delta)
{
    const auto count=static_cast<int>(fieldCount());
    if(count==0)return;
    selection_=static_cast<std::size_t>(std::clamp(static_cast<int>(selection_)+delta,0,count-1));
}

void AvatarEditorModel::selectRow(std::size_t index)
{
    if(index<fieldCount())selection_=index;
}

EditorField AvatarEditorModel::field(std::size_t index) const
{
    const auto list=rows();
    if(index>=list.size())return {};
    const auto& row=list[index];
    EditorField result{row.label,{},EditorRowKind::Cards,std::nullopt,std::nullopt,{}};
    // "Average", "A little wide", "Wide", "Very wide".
    auto sliderText=[&](std::uint8_t value,const char* less,const char* more) {
        const int steps=static_cast<int>(std::lround((value-NeutralFaceParameter)/static_cast<double>(SliderStep)));
        if(steps==0)return std::string("Average");
        std::string way=steps<0?less:more;
        const int size=std::abs(steps);
        if(size>2&&size<=5)return way;
        way[0]=static_cast<char>(std::tolower(static_cast<unsigned char>(way[0])));
        return (size<=2?"A little ":"Very ")+way;
    };
    switch(row.kind) {
    case Kind::BodyType:
        result.value=working_.bodyType==1?"Male":"Female";
        break;
    case Kind::Height: {
        char text[16];
        std::snprintf(text,sizeof text,"%.2f m",working_.heightMillimeters/1000.0);
        result.value=text;
        result.kind=EditorRowKind::Slider;
        result.slider=std::clamp((working_.heightMillimeters-(MinimumHeightMillimeters+MaximumHeightMillimeters)/2.0f)/
                                 ((MaximumHeightMillimeters-MinimumHeightMillimeters)/2.0f),-1.0f,1.0f);
        break;
    }
    case Kind::Build:
        result.value=sliderText(working_.build,"Slim","Heavy");
        result.kind=EditorRowKind::Slider;
        result.slider=sliderPosition(working_.build);
        break;
    case Kind::Proportion:
        result.value=sliderText(working_.proportion,"Compact","Long-limbed");
        result.kind=EditorRowKind::Slider;
        result.slider=sliderPosition(working_.proportion);
        break;
    case Kind::Color: {
        const auto slot=static_cast<AvatarColorSlot>(row.index);
        const auto color=drawnColor(working_,slot);
        const auto palette=colorPalette(slot);
        const auto found=std::ranges::find(palette,color);
        result.value=found==palette.end()?"Custom":std::to_string(found-palette.begin()+1)+" of "+std::to_string(palette.size());
        result.kind=EditorRowKind::Swatches;
        result.swatch=color;
        if(category_==EditorCategory::FacialHair)result.note="Facial hair and eyebrows take the hair color.";
        break;
    }
    case Kind::Entry: {
        const auto slot=static_cast<AvatarItemSlot>(row.index);
        result.value=entryName(entryOf(working_,slot),slot);
        if(slot==AvatarItemSlot::Eyebrows)result.note="Eyebrows take the hair color.";
        if(slot==AvatarItemSlot::Rings&&working_.accessories[accessoryIndex(AvatarItemSlot::Gloves)])result.note="Gloves hide rings.";
        break;
    }
    case Kind::Sides:
        result.value=SideNames[static_cast<int>(working_.sides[static_cast<std::size_t>(row.index)])];
        result.kind=EditorRowKind::Chips;
        break;
    case Kind::Face: {
        const auto value=working_.face[static_cast<std::size_t>(row.index)];
        const int body=working_.bodyType==1?1:0;
        const Feature* feature=nullptr;
        for(const auto& candidate:slidersOf(category_))if(catalog_->faceParameter(body,candidate.control)==row.index)feature=&candidate;
        result.value=feature?sliderText(value,feature->less,feature->more):std::to_string(value);
        result.kind=EditorRowKind::Slider;
        result.slider=sliderPosition(value);
        break;
    }
    case Kind::Preset:
    case Kind::Outfit:
        result.kind=row.kind==Kind::Preset?EditorRowKind::Presets:EditorRowKind::Cards;
        result.value=row.kind==Kind::Preset?"Custom":"Your own";
        for(const auto& option:options(index))if(option.current)result.value=option.label;
        break;
    }
    return result;
}

AvatarDescriptor AvatarEditorModel::withPreset(const AvatarDescriptor& source,const FacePreset& preset) const
{
    auto result=source;
    const int body=result.bodyType==1?1:0;
    auto value=[&](std::string_view name){
        for(const auto& [control,amount]:preset.values)if(control==name)return amount;
        return 0.0f;
    };
    if(preset.group=="body") {
        const int authored=catalog_->authoredHeightMillimeters[body];
        const int height=authored+static_cast<int>(std::lround(value("height")*PresetHeightRange/HeightStep))*HeightStep;
        result.heightMillimeters=static_cast<std::uint16_t>(std::clamp<int>(height,MinimumHeightMillimeters,MaximumHeightMillimeters));
        result.build=sliderValue(value("build"));
        if(!catalog_->faceControls[body].empty())result.proportion=sliderValue(value("proportion"));
        return result;
    }
    // Every control of the page is set (0 when the preset leaves it), and any other control the
    // preset names too.
    for(const auto& feature:slidersOf(category_))
        if(const int parameter=catalog_->faceParameter(body,feature.control);parameter>=0)
            result.face[static_cast<std::size_t>(parameter)]=sliderValue(std::clamp(value(feature.control)*PresetStrength,-1.0f,1.0f));
    for(const auto& [control,amount]:preset.values)
        if(const int parameter=catalog_->faceParameter(body,control);parameter>=0)
            result.face[static_cast<std::size_t>(parameter)]=sliderValue(std::clamp(amount*PresetStrength,-1.0f,1.0f));
    return result;
}

std::vector<EditorOption> AvatarEditorModel::options(std::size_t index) const
{
    const auto list=rows();
    std::vector<EditorOption> out;
    if(index>=list.size())return out;
    const auto& row=list[index];
    switch(row.kind) {
    case Kind::BodyType:
        for(std::uint8_t body=0;body<2;++body) {
            auto result=working_;
            if(result.bodyType!=body) {
                const int offset=working_.heightMillimeters-catalog_->authoredHeightMillimeters[working_.bodyType==1?1:0];
                result.bodyType=body;
                result.heightMillimeters=static_cast<std::uint16_t>(std::clamp<int>(catalog_->authoredHeightMillimeters[body]+offset,
                    MinimumHeightMillimeters,MaximumHeightMillimeters));
                if(catalog_->faceControls[body].empty()) {
                    result.face.fill(NeutralFaceParameter);
                    result.proportion=NeutralFaceParameter;
                }
            }
            out.push_back({body==1?"Male":"Female",result,working_.bodyType==body,std::nullopt});
        }
        break;
    case Kind::Color: {
        const auto slot=static_cast<AvatarColorSlot>(row.index);
        const auto current=drawnColor(working_,slot);
        for(const auto& candidate:colorPalette(slot)) {
            auto result=working_;
            result.colors[at(slot)]=candidate;
            out.push_back({{},result,candidate==current,candidate});
        }
        break;
    }
    case Kind::Entry: {
        const auto slot=static_cast<AvatarItemSlot>(row.index);
        auto current=entryOf(working_,slot);
        if(slot==AvatarItemSlot::Ears&&current==0)
            if(const auto ears=catalog_->itemsOf(AvatarItemSlot::Ears);!ears.empty())current=ears.front()->id;
        for(auto id:choices(slot)) {
            auto result=working_;
            setEntry(result,slot,id);
            out.push_back({entryName(id,slot),result,id==current,std::nullopt});
        }
        break;
    }
    case Kind::Sides:
        for(std::uint8_t side=0;side<3;++side) {
            auto result=working_;
            result.sides[static_cast<std::size_t>(row.index)]=static_cast<AvatarSides>(side);
            out.push_back({SideNames[side],result,working_.sides[static_cast<std::size_t>(row.index)]==static_cast<AvatarSides>(side),std::nullopt});
        }
        break;
    case Kind::Preset: {
        const auto group=presetsOf(category_).first;
        for(const auto& preset:catalog_->facePresets) {
            if(preset.group!=group)continue;
            auto result=withPreset(working_,preset);
            const bool current=preset.group=="body"?result.heightMillimeters==working_.heightMillimeters&&result.build==working_.build&&
                result.proportion==working_.proportion:result.face==working_.face;
            out.push_back({preset.name,std::move(result),current,std::nullopt});
        }
        break;
    }
    case Kind::Outfit:
        for(const auto& outfit:catalog_->outfits) {
            auto result=working_;
            for(const auto& [slot,id]:outfit.items)setEntry(result,slot,id);
            for(const auto& [slot,color]:outfit.colors)result.colors[at(slot)]=color;
            out.push_back({outfit.name,result,result==working_,std::nullopt});
        }
        break;
    case Kind::Height:
    case Kind::Build:
    case Kind::Proportion:
    case Kind::Face:
        break;
    }
    return out;
}

bool AvatarEditorModel::choose(std::size_t row,std::size_t option)
{
    const auto list=options(row);
    if(option>=list.size())return false;
    const auto before=working_;
    working_=list[option].result;
    // Makeup's first look in a colour: the palette's first rather than unset black.
    for(auto [style,color]:{std::pair{AvatarItemSlot::EyeShadow,AvatarColorSlot::EyeShadow},{AvatarItemSlot::Blush,AvatarColorSlot::Blush},
                            {AvatarItemSlot::Lips,AvatarColorSlot::Lips}})
        if(working_.makeup[makeupIndex(style)]&&working_.colors[at(color)]==AvatarColor{})working_.colors[at(color)]=drawnColor(working_,color);
    selection_=std::min(row,fieldCount()-1);
    return !(working_==before);
}

bool AvatarEditorModel::adjust(int delta)
{
    const auto list=rows();
    if(selection_>=list.size()||delta==0)return false;
    const auto& row=list[selection_];
    const auto before=working_;
    switch(row.kind) {
    case Kind::Height:
        working_.heightMillimeters=static_cast<std::uint16_t>(std::clamp<int>(
            (working_.heightMillimeters+delta*HeightStep)/HeightStep*HeightStep,MinimumHeightMillimeters,MaximumHeightMillimeters));
        break;
    case Kind::Build:
        working_.build=stepSlider(working_.build,delta);
        break;
    case Kind::Proportion:
        working_.proportion=stepSlider(working_.proportion,delta);
        break;
    case Kind::Face:
        working_.face[static_cast<std::size_t>(row.index)]=stepSlider(working_.face[static_cast<std::size_t>(row.index)],delta);
        break;
    default: {
        // Cards, swatches, presets and chips step to the neighbouring choice, wrapping.
        const auto choicesList=options(selection_);
        if(choicesList.empty())break;
        const auto current=std::ranges::find_if(choicesList,&EditorOption::current);
        const int count=static_cast<int>(choicesList.size());
        const int index=current==choicesList.end()?(delta>0?-1:0):static_cast<int>(current-choicesList.begin());
        return choose(selection_,static_cast<std::size_t>(((index+delta)%count+count)%count));
    }
    }
    selection_=std::min(selection_,fieldCount()-1);
    return !(working_==before);
}

AvatarDescriptor AvatarEditorModel::coherentRandom(std::uint8_t bodyType)
{
    return randomDescriptor(*catalog_,bodyType,random_);
}

void AvatarEditorModel::randomize()
{
    working_=coherentRandom(working_.bodyType);
    selection_=std::min(selection_,fieldCount()?fieldCount()-1:0);
}

void AvatarEditorModel::takeCategory(const AvatarDescriptor& source)
{
    const int body=working_.bodyType==1?1:0;
    auto takeColor=[&](AvatarColorSlot slot){working_.colors[at(slot)]=source.colors[at(slot)];};
    auto takeEntry=[&](AvatarItemSlot slot) {
        setEntry(working_,slot,entryOf(source,slot));
        if(slot>=AvatarItemSlot::Earrings&&slot<=AvatarItemSlot::Rings)working_.sides[accessoryIndex(slot)]=source.sides[accessoryIndex(slot)];
    };
    auto takeSliders=[&] {
        for(const auto& feature:slidersOf(category_))
            if(const int parameter=catalog_->faceParameter(body,feature.control);parameter>=0)
                working_.face[static_cast<std::size_t>(parameter)]=source.face[static_cast<std::size_t>(parameter)];
    };
    switch(category_) {
    case EditorCategory::Body:
        working_.heightMillimeters=source.heightMillimeters;working_.build=source.build;working_.proportion=source.proportion;break;
    case EditorCategory::Skin: takeColor(AvatarColorSlot::Skin); break;
    case EditorCategory::Face: case EditorCategory::Nose: case EditorCategory::Mouth: takeSliders(); break;
    case EditorCategory::Eyes: takeSliders(); takeColor(AvatarColorSlot::Eyes); break;
    case EditorCategory::Eyebrows: takeSliders(); takeEntry(AvatarItemSlot::Eyebrows); break;
    case EditorCategory::Ears: takeSliders(); takeEntry(AvatarItemSlot::Ears); break;
    case EditorCategory::Hair: takeEntry(AvatarItemSlot::Hair); takeColor(AvatarColorSlot::Hair); break;
    case EditorCategory::FacialHair: takeEntry(AvatarItemSlot::FacialHair); break;
    case EditorCategory::Makeup:
        for(auto slot:{AvatarItemSlot::EyeShadow,AvatarItemSlot::Eyeliner,AvatarItemSlot::Blush,AvatarItemSlot::Lips})takeEntry(slot);
        for(auto slot:{AvatarColorSlot::EyeShadow,AvatarColorSlot::Blush,AvatarColorSlot::Lips})takeColor(slot);
        break;
    case EditorCategory::Outfits:
        for(auto slot:{AvatarItemSlot::Top,AvatarItemSlot::Bottom,AvatarItemSlot::Shoes,AvatarItemSlot::Hat,AvatarItemSlot::Gloves})takeEntry(slot);
        for(auto slot:{AvatarColorSlot::Top,AvatarColorSlot::Bottom,AvatarColorSlot::Shoes,AvatarColorSlot::Accessory,AvatarColorSlot::Gloves})
            takeColor(slot);
        break;
    case EditorCategory::Tops: takeEntry(AvatarItemSlot::Top); takeColor(AvatarColorSlot::Top); break;
    case EditorCategory::Bottoms: takeEntry(AvatarItemSlot::Bottom); takeColor(AvatarColorSlot::Bottom); break;
    case EditorCategory::Shoes: takeEntry(AvatarItemSlot::Shoes); takeColor(AvatarColorSlot::Shoes); break;
    case EditorCategory::Headwear: takeEntry(AvatarItemSlot::Hat); takeColor(AvatarColorSlot::Accessory); break;
    case EditorCategory::Glasses: takeEntry(AvatarItemSlot::Glasses); takeColor(AvatarColorSlot::Accessory); break;
    case EditorCategory::Earrings: takeEntry(AvatarItemSlot::Earrings); takeColor(AvatarColorSlot::Jewelry); break;
    case EditorCategory::Wristwear: takeEntry(AvatarItemSlot::Wristwear); takeColor(AvatarColorSlot::Jewelry); takeColor(AvatarColorSlot::Accessory); break;
    case EditorCategory::Gloves: takeEntry(AvatarItemSlot::Gloves); takeColor(AvatarColorSlot::Gloves); break;
    case EditorCategory::Rings: takeEntry(AvatarItemSlot::Rings); takeColor(AvatarColorSlot::Jewelry); break;
    }
}

void AvatarEditorModel::randomizeCategory()
{
    // Only what the category shown covers changes. A category that is mostly "none" in a random
    // avatar (accessories, makeup) always gets something here: the player asked for one.
    auto fresh=coherentRandom(working_.bodyType);
    std::uniform_real_distribution<float> unit(0.0f,1.0f);
    auto pick=[&](AvatarItemSlot slot) {
        const auto offered=choices(slot);
        std::vector<std::uint16_t> real;
        for(auto id:offered)if(id)real.push_back(id);
        if(real.empty())return std::uint16_t{0};
        return real[std::uniform_int_distribution<std::size_t>(0,real.size()-1)(random_)];
    };
    auto palette=[&](AvatarColorSlot slot,std::size_t natural) {
        const auto colors=colorPalette(slot);
        return colors[std::uniform_int_distribution<std::size_t>(0,std::min(natural,colors.size())-1)(random_)];
    };
    switch(category_) {
    case EditorCategory::Makeup:
        for(auto slot:{AvatarItemSlot::EyeShadow,AvatarItemSlot::Eyeliner,AvatarItemSlot::Blush,AvatarItemSlot::Lips})
            setEntry(fresh,slot,unit(random_)<0.65f?pick(slot):0);
        fresh.colors[at(AvatarColorSlot::EyeShadow)]=palette(AvatarColorSlot::EyeShadow,8);
        fresh.colors[at(AvatarColorSlot::Blush)]=palette(AvatarColorSlot::Blush,5);
        fresh.colors[at(AvatarColorSlot::Lips)]=palette(AvatarColorSlot::Lips,8);
        break;
    case EditorCategory::Outfits:
        if(!catalog_->outfits.empty()) {
            const auto& outfit=catalog_->outfits[std::uniform_int_distribution<std::size_t>(0,catalog_->outfits.size()-1)(random_)];
            for(const auto& [slot,id]:outfit.items)setEntry(fresh,slot,id);
            for(const auto& [slot,color]:outfit.colors)fresh.colors[at(slot)]=color;
        }
        break;
    case EditorCategory::Headwear: setEntry(fresh,AvatarItemSlot::Hat,pick(AvatarItemSlot::Hat)); break;
    case EditorCategory::Glasses: setEntry(fresh,AvatarItemSlot::Glasses,pick(AvatarItemSlot::Glasses)); break;
    case EditorCategory::FacialHair: if(working_.bodyType==1)setEntry(fresh,AvatarItemSlot::FacialHair,pick(AvatarItemSlot::FacialHair)); break;
    case EditorCategory::Earrings: case EditorCategory::Wristwear: case EditorCategory::Gloves: case EditorCategory::Rings: {
        const auto slot=category_==EditorCategory::Earrings?AvatarItemSlot::Earrings:category_==EditorCategory::Wristwear?
            AvatarItemSlot::Wristwear:category_==EditorCategory::Gloves?AvatarItemSlot::Gloves:AvatarItemSlot::Rings;
        setEntry(fresh,slot,pick(slot));
        if(slot!=AvatarItemSlot::Gloves&&fresh.accessories[accessoryIndex(slot)])
            fresh.sides[accessoryIndex(slot)]=slot==AvatarItemSlot::Earrings?AvatarSides::Both:
                static_cast<AvatarSides>(std::uniform_int_distribution<int>(0,2)(random_));
        fresh.colors[at(AvatarColorSlot::Jewelry)]=palette(AvatarColorSlot::Jewelry,6);
        fresh.colors[at(AvatarColorSlot::Gloves)]=palette(AvatarColorSlot::Gloves,24);
        break;
    }
    default: break;
    }
    takeCategory(fresh);
}

bool AvatarEditorModel::resetCategory()
{
    const auto before=working_;
    takeCategory(start_);
    selection_=std::min(selection_,fieldCount()?fieldCount()-1:0);
    return !(working_==before);
}

void AvatarEditorModel::revert()
{
    working_=start_;
    selection_=std::min(selection_,fieldCount()?fieldCount()-1:0);
}

void AvatarEditorModel::markSaved()
{
    stored_=encoded();
    start_=working_;
}
}
