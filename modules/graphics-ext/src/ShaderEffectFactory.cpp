// SPDX-License-Identifier: MS-PL
#include "CNA/Graphics/ShaderEffectFactory.hpp"

#ifdef CNA_CNAEXT

#include "CNA/Graphics/ShaderDiagnostics.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"

#include <stdexcept>

namespace CNA::Graphics {

    using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
    using Microsoft::Xna::Framework::Graphics::ShaderEffect;

    ShaderEffectFactory::ShaderEffectFactory(GraphicsDevice& device) : device_(device) {}

    ShaderEffectFactory::~ShaderEffectFactory() = default;

    ShaderEffect* ShaderEffectFactory::acquire(const std::string& name,
                                               const std::string& vertexSource,
                                               const std::string& fragmentSource)
    {
        if (name.empty())
            throw std::invalid_argument("CNA::Graphics::ShaderEffectFactory::acquire: a shader "
                                        "needs a name -- the name is the cache key");

        const auto existing = effects_.find(name);
        if (existing != effects_.end())
            return existing->second.get();

        auto effect = std::make_unique<ShaderEffect>(device_, vertexSource, fragmentSource);
        ++compileCount_;

        // Reported here rather than by every caller: the factory is the one place that knows a
        // compile just happened, and it already knows the shader's name (MOD-219).
        bool logged = false;
        detail::ReportShaderCompileFailure(device_, name, effect.get(), logged);

        ShaderEffect* raw = effect.get();
        effects_.emplace(name, std::move(effect));
        return raw;
    }

    bool ShaderEffectFactory::contains(const std::string& name) const
    {
        return effects_.find(name) != effects_.end();
    }

    std::size_t ShaderEffectFactory::getCompileCount() const { return compileCount_; }

    void ShaderEffectFactory::clear() { effects_.clear(); }

} // namespace CNA::Graphics

#endif // CNA_CNAEXT
