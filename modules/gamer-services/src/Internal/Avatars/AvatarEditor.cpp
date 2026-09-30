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

std::size_t at(AvatarColorSlot slot){return static_cast<std::size_t>(slot);}
std::size_t at(AvatarItemSlot slot){return static_cast<std::size_t>(slot);}
bool optionalSlot(AvatarItemSlot slot){return slot==AvatarItemSlot::Glasses||slot==AvatarItemSlot::Hat;}

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
constexpr Feature FaceFeatures[]{
    {"headWidth","Head width","Narrow","Wide"},{"headHeight","Head length","Short","Long"},{"jawWidth","Jaw","Narrow","Broad"},
    {"chinSize","Chin","Small","Strong"},{"cheekFullness","Cheeks","Lean","Full"},{"earSize","Ears","Small","Large"}};
constexpr Feature EyeFeatures[]{
    {"eyeSize","Eye size","Small","Large"},{"eyeSpacing","Eye spacing","Close","Wide"},{"eyeHeight","Eye height","Low","High"},
    {"eyeTilt","Eye tilt","Down","Up"},{"browHeight","Brows","Low","High"}};
constexpr Feature NoseMouthFeatures[]{
    {"noseSize","Nose size","Small","Large"},{"noseWidth","Nose width","Narrow","Wide"},{"noseHeight","Nose height","Low","High"},
    {"mouthWidth","Mouth width","Narrow","Wide"},{"mouthHeight","Mouth height","Low","High"}};

// Named shapes, each a few features moved together (-1..1 of their range). "Balanced" is the
// authored face.
struct Preset {const char* label;std::array<std::pair<const char*,float>,5> moves;};
constexpr Preset FacePresets[]{
    {"Balanced",{}},
    {"Round",{{{"headWidth",0.35f},{"headHeight",-0.25f},{"jawWidth",0.25f},{"chinSize",-0.3f},{"cheekFullness",0.5f}}}},
    {"Long",{{{"headWidth",-0.25f},{"headHeight",0.4f},{"jawWidth",-0.2f},{"chinSize",0.3f},{"cheekFullness",-0.25f}}}},
    {"Heart",{{{"headWidth",0.2f},{"jawWidth",-0.45f},{"chinSize",-0.35f},{"cheekFullness",0.3f}}}},
    {"Square",{{{"headWidth",0.15f},{"jawWidth",0.55f},{"chinSize",0.35f},{"cheekFullness",-0.2f}}}},
    {"Soft",{{{"jawWidth",-0.1f},{"chinSize",-0.15f},{"cheekFullness",0.45f}}}}};
constexpr Preset EyePresets[]{
    {"Balanced",{}},
    {"Bright",{{{"eyeSize",0.45f},{"browHeight",0.2f}}}},
    {"Narrow",{{{"eyeSize",-0.4f},{"eyeSpacing",-0.1f}}}},
    {"Wide-set",{{{"eyeSpacing",0.5f},{"eyeSize",0.1f}}}},
    {"Upturned",{{{"eyeTilt",0.55f},{"browHeight",0.15f}}}},
    {"Downturned",{{{"eyeTilt",-0.5f},{"browHeight",-0.2f}}}}};
constexpr Preset NoseMouthPresets[]{
    {"Balanced",{}},
    {"Button nose",{{{"noseSize",-0.35f},{"noseWidth",-0.1f},{"noseHeight",0.15f}}}},
    {"Broad nose",{{{"noseSize",0.25f},{"noseWidth",0.55f}}}},
    {"Long nose",{{{"noseSize",0.35f},{"noseWidth",-0.2f},{"noseHeight",-0.3f}}}},
    {"Wide smile",{{{"mouthWidth",0.5f},{"mouthHeight",0.1f}}}},
    {"Small mouth",{{{"mouthWidth",-0.45f}}}}};

std::span<const Feature> featuresOf(EditorCategory category)
{
    switch(category) {
    case EditorCategory::Face: return FaceFeatures;
    case EditorCategory::Eyes: return EyeFeatures;
    case EditorCategory::NoseAndMouth: return NoseMouthFeatures;
    default: return {};
    }
}
std::span<const Preset> presetsOf(EditorCategory category)
{
    switch(category) {
    case EditorCategory::Face: return FacePresets;
    case EditorCategory::Eyes: return EyePresets;
    case EditorCategory::NoseAndMouth: return NoseMouthPresets;
    default: return {};
    }
}
}

const char* editorCategoryName(EditorCategory category)
{
    static const char* const names[EditorCategoryCount]{"Body","Skin","Face","Eyes","Nose & mouth","Hair","Facial hair","Tops","Bottoms",
        "Shoes","Glasses","Headwear"};
    return names[static_cast<int>(category)];
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
    // The avatar as the edited catalog can express it: ids it does not have become the slot's
    // first item (or none), and a catalog without face controls keeps the authored face.
    AvatarDescriptor result=source;
    result.catalogVersion=catalog_->version;
    const int body=result.bodyType==1?1:0;
    for(std::size_t slot=0;slot<AvatarItemSlotCount;++slot) {
        const auto kind=static_cast<AvatarItemSlot>(slot);
        const auto* item=catalog_->item(result.items[slot]);
        if(item&&item->slot==kind)continue;
        const auto offered=choices(kind);
        result.items[slot]=optionalSlot(kind)||offered.empty()?0:offered.front();
    }
    if(result.facialHair&&!catalog_->featureItem(result.facialHair))result.facialHair=0;
    if(catalog_->faceControls[body].empty())result.face.fill(NeutralFaceParameter);
    return result;
}

std::vector<std::uint16_t> AvatarEditorModel::choices(AvatarItemSlot slot) const
{
    std::vector<std::uint16_t> ids;
    if(slot==AvatarItemSlot::FacialHair) {
        for(const auto& item:catalog_->featureItems)ids.push_back(item.id);
    } else {
        for(const auto& item:catalog_->items)if(item.slot==slot)ids.push_back(item.id);
    }
    std::ranges::sort(ids);
    if(optionalSlot(slot)||slot==AvatarItemSlot::FacialHair)ids.insert(ids.begin(),0);
    return ids;
}

std::string AvatarEditorModel::itemName(std::uint16_t id,bool feature) const
{
    if(id==0)return "None";
    const auto* item=feature?catalog_->featureItem(id):catalog_->item(id);
    if(!item)return "Item "+std::to_string(id);
    std::string_view name=item->name;
    // Catalog names carry their slot as a prefix ("top_hoodie", "facial_beard").
    if(const auto underscore=name.find('_');underscore!=std::string_view::npos)name.remove_prefix(underscore+1);
    // Names whose file-name spelling reads badly.
    static constexpr std::pair<std::string_view,std::string_view> spoken[]{
        {"tshirt","T-shirt"},{"longsleeve","Long sleeve"},{"sidepart","Side part"},{"longskirt","Long skirt"},{"buzz","Buzz cut"}};
    for(const auto& [file,label]:spoken)if(name==file)return std::string(label);
    return readable(name);
}

int AvatarEditorModel::faceParameter(std::string_view name) const
{
    for(const auto& control:catalog_->faceControls[working_.bodyType==1?1:0])
        if(control.name==name)return control.parameter;
    return -1;
}

std::vector<AvatarEditorModel::Row> AvatarEditorModel::rows() const
{
    std::vector<Row> list;
    auto item=[&](AvatarItemSlot slot,const char* label){list.push_back({Kind::Item,static_cast<int>(slot),label});};
    auto color=[&](AvatarColorSlot slot,const char* label){list.push_back({Kind::Color,static_cast<int>(slot),label});};
    const bool shaped=!catalog_->faceControls[working_.bodyType==1?1:0].empty();
    switch(category_) {
    case EditorCategory::Body:
        list.push_back({Kind::BodyType,0,"Body"});
        list.push_back({Kind::Height,0,"Height"});
        list.push_back({Kind::Build,0,"Build"});
        break;
    case EditorCategory::Skin:
        color(AvatarColorSlot::Skin,"Skin tone");
        break;
    case EditorCategory::Face:
    case EditorCategory::Eyes:
    case EditorCategory::NoseAndMouth:
        if(category_==EditorCategory::Eyes)color(AvatarColorSlot::Eyes,"Eye color");
        if(!shaped)break;
        list.push_back({Kind::Preset,0,category_==EditorCategory::Face?"Face shape":category_==EditorCategory::Eyes?"Eye shape":"Shape"});
        for(const auto& feature:featuresOf(category_)) {
            const int parameter=faceParameter(feature.control);
            if(parameter>=0)list.push_back({Kind::Face,parameter,feature.label});
        }
        break;
    case EditorCategory::Hair:
        item(AvatarItemSlot::Hair,"Hairstyle");
        color(AvatarColorSlot::Hair,"Hair color");
        break;
    case EditorCategory::FacialHair:
        if(!catalog_->featureItems.empty())list.push_back({Kind::FacialHair,0,"Style"});
        color(AvatarColorSlot::Hair,"Color");
        break;
    case EditorCategory::Tops:
        item(AvatarItemSlot::Top,"Top");
        color(AvatarColorSlot::Top,"Color");
        break;
    case EditorCategory::Bottoms:
        item(AvatarItemSlot::Bottom,"Bottoms");
        color(AvatarColorSlot::Bottom,"Color");
        break;
    case EditorCategory::Shoes:
        item(AvatarItemSlot::Shoes,"Shoes");
        color(AvatarColorSlot::Shoes,"Color");
        break;
    case EditorCategory::Glasses:
        item(AvatarItemSlot::Glasses,"Glasses");
        color(AvatarColorSlot::Accessory,"Frame color");
        break;
    case EditorCategory::Headwear:
        item(AvatarItemSlot::Hat,"Headwear");
        color(AvatarColorSlot::Accessory,"Color");
        break;
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
    case EditorCategory::Face: case EditorCategory::Eyes: case EditorCategory::NoseAndMouth: case EditorCategory::Hair:
    case EditorCategory::FacialHair: case EditorCategory::Glasses: case EditorCategory::Headwear:
        return EditorView::Head;
    case EditorCategory::Tops: return EditorView::Upper;
    case EditorCategory::Bottoms: return EditorView::Lower;
    case EditorCategory::Shoes: return EditorView::Feet;
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
    EditorField result{row.label,{},EditorRowKind::Cards,std::nullopt,std::nullopt};
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
    case Kind::Color: {
        const auto& color=working_.colors[static_cast<std::size_t>(row.index)];
        const auto palette=colorPalette(static_cast<AvatarColorSlot>(row.index));
        const auto found=std::ranges::find(palette,color);
        result.value=found==palette.end()?"Custom":std::to_string(found-palette.begin()+1)+" of "+std::to_string(palette.size());
        result.kind=EditorRowKind::Swatches;
        result.swatch=color;
        break;
    }
    case Kind::Item:
        result.value=itemName(working_.items[static_cast<std::size_t>(row.index)],false);
        break;
    case Kind::FacialHair:
        result.value=itemName(working_.facialHair,true);
        break;
    case Kind::Face: {
        const auto value=working_.face[static_cast<std::size_t>(row.index)];
        const Feature* feature=nullptr;
        for(const auto& candidate:featuresOf(category_))if(faceParameter(candidate.control)==row.index)feature=&candidate;
        result.value=feature?sliderText(value,feature->less,feature->more):std::to_string(value);
        result.kind=EditorRowKind::Slider;
        result.slider=sliderPosition(value);
        break;
    }
    case Kind::Preset: {
        result.kind=EditorRowKind::Presets;
        result.value="Custom";
        for(const auto& option:options(index))if(option.current)result.value=option.label;
        break;
    }
    }
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
                if(catalog_->faceControls[body].empty())result.face.fill(NeutralFaceParameter);
            }
            out.push_back({body==1?"Male":"Female",result,working_.bodyType==body,std::nullopt});
        }
        break;
    case Kind::Color: {
        auto& color=working_.colors[static_cast<std::size_t>(row.index)];
        for(const auto& candidate:colorPalette(static_cast<AvatarColorSlot>(row.index))) {
            auto result=working_;
            result.colors[static_cast<std::size_t>(row.index)]=candidate;
            out.push_back({{},result,candidate==color,candidate});
        }
        break;
    }
    case Kind::Item:
    case Kind::FacialHair: {
        const auto slot=row.kind==Kind::Item?static_cast<AvatarItemSlot>(row.index):AvatarItemSlot::FacialHair;
        for(auto id:choices(slot)) {
            auto result=working_;
            (row.kind==Kind::Item?result.items[static_cast<std::size_t>(row.index)]:result.facialHair)=id;
            const auto current=row.kind==Kind::Item?working_.items[static_cast<std::size_t>(row.index)]:working_.facialHair;
            out.push_back({itemName(id,row.kind==Kind::FacialHair),result,id==current,std::nullopt});
        }
        break;
    }
    case Kind::Preset:
        for(const auto& preset:presetsOf(category_)) {
            auto result=working_;
            for(const auto& feature:featuresOf(category_)) {
                const int parameter=faceParameter(feature.control);
                if(parameter<0)continue;
                float move=0.0f;
                for(const auto& [control,amount]:preset.moves)if(control&&std::string_view(control)==feature.control)move=amount;
                result.face[static_cast<std::size_t>(parameter)]=sliderValue(std::clamp(move*PresetStrength,-1.0f,1.0f));
            }
            out.push_back({preset.label,result,result.face==working_.face,std::nullopt});
        }
        break;
    case Kind::Height:
    case Kind::Build:
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
    case Kind::Face:
        working_.face[static_cast<std::size_t>(row.index)]=stepSlider(working_.face[static_cast<std::size_t>(row.index)],delta);
        break;
    default: {
        // Cards, swatches and presets step to the neighbouring choice, wrapping.
        const auto choicesList=options(selection_);
        if(choicesList.empty())break;
        const auto current=std::ranges::find_if(choicesList,&EditorOption::current);
        const int count=static_cast<int>(choicesList.size());
        const int index=current==choicesList.end()?(delta>0?-1:0):static_cast<int>(current-choicesList.begin());
        working_=choicesList[static_cast<std::size_t>(((index+delta)%count+count)%count)].result;
        break;
    }
    }
    selection_=std::min(selection_,fieldCount()-1);
    return !(working_==before);
}

AvatarDescriptor AvatarEditorModel::coherentRandom(std::uint8_t bodyType)
{
    auto result=randomDescriptor(*catalog_,bodyType,random_);
    std::uniform_real_distribution<float> unit(0.0f,1.0f);
    auto chance=[&](float p){return unit(random_)<p;};
    auto pick=[&](auto range){return range[std::uniform_int_distribution<std::size_t>(0,range.size()-1)(random_)];};
    // Natural hair and eye colours mostly (the palettes list them first); a fun one now and then.
    const auto hair=colorPalette(AvatarColorSlot::Hair);
    result.colors[at(AvatarColorSlot::Hair)]=chance(0.88f)?hair[std::uniform_int_distribution<std::size_t>(0,std::min<std::size_t>(6,hair.size())-1)(random_)]:pick(hair);
    const auto eyes=colorPalette(AvatarColorSlot::Eyes);
    result.colors[at(AvatarColorSlot::Eyes)]=pick(eyes);
    // Clothes that go together: a top colour, bottoms in a neutral or a colour two steps round the
    // wheel from it, shoes neutral mostly, accessories matching the top or neutral.
    const auto cloth=colorPalette(AvatarColorSlot::Top);
    const std::vector<std::size_t> neutrals{8,9,10};  // white, charcoal, brown
    const auto top=std::uniform_int_distribution<std::size_t>(0,cloth.size()-1)(random_);
    result.colors[at(AvatarColorSlot::Top)]=cloth[top];
    // The first eight cloth colours are a hue wheel.
    const auto around=[&](std::size_t from,int steps){return cloth[(from+static_cast<std::size_t>(8+steps))%8];};
    result.colors[at(AvatarColorSlot::Bottom)]=chance(0.6f)?cloth[pick(neutrals)]:top<8?around(top,chance(0.5f)?2:-2):cloth[pick(neutrals)];
    result.colors[at(AvatarColorSlot::Shoes)]=chance(0.7f)?cloth[pick(neutrals)]:cloth[top];
    result.colors[at(AvatarColorSlot::Accessory)]=chance(0.5f)?cloth[top]:cloth[pick(neutrals)];
    // A face within its usual range: nothing at an extreme.
    if(!catalog_->faceControls[result.bodyType].empty()) {
        std::normal_distribution<float> spread(0.0f,0.22f);
        for(auto& value:result.face)value=sliderValue(std::clamp(spread(random_),-0.55f,0.55f));
    }
    auto sometimes=[&](AvatarItemSlot slot,float p) {
        const auto offered=choices(slot);
        result.items[at(slot)]=offered.size()>1&&chance(p)?offered[std::uniform_int_distribution<std::size_t>(1,offered.size()-1)(random_)]:0;
    };
    sometimes(AvatarItemSlot::Glasses,0.2f);
    sometimes(AvatarItemSlot::Hat,0.15f);
    if(bodyType==0)result.facialHair=0;
    else if(!catalog_->featureItems.empty())
        result.facialHair=chance(0.35f)?catalog_->featureItems[std::uniform_int_distribution<std::size_t>(0,catalog_->featureItems.size()-1)(random_)].id:0;
    return result;
}

void AvatarEditorModel::randomize()
{
    working_=coherentRandom(working_.bodyType);
    selection_=std::min(selection_,fieldCount()-1);
}

void AvatarEditorModel::randomizeCategory()
{
    // Only what the category shown covers changes.
    const auto fresh=coherentRandom(working_.bodyType);
    auto takeColor=[&](AvatarColorSlot slot){working_.colors[at(slot)]=fresh.colors[at(slot)];};
    auto takeItem=[&](AvatarItemSlot slot){working_.items[at(slot)]=fresh.items[at(slot)];};
    auto takeFeatures=[&] {
        for(const auto& feature:featuresOf(category_))
            if(const int parameter=faceParameter(feature.control);parameter>=0)
                working_.face[static_cast<std::size_t>(parameter)]=fresh.face[static_cast<std::size_t>(parameter)];
    };
    switch(category_) {
    case EditorCategory::Body: working_.heightMillimeters=fresh.heightMillimeters;working_.build=fresh.build;break;
    case EditorCategory::Skin: takeColor(AvatarColorSlot::Skin);break;
    case EditorCategory::Face: takeFeatures();break;
    case EditorCategory::Eyes: takeFeatures();takeColor(AvatarColorSlot::Eyes);break;
    case EditorCategory::NoseAndMouth: takeFeatures();break;
    case EditorCategory::Hair: takeItem(AvatarItemSlot::Hair);takeColor(AvatarColorSlot::Hair);break;
    case EditorCategory::FacialHair: working_.facialHair=working_.bodyType==1&&!catalog_->featureItems.empty()?
        catalog_->featureItems[std::uniform_int_distribution<std::size_t>(0,catalog_->featureItems.size()-1)(random_)].id:0;break;
    case EditorCategory::Tops: takeItem(AvatarItemSlot::Top);takeColor(AvatarColorSlot::Top);break;
    case EditorCategory::Bottoms: takeItem(AvatarItemSlot::Bottom);takeColor(AvatarColorSlot::Bottom);break;
    case EditorCategory::Shoes: takeItem(AvatarItemSlot::Shoes);takeColor(AvatarColorSlot::Shoes);break;
    case EditorCategory::Glasses: {
        const auto offered=choices(AvatarItemSlot::Glasses);
        working_.items[at(AvatarItemSlot::Glasses)]=offered[std::uniform_int_distribution<std::size_t>(0,offered.size()-1)(random_)];
        takeColor(AvatarColorSlot::Accessory);
        break;
    }
    case EditorCategory::Headwear: {
        const auto offered=choices(AvatarItemSlot::Hat);
        working_.items[at(AvatarItemSlot::Hat)]=offered[std::uniform_int_distribution<std::size_t>(0,offered.size()-1)(random_)];
        takeColor(AvatarColorSlot::Accessory);
        break;
    }
    }
}

void AvatarEditorModel::revert()
{
    working_=start_;
    selection_=std::min(selection_,fieldCount()-1);
}

void AvatarEditorModel::markSaved()
{
    stored_=encoded();
    start_=working_;
}
}
