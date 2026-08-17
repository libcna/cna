// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstdint>
#include <string>

#include "Microsoft/Xna/Framework/Graphics/EffectAnnotationCollection.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    class Effect;

    /**
     * @brief Represents a single rendering pass within an effect technique.
     *
     * Each pass encapsulates vertex and pixel shader programs together with
     * any associated render-state changes.
     */
    class EffectPass
    {
    public:
        /**
         * @brief Constructs an EffectPass owned by the given effect with the given name.
         *
         * @param owner      Pointer to the Effect that owns this pass.
         * @param name       Name of this pass as declared in the effect.
         * @param techniqueId Identity token (EffectTechnique::getIdInternal()) of the technique
         *                    this pass belongs to. Defaults to 0 ("no owning technique"), which
         *                    Apply() never validates against since it is only ever compared when
         *                    owner is non-null and the effect has a real current technique.
         */
        explicit EffectPass(Effect* owner, std::string name, std::uint64_t techniqueId = 0,
                            std::uint32_t passIndex = 0);

        /**
         * @brief Gets the name of this pass.
         *
         * @return The pass name string.
         */
        [[nodiscard]] const std::string& getNameProperty() const;

        /**
         * @brief Gets the annotations attached to this pass (mutable overload).
         *
         * @return Reference to the annotation collection.
         */
        [[nodiscard]] EffectAnnotationCollection& getAnnotationsProperty();

        /**
         * @brief Gets the annotations attached to this pass (const overload).
         *
         * @return Const reference to the annotation collection.
         */
        [[nodiscard]] const EffectAnnotationCollection& getAnnotationsProperty() const;

        /**
         * @brief Applies this pass to the graphics device, making its shaders and state active.
         *
         * Must be called before issuing draw calls that should use this pass.
         *
         * @throws System::InvalidOperationException If owner is non-null and this pass does not
         * belong to the owning Effect's currently-selected technique (getCurrentTechniqueProperty()),
         * matching FNA's own "Applied a pass not in the current technique!" guard. This includes the
         * case where CurrentTechnique has been set to null: FNA dereferences it unconditionally and
         * crashes with a NullReferenceException, which CNA maps to this same, defined exception
         * instead of undefined behavior.
         */
        void Apply();

    private:
        Effect* owner_;
        std::string name_;
        EffectAnnotationCollection annotations_;
        std::uint64_t techniqueId_;
        std::uint32_t passIndex_ = 0;
    };
}
