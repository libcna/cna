// SPDX-License-Identifier: MS-PL
#pragma once

#include <array>
#include <optional>
#include <string>
#include <vector>

#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"

namespace System
{
    class Object;
}

namespace Microsoft::Xna::Framework::Graphics
{
    class Effect;
    class Model;
    class ModelMeshPart;
    class VertexBuffer;
}

namespace CNA::Internal::Graphics
{
    /**
     * @brief Complete draw state one material selection assigns to a ModelMeshPart.
     *
     * Internal bridge between the glTF/.cnj content readers and Model's public, index-based
     * material-variant property. A material can choose another UV set or texture transform, so an
     * Effect pointer alone is not enough: the vertex buffer, sampler state and morph carrier can
     * all differ as well (plan_gltf.md GLTF-341/GLTF-342).
     */
    struct ModelMaterialVariantPartStateEXT
    {
        Microsoft::Xna::Framework::Graphics::VertexBuffer* vertexBuffer = nullptr;
        Microsoft::Xna::Framework::Graphics::Effect* effect = nullptr;
        System::Object* tag = nullptr;
        std::array<Microsoft::Xna::Framework::Graphics::SamplerState, 5> samplerStates{};
        int numVertices = 0;
    };

    /** @brief Default state and sparse per-variant overrides for one mesh part. */
    struct ModelMaterialVariantBindingEXT
    {
        Microsoft::Xna::Framework::Graphics::ModelMeshPart* part = nullptr;
        ModelMaterialVariantPartStateEXT defaultState;
        std::vector<std::optional<ModelMaterialVariantPartStateEXT>> variants;
    };

    /** @brief Shared state so copies of an XNA-style Model observe one material selection. */
    struct ModelMaterialVariantsEXT
    {
        std::vector<std::string> names;
        std::vector<ModelMaterialVariantBindingEXT> bindings;
        int activeVariant = -1;
    };

    /**
     * @brief Installs importer-built material-variant state on a Model.
     *
     * Kept under CNA/Internal rather than exposed as public model construction surface. The public
     * contract is Model's names/selected-index properties; only content readers need to supply raw
     * buffers, effects and morph carriers.
     */
    void ConfigureModelMaterialVariantsEXT(
        Microsoft::Xna::Framework::Graphics::Model& model,
        std::vector<std::string> names,
        std::vector<ModelMaterialVariantBindingEXT> bindings);
}
