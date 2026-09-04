// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Renderers/Llgl/LlglRendererSelection.hpp"

#include <LLGL/RenderSystem.h>
#include <LLGL/Report.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace CNA::Internal::Renderers::Llgl::Detail
{
    namespace
    {
        constexpr const char* kRendererOverrideEnvVar = "CNA_LLGL_RENDERER";

        std::string NormalizeRendererValue(std::string_view value)
        {
            std::string normalized;
            normalized.reserve(value.size());

            for (const char ch : value)
            {
                if (std::isalnum(static_cast<unsigned char>(ch)) != 0)
                {
                    normalized.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(ch))));
                }
            }

            return normalized;
        }

        std::string DescribeCompiledInModules()
        {
            std::string description;
            for (const RendererModule module : {RendererModule::OpenGL, RendererModule::Vulkan, RendererModule::Null})
            {
                if (!IsRendererModuleCompiledIn(module))
                    continue;
                if (!description.empty())
                    description += ", ";
                description += GetRendererModuleName(module);
            }
            return description.empty() ? std::string("<none>") : description;
        }
    }

    const char* GetRendererModuleName(RendererModule module)
    {
        switch (module)
        {
            case RendererModule::OpenGL: return "OpenGL";
            case RendererModule::Vulkan: return "Vulkan";
            case RendererModule::Null:   return "Null";
        }
        return "OpenGL";
    }

    bool IsRendererModuleCompiledIn(RendererModule module)
    {
        switch (module)
        {
            case RendererModule::OpenGL:
#ifdef CNA_LLGL_HAS_OPENGL
                return true;
#else
                return false;
#endif
            case RendererModule::Vulkan:
#ifdef CNA_LLGL_HAS_VULKAN
                return true;
#else
                return false;
#endif
            case RendererModule::Null:
#ifdef CNA_LLGL_HAS_NULL
                return true;
#else
                return false;
#endif
        }
        return false;
    }

    std::vector<RendererModule> GetDefaultRendererPreference()
    {
        std::vector<RendererModule> preference;
        // OpenGL is CNA's supported LLGL runtime on Linux/X11. The pinned Vulkan module remains
        // buildable for compile coverage, but cannot be selected: its native validation route
        // currently reports image-layout and teardown violations.
        for (const RendererModule module : {RendererModule::OpenGL})
        {
            if (IsRendererModuleCompiledIn(module))
                preference.push_back(module);
        }
        return preference;
    }

    std::vector<RendererModule> ParseRendererModuleOverride(const char* value)
    {
        const std::string normalized = NormalizeRendererValue(value != nullptr ? value : "");

        if (normalized.empty() || normalized == "AUTO" || normalized == "DEFAULT")
            return GetDefaultRendererPreference();

        std::optional<RendererModule> requested;
        if (normalized == "OPENGL" || normalized == "GL")
            requested = RendererModule::OpenGL;
        else if (normalized == "VULKAN" || normalized == "VK")
            requested = RendererModule::Vulkan;
        else if (normalized == "NULL" || normalized == "NONE")
            requested = RendererModule::Null;

        if (!requested.has_value())
        {
            throw std::runtime_error(
                std::string(kRendererOverrideEnvVar) + "='" + (value != nullptr ? value : "") +
                "' names no LLGL renderer module -- expected auto, opengl, vulkan or null");
        }

        if (!IsRendererModuleCompiledIn(*requested))
        {
            throw std::runtime_error(
                std::string(kRendererOverrideEnvVar) + "='" + (value != nullptr ? value : "") +
                "' requests the " + GetRendererModuleName(*requested) +
                " module, which this build does not contain (built modules: " +
                DescribeCompiledInModules() + ")");
        }

        if (*requested == RendererModule::Vulkan)
        {
            throw std::runtime_error(
                std::string(kRendererOverrideEnvVar) + "='" + (value != nullptr ? value : "") +
                "' requests LLGL Vulkan, which is compile-covered but not a supported CNA runtime "
                "path at this pinned dependency revision; use 'opengl'");
        }

        return {*requested};
    }

    std::vector<RendererModule> ResolveRendererPreference(const char* environmentValue)
    {
        std::vector<RendererModule> preference = ParseRendererModuleOverride(environmentValue);
        if (preference.empty())
        {
            throw std::runtime_error(
                "LLGL renderer: no renderer module is available in this build (built modules: " +
                DescribeCompiledInModules() + ")");
        }
        return preference;
    }

    RendererModule ResolveRendererModule()
    {
        // The probe result is cached rather than recomputed because the two callers must agree:
        // GraphicsDevice asks before the window exists (to pick the window's SDL flags) and the
        // renderer asks afterwards (to load the render system it will actually draw with). A second
        // probe could legitimately answer differently -- a driver that has since become
        // unavailable, for instance -- and leave the window configured for the wrong API.
        static std::optional<RendererModule> cached;
        if (cached.has_value())
            return *cached;

        const std::vector<RendererModule> preference =
            ResolveRendererPreference(std::getenv(kRendererOverrideEnvVar));

        std::string failures;
        for (const RendererModule module : preference)
        {
            LLGL::RenderSystemDescriptor rendererDesc;
            rendererDesc.moduleName = GetRendererModuleName(module);

            LLGL::Report report;
            // Loading a module builds a real device (a VkInstance and VkDevice for Vulkan), so a
            // module that loads here is genuinely usable rather than merely present. The render
            // system is released immediately: LLGL's OpenGL module defers its context creation to
            // the first swap chain, which needs the window this probe runs before.
            LLGL::RenderSystemPtr renderer = LLGL::RenderSystem::Load(rendererDesc, &report);
            if (renderer)
            {
                cached = module;
                return module;
            }

            if (!failures.empty())
                failures += "; ";
            failures += std::string(GetRendererModuleName(module)) + ": " +
                        (report.GetText() != nullptr ? report.GetText() : "unknown error");
        }

        throw std::runtime_error(
            "LLGL renderer: no renderer module could be loaded (" +
            (failures.empty() ? std::string("no candidates") : failures) + ")");
    }

    bool RendererModuleNeedsOpenGLWindow(RendererModule module)
    {
        return module == RendererModule::OpenGL;
    }
}
