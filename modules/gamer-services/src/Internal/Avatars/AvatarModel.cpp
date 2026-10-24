// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/GamerServices/AvatarAssets.hpp"
#include "AvatarCatalogArithmetic.hpp"
#include "CNA/Internal/GamerServices/IGamerServicesBackend.hpp"
#include "CNA/Internal/Graphics/ImageLoader.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <functional>
#include <map>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <utility>

namespace CNA::Internal::GamerServices::Avatars {
AvatarImage decodeAvatarImage(std::span<const std::uint8_t> png)
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

namespace {
using Microsoft::Xna::Framework::Vector3;

// Namespace scope (not function statics): they must outlive the loader thread at exit.
std::mutex faceLock;
std::map<std::string,std::shared_ptr<const std::vector<AvatarImage>>,std::less<>> cachedTiles;
std::mutex assetLock;
// Parsed models and decoded textures by content hash: the editor assembles dozens of avatars that
// share a body and most items. Models are kept while recently used, textures while any model uses them.
std::map<std::string,std::pair<std::shared_ptr<const AvatarGlb>,std::uint64_t>,std::less<>> cachedModels;
std::map<std::string,std::weak_ptr<const AvatarImage>,std::less<>> cachedImages;
std::uint64_t modelUse=0;
constexpr std::size_t CachedModelLimit=96;

std::shared_ptr<const std::vector<AvatarImage>> faceTiles(const CatalogManifest& manifest)
{
    // One decoded atlas per distinct atlas file.
    const auto& hash=manifest.assets.at(manifest.faceAsset).sha256;
    std::lock_guard guard(faceLock);
    if(auto found=cachedTiles.find(hash);found!=cachedTiles.end())return found->second;
    const auto bytes=resolveAsset(manifest,manifest.faceAsset);
    if(!bytes)throw std::runtime_error("avatar face atlas is unavailable");
    const auto atlas=decodeAvatarImage(bytes->view);
    const int size=manifest.face.tileSize;
    if(atlas.width!=manifest.face.columns*size||atlas.height%size||manifest.face.columns*(atlas.height/size)!=manifest.face.tiles)
        throw std::runtime_error("avatar face atlas size");
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

// A listed model, parsed once while it stays in use; empty when unavailable or malformed.
std::shared_ptr<const AvatarGlb> model(const CatalogManifest& manifest,const std::string& name)
{
    const auto listed=manifest.assets.find(name);
    if(listed==manifest.assets.end())return nullptr;
    const auto& hash=listed->second.sha256;
    {
        std::lock_guard guard(assetLock);
        if(auto found=cachedModels.find(hash);found!=cachedModels.end()) {
            found->second.second=++modelUse;
            return found->second.first;
        }
    }
    const auto bytes=resolveAsset(manifest,name);
    if(!bytes)return nullptr;
    std::shared_ptr<const AvatarGlb> parsed;
    try {parsed=std::make_shared<const AvatarGlb>(parseAvatarGlb(bytes->view));} catch(const std::runtime_error&) {return nullptr;}
    std::lock_guard guard(assetLock);
    if(cachedModels.size()>=CachedModelLimit)
        cachedModels.erase(std::ranges::min_element(cachedModels,{},[](const auto& entry){return entry.second.second;}));
    cachedModels[hash]={parsed,++modelUse};
    return parsed;
}

// A decoded texture shared by every model that uses the same contents.
std::shared_ptr<const AvatarImage> image(const std::string& hash,const std::function<std::optional<AssetBytes>()>& bytes)
{
    {
        std::lock_guard guard(assetLock);
        if(auto found=cachedImages.find(hash);found!=cachedImages.end())
            if(auto shared=found->second.lock())return shared;
    }
    const auto png=bytes();
    if(!png)throw std::runtime_error("avatar texture is unavailable");
    auto decoded=std::make_shared<const AvatarImage>(decodeAvatarImage(png->view));
    std::lock_guard guard(assetLock);
    std::erase_if(cachedImages,[](const auto& entry){return entry.second.expired();});
    cachedImages[hash]=decoded;
    return decoded;
}

Vector3 toVector(const AvatarColor& color){return Vector3(color.r/255.0f,color.g/255.0f,color.b/255.0f);}

// Constant tables, never constructed or destroyed. The shared loader thread reads them while exit()
// runs static destructors; a function-local map built by the first avatar job was destroyed before
// ~Loader joined that job, which then faulted or walked freed nodes forever (RUST-UPSTREAM-031).
Vector3 tintColor(const AvatarDescriptor& descriptor,const std::string& tint)
{
    static constexpr std::pair<std::string_view,AvatarColorSlot> slots[]{{"skin",AvatarColorSlot::Skin},
        {"hair",AvatarColorSlot::Hair},{"eyes",AvatarColorSlot::Eyes},{"top",AvatarColorSlot::Top},
        {"bottom",AvatarColorSlot::Bottom},{"shoes",AvatarColorSlot::Shoes},{"accessory",AvatarColorSlot::Accessory},
        {"lips",AvatarColorSlot::Lips},{"eyeShadow",AvatarColorSlot::EyeShadow},{"blush",AvatarColorSlot::Blush},
        {"jewelry",AvatarColorSlot::Jewelry},{"gloves",AvatarColorSlot::Gloves}};
    for(const auto& [name,slot]:slots)
        if(name==tint)return toVector(drawnColor(descriptor,slot));
    return Vector3(1,1,1);
}

// The decal a body feature is, and the makeup it belongs to (makeup decals are drawn only with it).
struct FeatureOf {std::string_view name;AvatarFeature feature;std::optional<AvatarItemSlot> makeup;int side;};
constexpr FeatureOf FeatureTable[]{{"eyeLeft",AvatarFeature::EyeLeft,{},0},{"eyeRight",AvatarFeature::EyeRight,{},0},
    {"eyebrowLeft",AvatarFeature::EyebrowLeft,{},0},{"eyebrowRight",AvatarFeature::EyebrowRight,{},0},
    {"mouth",AvatarFeature::Mouth,{},0},{"lips",AvatarFeature::Lips,AvatarItemSlot::Lips,0},
    {"shadowLeft",AvatarFeature::Static,AvatarItemSlot::EyeShadow,0},{"shadowRight",AvatarFeature::Static,AvatarItemSlot::EyeShadow,1},
    {"linerLeft",AvatarFeature::Static,AvatarItemSlot::Eyeliner,0},{"linerRight",AvatarFeature::Static,AvatarItemSlot::Eyeliner,1},
    {"blushLeft",AvatarFeature::Static,AvatarItemSlot::Blush,0},{"blushRight",AvatarFeature::Static,AvatarItemSlot::Blush,1}};

const FeatureOf* featureOf(const std::string& name)
{
    for(const auto& entry:FeatureTable)
        if(entry.name==name)return &entry;
    return nullptr;
}

// Which face controls shape a part: every one (the body, ears, facial hair), only the whole-head
// ones (hair, hats), or none.
enum class Scope : std::uint8_t { None, Head, Face };

float headWeight(const AvatarVertex& vertex)
{
    static const int head=boneIndex("Head");
    float weight=0.0f;
    const std::array<float,4> weights{vertex.weights.X,vertex.weights.Y,vertex.weights.Z,vertex.weights.W};
    for(int k=0;k<4;++k)
        if(vertex.joints[k]==head)weight+=weights[k];
    return weight;
}

// A face description's deformers, with what depends only on the description worked out once: the
// scale factors, the move and the rotation for its control's value. Applied in catalog order, so the
// sum is exactly the same as evaluating each control afresh at every point.
struct ActiveDeformer {
    const FaceDeformer* deformer;
    Vector3 factor;    // Scale: amount^t - 1
    float t;           // Move: the control's value
    float cosine,sine; // Rotate
};

std::vector<ActiveDeformer> activeDeformers(const std::vector<FaceControl>& controls,
    const std::array<std::uint8_t,FaceParameterCount>& face,Scope scope)
{
    std::vector<ActiveDeformer> out;
    for(const auto& control:controls) {
        const float t=std::clamp((static_cast<float>(face[static_cast<std::size_t>(control.parameter)])-128.0f)/127.0f,-1.0f,1.0f);
        if(t==0.0f||(!control.wholeHead&&scope!=Scope::Face))continue;
        for(const auto& deformer:control.deformers) {
            ActiveDeformer active{&deformer,Vector3::Zero,t,1.0f,0.0f};
            if(deformer.kind==FaceDeformer::Kind::Scale)
                active.factor=Vector3(std::pow(deformer.amount.X,t)-1.0f,std::pow(deformer.amount.Y,t)-1.0f,std::pow(deformer.amount.Z,t)-1.0f);
            else if(deformer.kind==FaceDeformer::Kind::Rotate) {
                const float angle=deformer.degrees*t*3.14159265f/180.0f;
                active.cosine=std::cos(angle);
                active.sine=std::sin(angle);
            }
            out.push_back(active);
        }
    }
    return out;
}

// The displacement face controls give an authored point (summed over every deformer, each from
// the undeformed position).
Vector3 faceDisplacement(const Vector3& position,const std::vector<ActiveDeformer>& deformers)
{
    Vector3 displacement=Vector3::Zero;
    for(const auto& active:deformers) {
        const auto& deformer=*active.deformer;
        // Outside the ellipsoid's box the weight is 0: most points are outside most deformers.
        const float qx=position.X-deformer.centre.X,qy=position.Y-deformer.centre.Y,qz=position.Z-deformer.centre.Z;
        if(std::fabs(qx)>=deformer.radii.X||std::fabs(qy)>=deformer.radii.Y||std::fabs(qz)>=deformer.radii.Z)continue;
        const float nx=qx/deformer.radii.X,ny=qy/deformer.radii.Y,nz=qz/deformer.radii.Z;
        const float x=std::clamp((std::sqrt((nx*nx)+(ny*ny)+(nz*nz))-deformer.inner)/(1.0f-deformer.inner),0.0f,1.0f);
        const float w=1.0f-x*x*(3.0f-2.0f*x);
        if(w<=0.0f)continue;
        const Vector3 q(qx,qy,qz);
        switch(deformer.kind) {
        case FaceDeformer::Kind::Scale:
            displacement+=Vector3(q.X*active.factor.X,q.Y*active.factor.Y,q.Z*active.factor.Z)*w;
            break;
        case FaceDeformer::Kind::Move:
            displacement+=deformer.amount*(active.t*w);
            break;
        case FaceDeformer::Kind::Rotate: {
            const Vector3& axis=deformer.amount;
            const Vector3 rotated=q*active.cosine+Vector3::Cross(axis,q)*active.sine+
                axis*(catalogDot(q,axis)*(1.0f-active.cosine));
            displacement+=(rotated-q)*w;
            break;
        }
        }
    }
    return displacement;
}

Vector3 faceDisplacement(const Vector3& position,const std::vector<FaceControl>& controls,
    const std::array<std::uint8_t,FaceParameterCount>& face,Scope scope)
{
    return faceDisplacement(position,activeDeformers(controls,face,scope));
}

// The face shape (catalog faceControls), applied in proportion to each vertex's Head weight.
void applyFaceControls(std::vector<AvatarVertex>& vertices,const std::vector<FaceControl>& controls,
    const std::array<std::uint8_t,FaceParameterCount>& face,Scope scope)
{
    if(scope==Scope::None)return;
    const auto deformers=activeDeformers(controls,face,scope);
    if(deformers.empty())return;
    for(auto& vertex:vertices) {
        const float weight=headWeight(vertex);
        if(weight>0.0f)vertex.position+=faceDisplacement(vertex.position,deformers)*weight;
    }
}

// Hair under a hat that covers it (the hat's hairCover): what the hat covers is flattened to just
// above the head surface, fading out below the cover's line, and triangles wholly inside the
// covered region are dropped.
void flattenUnderHat(AvatarPrimitive& primitive,const HeadSurface& surface,const HairCover& cover)
{
    constexpr float Pi=3.14159265358979f;
    auto smooth=[](float e0,float e1,float x){const float t=std::clamp((x-e0)/(e1-e0),0.0f,1.0f);return t*t*(3.0f-2.0f*t);};
    auto line=[&](float phi) {
        const float a=std::fabs(std::remainder(phi,2.0f*Pi));
        const auto& l=cover.line;
        if(a<0.30f*Pi)return Pi*(l[0]+(l[1]-l[0])*smooth(0.0f,0.30f*Pi,a));
        if(a<0.55f*Pi)return Pi*(l[1]+(l[2]-l[1])*smooth(0.30f*Pi,0.55f*Pi,a));
        return Pi*(l[2]+(l[3]-l[2])*smooth(0.55f*Pi,Pi,a));
    };
    std::vector<bool> covered(primitive.vertices.size());
    for(std::size_t index=0;index<primitive.vertices.size();++index) {
        auto& position=primitive.vertices[index].position;
        const Vector3 local=position-surface.centre;
        const float r=catalogLength(local);
        if(r<1e-6f)continue;
        const Vector3 d=local/r;
        const float theta=std::acos(std::clamp(d.Y,-1.0f,1.0f)), phi=std::atan2(d.X,d.Z);
        const float limitLine=line(phi);
        covered[index]=theta<limitLine-0.05f*Pi;
        const float amount=1.0f-smooth(limitLine-0.03f*Pi,limitLine+0.02f*Pi,theta);
        if(amount<=0.0f)continue;
        const float limit=surface.radius(theta,phi)+cover.lift;
        position=surface.centre+d*(r+(std::min(r,limit)-r)*amount);
    }
    std::vector<std::uint16_t> kept;
    kept.reserve(primitive.indices.size());
    for(std::size_t i=0;i+2<primitive.indices.size();i+=3)
        if(!(covered[primitive.indices[i]]&&covered[primitive.indices[i+1]]&&covered[primitive.indices[i+2]]))
            kept.insert(kept.end(),primitive.indices.begin()+static_cast<std::ptrdiff_t>(i),primitive.indices.begin()+static_cast<std::ptrdiff_t>(i)+3);
    primitive.indices=std::move(kept);
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
        catalogNormalize(direction);
        const Vector3 offset=vertex.position-origin;
        const Vector3 along=direction*catalogDot(offset,direction);
        vertex.position=origin+along+(offset-along)*factor;
    }
}

// Body proportions (a format 3 description's proportion byte): every bone's segment is lengthened
// or shortened and the head scaled, and each vertex follows its bones' new placement (blended by its
// weights), so clothes stay fitted. Compact proportions read younger: a larger head, a shorter neck,
// torso and limbs, narrower shoulders.
struct Proportions {
    std::array<Vector3,BoneCount> positions;  // morphed joint positions
    std::array<float,BoneCount> stretch{};    // geometry stretch along the bone's own axis
    std::array<Vector3,BoneCount> axis{};
    float head=1.0f;
    bool identity=true;
};

Proportions proportionsFor(const std::array<Vector3,BoneCount>& authored,const std::array<Vector3,BoneCount>& local,std::uint8_t value)
{
    Proportions out;
    out.positions=authored;
    out.stretch.fill(1.0f);
    const float t=std::clamp((static_cast<float>(value)-128.0f)/127.0f,-1.0f,1.0f);
    if(t==0.0f)return out;
    out.identity=false;
    auto at=[&](float compact,float tall){return 1.0f+(t<0?-t*(compact-1.0f):t*(tall-1.0f));};
    const float legs=at(0.84f,1.07f), arms=at(0.88f,1.05f), torso=at(0.90f,1.05f), neck=at(0.80f,1.06f), shoulders=at(0.88f,1.03f);
    out.head=at(1.10f,0.96f);
    static const int root=boneIndex("Root"), backLower=boneIndex("BackLower"), head=boneIndex("Head"), neckBone=boneIndex("Neck");
    // Per bone: how its translation from its parent scales (x, y, z).
    std::array<Vector3,BoneCount> factor;
    factor.fill(Vector3(1,1,1));
    auto set=[&](std::string_view name,Vector3 f){if(const int b=boneIndex(name);b>=0)factor[b]=f;};
    for(const char* side:{"Left","Right"}) {
        const std::string s(side);
        set("Hip"+s,Vector3(1,legs,1));
        set("Knee"+s,Vector3(legs,legs,legs));
        set("Ankle"+s,Vector3(legs,legs,legs));
        set("Collar"+s,Vector3(shoulders,torso,1));
        set("Shoulder"+s,Vector3(shoulders,1,1));
        set("Elbow"+s,Vector3(arms,arms,arms));
        set("Wrist"+s,Vector3(arms,arms,arms));
    }
    set("BackUpper",Vector3(1,torso,1));
    set("Neck",Vector3(1,torso,1));
    set("Head",Vector3(1,neck,1));
    // Helper slots follow their parent's segment.
    const auto& parents=parentBones();
    for(int bone=0;bone<BoneCount;++bone)
        if(boneName(bone).starts_with("Slot")&&parents[bone]>0) {
            const int parent=parents[bone];
            const auto name=boneName(parent);
            if(name.starts_with("Hip")||name.starts_with("Knee"))factor[bone]=Vector3(legs,legs,legs);
            else if(name.starts_with("Shoulder")||name.starts_with("Elbow"))factor[bone]=Vector3(arms,arms,arms);
        }
    for(int bone=0;bone<BoneCount;++bone) {
        const int parent=parents[bone];
        if(parent<0)continue;
        Vector3 offset=local[bone];
        if(bone==backLower||(parent==root&&boneName(bone).starts_with("Slot"))) {
            // Above the root: the hips' height scales with the legs, the rest with the torso.
            const float hipHeight=authored[boneIndex("HipLeft")].Y;
            offset.Y=std::min(offset.Y,hipHeight)*legs+std::max(offset.Y-hipHeight,0.0f)*torso;
        } else {
            offset=Vector3(offset.X*factor[bone].X,offset.Y*factor[bone].Y,offset.Z*factor[bone].Z);
        }
        out.positions[bone]=(parent==root?out.positions[root]:out.positions[parent])+offset;
    }
    // Geometry stretches with the segment it belongs to.
    for(const auto& [bone,toward]:BuildAxes) {
        Vector3 direction=authored[toward]-authored[bone];
        catalogNormalize(direction);
        out.axis[bone]=direction;
        const float before=catalogDistance(authored[toward],authored[bone]);
        out.stretch[bone]=before>1e-4f?catalogDistance(out.positions[toward],out.positions[bone])/before:1.0f;
    }
    (void)neckBone;
    (void)head;
    return out;
}

void applyProportions(std::vector<AvatarVertex>& vertices,const std::array<Vector3,BoneCount>& authored,const Proportions& morph)
{
    if(morph.identity)return;
    static const int head=boneIndex("Head");
    for(auto& vertex:vertices) {
        const std::array<float,4> weights{vertex.weights.X,vertex.weights.Y,vertex.weights.Z,vertex.weights.W};
        Vector3 moved=Vector3::Zero;
        for(int k=0;k<4;++k) {
            if(weights[k]<=0.0f)continue;
            const int bone=vertex.joints[k];
            Vector3 offset=vertex.position-authored[bone];
            if(bone==head) {
                offset*=morph.head;
            } else if(morph.stretch[bone]!=1.0f) {
                const Vector3& axis=morph.axis[bone];
                offset+=axis*(catalogDot(offset,axis)*(morph.stretch[bone]-1.0f));
            }
            moved+=(morph.positions[bone]+offset)*weights[k];
        }
        vertex.position=moved;
    }
}
}

namespace {
// A version whose pack could not be installed is not asked for again for a minute, so a crowd of
// renderers of one unavailable avatar does not become a crowd of requests.
std::mutex catalogLock;
std::map<std::uint16_t,std::chrono::steady_clock::time_point> failedUpdates;
constexpr auto UpdateRetryDelay=std::chrono::seconds(60);
}

std::shared_ptr<const CatalogManifest> catalogManifest(std::uint16_t version)
{
    // A development catalog below the baseline is never resolved: not compiled in, not read from a
    // stale pack on disk, not asked of the service.
    if(!isSupportedCatalogVersion(version))return nullptr;
    // Layer A: the catalogs of this release, then the packs installed before.
    if(auto embedded=embeddedManifest(version))return embedded;
    if(auto installed=installedManifest(version))return installed;
    // Layer B: install exactly this version as one pack from the service, if this client accepts it.
    {
        std::lock_guard guard(catalogLock);
        if(auto failed=failedUpdates.find(version);failed!=failedUpdates.end()&&std::chrono::steady_clock::now()<failed->second)
            return nullptr;
    }
    auto service=[&]()->std::shared_ptr<IGamerServicesBackend>{try{return backend();}catch(...){return nullptr;}}();
    if(!service||!service->serviceEnabled())return nullptr;
    const auto policy=service->avatarCatalogPolicy();
    if(!policy.updates||installedCatalogRoot().empty())return nullptr;
    auto outcome=CatalogInstall::Failed;
    if(const auto text=onServiceExecutor<std::string>([version](IGamerServicesBackend& s){return s.avatarCatalogPack(version);})) {
        try {
            auto download=[](const std::string& hash,std::size_t size) {
                auto bytes=onServiceExecutor<std::vector<unsigned char>>([&](IGamerServicesBackend& s){return s.catalogFile(hash,size);});
                if(!bytes)throw std::runtime_error("avatar catalog file could not be downloaded");
                return std::move(*bytes);
            };
            auto pack=parseCatalogPack(*text);
            // Refused before anything is downloaded: a pack this client could not read or keep.
            if(pack.version==version&&pack.totalBytes<=policy.maximumBytes&&pack.reader<=CatalogReaderLevel) {
                const auto manifest=download(pack.manifestSha256,pack.manifestSize);
                attachCatalogManifest(pack,std::string(manifest.begin(),manifest.end()));
                outcome=installCatalogPack(pack,policy.maximumBytes,[&](const CatalogAsset& asset){return download(asset.sha256,asset.size);});
            }
        } catch(const std::runtime_error&) {
            outcome=CatalogInstall::Failed;
        }
    }
    if(outcome==CatalogInstall::Installed||outcome==CatalogInstall::AlreadyInstalled)
        if(auto installed=installedManifest(version))return installed;
    std::lock_guard guard(catalogLock);
    failedUpdates[version]=std::chrono::steady_clock::now()+UpdateRetryDelay;
    return nullptr;
}

void forgetCatalogUpdateFailures()
{
    std::lock_guard guard(catalogLock);
    failedUpdates.clear();
}

std::shared_ptr<const AvatarModel> buildAvatarModel(const AvatarDescriptor& descriptor)
{
    // Not even the default avatar: a development catalog's description is not a CNA avatar.
    if(!isSupportedCatalogVersion(descriptor.catalogVersion))
        throw std::runtime_error("avatar description names development catalog "+std::to_string(descriptor.catalogVersion)+", below the baseline");
    auto catalog=catalogManifest(descriptor.catalogVersion);
    auto result=std::make_shared<AvatarModel>();
    if(!catalog) {
        // The named catalog is neither compiled in nor obtainable: draw the newest compiled-in body
        // with every item's slot default rather than reading the ids against another version.
        result->catalogUnavailable=true;
        catalog=embeddedCatalogs().back();
    }
    const auto& manifest=*catalog;
    const bool known=!result->catalogUnavailable;
    const int body=descriptor.bodyType==1?1:0;
    const auto bodyGlb=model(manifest,manifest.bodies[body]);
    if(!bodyGlb)throw std::runtime_error("avatar body asset is unavailable");
    struct Piece {std::shared_ptr<const AvatarGlb> glb;Scope scope=Scope::None;ItemFollow follow=ItemFollow::Shape;
                  AvatarSides sides=AvatarSides::Both;const HairCover* cover=nullptr;};
    std::vector<Piece> pieces{{bodyGlb,Scope::Face}};
    auto fitted=[&](const std::shared_ptr<const AvatarGlb>& glb) {
        for(int bone=0;bone<BoneCount;++bone)
            if(catalogDistance(glb->bindTranslations[bone],bodyGlb->bindTranslations[bone])>1e-3f)
                throw std::runtime_error("avatar item is not fitted to this body");
        return glb;
    };
    auto worn=[&](std::uint16_t id,AvatarItemSlot slot)->const CatalogItem* {return id&&known?manifest.item(id,slot):nullptr;};
    // What the worn items hide (gloves hide rings).
    std::vector<AvatarItemSlot> hidden;
    for(std::size_t slot=0;slot<AvatarItemSlotCount;++slot)
        if(const auto* item=worn(descriptor.items[slot],static_cast<AvatarItemSlot>(slot)))hidden.insert(hidden.end(),item->hides.begin(),item->hides.end());
    for(std::size_t index=0;index<AvatarAccessorySlotCount;++index) {
        const auto slot=static_cast<AvatarItemSlot>(static_cast<std::size_t>(AvatarItemSlot::Earrings)+index);
        if(const auto* item=worn(descriptor.accessories[index],slot))hidden.insert(hidden.end(),item->hides.begin(),item->hides.end());
    }
    auto isHidden=[&](AvatarItemSlot slot){return std::ranges::find(hidden,slot)!=hidden.end();};
    const auto* hat=worn(descriptor.items[static_cast<std::size_t>(AvatarItemSlot::Hat)],AvatarItemSlot::Hat);
    const HairCover* cover=hat&&hat->hairCover&&!manifest.headSurface[body].radii.empty()?&*hat->hairCover:nullptr;
    // Ears: the description's shape, else the catalog's default (the first).
    const CatalogItem* earShape=nullptr;
    const CatalogItem* firstEarShape=nullptr;
    {
        const auto defaults=manifest.itemsOf(AvatarItemSlot::Ears);
        firstEarShape=defaults.empty()?nullptr:defaults.front();
        earShape=worn(descriptor.ears,AvatarItemSlot::Ears);
        if(!earShape) {
            if(descriptor.ears)result->substitutedItems.push_back(descriptor.ears);
            earShape=firstEarShape;
        }
        if(earShape)
            if(auto glb=model(manifest,earShape->assets[body]))pieces.push_back({fitted(glb),Scope::Face});
    }
    for(std::size_t slot=0;slot<AvatarItemSlotCount;++slot) {
        const auto kind=static_cast<AvatarItemSlot>(slot);
        const auto id=descriptor.items[slot];
        const auto* item=worn(id,kind);
        std::shared_ptr<const AvatarGlb> glb=item?model(manifest,item->assets[body]):nullptr;
        if(!glb&&id) {
            // Unknown or unreadable item: required slots fall back to the first item of the slot.
            result->substitutedItems.push_back(id);
            const bool optional=kind==AvatarItemSlot::Glasses||kind==AvatarItemSlot::Hat;
            auto fallback=std::ranges::find(manifest.items,kind,&CatalogItem::slot);
            if(!optional&&fallback!=manifest.items.end())glb=model(manifest,fallback->assets[body]);
            item=nullptr;
        }
        if(!glb)continue;
        Piece piece{fitted(glb)};
        if(kind==AvatarItemSlot::Hair||kind==AvatarItemSlot::Hat)piece.scope=Scope::Head;
        if(kind==AvatarItemSlot::Hair)piece.cover=cover;
        if(item)piece.follow=item->follow;
        if(kind==AvatarItemSlot::Glasses&&piece.follow==ItemFollow::Shape)piece.scope=Scope::Head;
        pieces.push_back(std::move(piece));
    }
    if(descriptor.facialHair) {
        const auto* feature=worn(descriptor.facialHair,AvatarItemSlot::FacialHair);
        auto glb=feature?model(manifest,feature->assets[body]):nullptr;
        if(glb)pieces.push_back({fitted(glb),Scope::Face});
        else result->substitutedItems.push_back(descriptor.facialHair);
    }
    for(std::size_t index=0;index<AvatarAccessorySlotCount;++index) {
        const auto slot=static_cast<AvatarItemSlot>(static_cast<std::size_t>(AvatarItemSlot::Earrings)+index);
        const auto id=descriptor.accessories[index];
        if(!id)continue;
        const auto* item=worn(id,slot);
        auto glb=item?model(manifest,item->assets[body]):nullptr;
        if(!glb){result->substitutedItems.push_back(id);continue;}
        if(isHidden(slot))continue;
        pieces.push_back({fitted(glb),Scope::None,item->follow,descriptor.sides[index]});
    }

    // Authored skeleton, proportions, and where the face's anchors move.
    const auto& rig=bodyGlb->bindTranslations;
    std::array<Vector3,BoneCount> authored{};
    for(int bone=0;bone<BoneCount;++bone) {
        const int parent=parentBones()[bone];
        authored[bone]=parent<0?rig[bone]:authored[parent]+rig[bone];
    }
    const auto& controls=manifest.faceControls[body];
    const bool shaped=!controls.empty()&&std::ranges::any_of(descriptor.face,[](std::uint8_t v){return v!=NeutralFaceParameter;});
    const auto morph=proportionsFor(authored,rig,descriptor.proportion);
    const float authoredHeight=manifest.authoredHeightMillimeters[body]/1000.0f;
    static const int headBone=boneIndex("Head");
    const float morphedHeight=morph.positions[headBone].Y+(authoredHeight-authored[headBone].Y)*morph.head;
    const float height=descriptor.heightMillimeters/1000.0f;
    const float scale=height/morphedHeight;
    result->height=height;
    for(int bone=0;bone<BoneCount;++bone) {
        const int parent=parentBones()[bone];
        result->bindPositions[bone]=morph.positions[bone]*scale;
        result->bindTranslations[bone]=parent<0?result->bindPositions[bone]:result->bindPositions[bone]-result->bindPositions[parent];
    }
    const auto& anchors=manifest.faceAnchors[body];
    auto follow=[&](const Vector3& point){return shaped?faceDisplacement(point,controls,descriptor.face,Scope::Face):Vector3::Zero;};
    const Vector3 eyeLeft=anchors.eyeLeft+follow(anchors.eyeLeft), eyeRight=anchors.eyeRight+follow(anchors.eyeRight);
    // Earrings are made for the first ear shape's lobes: the shape worn carries them to its own, and
    // face controls move them as they move the lobe (a catalog without lobes: as the ear's root).
    std::array<Vector3,2> lobe{anchors.earLeft,anchors.earRight},carry{Vector3::Zero,Vector3::Zero};
    if(earShape&&firstEarShape&&earShape->lobes&&firstEarShape->lobes)
        for(int side=0;side<2;++side) {
            lobe[side]=(*earShape->lobes)[body][side];
            carry[side]=lobe[side]-(*firstEarShape->lobes)[body][side];
        }
    const std::array<Vector3,2> earMove{carry[0]+follow(lobe[0]),carry[1]+follow(lobe[1])};
    const float eyeSpan=std::fabs(anchors.eyeLeft.X-anchors.eyeRight.X);
    const float eyeStretch=eyeSpan>1e-4f?std::fabs(eyeLeft.X-eyeRight.X)/eyeSpan:1.0f;
    const Vector3 eyeCentre=(anchors.eyeLeft+anchors.eyeRight)*0.5f, eyeMove=(eyeLeft+eyeRight)*0.5f-eyeCentre;

    const float build=1.0f+(static_cast<float>(descriptor.build)-128.0f)/127.0f*0.18f;
    const auto* brows=descriptor.eyebrows&&known?manifest.item(descriptor.eyebrows,AvatarItemSlot::Eyebrows):nullptr;
    if(descriptor.eyebrows&&!brows)result->substitutedItems.push_back(descriptor.eyebrows);
    result->eyebrowTiles=manifest.face.eyebrows;
    if(brows)
        for(int state=0;state<5;++state)
            for(int side=0;side<2;++side)result->eyebrowTiles[state][side]=brows->tiles[static_cast<std::size_t>(state*2+side)];
    std::array<const CatalogItem*,AvatarMakeupSlotCount> makeup{};
    for(std::size_t index=0;index<AvatarMakeupSlotCount;++index) {
        const auto id=descriptor.makeup[index];
        makeup[index]=id&&known?manifest.item(id,static_cast<AvatarItemSlot>(static_cast<std::size_t>(AvatarItemSlot::EyeShadow)+index)):nullptr;
        if(id&&!makeup[index])result->substitutedItems.push_back(id);
    }
    if(const auto* lips=makeup[makeupIndex(AvatarItemSlot::Lips)])
        std::copy_n(lips->tiles.begin(),14,result->lipTiles.begin());

    std::vector<AvatarModelPart> decals;
    for(const auto& piece:pieces) {
        for(const auto& source:piece.glb->primitives) {
            // A side the accessory is not worn on.
            if(source.side&&piece.sides!=AvatarSides::Both&&source.side!=static_cast<int>(piece.sides))continue;
            AvatarModelPart part;
            part.feature=AvatarFeature::None;
            const CatalogItem* style=nullptr;
            if(!source.feature.empty()) {
                const auto* feature=featureOf(source.feature);
                if(!feature)continue;
                if(feature->makeup) {
                    style=makeup[makeupIndex(*feature->makeup)];
                    if(!style)continue;
                    part.alpha=style->opacity;
                    if(feature->feature==AvatarFeature::Static) {
                        part.tile=style->tiles[static_cast<std::size_t>(feature->side)];
                        part.eye=feature->side;
                        if(style->tiles.size()==4)part.closedTile=style->tiles[static_cast<std::size_t>(2+feature->side)];
                    }
                }
                part.feature=feature->feature;
            }
            AvatarPrimitive primitive=source;
            if(piece.cover)flattenUnderHat(primitive,manifest.headSurface[body],*piece.cover);
            if(primitive.indices.empty())continue;
            part.vertices=std::move(primitive.vertices);
            if(shaped)applyFaceControls(part.vertices,controls,descriptor.face,piece.scope);
            if(shaped&&piece.follow==ItemFollow::Eyes) {
                // Glasses ride on the eyes: moved with them, widened or narrowed with their spacing.
                for(auto& vertex:part.vertices) {
                    vertex.position.X=eyeCentre.X+(vertex.position.X-eyeCentre.X)*eyeStretch;
                    vertex.position+=eyeMove;
                }
            } else if(piece.follow==ItemFollow::Ears) {
                // Earrings hang from the lobe on their side.
                for(auto& vertex:part.vertices)vertex.position+=earMove[vertex.position.X>=0.0f?0:1];
            }
            applyProportions(part.vertices,authored,morph);
            applyBuild(part.vertices,morph.positions,build);
            for(auto& vertex:part.vertices)vertex.position*=scale;
            part.indices=std::move(primitive.indices);
            for(std::size_t i=0;i+2<part.indices.size();i+=3)std::swap(part.indices[i+1],part.indices[i+2]);
            part.color=primitive.color*tintColor(descriptor,primitive.tint);
            part.layer=primitive.layer;
            part.specular=primitive.specular;
            part.shininess=primitive.shininess;
            part.blend=primitive.blend;
            if(!primitive.texture.empty()) {
                const auto listed=manifest.assets.find(primitive.texture);
                if(listed==manifest.assets.end())throw std::runtime_error("avatar texture "+primitive.texture+" is not in the catalog");
                result->images.push_back(image(listed->second.sha256,[&]{return resolveAsset(manifest,primitive.texture);}));
                part.image=static_cast<int>(result->images.size())-1;
            } else if(!primitive.texturePng.empty()) {
                const auto& png=primitive.texturePng;
                result->images.push_back(image(sha256Hex(png),[&]{return std::optional<AssetBytes>(AssetBytes{nullptr,png});}));
                part.image=static_cast<int>(result->images.size())-1;
            }
            (part.feature==AvatarFeature::None&&!part.blend?result->parts:decals).push_back(std::move(part));
        }
    }
    // Translucent parts over the opaque ones, then the face decals layer by layer.
    std::ranges::stable_sort(decals,{},[](const AvatarModelPart& part){return std::pair(part.feature!=AvatarFeature::None,part.layer);});
    for(auto& decal:decals)result->parts.push_back(std::move(decal));
    result->faceTiles=faceTiles(manifest);
    result->face=manifest.face;
    return result;
}

namespace {
// One shared loader thread: avatar assembly never blocks the game thread, and there is never
// more than one worker however many renderers a game creates. A single-threaded browser has no
// thread to give it, so there a job runs where it is submitted.
#ifdef __EMSCRIPTEN__
class Loader {
public:
    Loader()
    {
        (void)embeddedCatalogs();
    }
    void submit(const std::function<void()>& job)
    {
        job();
    }
};
#else
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
#endif

std::mutex cacheLock;
std::map<std::vector<std::uint8_t>,std::weak_ptr<const AvatarModel>> cache;

Loader& loader()
{
    static Loader shared;
    return shared;
}
}

std::shared_ptr<CatalogLoad> loadCatalogAsync(std::uint16_t version)
{
    auto load=std::make_shared<CatalogLoad>();
    if(!isSupportedCatalogVersion(version)) {
        load->done=true;
        return load;
    }
    if(auto local=embeddedManifest(version)) {
        load->manifest=std::move(local);
        load->done=true;
        return load;
    }
    loader().submit([load,version] {
        auto manifest=catalogManifest(version);
        std::lock_guard guard(load->lock);
        load->manifest=std::move(manifest);
        load->done=true;
    });
    return load;
}

std::shared_ptr<AvatarLoad> loadAvatarAsync(const AvatarDescriptor& descriptor)
{
    auto load=std::make_shared<AvatarLoad>();
    if(!isEncodable(descriptor)) {
        load->error="avatar description is not encodable";
        load->done=true;
        return load;
    }
    const auto key=encode(descriptor);
    {
        std::lock_guard guard(cacheLock);
        const auto found=cache.find(key);
        if(auto model=found!=cache.end()?found->second.lock():nullptr) {
            load->model=std::move(model);
            load->done=true;
            return load;
        }
    }
    loader().submit([load,descriptor,key] {
        std::shared_ptr<const AvatarModel> model;
        std::string error;
        try {model=buildAvatarModel(descriptor);}
        catch(const std::exception& failure) {error=failure.what();}
        if(model) {
            std::lock_guard guard(cacheLock);
            // Keys of avatars nobody draws any more would otherwise pile up for the whole run.
            std::erase_if(cache,[](const auto& entry){return entry.second.expired();});
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
