#pragma once

#include "Microsoft/Xna/Framework/Graphics/EffectParameterCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectTechniqueCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsResource.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    class GraphicsDevice;

    class Effect : public GraphicsResource
    {
    public:
        explicit Effect(GraphicsDevice& device);
        ~Effect() override;

        Effect(const Effect&) = delete;
        Effect& operator=(const Effect&) = delete;

        [[nodiscard]] EffectTechnique* getCurrentTechniqueProperty() const;
        void setCurrentTechniqueProperty(EffectTechnique* value);

        [[nodiscard]] EffectParameterCollection& getParametersProperty();
        [[nodiscard]] const EffectParameterCollection& getParametersProperty() const;

        [[nodiscard]] EffectTechniqueCollection& getTechniquesProperty();
        [[nodiscard]] const EffectTechniqueCollection& getTechniquesProperty() const;

        void Apply();

        [[nodiscard]] const std::string& GetTypeName() const override;

    protected:
        virtual void OnApply() = 0;
        void Dispose(bool disposing) override;

        GraphicsDevice* device_;

    private:
        EffectParameterCollection parameters_;
        EffectTechniqueCollection techniques_;
        EffectTechnique* currentTechnique_ = nullptr;
    };
}
