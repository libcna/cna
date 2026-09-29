// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/GamerServices/AvatarAssets.hpp"
#include "CNA/Internal/GamerServices/IGamerServicesBackend.hpp"
#include "CNA/Internal/Graphics/ImageLoader.hpp"
#include <algorithm>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <functional>
#include <map>
#include <stdexcept>
#include <thread>

namespace CNA::Internal::GamerServices::Avatars {
namespace {
using Microsoft::Xna::Framework::Vector3;

AvatarImage decodePng(std::span<const std::uint8_t> png)
{
    const auto image=CNA::Internal::Graphics::ImageLoader::LoadFromMemory(png.data(),png.size());
    if(image.width<=0||image.height<=0||image.width>2048||image.height>2048||
       image.pixels.size()!=static_cast<std::size_t>(image.width)*image.height*4)
        throw std::runtime_error("avatar texture could not be decoded");
    AvatarImage out{image.width,image.height,image.pixels};
    for(std::size_t i=0;i<out.rgba.size();i+=4) {
        const unsigned alpha=out.rgba[i+3];
        for(int c=0;c<3;++c)out.rgba[i+c]=static_cast<std::uint8_t>((out.rgba[i+c]*alpha+127)/255);
    }
    return out;
}

// Namespace scope (not function statics): they must outlive the loader thread at exit.
std::mutex faceLock;
std::map<std::string,std::shared_ptr<const std::vector<AvatarImage>>,std::less<>> cachedTiles;

std::shared_ptr<const std::vector<AvatarImage>> faceTiles(const CatalogManifest& manifest)
{
    // One decoded atlas per distinct atlas file.
    const auto& hash=manifest.assets.at(manifest.faceAsset).sha256;
    std::lock_guard guard(faceLock);
    if(auto found=cachedTiles.find(hash);found!=cachedTiles.end())return found->second;
    const auto bytes=resolveAsset(manifest,manifest.faceAsset);
    if(!bytes)throw std::runtime_error("avatar face atlas is unavailable");
    const auto atlas=decodePng(bytes->view);
    const int size=manifest.face.tileSize;
    if(atlas.width!=manifest.face.columns*size||atlas.height%size)throw std::runtime_error("avatar face atlas size");
    auto tiles=std::make_shared<std::vector<AvatarImage>>();
    for(int ty=0;ty<atlas.height/size;++ty)
        for(int tx=0;tx<manifest.face.columns;++tx) {
            AvatarImage tile{size,size,std::vector<std::uint8_t>(static_cast<std::size_t>(size)*size*4)};
            for(int y=0;y<size;++y) {
                const auto* row=atlas.rgba.data()+((static_cast<std::size_t>(ty)*size+y)*atlas.width+static_cast<std::size_t>(tx)*size)*4;
                std::copy(row,row+size*4,tile.rgba.begin()+static_cast<std::ptrdiff_t>(y)*size*4);
            }
            tiles->push_back(std::move(tile));
        }
    return cachedTiles.emplace(hash,std::move(tiles)).first->second;
}

Vector3 toVector(const AvatarColor& color){return Vector3(color.r/255.0f,color.g/255.0f,color.b/255.0f);}

Vector3 tintColor(const AvatarDescriptor& descriptor,const std::string& tint)
{
    static const std::map<std::string,AvatarColorSlot,std::less<>> slots{{"skin",AvatarColorSlot::Skin},
        {"hair",AvatarColorSlot::Hair},{"eyes",AvatarColorSlot::Eyes},{"top",AvatarColorSlot::Top},
        {"bottom",AvatarColorSlot::Bottom},{"shoes",AvatarColorSlot::Shoes},{"accessory",AvatarColorSlot::Accessory}};
    auto found=slots.find(tint);
    return found==slots.end()?Vector3(1,1,1):toVector(descriptor.colors[static_cast<std::size_t>(found->second)]);
}

AvatarFeature featureOf(const std::string& name)
{
    static const std::map<std::string,AvatarFeature,std::less<>> features{{"eyeLeft",AvatarFeature::EyeLeft},
        {"eyeRight",AvatarFeature::EyeRight},{"eyebrowLeft",AvatarFeature::EyebrowLeft},
        {"eyebrowRight",AvatarFeature::EyebrowRight},{"mouth",AvatarFeature::Mouth}};
    auto found=features.find(name);
    return found==features.end()?AvatarFeature::None:found->second;
}

// A format 2 description's face shape (catalog faceControls): every deformer's displacement is
// taken from the undeformed position, summed, and applied in proportion to the Head weight.
void applyFaceControls(std::vector<AvatarVertex>& vertices,const std::vector<FaceControl>& controls,
    const std::array<std::uint8_t,FaceParameterCount>& face,bool faceScope)
{
    static const int head=boneIndex("Head");
    for(auto& vertex:vertices) {
        float weight=0.0f;
        const std::array<float,4> weights{vertex.weights.X,vertex.weights.Y,vertex.weights.Z,vertex.weights.W};
        for(int k=0;k<4;++k)
            if(vertex.joints[k]==head)weight+=weights[k];
        if(weight<=0.0f)continue;
        Vector3 displacement=Vector3::Zero;
        for(const auto& control:controls) {
            const float t=std::clamp((static_cast<float>(face[static_cast<std::size_t>(control.parameter)])-128.0f)/127.0f,-1.0f,1.0f);
            if(t==0.0f||(!control.wholeHead&&!faceScope))continue;
            for(const auto& deformer:control.deformers) {
                const Vector3 q=vertex.position-deformer.centre;
                const Vector3 n(q.X/deformer.radii.X,q.Y/deformer.radii.Y,q.Z/deformer.radii.Z);
                const float x=std::clamp((n.Length()-deformer.inner)/(1.0f-deformer.inner),0.0f,1.0f);
                const float w=1.0f-x*x*(3.0f-2.0f*x);
                if(w<=0.0f)continue;
                switch(deformer.kind) {
                case FaceDeformer::Kind::Scale:
                    displacement+=Vector3(q.X*(std::pow(deformer.amount.X,t)-1.0f),q.Y*(std::pow(deformer.amount.Y,t)-1.0f),
                                          q.Z*(std::pow(deformer.amount.Z,t)-1.0f))*w;
                    break;
                case FaceDeformer::Kind::Move:
                    displacement+=deformer.amount*(t*w);
                    break;
                case FaceDeformer::Kind::Rotate: {
                    const float angle=deformer.degrees*t*3.14159265f/180.0f;
                    const Vector3& axis=deformer.amount;
                    const Vector3 rotated=q*std::cos(angle)+Vector3::Cross(axis,q)*std::sin(angle)+
                        axis*(Vector3::Dot(q,axis)*(1.0f-std::cos(angle)));
                    displacement+=(rotated-q)*w;
                    break;
                }
                }
            }
        }
        vertex.position+=displacement*weight;
    }
}

// Bones whose flesh thickens with the build, and the joint their thickness is measured from.
struct BuildAxis {int bone; int toward;};
constexpr BuildAxis BuildAxes[]={{1,5},{5,14},{2,6},{3,8},{6,11},{8,15},{12,20},{16,22},{20,25},{22,28},{25,33},{28,36}};

void applyBuild(std::vector<AvatarVertex>& vertices,const std::array<Vector3,BoneCount>& positions,float factor)
{
    if(std::fabs(factor-1.0f)<1e-4f)return;
    for(auto& vertex:vertices) {
        const std::array<float,4> weights{vertex.weights.X,vertex.weights.Y,vertex.weights.Z,vertex.weights.W};
        int dominant=0;
        for(int k=1;k<4;++k)
            if(weights[k]>weights[dominant])dominant=k;
        const int bone=vertex.joints[dominant];
        auto axis=std::ranges::find(BuildAxes,bone,&BuildAxis::bone);
        if(axis==std::end(BuildAxes))continue;
        const Vector3 origin=positions[axis->bone];
        Vector3 direction=positions[axis->toward]-origin;
        direction.Normalize();
        const Vector3 offset=vertex.position-origin;
        const Vector3 along=direction*Vector3::Dot(offset,direction);
        vertex.position=origin+along+(offset-along)*factor;
    }
}
}

namespace {
std::mutex catalogLock;
std::map<std::uint16_t,std::shared_ptr<const CatalogManifest>> serviceCatalogs;
}

std::shared_ptr<const CatalogManifest> catalogManifest(std::uint16_t version)
{
    if(auto embedded=embeddedManifest(version))return embedded;
    {
        std::lock_guard guard(catalogLock);
        if(auto found=serviceCatalogs.find(version);found!=serviceCatalogs.end())return found->second;
    }
    const auto text=onServiceExecutor<std::string>([version](IGamerServicesBackend& service){return service.avatarCatalog(version);});
    if(!text)return nullptr;
    try {
        auto manifest=std::make_shared<const CatalogManifest>(parseManifest(*text));
        if(manifest->version!=version)return nullptr;
        std::lock_guard guard(catalogLock);
        return serviceCatalogs.emplace(version,std::move(manifest)).first->second;
    } catch(const std::runtime_error&) {
        return nullptr;
    }
}

std::shared_ptr<const AvatarModel> buildAvatarModel(const AvatarDescriptor& descriptor)
{
    auto catalog=catalogManifest(descriptor.catalogVersion);
    auto model=std::make_shared<AvatarModel>();
    if(!catalog) {
        // The named catalog is neither compiled in nor obtainable: draw the newest compiled-in body
        // with every item's slot default rather than reading the ids against another version.
        model->catalogUnavailable=true;
        catalog=embeddedCatalogs().back();
    }
    const auto& manifest=*catalog;
    const int body=descriptor.bodyType==1?1:0;
    const auto bodyBytes=resolveAsset(manifest,manifest.bodies[body]);
    if(!bodyBytes)throw std::runtime_error("avatar body asset is unavailable");
    auto bodyGlb=parseAvatarGlb(bodyBytes->view);
    // (asset, part of the face: the body and facial hair take every face control, other items
    // only the whole-head ones)
    std::vector<std::pair<AvatarGlb,bool>> assets;
    assets.emplace_back(std::move(bodyGlb),true);
    const auto* hat=descriptor.items[static_cast<std::size_t>(AvatarItemSlot::Hat)]&&!model->catalogUnavailable
        ?manifest.item(descriptor.items[static_cast<std::size_t>(AvatarItemSlot::Hat)]):nullptr;
    const bool underHat=hat&&hat->slot==AvatarItemSlot::Hat&&hat->coversHair;
    for(std::size_t slot=0;slot<AvatarItemSlotCount;++slot) {
        const auto id=descriptor.items[slot];
        const auto* item=id&&!model->catalogUnavailable?manifest.item(id):nullptr;
        std::optional<AvatarGlb> glb;
        if(item&&item->slot==static_cast<AvatarItemSlot>(slot)) {
            // Hair under a hat that covers it is the style's hat variant, when the catalog has one.
            const auto& files=underHat&&!item->hatAssets[body].empty()?item->hatAssets:item->assets;
            if(auto bytes=resolveAsset(manifest,files[body])) {
                try {glb=parseAvatarGlb(bytes->view);} catch(const std::runtime_error&) {}
            }
        }
        if(!glb&&id) {
            // Unknown or unreadable item: required slots fall back to the first item of the slot.
            model->substitutedItems.push_back(id);
            const bool optional=slot==static_cast<std::size_t>(AvatarItemSlot::Glasses)||slot==static_cast<std::size_t>(AvatarItemSlot::Hat);
            auto fallback=std::ranges::find(manifest.items,static_cast<AvatarItemSlot>(slot),&CatalogItem::slot);
            if(!optional&&fallback!=manifest.items.end())
                if(auto bytes=resolveAsset(manifest,fallback->assets[body]))glb=parseAvatarGlb(bytes->view);
        }
        if(glb) {
            for(int bone=0;bone<BoneCount;++bone)
                if(Vector3::Distance(glb->bindTranslations[bone],assets.front().first.bindTranslations[bone])>1e-3f)
                    throw std::runtime_error("avatar item is not fitted to this body");
            assets.emplace_back(std::move(*glb),false);
        }
    }
    if(descriptor.facialHair) {
        const auto* feature=model->catalogUnavailable?nullptr:manifest.featureItem(descriptor.facialHair);
        std::optional<AvatarGlb> glb;
        if(feature)
            if(auto bytes=resolveAsset(manifest,feature->assets[body])) {
                try {glb=parseAvatarGlb(bytes->view);} catch(const std::runtime_error&) {}
            }
        if(glb) {
            for(int bone=0;bone<BoneCount;++bone)
                if(Vector3::Distance(glb->bindTranslations[bone],assets.front().first.bindTranslations[bone])>1e-3f)
                    throw std::runtime_error("avatar item is not fitted to this body");
            assets.emplace_back(std::move(*glb),true);
        } else {
            model->substitutedItems.push_back(descriptor.facialHair);
        }
    }
    const float authored=manifest.authoredHeightMillimeters[body]/1000.0f;
    const float height=descriptor.heightMillimeters/1000.0f;
    const float scale=height/authored;
    model->height=height;
    const auto& rig=assets.front().first.bindTranslations;
    for(int bone=0;bone<BoneCount;++bone) {
        model->bindTranslations[bone]=rig[bone]*scale;
        const int parent=parentBones()[bone];
        model->bindPositions[bone]=parent<0?model->bindTranslations[bone]:model->bindPositions[parent]+model->bindTranslations[bone];
    }
    std::array<Vector3,BoneCount> authoredPositions{};
    for(int bone=0;bone<BoneCount;++bone) {
        const int parent=parentBones()[bone];
        authoredPositions[bone]=parent<0?rig[bone]:authoredPositions[parent]+rig[bone];
    }
    const float build=1.0f+(static_cast<float>(descriptor.build)-128.0f)/127.0f*0.18f;
    std::vector<AvatarModelPart> decals;
    const auto& controls=manifest.faceControls[body];
    const bool shaped=descriptor.usesFaceFormat()&&!controls.empty();
    for(auto& [glb,faceScope]:assets) {
        for(auto& primitive:glb.primitives) {
            AvatarModelPart part;
            part.vertices=std::move(primitive.vertices);
            if(shaped)applyFaceControls(part.vertices,controls,descriptor.face,faceScope);
            applyBuild(part.vertices,authoredPositions,build);
            for(auto& vertex:part.vertices)vertex.position*=scale;
            part.indices=std::move(primitive.indices);
            for(std::size_t i=0;i+2<part.indices.size();i+=3)std::swap(part.indices[i+1],part.indices[i+2]);
            part.color=primitive.color*tintColor(descriptor,primitive.tint);
            part.feature=featureOf(primitive.feature);
            part.layer=primitive.layer;
            part.specular=primitive.specular;
            if(!primitive.texturePng.empty()) {
                model->images.push_back(decodePng(primitive.texturePng));
                part.image=static_cast<int>(model->images.size())-1;
            }
            (part.feature==AvatarFeature::None?model->parts:decals).push_back(std::move(part));
        }
    }
    std::ranges::stable_sort(decals,{},&AvatarModelPart::layer);
    for(auto& decal:decals)model->parts.push_back(std::move(decal));
    model->faceTiles=faceTiles(manifest);
    model->face=manifest.face;
    return model;
}

namespace {
// One shared loader thread: avatar assembly never blocks the game thread, and there is never
// more than one worker however many renderers a game creates.
class Loader {
public:
    Loader()
    {
        // Constructed first, so these outlive the worker at exit.
        (void)embeddedCatalogs();
        worker_=std::thread([this]{run();});
    }
    ~Loader()
    {
        {std::lock_guard guard(lock_);stopping_=true;}
        wake_.notify_all();
        worker_.join();
    }
    void submit(std::function<void()> job)
    {
        {std::lock_guard guard(lock_);jobs_.push_back(std::move(job));}
        wake_.notify_one();
    }
private:
    void run()
    {
        for(;;) {
            std::function<void()> job;
            {
                std::unique_lock guard(lock_);
                wake_.wait(guard,[&]{return stopping_||!jobs_.empty();});
                if(stopping_)return;
                job=std::move(jobs_.front());
                jobs_.pop_front();
            }
            job();
        }
    }
    std::mutex lock_;
    std::condition_variable wake_;
    std::deque<std::function<void()>> jobs_;
    bool stopping_=false;
    std::thread worker_;
};

std::mutex cacheLock;
std::map<std::vector<std::uint8_t>,std::weak_ptr<const AvatarModel>> cache;
}

std::shared_ptr<AvatarLoad> loadAvatarAsync(const AvatarDescriptor& descriptor)
{
    auto load=std::make_shared<AvatarLoad>();
    const auto key=encode(descriptor);
    {
        std::lock_guard guard(cacheLock);
        if(auto model=cache[key].lock()) {
            load->model=std::move(model);
            load->done=true;
            return load;
        }
    }
    static Loader loader;
    loader.submit([load,descriptor,key] {
        std::shared_ptr<const AvatarModel> model;
        std::string error;
        try {model=buildAvatarModel(descriptor);}
        catch(const std::exception& failure) {error=failure.what();}
        if(model) {
            std::lock_guard guard(cacheLock);
            cache[key]=model;
        }
        std::lock_guard guard(load->lock);
        load->model=std::move(model);
        load->error=std::move(error);
        load->done=true;
    });
    return load;
}
}
