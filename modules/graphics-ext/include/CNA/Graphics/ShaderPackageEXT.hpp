// SPDX-License-Identifier: MS-PL
#pragma once

#ifdef CNA_CNAEXT

#include "CNA/Graphics/ShaderCodeEXT.hpp"

#include <string>
#include <vector>

namespace Microsoft::Xna::Framework::Graphics
{
    class GraphicsDevice;
}

namespace CNA::Graphics
{
    /** @addtogroup cnaext_engine
     *  @{
     */

    /** @brief Identifies one portable resource kind required at a logical shader binding. */
    enum class ShaderBindingTypeEXT : int
    {
        /** @brief A sampled two-dimensional texture. */
        SampledTexture2D = 0,
        /** @brief A sampled cube texture. */
        SampledTextureCube = 1,
        /** @brief A sampled three-dimensional texture. */
        SampledTexture3D = 2,
        /** @brief A sampled two-dimensional texture array. */
        SampledTexture2DArray = 3,
        /** @brief A shader storage buffer. */
        StorageBuffer = 4,
        /** @brief A two-dimensional storage texture. */
        StorageTexture2D = 5,
        /** @brief A read-only uniform/constant buffer. */
        ConstantBuffer = 6,
        /** @brief Number of declared binding kinds; not itself a binding kind. */
        Count = 7
    };

    /** @brief One renderer-neutral logical resource binding required by a shader package. */
    class ShaderBindingRequirementEXT final
    {
    public:
        /**
         * @brief Creates one validated binding requirement.
         *
         * @param name Non-empty logical resource name used by diagnostics and callers.
         * @param binding Non-negative logical binding/unit index.
         * @param type Portable resource kind required at the binding.
         * @param stage Shader stage that consumes the resource.
         * @throws std::invalid_argument If the name/index/type/stage is invalid.
         */
        ShaderBindingRequirementEXT(
            std::string name, int binding, ShaderBindingTypeEXT type,
            CNA::ShaderStageEXT stage);

        /**
         * @brief Returns the logical resource name.
         * @return Non-empty owned name.
         */
        [[nodiscard]] const std::string& getName() const noexcept;

        /**
         * @brief Returns the logical binding or texture-unit index.
         * @return Non-negative binding supplied at construction.
         */
        [[nodiscard]] int getBinding() const noexcept;

        /**
         * @brief Returns the required portable resource kind.
         * @return Binding type supplied at construction.
         */
        [[nodiscard]] ShaderBindingTypeEXT getType() const noexcept;

        /**
         * @brief Returns the stage that consumes this binding.
         * @return Stage supplied at construction.
         */
        [[nodiscard]] CNA::ShaderStageEXT getStage() const noexcept;

    private:
        std::string name_;
        int binding_;
        ShaderBindingTypeEXT type_;
        CNA::ShaderStageEXT stage_;
    };

    /**
     * @brief Owned result of selecting one complete language from a shader package.
     *
     * A usable result owns exactly one code value for every required stage. An unusable result
     * owns no code and carries a deterministic diagnostic that lists every considered language,
     * code label and refusal reason.
     */
    class ShaderPackageSelectionEXT final
    {
    public:
        /**
         * @brief Returns whether the package had one complete unambiguous usable language.
         * @return True only when selected code is available for every required stage.
         */
        [[nodiscard]] bool isUsable() const noexcept;

        /**
         * @brief Returns the selected language or `Unknown` when selection failed.
         * @return Exact selected source language or binary format.
         */
        [[nodiscard]] CNA::ShaderLanguageEXT getLanguage() const noexcept;

        /**
         * @brief Returns all selected stage payloads in the package's required-stage order.
         * @return Owned immutable code values, or an empty collection after failure.
         */
        [[nodiscard]] const std::vector<ShaderCodeEXT>& getCode() const noexcept;

        /**
         * @brief Finds the selected payload for one stage.
         * @param stage Stage to find.
         * @return Pointer to the owned code value, or null when absent or selection failed.
         */
        [[nodiscard]] const ShaderCodeEXT* findStage(CNA::ShaderStageEXT stage) const noexcept;

        /**
         * @brief Returns the complete deterministic selection diagnostic.
         * @return Owned success summary or complete considered-variant failure report.
         */
        [[nodiscard]] const std::string& getDiagnostic() const noexcept;

    private:
        friend class ShaderPackageEXT;

        ShaderPackageSelectionEXT(
            CNA::ShaderLanguageEXT language, std::vector<ShaderCodeEXT> code,
            std::string diagnostic);

        CNA::ShaderLanguageEXT language_ = CNA::ShaderLanguageEXT::Unknown;
        std::vector<ShaderCodeEXT> code_;
        std::string diagnostic_;
    };

    /**
     * @brief Owned renderer-neutral collection of shader variants and their portable contract.
     *
     * The package contains code and metadata only. It owns no renderer/native program and can be
     * copied or moved before a live device selects a compatible language in `MOD-2213`.
     */
    class ShaderPackageEXT final
    {
    public:
        /**
         * @brief Creates and validates a multi-language shader package.
         *
         * Each required stage must occur in at least one code variant, and every code/binding
         * stage must be declared as required. A language may deliberately omit a required stage;
         * the deterministic selector reports that candidate as incomplete. Duplicate code for one
         * language/stage is likewise retained so the selector can reject it as ambiguous with a
         * complete considered-variant diagnostic.
         *
         * @param variants One or more owned code variants.
         * @param requiredStages Non-empty unique list of stages the selected program must provide.
         * @param bindingRequirements Portable logical resources consumed by those stages.
         * @throws std::invalid_argument If a collection is empty/inconsistent or bindings collide.
         */
        ShaderPackageEXT(
            std::vector<ShaderCodeEXT> variants,
            std::vector<CNA::ShaderStageEXT> requiredStages,
            std::vector<ShaderBindingRequirementEXT> bindingRequirements = {});

        /**
         * @brief Returns every owned language/stage code variant in declaration order.
         * @return Immutable variant collection.
         */
        [[nodiscard]] const std::vector<ShaderCodeEXT>& getVariants() const noexcept;

        /**
         * @brief Returns the stages a selected program must provide.
         * @return Immutable unique stage list in declaration order.
         */
        [[nodiscard]] const std::vector<CNA::ShaderStageEXT>& getRequiredStages() const noexcept;

        /**
         * @brief Returns the package's portable logical resource requirements.
         * @return Immutable binding collection in declaration order.
         */
        [[nodiscard]] const std::vector<ShaderBindingRequirementEXT>&
            getBindingRequirements() const noexcept;

        /**
         * @brief Returns whether one stage is required by the selected program.
         * @param stage Stage to search for; invalid values simply return false.
         * @return True when @p stage occurs in the required-stage list.
         */
        [[nodiscard]] bool requiresStage(CNA::ShaderStageEXT stage) const noexcept;

        /**
         * @brief Selects one complete shader language for the live graphics device.
         *
         * Binary formats are preferred in fixed order (`SpirV`, then `Dxil`), followed by text
         * languages in their public identity order. Declaration order never affects selection.
         * Each required language/stage pair is queried from the live renderer; graphics and
         * compute capabilities plus portable binding requirements are checked separately.
         * Duplicate code for one language/stage makes that language ambiguous and unusable.
         *
         * @param device Live graphics device whose implemented paths and capabilities are queried.
         * @return Owned selected code or an unusable result with every considered variant listed.
         */
        [[nodiscard]] ShaderPackageSelectionEXT selectFor(
            const Microsoft::Xna::Framework::Graphics::GraphicsDevice& device) const;

    private:
        std::vector<ShaderCodeEXT> variants_;
        std::vector<CNA::ShaderStageEXT> requiredStages_;
        std::vector<ShaderBindingRequirementEXT> bindingRequirements_;
    };

    /** @} */ // end of cnaext_engine
}

#endif // CNA_CNAEXT
