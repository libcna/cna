// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/GamerServices/AvatarAssets.hpp"
#include <algorithm>
#include <stdexcept>

namespace CNA::Internal::GamerServices::Avatars {
namespace {
using Microsoft::Xna::Framework::Quaternion;
using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Vector4;

constexpr int PresetCount=31;

// glTF cubic spline between two keys: value + tangents scaled by the key interval.
Vector4 evaluate(const AvatarTrack& track,double seconds)
{
    const auto& times=track.times;
    const auto value=[&](std::size_t key){return track.values[key*3+1];};
    if(times.size()==1||seconds<=times.front())return value(0);
    if(seconds>=times.back())return value(times.size()-1);
    const auto upper=static_cast<std::size_t>(std::upper_bound(times.begin(),times.end(),static_cast<float>(seconds))-times.begin());
    const std::size_t k=std::min(upper,times.size()-1)-1;
    const float td=times[k+1]-times[k];
    const float s=td>0?static_cast<float>((seconds-times[k])/td):0.0f;
    const float s2=s*s, s3=s2*s;
    const Vector4 v0=value(k), b0=track.values[k*3+2], a1=track.values[(k+1)*3], v1=value(k+1);
    return v0*(2*s3-3*s2+1)+b0*(td*(s3-2*s2+s))+v1*(-2*s3+3*s2)+a1*(td*(s3-s2));
}
}

const AvatarClipLibrary& clipLibrary()
{
    static const AvatarClipLibrary library=[] {
        const auto& manifest=newestEmbeddedManifest();
        const auto bytes=resolveAsset(manifest,manifest.animationsAsset);
        if(!bytes)throw std::runtime_error("avatar animations are unavailable");
        auto glb=parseAvatarGlb(bytes->view);
        if(glb.clips.size()!=PresetCount)throw std::runtime_error("avatar animations: expected every preset");
        AvatarClipLibrary result;
        result.clips=std::move(glb.clips);
        result.bindTranslations=glb.bindTranslations;
        return result;
    }();
    return library;
}

AvatarExpressionKey sampleClip(const AvatarClip& clip,double seconds,std::array<Quaternion,BoneCount>& rotations,
    Vector3& rootTranslation)
{
    rotations.fill(Quaternion::Identity);
    rootTranslation=Vector3::Zero;
    for(const auto& track:clip.tracks) {
        const auto value=evaluate(track,seconds);
        if(track.translation) {
            rootTranslation=Vector3(value.X,value.Y,value.Z);
        } else {
            Quaternion rotation(value.X,value.Y,value.Z,value.W);
            const float length=rotation.Length();
            rotations[track.bone]=length>1e-6f?Quaternion(rotation.X/length,rotation.Y/length,rotation.Z/length,rotation.W/length)
                                              :Quaternion::Identity;
        }
    }
    AvatarExpressionKey current{};
    for(const auto& key:clip.expressions) {
        if(key.time>seconds+1e-6)break;
        current=key;
    }
    current.time=static_cast<float>(seconds);
    return current;
}
}
