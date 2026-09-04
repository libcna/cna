// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Renderers/Igl/IglRendererSelection.hpp"

#include <cctype>
#include <cstdlib>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace CNA::Internal::Renderers::Igl::Detail
{
    namespace
    {
        constexpr const char* kBackendOverrideEnvVar = "CNA_IGL_BACKEND";

        std::string NormalizeBackendValue(std::string_view value)
        {
            std::string normalized;
            normalized.reserve(value.size());

            for (const char ch : value)
            {
                if (std::isalnum(static_cast<unsigned char>(ch)) != 0)
                {
                    normalized.push_back(
                        static_cast<char>(std::toupper(static_cast<unsigned char>(ch))));
                }
            }

            return normalized;
        }

        std::string DescribeCompiledInBackends()
        {
            std::string description;
            for (const RendererBackend backend : {RendererBackend::OpenGL, RendererBackend::Vulkan})
            {
                if (!IsRendererBackendCompiledIn(backend))
                    continue;
                if (!description.empty())
                    description += ", ";
                description += GetRendererBackendName(backend);
            }
            return description.empty() ? std::string("<none>") : description;
        }
    }

    const char* GetRendererBackendName(const RendererBackend backend)
    {
        switch (backend)
        {
            case RendererBackend::OpenGL: return "OpenGL";
            case RendererBackend::Vulkan: return "Vulkan";
        }
        return "OpenGL";
    }

    bool IsRendererBackendCompiledIn(const RendererBackend backend)
    {
        switch (backend)
        {
            case RendererBackend::OpenGL:
#ifdef CNA_IGL_HAS_OPENGL
                return true;
#else
                return false;
#endif
            case RendererBackend::Vulkan:
#ifdef CNA_IGL_HAS_VULKAN
                return true;
#else
                return false;
#endif
        }
        return false;
    }

    std::vector<RendererBackend> GetDefaultRendererPreference()
    {
        std::vector<RendererBackend> preference;
        for (const RendererBackend backend : {RendererBackend::OpenGL, RendererBackend::Vulkan})
        {
            if (IsRendererBackendCompiledIn(backend))
                preference.push_back(backend);
        }
        return preference;
    }

    std::vector<RendererBackend> ParseRendererBackendOverride(const char* value)
    {
        const std::string normalized = NormalizeBackendValue(value != nullptr ? value : "");

        if (normalized.empty() || normalized == "AUTO" || normalized == "DEFAULT")
            return GetDefaultRendererPreference();

        std::optional<RendererBackend> requested;
        if (normalized == "OPENGL" || normalized == "GL" || normalized == "GLX")
            requested = RendererBackend::OpenGL;
        else if (normalized == "VULKAN" || normalized == "VK")
            requested = RendererBackend::Vulkan;

        if (!requested.has_value())
        {
            throw std::runtime_error(
                std::string(kBackendOverrideEnvVar) + "='" + (value != nullptr ? value : "") +
                "' names no IGL backend -- expected auto, opengl or vulkan");
        }

        if (!IsRendererBackendCompiledIn(*requested))
        {
            throw std::runtime_error(
                std::string(kBackendOverrideEnvVar) + "='" + (value != nullptr ? value : "") +
                "' requests the " + GetRendererBackendName(*requested) +
                " backend, which this build does not contain (built backends: " +
                DescribeCompiledInBackends() + ")");
        }

        return {*requested};
    }

    std::vector<RendererBackend> ResolveRendererPreference(const char* environmentValue)
    {
        std::vector<RendererBackend> preference = ParseRendererBackendOverride(environmentValue);
        if (preference.empty())
        {
            throw std::runtime_error(
                "IGL renderer: no backend is available in this build (built backends: " +
                DescribeCompiledInBackends() + ")");
        }
        return preference;
    }

    RendererBackend ResolveRendererBackend()
    {
        // Cached rather than recomputed because the two callers must agree: GraphicsDevice asks
        // before the window exists (to pick the window's render intent) and the renderer asks
        // afterwards (to build the igl::IDevice it will draw with). Re-reading the environment
        // between those two points could legitimately answer differently and leave the window
        // configured for the wrong API.
        static std::optional<RendererBackend> cached;
        if (cached.has_value())
            return *cached;

        const std::vector<RendererBackend> preference =
            ResolveRendererPreference(std::getenv(kBackendOverrideEnvVar));

        cached = preference.front();
        return *cached;
    }

    bool RendererBackendNeedsOpenGLWindow(const RendererBackend backend)
    {
        return backend == RendererBackend::OpenGL;
    }

    bool RendererBackendNeedsVulkanWindow(const RendererBackend backend)
    {
        return backend == RendererBackend::Vulkan;
    }

    RendererWindowKind GetRendererBackendWindowKind(const RendererBackend backend)
    {
        if (RendererBackendNeedsOpenGLWindow(backend))
            return RendererWindowKind::OpenGL;
        if (RendererBackendNeedsVulkanWindow(backend))
            return RendererWindowKind::Vulkan;

        // Unreachable for the two backends that exist, and deliberately not `Plain`: a backend
        // this function does not know cannot be given a window silently, so it gets one no
        // renderer can adopt and the device fails by name instead of drawing into the wrong API.
        return RendererWindowKind::None;
    }

    RendererGlFramebufferRequest GetRendererBackendGlFramebufferRequest(
        const RendererBackend backend)
    {
        if (!RendererBackendNeedsOpenGLWindow(backend))
            return RendererGlFramebufferRequest{};

        RendererGlFramebufferRequest request;
        // The depth/stencil pair this renderer's own back-buffer format assumes
        // (igl::TextureFormat::S8_UInt_Z24_UNorm, see IglPlatformSurface::CreateDevice).
        request.depthBits = 24;
        request.stencilBits = 8;
        request.doubleBuffered = true;
        // Back-buffer MSAA on this backend is the platform GL visual's own multisample buffer --
        // there is no resolve pass here that could add it later, so the visual has to carry it.
        request.wantsMultiSample = true;
        return request;
    }

    RendererBackend ResolveRendererBackendForWindow() noexcept
    {
        try
        {
            return ResolveRendererBackend();
        }
        catch (...)
        {
            // CNA_IGL_BACKEND names something this build cannot honour. The descriptor this
            // answer feeds cannot report an error, so report the build's own first preference and
            // let ResolveRendererBackend() raise the real message when the renderer is created.
            const std::vector<RendererBackend> preference = GetDefaultRendererPreference();
            return preference.empty() ? RendererBackend::OpenGL : preference.front();
        }
    }
}
