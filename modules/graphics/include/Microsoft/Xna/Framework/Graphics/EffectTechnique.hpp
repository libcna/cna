// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstdint>
#include <string>

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectAnnotationCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectPassCollection.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    class Effect;

    /**
     * @brief Represents a rendering technique within an effect, containing one or more passes.
     *
     * A technique groups related passes that collectively implement one rendering strategy.
     */
    class EffectTechnique
    {
    public:
        /** @brief Constructs a default EffectTechnique with no owner and an empty name. */
        EffectTechnique() = default;

        /**
         * @brief Constructs an EffectTechnique owned by the given effect with the given name.
         *
         * @param owner Pointer to the Effect that owns this technique.
         * @param name  Name of this technique as declared in the effect.
         */
        EffectTechnique(Effect* owner, std::string name);

        /** @brief Internal reflected-technique constructor with an initially empty pass list. */
        CNAEXT EffectTechnique(Effect* owner, std::string name, std::uint32_t techniqueIndex,
                               bool addDefaultPass);

        /**
         * @brief Gets the name of this technique.
         *
         * @return The technique name string.
         */
        [[nodiscard]] const std::string& getNameProperty() const;

        /**
         * @brief Gets the collection of passes in this technique (mutable overload).
         *
         * @return Reference to the pass collection.
         */
        [[nodiscard]] EffectPassCollection& getPassesProperty();

        /**
         * @brief Gets the collection of passes in this technique (const overload).
         *
         * @return Const reference to the pass collection.
         */
        [[nodiscard]] const EffectPassCollection& getPassesProperty() const;

        /**
         * @brief Gets the annotations attached to this technique (mutable overload).
         *
         * @return Reference to the annotation collection.
         */
        [[nodiscard]] EffectAnnotationCollection& getAnnotationsProperty();

        /**
         * @brief Gets the annotations attached to this technique (const overload).
         *
         * @return Const reference to the annotation collection.
         */
        [[nodiscard]] const EffectAnnotationCollection& getAnnotationsProperty() const;

        /**
         * @brief Gets a stable identity token for this technique.
         *
         * Unlike a raw `EffectTechnique*`, this value stays valid even if the owning
         * EffectTechniqueCollection reallocates its backing storage. Used by EffectPass::Apply()
         * to detect whether a pass belongs to the effect's currently-selected technique, mirroring
         * FNA's opaque native `TechniquePointer` comparison without depending on C++ object
         * addresses.
         *
         * @return The identity token, unique per constructed EffectTechnique instance.
         *
         * @note CNAEXT — FNA's equivalent (`TechniquePointer`) is an internal implementation
         * detail with no public C++ analogue needed here.
         */
        CNAEXT [[nodiscard]] std::uint64_t getIdInternal() const;

        /** @brief Stable zero-based runtime technique index for a compiled effect. */
        CNAEXT [[nodiscard]] std::uint32_t getIndexInternal() const;

    private:
        static std::uint64_t NextId();

        std::string name_;
        EffectPassCollection passes_;
        EffectAnnotationCollection annotations_;
        std::uint64_t id_ = NextId();
        std::uint32_t techniqueIndex_ = 0;
    };
}
