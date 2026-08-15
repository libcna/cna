// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Graphics/GraphicsAdapter.hpp"

#include "CNA/Platform/CurrentPlatform.hpp"
#include "CNA/Platform/IPlatformSystemServices.hpp"

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
#ifdef CNA_RENDERER_DIRECTX9
#include "CNA/Internal/Renderers/DirectX9/D3D9FormatMapping.hpp"
#include "CNA/Internal/Renderers/DirectX9/D3D9ProfileCapabilities.hpp"
#endif

namespace Microsoft::Xna::Framework::Graphics
{
    std::vector<std::unique_ptr<GraphicsAdapter>> GraphicsAdapter::adapters_;

    namespace
    {
        std::string getDisplayName(
            const CNA::Platform::DisplayInfo& display, const std::size_t fallbackIndex)
        {
            if (!display.name.empty())
            {
                return display.name;
            }

            return "Display " + std::to_string(fallbackIndex);
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
        const std::uint32_t displayId,
        DisplayModeCollection modes,
        std::string name,
        std::string description,
        SharpRuntime::intcs vendorId,
        SharpRuntime::intcs deviceId
    )
        : displayId_(displayId),
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
        return queryCurrentDisplayMode(displayId_);
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
        return static_cast<IntPtr>(displayId_);
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
#ifdef CNA_RENDERER_DIRECTX9
        using namespace CNA::Internal::Renderers::DirectX9;

        // D9-32's own construction-time finding, re-used here: Reach has no floor worth checking
        // -- every real D3D9 HAL device already exceeds vs_2_0/ps_2_0 and every other Reach
        // minimum. Only HiDef's floor is ever hardware-dependent (D9-100's own table).
        if (graphicsProfile == GraphicsProfile::Reach)
            return true;

        return MeetsHiDefFloorEXT(QueryAdapterCapsEXT());
#else
        // D9-101: the other 9 CNA renderers have no D3DCAPS9 to consult -- an implementation here
        // would be a hardcoded table pretending to be a capability query (plan_dx9.md's own
        // "Boundaries" section explicitly refuses that), so they keep this honest.
        (void)graphicsProfile;
        return true;
#endif
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
#ifdef CNA_RENDERER_DIRECTX9
        using namespace CNA::Internal::Renderers::DirectX9;

        // D9-102: a render-target format must be BOTH valid for the requested profile (D9-100's
        // own whitelist -- a Reach game may not request a HiDef-only format even if the
        // underlying hardware could technically support it) AND actually supported by the real
        // device (IDirect3D9::CheckDeviceFormat) -- either failing falls back to Color, matching
        // XNA's own documented fallback and this method's pre-D3D9 stub behavior.
        const bool profileValid = IsValidTextureFormatForProfileEXT(
            static_cast<int>(graphicsProfile), static_cast<int>(format));
        const D3DFORMAT requestedD3DFormat = SurfaceFormatToD3D9(static_cast<int>(format));
        const bool hardwareValid = requestedD3DFormat != D3DFMT_UNKNOWN &&
            IsRenderTargetFormatSupportedByHardwareEXT(requestedD3DFormat);

        selectedFormat = (profileValid && hardwareValid) ? format : SurfaceFormat::Color;
        selectedDepthFormat = depthFormat;

        const D3DFORMAT selectedD3DFormat = SurfaceFormatToD3D9(static_cast<int>(selectedFormat));
        selectedMultiSampleCount = ClampMultiSampleCountForFormatEXT(
            selectedD3DFormat, static_cast<int>(multiSampleCount));
#else
        (void)graphicsProfile;

        selectedFormat = isSupportedRenderTargetFormat(format) ? format : SurfaceFormat::Color;
        selectedDepthFormat = depthFormat;
        selectedMultiSampleCount = 0;
#endif

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
#ifdef CNA_RENDERER_DIRECTX9
        using namespace CNA::Internal::Renderers::DirectX9;

        // D9-102: the back buffer (swap chain) has its own, stricter display-compatibility
        // restriction distinct from a general render-target texture (D9-30's own finding, e.g.
        // Color's own A8B8G8R8 is texture-valid but not display-valid) -- probed via
        // IDirect3D9::CheckDeviceType, not CheckDeviceFormat. Still gated on the profile
        // whitelist first, same as QueryRenderTargetFormat.
        const bool profileValid = IsValidTextureFormatForProfileEXT(
            static_cast<int>(graphicsProfile), static_cast<int>(format));
        const D3DFORMAT requestedD3DFormat = SurfaceFormatToD3D9(static_cast<int>(format));
        const bool hardwareValid = requestedD3DFormat != D3DFMT_UNKNOWN &&
            IsBackBufferFormatSupportedByHardwareEXT(requestedD3DFormat);

        selectedFormat = (profileValid && hardwareValid) ? format : SurfaceFormat::Color;
        selectedDepthFormat = depthFormat;

        const D3DFORMAT selectedD3DFormat = SurfaceFormatToD3D9(static_cast<int>(selectedFormat));
        selectedMultiSampleCount = ClampMultiSampleCountForFormatEXT(
            selectedD3DFormat, static_cast<int>(multiSampleCount));
#else
        (void)graphicsProfile;

        selectedFormat = SurfaceFormat::Color;
        selectedDepthFormat = depthFormat;
        selectedMultiSampleCount = 0;
#endif

        return format == selectedFormat &&
            depthFormat == selectedDepthFormat &&
            multiSampleCount == selectedMultiSampleCount;
    }

    void GraphicsAdapter::AdaptersChanged()
    {
        adapters_.clear();

        SharpRuntime::intcs vendorId = 0, deviceId = 0;
        queryPciIds(vendorId, deviceId);

        CNA::Platform::IPlatformDisplays* displayService =
            CNA::Platform::GetCurrentPlatform().GetDisplays();
        const std::vector<CNA::Platform::DisplayInfo> displays =
            displayService != nullptr
                ? displayService->GetDisplays()
                : std::vector<CNA::Platform::DisplayInfo>{};

        if (displays.empty())
        {
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

        for (std::size_t i = 0; i < displays.size(); ++i)
        {
            // Matches FNA's platform implementation: DeviceName is a synthetic
            // Windows-style path (not the real display name — real XNA convention, kept even on
            // non-Windows platforms), while Description is the actual display name.
            const std::string deviceName = "\\\\.\\DISPLAY" + std::to_string(i + 1);
            const std::string description = getDisplayName(displays[i], i);
            // All displays share the same GPU — pass PCI IDs to every adapter.
            adapters_.push_back(std::unique_ptr<GraphicsAdapter>(
                new GraphicsAdapter(
                    displays[i].id,
                    DisplayModeCollection(queryDisplayModes(displays[i].id)),
                    deviceName,
                    description,
                    vendorId, deviceId
                )
            ));
        }
    }

    const std::string& GraphicsAdapter::GetTypeName() const
    {
        static const std::string typeName = "Microsoft.Xna.Framework.Graphics.GraphicsAdapter";
        return typeName;
    }

    std::vector<DisplayMode> GraphicsAdapter::queryDisplayModes(const std::uint32_t displayId)
    {
        std::vector<DisplayMode> result;
        CNA::Platform::IPlatformDisplays* displays =
            CNA::Platform::GetCurrentPlatform().GetDisplays();
        if (displays == nullptr || displayId == 0)
        {
            result.emplace_back(800, 480, SurfaceFormat::Color);
            return result;
        }

        const std::vector<CNA::Platform::DisplayMode> modes = displays->GetDisplayModes(displayId);
        if (!modes.empty())
        {
            // Matches FNA's platform implementation: iterate in reverse and skip
            // width/height duplicates caused by multiple refresh rates at the same resolution.
            for (auto mode = modes.rbegin(); mode != modes.rend(); ++mode)
            {
                bool dupe = false;
                for (const DisplayMode& existing : result)
                {
                    if (mode->width == existing.getWidthProperty() &&
                        mode->height == existing.getHeightProperty())
                    {
                        dupe = true;
                        break;
                    }
                }

                if (!dupe)
                {
                    result.emplace_back(mode->width, mode->height, SurfaceFormat::Color);
                }
            }
        }

        if (result.empty())
        {
            result.push_back(queryCurrentDisplayMode(displayId));
        }

        return result;
    }

    DisplayMode GraphicsAdapter::queryCurrentDisplayMode(const std::uint32_t displayId)
    {
        CNA::Platform::IPlatformDisplays* displays =
            CNA::Platform::GetCurrentPlatform().GetDisplays();
        if (displays == nullptr || displayId == 0)
        {
            return DisplayMode(800, 480, SurfaceFormat::Color);
        }

        CNA::Platform::DisplayMode mode;
        if (!displays->TryGetCurrentDisplayMode(displayId, mode) ||
            mode.width <= 0 || mode.height <= 0)
        {
            return DisplayMode(800, 480, SurfaceFormat::Color);
        }

        return DisplayMode(mode.width, mode.height, SurfaceFormat::Color);
    }
}
