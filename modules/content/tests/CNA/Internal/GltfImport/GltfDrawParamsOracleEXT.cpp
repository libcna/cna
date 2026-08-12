// SPDX-License-Identifier: MS-PL
//
// plan_gltf.md GLTF-008 -- see GltfDrawParamsOracleEXT.hpp for the scope rules. Test scope only.

#include "GltfDrawParamsOracleEXT.hpp"

#include <cmath>
#include <cstdio>

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "Microsoft/Xna/Framework/Graphics/AlphaModeEXT.hpp"
#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"
#include "Microsoft/Xna/Framework/Graphics/IEffectMatrices.hpp"
#include "Microsoft/Xna/Framework/Graphics/Model.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelBone.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelBoneCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelEffectCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMesh.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMeshCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMeshPart.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMeshPartCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/PbrEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedPbrEffect.hpp"

namespace CnaTest::GltfOracle
{
    using Microsoft::Xna::Framework::Matrix;
    using Microsoft::Xna::Framework::Graphics::AlphaModeEXT;
    using Microsoft::Xna::Framework::Graphics::Effect;
    using Microsoft::Xna::Framework::Graphics::IEffectMatrices;
    using Microsoft::Xna::Framework::Graphics::Model;
    using Microsoft::Xna::Framework::Graphics::ModelMesh;
    using Microsoft::Xna::Framework::Graphics::ModelMeshPart;
    using Microsoft::Xna::Framework::Graphics::PbrEffect;
    using Microsoft::Xna::Framework::Graphics::PrimitiveType;
    using Microsoft::Xna::Framework::Graphics::SkinnedPbrEffect;

    namespace
    {
        std::string Num(float v)
        {
            if (!std::isfinite(v)) { return "null"; }
            char buffer[32];
            std::snprintf(buffer, sizeof(buffer), "%.9g", static_cast<double>(v));
            return buffer;
        }

        std::string Quote(const std::string& s)
        {
            std::string out = "\"";
            for (const char c : s)
            {
                if (c == '"' || c == '\\') { out += '\\'; }
                out += c;
            }
            return out + "\"";
        }

        template <std::size_t N>
        std::string Flat(const std::array<float, N>& values)
        {
            std::string out = "[";
            for (std::size_t i = 0; i < N; ++i)
            {
                if (i != 0) { out += ","; }
                out += Num(values[i]);
            }
            return out + "]";
        }

        std::string Bool(bool v) { return v ? "true" : "false"; }

        ColumnMajor4x4 ToColumnMajor(const Matrix& m)
        {
            ColumnMajor4x4 out{};
            m.ToColumnMajor(out.data());
            return out;
        }

        ColumnMajor4x4 FromRaw(const float* raw)
        {
            ColumnMajor4x4 out{};
            for (std::size_t i = 0; i < out.size(); ++i) { out[i] = raw[i]; }
            return out;
        }

        /// The XNA enum name, so a failure reads "TriangleList vs LineStrip" rather than "0 vs 3".
        std::string PrimitiveTypeName(PrimitiveType type)
        {
            switch (type)
            {
                case PrimitiveType::TriangleList:  return "TriangleList";
                case PrimitiveType::TriangleStrip: return "TriangleStrip";
                case PrimitiveType::LineList:      return "LineList";
                case PrimitiveType::LineStrip:     return "LineStrip";
                case PrimitiveType::PointListEXT:  return "PointListEXT";
            }
            return "?";
        }

        /// glTF's own spelling, so an L6 record compares directly against the manifest's
        /// `l3.primitives[].material.alphaMode` without a translation table in the assertion.
        std::string AlphaModeName(AlphaModeEXT mode)
        {
            switch (mode)
            {
                case AlphaModeEXT::Opaque: return "OPAQUE";
                case AlphaModeEXT::Mask:   return "MASK";
                case AlphaModeEXT::Blend:  return "BLEND";
            }
            return "?";
        }

        /// The alpha state lives on the two PBR effects only; every other effect legitimately has
        /// none, and the record says so rather than inventing an OPAQUE default that would look
        /// like a real answer.
        void CaptureAlphaState(const Effect* effect, DrawParamsDump& dump)
        {
            if (const auto* pbr = dynamic_cast<const PbrEffect*>(effect))
            {
                dump.carriesAlphaState = true;
                dump.alphaMode   = AlphaModeName(pbr->getAlphaModeEXTProperty());
                dump.alphaCutoff = pbr->getAlphaCutoffEXTProperty();
                dump.doubleSided = pbr->getDoubleSidedEXTProperty();
                return;
            }
            if (const auto* skinned = dynamic_cast<const SkinnedPbrEffect*>(effect))
            {
                dump.carriesAlphaState = true;
                dump.alphaMode   = AlphaModeName(skinned->getAlphaModeEXTProperty());
                dump.alphaCutoff = skinned->getAlphaCutoffEXTProperty();
                dump.doubleSided = skinned->getDoubleSidedEXTProperty();
            }
        }
    }

    ColumnMajor3x3 NormalMatrixFromWorldEXT(const ColumnMajor4x4& w)
    {
        // Identical to EasyGLRenderer::BindDrawParams()'s uNormalMatrix derivation, down to the
        // det == 0 -> all-zero fallback: an L6 record must be the number a renderer uploads, not a
        // cleaner one this file would have preferred.
        const float a = w[0], d = w[1], g = w[2];
        const float b = w[4], e = w[5], h = w[6];
        const float c = w[8], f = w[9], i = w[10];
        const float det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
        const float invDet = (det != 0.0f) ? (1.0f / det) : 0.0f;
        return {
            (e * i - f * h) * invDet, -(b * i - c * h) * invDet, (b * f - c * e) * invDet,
            -(d * i - f * g) * invDet, (a * i - c * g) * invDet, -(a * f - c * d) * invDet,
            (d * h - e * g) * invDet, -(a * h - b * g) * invDet, (a * e - b * d) * invDet,
        };
    }

    std::size_t ApplyBonePaletteEXT(Model& model, const std::vector<Matrix>& skinTransforms)
    {
        if (skinTransforms.empty()) { return 0; }

        std::size_t applied = 0;
        const auto& meshes = model.getMeshesProperty();
        for (int mi = 0; mi < meshes.getCountProperty(); ++mi)
        {
            const ModelMesh* mesh = meshes[mi];
            if (mesh == nullptr) { continue; }
            const auto& parts = mesh->getMeshPartsProperty();
            for (int pi = 0; pi < parts.getCountProperty(); ++pi)
            {
                ModelMeshPart* part = parts[pi];
                if (part == nullptr) { continue; }
                if (auto* skinned = dynamic_cast<SkinnedPbrEffect*>(part->getEffectProperty()))
                {
                    skinned->SetBoneTransforms(skinTransforms);
                    ++applied;
                }
            }
        }
        return applied;
    }

    std::vector<DrawParamsDump> CaptureDrawParamsEXT(Model& model, const Matrix& world,
                                                      const Matrix& view, const Matrix& projection)
    {
        // Bind exactly as the real draw path binds. Model::Draw does the binding and then issues
        // the draws, and the draws need a 3D-capable device the conformance suite deliberately
        // does not have; so the binding half is reproduced here, statement for statement, over the
        // same public API (ModelBone absolute transforms -> IEffectMatrices). The half that is NOT
        // reproduced -- GraphicsDevice's buffer bookkeeping -- contributes no §21.1 quantity.
        std::vector<Matrix> absoluteBoneTransforms(
            static_cast<std::size_t>(model.getBonesProperty().getCountProperty()));
        model.CopyAbsoluteBoneTransformsTo(absoluteBoneTransforms);

        std::vector<DrawParamsDump> out;
        const auto& meshes = model.getMeshesProperty();
        for (int mi = 0; mi < meshes.getCountProperty(); ++mi)
        {
            ModelMesh* mesh = meshes[mi];
            if (mesh == nullptr) { continue; }

            const int boneIndex = mesh->getParentBoneProperty() != nullptr
                                      ? mesh->getParentBoneProperty()->getIndexProperty()
                                      : 0;
            const Matrix meshWorld =
                boneIndex >= 0 && static_cast<std::size_t>(boneIndex) < absoluteBoneTransforms.size()
                    ? absoluteBoneTransforms[static_cast<std::size_t>(boneIndex)] * world
                    : world;

            const auto& effects = mesh->getEffectsProperty();
            for (int ei = 0; ei < effects.getCountProperty(); ++ei)
            {
                if (auto* em = dynamic_cast<IEffectMatrices*>(effects[ei]))
                {
                    em->setWorldProperty(meshWorld);
                    em->setViewProperty(view);
                    em->setProjectionProperty(projection);
                }
            }

            const auto& parts = mesh->getMeshPartsProperty();
            for (int pi = 0; pi < parts.getCountProperty(); ++pi)
            {
                const ModelMeshPart* part = parts[pi];
                if (part == nullptr) { continue; }
                const Effect* effect = part->getEffectProperty();
                // ModelMesh::Draw's own skip rule, repeated so the capture covers exactly the
                // parts that would be drawn -- no more, no fewer.
                if (effect == nullptr || part->getPrimitiveCountProperty() <= 0) { continue; }

                CNA::Internal::Renderers::GpuDrawParams p;
                effect->FillGpuDrawParams(p);

                DrawParamsDump dump;
                dump.meshIndex       = static_cast<std::size_t>(mi);
                dump.meshName        = mesh->getNameProperty();
                dump.partIndex       = static_cast<std::size_t>(pi);
                dump.parentBoneIndex = boneIndex;
                dump.effectTypeName  = effect->GetTypeName();

                dump.world         = ToColumnMajor(meshWorld);
                dump.view          = ToColumnMajor(view);
                dump.projection    = ToColumnMajor(projection);
                dump.worldColMajor = FromRaw(p.worldColMajor);
                dump.normalMatrix  = NormalMatrixFromWorldEXT(dump.worldColMajor);

                dump.diffuseColor = {p.diffuseColor[0], p.diffuseColor[1], p.diffuseColor[2],
                                     p.diffuseColor[3]};
                dump.metallicFactor  = p.pbrMetallicFactor;
                dump.roughnessFactor = p.pbrRoughnessFactor;
                dump.normalScale       = p.pbrNormalScale;
                dump.occlusionStrength = p.pbrOcclusionStrength;
                dump.baseColorTextureIsSrgb = p.pbrBaseColorTextureIsSrgb;
                dump.emissiveTextureIsSrgb  = p.pbrEmissiveTextureIsSrgb;
                dump.encodeOutputToSrgb     = p.pbrEncodeOutputToSrgb;
                dump.emissiveColor   = {p.emissiveColor[0], p.emissiveColor[1], p.emissiveColor[2]};
                dump.ambientColor    = {p.ambientColor[0], p.ambientColor[1], p.ambientColor[2]};

                dump.hasBaseColorMap         = p.texture0 != nullptr;
                dump.hasNormalMap            = p.pbrNormalMap != nullptr;
                dump.hasMetallicRoughnessMap = p.pbrMetallicRoughnessMap != nullptr;
                dump.hasOcclusionMap         = p.pbrOcclusionMap != nullptr;
                dump.hasEmissiveMap          = p.pbrEmissiveMap != nullptr;

                dump.boneCount        = p.boneCount;
                dump.weightsPerVertex = p.weightsPerVertex;
                dump.boneTransforms.reserve(static_cast<std::size_t>(p.boneCount));
                for (int b = 0; b < p.boneCount; ++b)
                {
                    dump.boneTransforms.push_back(FromRaw(p.boneTransforms + b * 16));
                }

                dump.alphaTest = {p.alphaTest[0], p.alphaTest[1], p.alphaTest[2], p.alphaTest[3]};
                CaptureAlphaState(effect, dump);

                dump.pbr                = p.pbr;
                dump.skinned            = p.skinned;
                dump.lightingEnabled    = p.lightingEnabled;
                // plan_gltf.md GLTF-376: the three directional lights as the shader receives them.
                // A disabled light is a zero diffuse colour, not a flag -- the shader adds all
                // three unconditionally.
                const float* const dirs[3] = {p.light0Dir, p.light1Dir, p.light2Dir};
                const float* const diffuse[3] = {p.light0Diffuse, p.light1Diffuse, p.light2Diffuse};
                for (std::size_t l = 0; l < 3; ++l)
                {
                    for (std::size_t c = 0; c < 3; ++c)
                    {
                        dump.lightDirections[l][c] = dirs[l][c];
                        dump.lightDiffuseColors[l][c] = diffuse[l][c];
                    }
                }
                dump.textureEnabled     = p.textureEnabled;
                dump.vertexColorEnabled = p.vertexColorEnabled;

                dump.primitiveType  = PrimitiveTypeName(part->getPrimitiveTypeEXTProperty());
                dump.primitiveCount = part->getPrimitiveCountProperty();

                out.push_back(std::move(dump));
            }
        }
        return out;
    }

    std::string ToJson(const DrawParamsDump& dump)
    {
        std::string out = "{";
        out += "\"meshIndex\":" + std::to_string(dump.meshIndex);
        out += ",\"meshName\":" + Quote(dump.meshName);
        out += ",\"partIndex\":" + std::to_string(dump.partIndex);
        out += ",\"parentBoneIndex\":" + std::to_string(dump.parentBoneIndex);
        out += ",\"effect\":" + Quote(dump.effectTypeName);
        out += ",\"world\":" + Flat(dump.world);
        out += ",\"view\":" + Flat(dump.view);
        out += ",\"projection\":" + Flat(dump.projection);
        out += ",\"worldColMajor\":" + Flat(dump.worldColMajor);
        out += ",\"normalMatrix\":" + Flat(dump.normalMatrix);
        out += ",\"diffuseColor\":" + Flat(dump.diffuseColor);
        out += ",\"metallicFactor\":" + Num(dump.metallicFactor);
        out += ",\"roughnessFactor\":" + Num(dump.roughnessFactor);
        out += ",\"normalScale\":" + Num(dump.normalScale);
        out += ",\"occlusionStrength\":" + Num(dump.occlusionStrength);
        out += ",\"baseColorTextureIsSrgb\":" + Bool(dump.baseColorTextureIsSrgb);
        out += ",\"emissiveTextureIsSrgb\":" + Bool(dump.emissiveTextureIsSrgb);
        out += ",\"encodeOutputToSrgb\":" + Bool(dump.encodeOutputToSrgb);
        out += ",\"emissiveColor\":" + Flat(dump.emissiveColor);
        out += ",\"ambientColor\":" + Flat(dump.ambientColor);
        out += ",\"hasBaseColorMap\":" + Bool(dump.hasBaseColorMap);
        out += ",\"hasNormalMap\":" + Bool(dump.hasNormalMap);
        out += ",\"hasMetallicRoughnessMap\":" + Bool(dump.hasMetallicRoughnessMap);
        out += ",\"hasOcclusionMap\":" + Bool(dump.hasOcclusionMap);
        out += ",\"hasEmissiveMap\":" + Bool(dump.hasEmissiveMap);
        out += ",\"boneCount\":" + std::to_string(dump.boneCount);
        out += ",\"boneTransforms\":[";
        for (std::size_t i = 0; i < dump.boneTransforms.size(); ++i)
        {
            if (i != 0) { out += ","; }
            out += Flat(dump.boneTransforms[i]);
        }
        out += "]";
        out += ",\"weightsPerVertex\":" + std::to_string(dump.weightsPerVertex);
        out += ",\"alphaTest\":" + Flat(dump.alphaTest);
        out += ",\"carriesAlphaState\":" + Bool(dump.carriesAlphaState);
        out += ",\"alphaMode\":" + Quote(dump.alphaMode);
        out += ",\"alphaCutoff\":" + Num(dump.alphaCutoff);
        out += ",\"doubleSided\":" + Bool(dump.doubleSided);
        out += ",\"pbr\":" + Bool(dump.pbr);
        out += ",\"skinned\":" + Bool(dump.skinned);
        out += ",\"lightingEnabled\":" + Bool(dump.lightingEnabled);
        out += ",\"textureEnabled\":" + Bool(dump.textureEnabled);
        out += ",\"vertexColorEnabled\":" + Bool(dump.vertexColorEnabled);
        out += ",\"primitiveType\":" + Quote(dump.primitiveType);
        out += ",\"primitiveCount\":" + std::to_string(dump.primitiveCount);
        out += "}";
        return out;
    }
}
