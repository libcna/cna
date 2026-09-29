// SPDX-License-Identifier: MS-PL
#include "GuidePortraits.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GamerServices/AvatarAnimation.hpp"
#include "Microsoft/Xna/Framework/GamerServices/AvatarDescription.hpp"
#include "Microsoft/Xna/Framework/GamerServices/AvatarRenderer.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include <algorithm>
#include <cmath>

namespace CNA::Internal::GamerServices::GuideUi {
namespace {
namespace Xna = Microsoft::Xna::Framework;
namespace GS = Microsoft::Xna::Framework::GamerServices;
constexpr int KeepFrames=240;
}

struct Portraits::Entry {
    std::vector<unsigned char> bytes;
    Framing framing=Framing::Head;
    int width=0,height=0;
    bool live=false;
    std::unique_ptr<GS::AvatarDescription> description;
    std::unique_ptr<GS::AvatarRenderer> renderer;
    std::unique_ptr<GS::AvatarAnimation> animation;
    std::unique_ptr<Xna::Graphics::RenderTarget2D> target;
    bool drawn=false;
    int unused=0;
    bool requested=false;
};

Portraits::Portraits()=default;
Portraits::~Portraits()=default;

Xna::Graphics::Texture2D* Portraits::request(const std::vector<unsigned char>& description,Framing framing,int width,int height,bool live)
{
    if(description.size()!=1021||description[0]==0||width<8||height<8)return nullptr;
    auto found=std::ranges::find_if(entries_,[&](const auto& entry) {
        return entry->framing==framing&&entry->width==width&&entry->height==height&&entry->live==live&&entry->bytes==description;
    });
    if(found==entries_.end()) {
        auto entry=std::make_unique<Entry>();
        entry->bytes=description;
        entry->framing=framing;
        entry->width=width;
        entry->height=height;
        entry->live=live;
        entry->description=std::make_unique<GS::AvatarDescription>(std::vector<SharpRuntime::bytecs>(description.begin(),description.end()));
        entry->renderer=std::make_unique<GS::AvatarRenderer>(entry->description.get(),false);
        entry->animation=std::make_unique<GS::AvatarAnimation>(GS::AvatarAnimationPreset::Stand0);
        entries_.push_back(std::move(entry));
        found=std::prev(entries_.end());
    }
    auto& entry=**found;
    entry.requested=true;
    entry.unused=0;
    return entry.drawn?entry.target.get():nullptr;
}

void Portraits::render(Xna::Graphics::GraphicsDevice& device,double seconds)
{
    bool drew=false;
    for(auto& owned:entries_) {
        auto& entry=*owned;
        const bool wanted=entry.requested;
        entry.requested=false;
        if(!wanted){++entry.unused;continue;}
        if(entry.drawn&&!entry.live)continue;
        if(entry.renderer->getStateProperty()!=GS::AvatarRendererState::Ready)continue;
        if(!entry.target)
            entry.target=std::make_unique<Xna::Graphics::RenderTarget2D>(device,entry.width,entry.height,false,Xna::Graphics::SurfaceFormat::Color,
                Xna::Graphics::DepthFormat::Depth24);
        // Where the head is, from the avatar's own bind pose: its height and proportions vary.
        const auto parents=entry.renderer->getParentBonesProperty();
        const auto bind=entry.renderer->getBindPoseProperty();
        std::vector<Xna::Vector3> world(static_cast<std::size_t>(bind.getCountProperty()));
        for(int bone=0;bone<bind.getCountProperty();++bone) {
            const auto local=bind[bone].getTranslationProperty();
            world[static_cast<std::size_t>(bone)]=parents[bone]<0?local:world[static_cast<std::size_t>(parents[bone])]+local;
        }
        const auto head=world[19];
        const float height=entry.description->getHeightProperty();
        Xna::Vector3 target,eye;
        float fov;
        const float aspect=static_cast<float>(entry.width)/static_cast<float>(entry.height);
        if(entry.framing==Framing::Head) {
            target=Xna::Vector3(0,head.Y+0.07f,0);
            eye=Xna::Vector3(0.12f,head.Y+0.10f,0.80f);
            fov=0.42f;
        } else {
            // The whole avatar, a little space above the head and under the feet.
            fov=0.50f;
            const float half=std::max(height*0.56f,height*0.30f/aspect);
            target=Xna::Vector3(0,height*0.49f,0);
            eye=Xna::Vector3(0.45f,height*0.56f,half/std::tan(fov/2));
        }
        entry.animation->setCurrentPositionProperty(System::TimeSpan::FromTicks(0));
        if(entry.live)entry.animation->Update(System::TimeSpan::FromTicks(static_cast<SharpRuntime::longcs>(std::fmod(seconds,60.0)*1.0e7)),true);
        auto expression=entry.animation->getExpressionProperty();
        if(!entry.live)expression.setMouthProperty(GS::AvatarMouth::Happy);
        entry.renderer->setWorldProperty(Xna::Matrix::CreateRotationY(entry.framing==Framing::Head?0.18f:-0.22f));
        entry.renderer->setViewProperty(Xna::Matrix::CreateLookAt(eye,target,Xna::Vector3::Up));
        entry.renderer->setProjectionProperty(Xna::Matrix::CreatePerspectiveFieldOfView(fov,aspect,0.05f,20.0f));
        Xna::Vector3 light(-0.45f,-0.45f,-0.77f);
        light.Normalize();
        entry.renderer->setLightDirectionProperty(light);
        entry.renderer->setLightColorProperty(Xna::Vector3(0.72f,0.70f,0.66f));
        entry.renderer->setAmbientLightColorProperty(Xna::Vector3(0.44f,0.46f,0.52f));
        device.SetRenderTarget(entry.target.get());
        device.Clear(Xna::Color(0,0,0,0));
        const auto bones=entry.animation->getBoneTransformsProperty();
        entry.renderer->Draw(std::vector<Xna::Matrix>(bones.begin(),bones.end()),expression);
        entry.drawn=true;
        drew=true;
    }
    if(drew)device.SetRenderTarget(nullptr);
    std::erase_if(entries_,[](const auto& entry){return entry->unused>KeepFrames;});
}

void Portraits::clear(){entries_.clear();}
}
