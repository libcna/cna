// SPDX-License-Identifier: MS-PL
#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "Microsoft/Xna/Framework/BoundingSphere.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelBoneCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/GltfImportReportEXT.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMeshCollection.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "System/Object.hpp"
#include "CNA/CNAHelper.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    class Model;
}

namespace CNA::Internal::Graphics
{
    struct ModelMaterialVariantBindingEXT;
    struct ModelMaterialVariantsEXT;
    void ConfigureModelMaterialVariantsEXT(
        Microsoft::Xna::Framework::Graphics::Model& model,
        std::vector<std::string> names,
        std::vector<ModelMaterialVariantBindingEXT> bindings);
}

namespace Microsoft::Xna::Framework::Graphics
{
    class GraphicsDevice;
    /**
     * @brief One camera imported from a source asset, with its projection and its placement apart.
     *
     * @note CNAEXT — not part of the XNA 4.0 API (plans/plan_gltf.md `GLTF-317` … `GLTF-321`). glTF §3.10
     * puts the projection on the camera and the placement on the node, and the two are kept apart
     * here for the same reason: an application animating the camera's node needs to recompute the
     * view without touching the projection.
     */
    /**
     * @brief Builds the projection glTF's infinite perspective camera describes (§3.10.3).
     *
     * @note CNAEXT — not part of the XNA 4.0 API (plans/plan_gltf.md `GLTF-319`). A glTF perspective
     * camera may omit `zfar`, which means the far plane is at infinity. XNA has no overload for
     * that, and substituting a large finite `zfar` is not equivalent: it changes every depth value,
     * not just the ones near the horizon, so a depth comparison that was exact becomes approximate.
     *
     * The matrix is `CreatePerspectiveFieldOfView`'s with the two `z` terms taken to their limits
     * as `zfar → ∞`: `M33 → −1` and `M43 → −2·znear` (right-handed, as XNA's own perspective
     * builders are).
     *
     * @param fieldOfView Vertical field of view, in radians.
     * @param aspectRatio Width divided by height.
     * @param nearPlaneDistance Distance to the near view plane; must be positive.
     * @return The infinite-far-plane projection matrix.
     * @throws std::invalid_argument if @p fieldOfView is not in `(0, π)` or @p nearPlaneDistance is
     * not positive — the same contract, and the same exception type, as `Matrix`'s own finite
     * `CreatePerspectiveFieldOfView`, since this builder is that one's limit case.
     */
    CNAEXT [[nodiscard]] Matrix CreateInfinitePerspectiveFieldOfViewEXT(
        float fieldOfView, float aspectRatio, float nearPlaneDistance);

    /**
     * @brief One camera placement imported from a glTF scene (plans/plan_gltf.md `GLTF-317`).
     *
     * @note CNAEXT — not part of the XNA 4.0 API. XNA's `Model` has no cameras at all. A placement
     * rather than a camera: one glTF camera instanced by several nodes is several placements, and
     * walking `data->cameras` instead of the scene graph would collapse them into one.
     *
     * Projection and placement are kept apart, as §3.10 keeps them, because an application
     * animating the camera node must recompute the view without touching the projection.
     */
    CNAEXT struct ModelCameraEXT
    {
        /** @brief The camera's name; may be empty. */
        std::string Name;
        /**
         * @brief The scene-node index this camera is attached to, or -1 when it has no node.
         *
         * Indexes `Model::Bones`, like every other scene-node index (plans/plan_gltf.md §15.1.2), so the
         * camera follows its bone when the model is posed.
         */
        int SceneNodeIndex = -1;
        /**
         * @brief The camera's projection matrix.
         *
         * Already built from the source's own parameters: a perspective camera with no `zfar` gets
         * the infinite projection (`GLTF-319`), and an orthographic one gets `CreateOrthographic`
         * over twice its magnifications (`GLTF-320`).
         */
        Matrix Projection = Matrix::getIdentityProperty();
        /**
         * @brief The camera node's world transform **at import time**.
         *
         * The **view** matrix is its inverse (`GLTF-321`): a glTF camera looks down its own −Z with
         * +Y up, which is exactly XNA's own convention, so no basis change is involved.
         *
         * @warning This is a snapshot, not a live value (plans/plan_gltf.md `GLTF-296`). A camera node is
         * an ordinary node and can be animated like any other, and posing the model updates
         * `Model::Bones` — not this. A camera's **live** placement is the absolute transform of the
         * bone @ref SceneNodeIndex names, which `Model::CopyAbsoluteBoneTransformsTo` produces. A
         * consumer that read this matrix every frame would render an animated camera as a
         * stationary one.
         */
        Matrix WorldTransform = Matrix::getIdentityProperty();
        /** @brief True for a perspective camera, false for an orthographic one. */
        bool IsPerspective = true;
        /**
         * @brief True when the source declared no `zfar`, so @ref Projection is infinite.
         *
         * Recorded because the two projections are genuinely different matrices and a consumer
         * reasoning about depth precision needs to know which it has.
         */
        bool HasInfiniteFarPlane = false;
        /**
         * @brief The aspect ratio @ref Projection was built with (perspective cameras only).
         *
         * plans/plan_gltf.md `GLTF-322`. Equal to @ref AspectRatio's authored value when the file
         * declared one, and `1` when it did not.
         */
        float AspectRatio = 1.0f;
        /**
         * @brief True when the file declared `aspectRatio`; false when @ref AspectRatio was assumed.
         *
         * §3.10.3 says an undefined `aspectRatio` means "use the viewport's", which an importer
         * cannot know — so one is assumed and this flag says so. Without it a consumer cannot tell
         * an author who framed a square shot from one who left the decision to the runtime, and
         * would either stretch a deliberate framing or letterbox an intentionally viewport-relative
         * one. A consumer that wants the viewport's aspect rebuilds @ref Projection from
         * @ref FieldOfView, @ref NearPlaneDistance and @ref FarPlaneDistance when this is false.
         */
        bool HasAuthoredAspectRatio = false;
        /**
         * @brief Vertical field of view in radians (perspective cameras only), from `yfov`.
         *
         * Carried rather than left to be recovered from @ref Projection: inverting a projection to
         * get back a parameter the file stated outright is work a consumer should not have to do,
         * and is not even possible for the infinite variant without knowing which variant it is.
         */
        float FieldOfView = 0.0f;
        /** @brief Distance to the near clip plane, from `znear`. */
        float NearPlaneDistance = 0.0f;
        /**
         * @brief Distance to the far clip plane, from `zfar`; `0` when @ref HasInfiniteFarPlane.
         */
        float FarPlaneDistance = 0.0f;
    };

    class ModelBone;
    class ModelMesh;
    struct SkinningData;

    /**
     * @brief One independently posed skin imported into a Model.
     *
     * @note CNAEXT — not part of the XNA 4.0 API (plans/plan_gltf.md `GLTF-265`). A glTF file may
     * contain several skins, while the conventional XNA `Model::Tag` carrier can hold only one
     * `SkinningData`. The first imported skin remains on `Model::Tag` for compatibility; this
     * collection is the complete, unambiguous mapping from every skin to the meshes its palette
     * drives. @ref Data and every mesh pointer are owned by the Model's content resources and
     * remain valid for the Model's lifetime.
     */
    CNAEXT struct ModelSkinEXT
    {
        /** @brief The source skin's display name; may be empty. */
        std::string Name;
        /** @brief The independent skeleton, bind pose and animation clips for this skin. */
        SkinningData* Data = nullptr;
        /** @brief Mesh placements whose skinned effects consume @ref Data's palette. */
        std::vector<ModelMesh*> Meshes;
    };

    /**
     * @brief A basic 3D model with per-mesh parent bones.
     */
    class Model
    {
    public:
        /** @brief Constructs an empty model. */
        Model() = default;

        /**
         * @brief Constructs a model from a graphics device, bones, and meshes.
         * @param graphicsDevice A valid reference to the GraphicsDevice.
         * @param bones The collection of bones.
         * @param meshes The collection of meshes.
         */
        CNAEXT Model(GraphicsDevice* graphicsDevice,
                    std::vector<ModelBone*> bones,
                    std::vector<ModelMesh*> meshes);

        /**
         * @brief Constructs a model from a graphics device, bones, meshes, and each mesh's parent bone.
         *
         * @param graphicsDevice A valid reference to the GraphicsDevice.
         * @param bones The collection of bones.
         * @param meshes The collection of meshes.
         * @param meshParentBones Per-mesh parent bone, in the same order as @p meshes. Must either
         *        be empty (every mesh's ParentBone stays nullptr, matching the 3-argument
         *        constructor) or have exactly one entry per mesh.
         * @param rootBoneIndex Index into @p bones naming the model's root bone. Defaults to `0`,
         *        matching the 3-argument constructor's own default. FNA's real content-pipeline
         *        loader (`ModelReader`) can assign an arbitrary root bone this way; this parameter
         *        is the public equivalent for hand-built models.
         */
        CNAEXT Model(GraphicsDevice* graphicsDevice,
                    std::vector<ModelBone*> bones,
                    std::vector<ModelMesh*> meshes,
                    std::vector<ModelBone*> meshParentBones,
                    std::size_t rootBoneIndex = 0);

        /**
         * @brief Gets the collection of bones that describe how each mesh relates to its parent.
         * @return The model's bone collection.
         */
        [[nodiscard]] const ModelBoneCollection& getBonesProperty() const;

        /**
         * @brief Gets the collection of meshes that compose this model.
         * @return The model's mesh collection.
         */
        [[nodiscard]] const ModelMeshCollection& getMeshesProperty() const;

        /**
         * @brief Gets the root bone for this model.
         * @return Pointer to the root ModelBone.
         */
        [[nodiscard]] ModelBone* getRootProperty() const;

        /**
         * @brief Gets the custom object attached to this model.
         * @return Pointer to the tag object, or nullptr.
         */
        [[nodiscard]] System::Object* getTagProperty() const;

        /**
         * @brief Sets the custom object attached to this model.
         * @param value Pointer to the tag object.
         */
        void setTagProperty(System::Object* value);

        /**
         * @brief Transfers ownership of GPU resources allocated by a content reader.
         * @param resources Shared ownership handle to the resources.
         */
        CNAEXT void setOwnedResources(std::shared_ptr<void> resources);

        /**
         * @brief Cameras the source asset declared, in scene-node order.
         *
         * @note CNAEXT — not part of the XNA 4.0 API (plans/plan_gltf.md `GLTF-317`). glTF files ship
         * their own cameras and CNA dropped every one of them, so an asset that had been framed by
         * its author arrived with no framing at all and each viewer had to invent one.
         *
         * A property rather than a `Tag` payload for the reason `Tag` keeps running into: it holds
         * one object, and `SkinningData` and `ModelAnimationsEXT` already contend for it. A skinned
         * model with cameras would otherwise have to choose.
         *
         * Empty for every model built by any other content path.
         *
         * @return The imported cameras.
         */
        CNAEXT [[nodiscard]] const std::vector<ModelCameraEXT>& getCamerasEXTProperty() const;

        /**
         * @brief Replaces the imported camera list.
         * @param value The cameras, normally in scene-node order.
         */
        CNAEXT void setCamerasEXTProperty(std::vector<ModelCameraEXT> value);

        /**
         * @brief Gets every independently posed skin imported with this model.
         *
         * @note CNAEXT — not part of the XNA 4.0 API (`GLTF-265`). Empty for unskinned models
         * and content paths that do not provide this mapping. A single-skin glTF model has one
         * entry whose @ref ModelSkinEXT::Data is also exposed through the legacy `Model::Tag`
         * convention. A multi-skin model keeps only that first pointer in `Tag`; callers must use
         * this collection to animate each skin and apply its palette only to the listed meshes.
         *
         * @return Skin records in source mesh-group order.
         */
        CNAEXT [[nodiscard]] const std::vector<ModelSkinEXT>& getSkinsEXTProperty() const;

        /**
         * @brief Replaces the imported skin-to-mesh mapping.
         * @param value The complete mapping, normally populated by the glTF content reader.
         */
        CNAEXT void setSkinsEXTProperty(std::vector<ModelSkinEXT> value);

        /**
         * @brief Gets structured diagnostics from this model's glTF import.
         *
         * @note CNAEXT — not part of the XNA 4.0 API (`GLTF-034`). The direct glTF reader and
         * `gltf_to_cnj`/Model `.cnj` path populate the same carrier. Other content paths return an
         * empty report, so reading this property never requires testing how the Model was built.
         *
         * @return The import report owned by this model.
         */
        CNAEXT [[nodiscard]] const GltfImportReportEXT&
        getGltfImportReportEXTProperty() const;

        /**
         * @brief Replaces this model's glTF import report.
         * @param value The complete replacement report.
         */
        CNAEXT void setGltfImportReportEXTProperty(GltfImportReportEXT value);

        /**
         * @brief Gets one sphere containing every mesh at its current parent-bone placement.
         *
         * @note CNAEXT — not part of the XNA 4.0 API (plans/plan_gltf.md `GLTF-128`). XNA exposes a
         * sphere on each `ModelMesh`, but no whole-model union; consumers otherwise have to walk
         * private vertex sidecars and duplicate `Model::Draw`'s bone composition just to frame or
         * cull an imported scene.
         *
         * The result is recomputed from the current absolute bone transforms, so rigid/node
         * animation is reflected immediately. For a mesh named by @ref getSkinsEXTProperty it
         * also conservatively unions the mesh sphere under the current bone palette carried by
         * its `SkinnedEffect`/`SkinnedPbrEffect`; bind-pose mesh-node cancellation and later
         * `AnimationPlayer` updates therefore remain inside the result. It is in model-root space
         * — glTF's composed scene space after node transforms and skinning, but before the
         * caller's `world` argument to `Model::Draw`. Transform the returned sphere by that
         * matrix when application world space is required.
         *
         * This has the same deformation contract as the mesh spheres it aggregates. Skinning is
         * conservative over the complete active palette and can therefore overbound a mesh whose
         * vertices use only a few joints. A morph that moves vertices outside an imported mesh
         * sphere still requires the caller to update that existing read-write
         * `ModelMesh::BoundingSphere` property.
         *
         * @return The merged sphere, or `std::nullopt` when the model has no meshes.
         */
        CNAEXT [[nodiscard]] std::optional<BoundingSphere>
        getBoundingSphereEXTProperty() const;

        /**
         * @brief Names of the material variants declared by the imported asset, in source order.
         *
         * @note CNAEXT — not part of the XNA 4.0 API (plans/plan_gltf.md `GLTF-341`/`GLTF-342`). The
         * index in this vector is the value accepted by @ref setMaterialVariantEXTProperty. An
         * index is used rather than a name because the glTF extension defines variant identity by
         * array position and does not make a display name a safe unique key.
         *
         * Empty for models from content paths that do not carry material variants.
         *
         * @return Variant display names in source order.
         */
        CNAEXT [[nodiscard]] const std::vector<std::string>&
        getMaterialVariantNamesEXTProperty() const;

        /**
         * @brief Gets the selected material-variant index, or `-1` for default materials.
         *
         * The default is always `-1`; loading a model never selects an extension variant
         * implicitly, preserving each primitive's core `material` mapping exactly.
         *
         * @return The selected variant index, or `-1`.
         */
        CNAEXT [[nodiscard]] int getMaterialVariantEXTProperty() const;

        /**
         * @brief Selects one imported material variant, or restores defaults with `-1`.
         *
         * Selection applies across the entire model. A primitive that has no mapping for the
         * selected variant uses its own default material, as `KHR_materials_variants` specifies;
         * selecting another variant therefore never leaves stale material state behind from the
         * previous selection.
         *
         * @param value Variant index from @ref getMaterialVariantNamesEXTProperty, or `-1`.
         * @throws std::out_of_range when @p value is less than `-1` or outside the names vector.
         */
        CNAEXT void setMaterialVariantEXTProperty(int value);

        /**
         * @brief Copies bone transforms relative to all parent bones to a given vector.
         * @param destinationBoneTransforms The vector receiving the absolute bone transforms.
         */
        void CopyAbsoluteBoneTransformsTo(std::vector<Matrix>& destinationBoneTransforms) const;

        /**
         * @brief Copies bone transforms from a given vector into this model's bones.
         * @param sourceBoneTransforms The vector of prepared bone transform data.
         */
        void CopyBoneTransformsFrom(const std::vector<Matrix>& sourceBoneTransforms);

        /**
         * @brief Copies bone transforms relative to the root bone to a given vector.
         * @param destinationBoneTransforms The vector receiving the bone transforms.
         */
        void CopyBoneTransformsTo(std::vector<Matrix>& destinationBoneTransforms) const;

        /**
         * @brief Draws all model meshes using their current effect settings.
         * @param world The world transform matrix.
         * @param view The view transform matrix.
         * @param projection The projection transform matrix.
         */
        void Draw(const Matrix& world, const Matrix& view, const Matrix& projection);

    private:
        friend void CNA::Internal::Graphics::ConfigureModelMaterialVariantsEXT(
            Model& model,
            std::vector<std::string> names,
            std::vector<CNA::Internal::Graphics::ModelMaterialVariantBindingEXT> bindings);

        std::vector<ModelCameraEXT> cameras_;
        std::vector<ModelSkinEXT> skins_;
        GltfImportReportEXT gltfImportReport_;
        std::shared_ptr<CNA::Internal::Graphics::ModelMaterialVariantsEXT> materialVariants_;
        ModelBoneCollection bones_;
        ModelMeshCollection meshes_;
        ModelBone* root_ = nullptr;
        System::Object* tag_ = nullptr;
        std::shared_ptr<void> ownedResources_;

        // Deviation from FNA, deliberate and behaviour-preserving (plans/plan_gltf.md GLTF-444).
        // FNA's Model.Draw shares one static Matrix[] across every Model in the process. It is
        // pure scratch -- CopyAbsoluteBoneTransformsTo overwrites it in full at the top of every
        // Draw and nothing reads it afterwards -- so no state is carried between calls and one
        // buffer per thread is observably identical to one per process for any single-threaded
        // caller. What it removes is the race: two threads drawing different models resize and
        // rewrite the same vector, and a resize while another thread holds a reference into it is
        // a use-after-free, not merely a wrong matrix. The cost is one vector per thread that has
        // ever drawn a model.
        static thread_local std::vector<Matrix> sharedDrawBoneMatrices_;
    };
}
