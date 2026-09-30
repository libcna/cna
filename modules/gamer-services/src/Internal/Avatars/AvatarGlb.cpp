// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/GamerServices/AvatarAssets.hpp"
#include "cgltf.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace CNA::Internal::GamerServices::Avatars {
namespace {
using Microsoft::Xna::Framework::Vector2;
using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Vector4;

// Downloaded assets are untrusted; these bounds keep a hostile file from costing more than a
// legitimate catalog asset does.
constexpr std::size_t MaximumPrimitives=64, MaximumVertices=65535, MaximumTotalVertices=200000;
constexpr std::size_t MaximumIndices=3*120000, MaximumImageBytes=1u<<20, MaximumClips=64, MaximumKeys=4096;
constexpr float MaximumCoordinate=5.0f;
constexpr std::array<std::string_view,8> Tints{"none","skin","hair","eyes","top","bottom","shoes","accessory"};
constexpr std::array<std::string_view,5> Features{"eyeLeft","eyeRight","eyebrowLeft","eyebrowRight","mouth"};

[[noreturn]] void malformed(const std::string& what){throw std::runtime_error("avatar asset: "+what);}

struct Gltf {
    cgltf_data* data=nullptr;
    ~Gltf(){if(data)cgltf_free(data);}
};

bool finite(float value){return std::isfinite(value)&&std::fabs(value)<=MaximumCoordinate*20;}

std::vector<float> floats(const cgltf_accessor* accessor,cgltf_type type,std::size_t maximumCount)
{
    if(!accessor||accessor->type!=type||accessor->count>maximumCount||accessor->is_sparse)malformed("accessor shape");
    const auto components=cgltf_num_components(type);
    std::vector<float> values(accessor->count*components);
    if(cgltf_accessor_unpack_floats(accessor,values.data(),values.size())!=values.size())malformed("accessor data");
    if(!std::ranges::all_of(values,finite))malformed("non-finite value");
    return values;
}

nlohmann::json extras(const cgltf_extras& extras)
{
    if(!extras.data)return nlohmann::json::object();
    auto json=nlohmann::json::parse(extras.data,nullptr,false);
    if(json.is_discarded()||!json.is_object())malformed("extras");
    return json;
}

int slotOf(const cgltf_node* node,const std::array<const cgltf_node*,BoneCount>& joints)
{
    auto found=std::ranges::find(joints,node);
    return found==joints.end()?-1:static_cast<int>(found-joints.begin());
}

void readRig(const cgltf_data* data,AvatarGlb& out,std::array<const cgltf_node*,BoneCount>& joints,std::vector<int>& skinToSlot)
{
    if(data->skins_count!=1||data->skins[0].joints_count!=BoneCount)malformed("expected one 71-joint skin");
    const auto& skin=data->skins[0];
    joints.fill(nullptr);
    skinToSlot.assign(BoneCount,-1);
    for(std::size_t index=0;index<skin.joints_count;++index) {
        const auto* node=skin.joints[index];
        const int slot=node&&node->name?boneIndex(node->name):-1;
        if(slot<0||joints[slot])malformed("joint names do not form the avatar skeleton");
        joints[slot]=node;
        skinToSlot[index]=slot;
    }
    for(int slot=0;slot<BoneCount;++slot) {
        const auto* node=joints[slot];
        const int parent=node->parent?slotOf(node->parent,joints):-1;
        if(parent!=parentBones()[slot])malformed("joint hierarchy differs from the XNA avatar skeleton");
        if(node->has_matrix||node->has_scale&&(node->scale[0]!=1||node->scale[1]!=1||node->scale[2]!=1))malformed("joint scale");
        if(node->has_rotation&&(std::fabs(node->rotation[3])<0.99999f))malformed("bind rotations must be identity");
        Vector3 translation;
        if(node->has_translation) {
            if(!std::all_of(node->translation,node->translation+3,[](float v){return std::isfinite(v)&&std::fabs(v)<MaximumCoordinate;}))
                malformed("joint translation");
            translation=Vector3(node->translation[0],node->translation[1],node->translation[2]);
        }
        out.bindTranslations[slot]=translation;
    }
}

void readPrimitive(const cgltf_primitive& primitive,const std::vector<int>& skinToSlot,AvatarPrimitive& out,std::size_t& total)
{
    if(primitive.type!=cgltf_primitive_type_triangles||!primitive.indices||primitive.targets_count)malformed("primitive type");
    const cgltf_accessor *position=nullptr,*normal=nullptr,*uv=nullptr,*joints=nullptr,*weights=nullptr;
    for(std::size_t index=0;index<primitive.attributes_count;++index) {
        const auto& attribute=primitive.attributes[index];
        if(attribute.type==cgltf_attribute_type_position)position=attribute.data;
        else if(attribute.type==cgltf_attribute_type_normal)normal=attribute.data;
        else if(attribute.type==cgltf_attribute_type_texcoord&&attribute.index==0)uv=attribute.data;
        else if(attribute.type==cgltf_attribute_type_joints&&attribute.index==0)joints=attribute.data;
        else if(attribute.type==cgltf_attribute_type_weights&&attribute.index==0)weights=attribute.data;
        else malformed("unexpected vertex attribute");
    }
    if(!position||!normal||!joints||!weights)malformed("missing vertex attribute");
    const auto count=position->count;
    if(count==0||count>MaximumVertices||(total+=count)>MaximumTotalVertices)malformed("vertex count");
    if(normal->count!=count||joints->count!=count||weights->count!=count||(uv&&uv->count!=count))malformed("attribute counts");
    const auto p=floats(position,cgltf_type_vec3,count), n=floats(normal,cgltf_type_vec3,count);
    const auto w=floats(weights,cgltf_type_vec4,count);
    const auto t=uv?floats(uv,cgltf_type_vec2,count):std::vector<float>(count*2,0.0f);
    if(joints->type!=cgltf_type_vec4)malformed("joint type");
    out.vertices.resize(count);
    for(std::size_t v=0;v<count;++v) {
        auto& vertex=out.vertices[v];
        vertex.position=Vector3(p[v*3],p[v*3+1],p[v*3+2]);
        if(std::fabs(vertex.position.X)>MaximumCoordinate||std::fabs(vertex.position.Y)>MaximumCoordinate||
           std::fabs(vertex.position.Z)>MaximumCoordinate)malformed("vertex position");
        vertex.normal=Vector3(n[v*3],n[v*3+1],n[v*3+2]);
        const float length=vertex.normal.Length();
        vertex.normal=length>1e-6f?vertex.normal/length:Vector3(0,1,0);
        vertex.uv=Vector2(t[v*2],t[v*2+1]);
        cgltf_uint raw[4];
        if(!cgltf_accessor_read_uint(joints,v,raw,4))malformed("joint data");
        float sum=0;
        std::array<float,4> weight{w[v*4],w[v*4+1],w[v*4+2],w[v*4+3]};
        for(int k=0;k<4;++k) {
            if(weight[k]<0)malformed("negative weight");
            if(raw[k]>=skinToSlot.size())malformed("joint index");
            vertex.joints[k]=static_cast<std::uint8_t>(skinToSlot[raw[k]]);
            sum+=weight[k];
        }
        if(sum<0.5f)malformed("vertex weights");
        vertex.weights=Vector4(weight[0]/sum,weight[1]/sum,weight[2]/sum,weight[3]/sum);
    }
    const auto* indices=primitive.indices;
    if(indices->type!=cgltf_type_scalar||indices->count%3||indices->count>MaximumIndices||indices->is_sparse)malformed("indices");
    out.indices.resize(indices->count);
    for(std::size_t i=0;i<indices->count;++i) {
        const auto index=cgltf_accessor_read_index(indices,i);
        if(index>=count)malformed("index out of range");
        out.indices[i]=static_cast<std::uint16_t>(index);
    }
    const auto* material=primitive.material;
    if(!material)malformed("material");
    const auto description=extras(material->extras);
    out.tint=description.value("cnaTint",std::string("none"));
    if(std::ranges::find(Tints,out.tint)==Tints.end())malformed("tint");
    out.feature=description.value("cnaFeature",std::string());
    if(!out.feature.empty()&&std::ranges::find(Features,out.feature)==Features.end())malformed("feature");
    out.layer=description.value("cnaLayer",0);
    if(out.layer<0||out.layer>1)malformed("layer");
    out.specular=description.value("cnaSpecular",1.0f);
    if(!(out.specular>=0.0f&&out.specular<=4.0f))malformed("specular");
    const auto& pbr=material->pbr_metallic_roughness;
    out.color=Vector3(std::clamp(pbr.base_color_factor[0],0.0f,1.0f),std::clamp(pbr.base_color_factor[1],0.0f,1.0f),
                      std::clamp(pbr.base_color_factor[2],0.0f,1.0f));
    if(const auto* texture=pbr.base_color_texture.texture) {
        const auto* image=texture->image;
        if(!image||image->uri||!image->buffer_view||!image->mime_type||std::strcmp(image->mime_type,"image/png")!=0)
            malformed("texture image");
        const auto* view=image->buffer_view;
        if(view->size>MaximumImageBytes)malformed("texture size");
        const auto* bytes=static_cast<const std::uint8_t*>(view->buffer->data)+view->offset;
        out.texturePng.assign(bytes,bytes+view->size);
    }
}

void readClip(const cgltf_animation& animation,const std::array<const cgltf_node*,BoneCount>& joints,AvatarClip& clip)
{
    clip.name=animation.name?animation.name:"";
    for(std::size_t index=0;index<animation.channels_count;++index) {
        const auto& channel=animation.channels[index];
        const int slot=slotOf(channel.target_node,joints);
        if(slot<0)malformed("animation target");
        AvatarTrack track;
        track.bone=slot;
        if(channel.target_path==cgltf_animation_path_type_translation&&slot==0)track.translation=true;
        else if(channel.target_path!=cgltf_animation_path_type_rotation)malformed("animation path");
        const auto* sampler=channel.sampler;
        if(sampler->interpolation!=cgltf_interpolation_type_cubic_spline)malformed("interpolation");
        const auto times=floats(sampler->input,cgltf_type_scalar,MaximumKeys);
        if(times.empty()||!std::ranges::is_sorted(times)||times.front()<0)malformed("key times");
        const auto type=track.translation?cgltf_type_vec3:cgltf_type_vec4;
        const auto values=floats(sampler->output,type,times.size()*3);
        if(values.size()!=times.size()*3*(track.translation?3:4))malformed("key values");
        track.times=times;
        const std::size_t width=track.translation?3:4;
        for(std::size_t v=0;v<values.size()/width;++v)
            track.values.emplace_back(values[v*width],values[v*width+1],values[v*width+2],track.translation?0.0f:values[v*width+3]);
        clip.duration=std::max(clip.duration,times.back());
        clip.tracks.push_back(std::move(track));
    }
    const auto description=extras(animation.extras);
    clip.loop=description.value("cnaLoop",false);
    if(description.contains("cnaExpressions")) {
        const auto& keys=description["cnaExpressions"];
        if(!keys.is_array()||keys.size()>MaximumKeys)malformed("expression keys");
        for(const auto& key:keys) {
            if(!key.is_array()||key.size()!=6)malformed("expression key");
            AvatarExpressionKey entry{key[0].get<float>(),key[1].get<int>(),key[2].get<int>(),key[3].get<int>(),key[4].get<int>(),
                key[5].get<int>()};
            if(!std::isfinite(entry.time)||entry.mouth<0||entry.mouth>13||entry.leftEye<0||entry.leftEye>13||entry.rightEye<0||
               entry.rightEye>13||entry.leftEyebrow<0||entry.leftEyebrow>4||entry.rightEyebrow<0||entry.rightEyebrow>4)
                malformed("expression value");
            clip.expressions.push_back(entry);
        }
        if(!std::ranges::is_sorted(clip.expressions,{},&AvatarExpressionKey::time))malformed("expression order");
    }
}
}

AvatarGlb parseAvatarGlb(std::span<const std::uint8_t> bytes)
{
    cgltf_options options{};
    options.type=cgltf_file_type_glb;
    Gltf gltf;
    if(bytes.size()<20||cgltf_parse(&options,bytes.data(),bytes.size(),&gltf.data)!=cgltf_result_success)malformed("not a GLB");
    // Everything must live inside the GLB: no external or data URIs, one binary buffer.
    if(gltf.data->buffers_count!=1||gltf.data->buffers[0].uri||!gltf.data->bin)malformed("buffers");
    for(std::size_t index=0;index<gltf.data->images_count;++index)
        if(gltf.data->images[index].uri)malformed("external image");
    if(cgltf_load_buffers(&options,gltf.data,nullptr)!=cgltf_result_success||cgltf_validate(gltf.data)!=cgltf_result_success)
        malformed("invalid glTF");
    try {
        AvatarGlb out;
        std::array<const cgltf_node*,BoneCount> joints{};
        std::vector<int> skinToSlot;
        readRig(gltf.data,out,joints,skinToSlot);
        std::size_t total=0;
        for(std::size_t index=0;index<gltf.data->nodes_count;++index) {
            const auto& node=gltf.data->nodes[index];
            if(!node.mesh)continue;
            if(node.skin!=&gltf.data->skins[0])malformed("mesh without the avatar skin");
            for(std::size_t p=0;p<node.mesh->primitives_count;++p) {
                if(out.primitives.size()>=MaximumPrimitives)malformed("too many primitives");
                readPrimitive(node.mesh->primitives[p],skinToSlot,out.primitives.emplace_back(),total);
            }
        }
        if(gltf.data->animations_count>MaximumClips)malformed("too many animations");
        for(std::size_t index=0;index<gltf.data->animations_count;++index)
            readClip(gltf.data->animations[index],joints,out.clips.emplace_back());
        return out;
    } catch(const nlohmann::json::exception& error) {
        malformed(error.what());
    }
}
}
