// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Graphics/GraphicsAdapter.hpp"

#include <SDL3/SDL.h>

#include <stdexcept>
#include <utility>

#ifdef __linux__
#include <fstream>
#include <string>
#endif

// plan_dx9.md Phase D9-10 (D9-101/D9-102): GraphicsProfile.Reach/HiDef is a real, D3DCAPS9-backed
// distinction ONLY on this renderer -- the other 9 CNA renderers have no D3DCAPS9 to consult and
// keep their honest `return true;`/hardcoded-fallback behavior below (see plan_dx9.md's own
// "Boundaries" section: an implementation would otherwise be a hardcoded table pretending to be a
// capability query, which this project explicitly refuses to fake).
#include "CNA/Internal/Renderers/Common/GraphicsRendererDescriptor.hpp"
#include "CNA/Internal/Renderers/Common/GraphicsRendererRegistry.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    std::vector<std::unique_ptr<GraphicsAdapter>> GraphicsAdapter::adapters_;

    namespace
    {
        std::string getDisplayName(SDL_DisplayID displayId, SharpRuntime::intcs fallbackIndex)
        {
            const char* name = SDL_GetDisplayName(displayId);
            if (name != nullptr && *name != '\0')
            {
                return std::string(name);
            }

            return "Display " + std::to_string(fallbackIndex);
        }

        SDL_DisplayID getDisplayIdByIndex(SharpRuntime::intcs index)
        {
            int count = 0;
            SDL_DisplayID* displays = SDL_GetDisplays(&count);
            if (displays == nullptr || count <= 0)
            {
                return 0;
            }

            SDL_DisplayID result = 0;
            if (index >= 0 && index < count)
            {
                result = displays[index];
            }

            SDL_free(displays);
            return result;
        }

        bool isSupportedRenderTargetFormat(SurfaceFormat format)
        {
            return format == SurfaceFormat::Color ||
                format == SurfaceFormat::Rgba1010102 ||
                format == SurfaceFormat::Rg32 ||
                format == SurfaceFormat::Rgba64 ||
                format == SurfaceFormat::Single ||
                format == SurfaceFormat::Vector2 ||
                format == SurfaceFormat::Vector4 ||
                format == SurfaceFormat::HalfSingle ||
                format == SurfaceFormat::HalfVector2 ||
                format == SurfaceFormat::HalfVector4 ||
                format == SurfaceFormat::HdrBlendable;
        }
    }

    GraphicsAdapter& GraphicsAdapter::getDefaultAdapterProperty()
    {
        const auto& adapters = getAdaptersProperty();
        if (adapters.empty())
        {
            throw std::runtime_error("No graphics adapters are available.");
        }

        return *adapters[0];
    }

    GraphicsAdapter::GraphicsAdapter(
        SharpRuntime::intcs displayIndex,
        DisplayModeCollection modes,
        std::string name,
        std::string description,
        SharpRuntime::intcs vendorId,
        SharpRuntime::intcs deviceId
    )
        : displayIndex_(displayIndex),
          supportedDisplayModes_(std::move(modes)),
          description_(std::move(description)),
          deviceName_(std::move(name)),
          useNullDevice_(false),
          useReferenceDevice_(false),
          vendorId_(vendorId),
          deviceId_(deviceId)
    {
    }

    void GraphicsAdapter::queryPciIds(SharpRuntime::intcs& vendorId, SharpRuntime::intcs& deviceId)
    {
        vendorId = 0;
        deviceId = 0;
#ifdef __linux__
        // Try each DRM card slot in order; first readable one wins.
        for (int card = 0; card < 4; ++card)
        {
            const std::string base = "/sys/class/drm/card" + std::to_string(card) + "/device/";
            std::ifstream vf(base + "vendor");
            std::ifstream df(base + "device");
            if (!vf.is_open() || !df.is_open())
                continue;
            std::string vs, ds;
            std::getline(vf, vs);
            std::getline(df, ds);
            if (vs.empty() || ds.empty())
                continue;
            try
            {
                vendorId = static_cast<SharpRuntime::intcs>(std::stoul(vs, nullptr, 16));
                deviceId = static_cast<SharpRuntime::intcs>(std::stoul(ds, nullptr, 16));
            }
            catch (...) {}
            break;
        }
#endif
    }

    DisplayMode GraphicsAdapter::getCurrentDisplayModeProperty() const
    {
        return queryCurrentDisplayMode(displayIndex_);
    }

    const DisplayModeCollection& GraphicsAdapter::getSupportedDisplayModesProperty() const
    {
        return supportedDisplayModes_;
    }

    const std::string& GraphicsAdapter::getDescriptionProperty() const
    {
        return description_;
    }

    SharpRuntime::intcs GraphicsAdapter::getDeviceIdProperty() const
    {
        return deviceId_;
    }

    const std::string& GraphicsAdapter::getDeviceNameProperty() const
    {
        return deviceName_;
    }

    bool GraphicsAdapter::getIsDefaultAdapterProperty() const
    {
        const auto& adapters = getAdaptersProperty();
        return !adapters.empty() && adapters[0].get() == this;
    }

    bool GraphicsAdapter::getIsWideScreenProperty() const
    {
        constexpr float limit = 4.0f / 3.0f;
        return getCurrentDisplayModeProperty().getAspectRatioProperty() > limit;
    }

    GraphicsAdapter::IntPtr GraphicsAdapter::getMonitorHandleProperty() const
    {
        return static_cast<IntPtr>(getDisplayIdByIndex(displayIndex_));
    }

    SharpRuntime::intcs GraphicsAdapter::getRevisionProperty() const
    {
        return 0;
    }

    SharpRuntime::intcs GraphicsAdapter::getSubSystemIdProperty() const
    {
        return 0;
    }

    bool GraphicsAdapter::getUseNullDeviceProperty() const
    {
        return useNullDevice_;
    }

    void GraphicsAdapter::setUseNullDeviceProperty(bool value)
    {
        useNullDevice_ = value;
    }

    bool GraphicsAdapter::getUseReferenceDeviceProperty() const
    {
        return useReferenceDevice_;
    }

    void GraphicsAdapter::setUseReferenceDeviceProperty(bool value)
    {
        useReferenceDevice_ = value;
    }

    SharpRuntime::intcs GraphicsAdapter::getVendorIdProperty() const
    {
        return vendorId_;
    }

    const std::vector<std::unique_ptr<GraphicsAdapter>>& GraphicsAdapter::getAdaptersProperty()
    {
        if (adapters_.empty())
        {
            AdaptersChanged();
        }

        return adapters_;
    }

    bool GraphicsAdapter::IsProfileSupported(GraphicsProfile graphicsProfile) const
    {
        // plan_runtimerenderer.md design decision 9: an ADAPTER-level query, asked before any
        // GraphicsDevice exists, so it goes through the renderer's static descriptor hook rather
        // than an IGraphicsRenderer virtual.
        const auto& queries =
            CNA::Internal::Renderers::GraphicsRendererRegistry::Default().adapterQueries;
        if (queries.isProfileSupported != nullptr)
            return queries.isProfileSupported(static_cast<int>(graphicsProfile));

        // D9-101: the other renderers have no capability structure to consult -- an implementation
        // here would be a hardcoded table pretending to be a capability query (plan_dx9.md's own
        // "Boundaries" section explicitly refuses that), so they keep this honest.
        (void)graphicsProfile;
        return true;
    }

    bool GraphicsAdapter::QueryRenderTargetFormat(
        GraphicsProfile graphicsProfile,
        SurfaceFormat format,
        DepthFormat depthFormat,
        SharpRuntime::intcs multiSampleCount,
        SurfaceFormat& selectedFormat,
        DepthFormat& selectedDepthFormat,
        SharpRuntime::intcs& selectedMultiSampleCount
    ) const
    {
        // plan_runtimerenderer.md design decision 9: an ADAPTER-level query, so it goes through the
        // renderer's static descriptor hooks rather than an IGraphicsRenderer virtual -- there is no
        // device yet. A renderer that supplies no hook keeps exactly the behaviour it had when this
        // was the #else branch of an #ifdef.
        //
        // D9-102 (the one renderer that does supply hooks): a render-target format must be BOTH
        // valid for the requested profile (a Reach game may not request a HiDef-only format even
        // where the hardware could support it) AND actually supported by the real device -- either
        // failing falls back to Color, matching XNA's own documented fallback.
        const auto& queries =
            CNA::Internal::Renderers::GraphicsRendererRegistry::Default().adapterQueries;

        const bool supported = queries.isRenderTargetFormatSupported != nullptr
            ? queries.isRenderTargetFormatSupported(
                  static_cast<int>(graphicsProfile), static_cast<int>(format))
            : isSupportedRenderTargetFormat(format);

        selectedFormat = supported ? format : SurfaceFormat::Color;
        selectedDepthFormat = depthFormat;
        selectedMultiSampleCount = queries.clampMultiSampleCount != nullptr
            ? queries.clampMultiSampleCount(
                  static_cast<int>(selectedFormat), static_cast<int>(multiSampleCount))
            : 0;

        return format == selectedFormat &&
            depthFormat == selectedDepthFormat &&
            multiSampleCount == selectedMultiSampleCount;
    }

    bool GraphicsAdapter::QueryBackBufferFormat(
        GraphicsProfile graphicsProfile,
        SurfaceFormat format,
        DepthFormat depthFormat,
        SharpRuntime::intcs multiSampleCount,
        SurfaceFormat& selectedFormat,
        DepthFormat& selectedDepthFormat,
        SharpRuntime::intcs& selectedMultiSampleCount
    ) const
    {
        // D9-102: the back buffer (swap chain) has its own, stricter display-compatibility
        // restriction distinct from a general render-target texture (Color's own A8B8G8R8 is
        // texture-valid but not display-valid), which is why this is a separate hook from
        // isRenderTargetFormatSupported. A renderer supplying no hook keeps the framework's own
        // behaviour: the back buffer is always Color.
        const auto& queries =
            CNA::Internal::Renderers::GraphicsRendererRegistry::Default().adapterQueries;

        selectedFormat = queries.isBackBufferFormatSupported != nullptr
                && queries.isBackBufferFormatSupported(
                       static_cast<int>(graphicsProfile), static_cast<int>(format))
            ? format
            : SurfaceFormat::Color;
        selectedDepthFormat = depthFormat;
        selectedMultiSampleCount = queries.clampMultiSampleCount != nullptr
            ? queries.clampMultiSampleCount(
                  static_cast<int>(selectedFormat), static_cast<int>(multiSampleCount))
            : 0;

        return format == selectedFormat &&
            depthFormat == selectedDepthFormat &&
            multiSampleCount == selectedMultiSampleCount;
    }

    void GraphicsAdapter::AdaptersChanged()
    {
        adapters_.clear();

        SharpRuntime::intcs vendorId = 0, deviceId = 0;
        queryPciIds(vendorId, deviceId);

        int count = 0;
        SDL_DisplayID* displays = SDL_GetDisplays(&count);

        if (displays == nullptr || count <= 0)
        {
            // SDL_free is a documented no-op on nullptr; guards the (unlikely but possible) case
            // where SDL returns a non-null array with count<=0.
            SDL_free(displays);

            adapters_.push_back(std::unique_ptr<GraphicsAdapter>(
                new GraphicsAdapter(
                    0,
                    DisplayModeCollection({DisplayMode(800, 480, SurfaceFormat::Color)}),
                    "\\\\.\\DISPLAY1",
                    "Default Display",
                    vendorId, deviceId
                )
            ));
            return;
        }

        for (int i = 0; i < count; ++i)
        {
            // Matches FNA's SDL3_FNAPlatform.GetGraphicsAdapters(): DeviceName is a synthetic
            // Windows-style path (not the real display name — real XNA convention, kept even on
            // non-Windows platforms), while Description is the actual display name.
            const std::string deviceName = "\\\\.\\DISPLAY" + std::to_string(i + 1);
            const std::string description = getDisplayName(displays[i], i);
            // All displays share the same GPU — pass PCI IDs to every adapter.
            adapters_.push_back(std::unique_ptr<GraphicsAdapter>(
                new GraphicsAdapter(
                    i,
                    DisplayModeCollection(queryDisplayModes(i)),
                    deviceName,
                    description,
                    vendorId, deviceId
                )
            ));
        }

        SDL_free(displays);
    }

    const std::string& GraphicsAdapter::GetTypeName() const
    {
        static const std::string typeName = "Microsoft.Xna.Framework.Graphics.GraphicsAdapter";
        return typeName;
    }

    std::vector<DisplayMode> GraphicsAdapter::queryDisplayModes(SharpRuntime::intcs displayIndex)
    {
        std::vector<DisplayMode> result;
        const SDL_DisplayID displayId = getDisplayIdByIndex(displayIndex);

        if (displayId == 0)
        {
            result.emplace_back(800, 480, SurfaceFormat::Color);
            return result;
        }

        int count = 0;
        SDL_DisplayMode** modes = SDL_GetFullscreenDisplayModes(displayId, &count);
        if (modes != nullptr && count > 0)
        {
            // Matches FNA's SDL3_FNAPlatform.GetGraphicsAdapters(): iterate in reverse and skip
            // width/height duplicates caused by multiple refresh rates at the same resolution.
            for (int i = count - 1; i >= 0; --i)
            {
                if (modes[i] == nullptr)
                {
                    continue;
                }

                bool dupe = false;
                for (const DisplayMode& existing : result)
                {
                    if (modes[i]->w == existing.getWidthProperty() && modes[i]->h == existing.getHeightProperty())
                    {
                        dupe = true;
                        break;
                    }
                }

                if (!dupe)
                {
                    result.emplace_back(modes[i]->w, modes[i]->h, SurfaceFormat::Color);
                }
            }

            SDL_free(modes);
        }

        if (result.empty())
        {
            result.push_back(queryCurrentDisplayMode(displayIndex));
        }

        return result;
    }

    DisplayMode GraphicsAdapter::queryCurrentDisplayMode(SharpRuntime::intcs displayIndex)
    {
        const SDL_DisplayID displayId = getDisplayIdByIndex(displayIndex);
        if (displayId == 0)
        {
            return DisplayMode(800, 480, SurfaceFormat::Color);
        }

        const SDL_DisplayMode* mode = SDL_GetCurrentDisplayMode(displayId);
        if (mode == nullptr)
        {
            return DisplayMode(800, 480, SurfaceFormat::Color);
        }

        return DisplayMode(mode->w, mode->h, SurfaceFormat::Color);
    }
}
