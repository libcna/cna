// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <optional>
#include "CNA/GraphicsRendererType.hpp"
#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DisplayMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/DisplayModeCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "System/Object.hpp"
#include "SharpRuntime/SharpRuntimeHelper.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    /**
     * @brief The device type GraphicsAdapter's UseNullDevice / UseReferenceDevice flags select.
     *
     * CNAEXT. XNA resolves the two flags into a Direct3D 9 `D3DDEVTYPE` -- NULLREF, REF or HAL --
     * which is an API CNA does not have. These are the same three intents, named for what they mean
     * rather than for D3D9: see GraphicsAdapter::GetRequiredRendererEXT() for the renderer each one
     * maps to, and for why `Reference` is not a claim about Direct3D 9 REF.
     */
    enum class CNAEXT GraphicsDeviceTypeEXT
    {
        /** @brief Whatever renderer the build selected. Both flags clear; the default. */
        Hardware = 0,
        /** @brief Render in software rather than on the GPU. UseReferenceDevice. */
        Reference = 1,
        /** @brief Draw and present nothing. UseNullDevice, which wins over UseReferenceDevice. */
        Null = 2
    };

    /** @brief Describes a graphics adapter/display available to the system. */
    class GraphicsAdapter final : public System::Object
    {
    public:
        using IntPtr = std::uintptr_t;

        /** @brief Returns the current display mode for this adapter. */
        [[nodiscard]] DisplayMode getCurrentDisplayModeProperty() const;

        /** @brief Returns the display modes supported by this adapter. */
        [[nodiscard]] const DisplayModeCollection& getSupportedDisplayModesProperty() const;

        /** @brief Returns the adapter description string. */
        [[nodiscard]] const std::string& getDescriptionProperty() const;

        /**
         * @brief Returns the PCI device identifier of the primary GPU.
         *
         * Queried via sysfs on Linux; returns 0 on other platforms or when the query fails.
         */
        [[nodiscard]] SharpRuntime::intcs getDeviceIdProperty() const;

        /** @brief Returns the device or display name. */
        [[nodiscard]] const std::string& getDeviceNameProperty() const;

        /** @brief Returns true if this is the default adapter. */
        [[nodiscard]] bool getIsDefaultAdapterProperty() const;

        /**
         * @brief Returns true if the current display mode has a widescreen aspect ratio.
         *
         * Common widescreen modes include 16:9 and 2:1. 16:10 is **not** one of them here: XNA
         * compares strictly greater than 1.6, and 16:10 is exactly 1.6.
         */
        [[nodiscard]] bool getIsWideScreenProperty() const;

        /**
         * @brief The rule `getIsWideScreenProperty()` applies, decoupled from any display.
         *
         * CNAEXT — CNA extension, not XNA API. It exists so the threshold XNA actually uses can be
         * pinned by a test: the property itself reads whatever display the host happens to have,
         * so a regression in the constant would go unnoticed on any one machine. `BINDFIX-033`
         * moved this from FNA's `4.0f/3.0f` to XNA's `1.6f`, and the two disagree over every mode
         * between them.
         *
         * @param aspectRatio Width divided by height.
         * @return true when @p aspectRatio is strictly greater than XNA's 1.6 limit.
         */
        CNAEXT [[nodiscard]] static bool IsWideScreenAspectRatioEXT(float aspectRatio);

        /** @brief Returns the native monitor handle for this adapter. */
        [[nodiscard]] IntPtr getMonitorHandleProperty() const;

        /** @brief Returns the adapter revision number. Always 0; not exposed by the platform. */
        [[nodiscard]] SharpRuntime::intcs getRevisionProperty() const;

        /** @brief Returns the subsystem identifier. Always 0; not exposed by the platform. */
        [[nodiscard]] SharpRuntime::intcs getSubSystemIdProperty() const;

        /**
         * @brief Gets whether a null device is requested instead of hardware.
         *
         * Process-wide, as in XNA, where it is a static property rather than per-adapter state.
         * It takes precedence over UseReferenceDevice; see GetCurrentDeviceTypeEXT() for the whole
         * rule and for what each mode means in CNA.
         *
         * @return @c true if a null device is requested.
         */
        [[nodiscard]] static bool getUseNullDeviceProperty();

        /**
         * @brief Requests a null device instead of hardware.
         *
         * Takes effect for every GraphicsDevice constructed afterwards; a device already created
         * keeps the mode it was created with, as in XNA, where the device captures the type at
         * construction.
         *
         * @param value @c true to request a null device.
         */
        static void setUseNullDeviceProperty(bool value);

        /**
         * @brief Gets whether a reference (software) device is requested.
         *
         * Process-wide, as in XNA. Ignored while UseNullDevice is set.
         *
         * @return @c true if a reference device is requested.
         */
        [[nodiscard]] static bool getUseReferenceDeviceProperty();

        /**
         * @brief Requests a reference (software) device.
         *
         * Takes effect for every GraphicsDevice constructed afterwards.
         *
         * @param value @c true to request a reference device.
         */
        static void setUseReferenceDeviceProperty(bool value);

        /**
         * @brief The device type the two flags above currently select.
         *
         * CNAEXT counterpart of XNA's internal `GraphicsAdapter.CurrentDeviceType`, which is the one
         * place XNA resolves the two flags: NULL wins, then REFERENCE, otherwise hardware. Exposed
         * here because in CNA the resolution has an observable consequence -- which renderer a new
         * device requires -- and a caller that sets the flags should be able to ask what it asked
         * for rather than re-deriving the precedence.
         *
         * @return The selected device type.
         */
        CNAEXT [[nodiscard]] static GraphicsDeviceTypeEXT GetCurrentDeviceTypeEXT();

        /**
         * @brief The renderer a device created in the current device type requires, if any.
         *
         * CNAEXT. XNA's flags select a Direct3D 9 device type, which CNA has no equivalent of: the
         * renderer is chosen by `CNA_GRAPHICS_RENDERER` or `CNA::GraphicsRendererSelection`, and a
         * renderer is what actually decides how a device behaves. So each mode maps to the CNA
         * renderer whose job is the same:
         *
         * - `Null` maps to `HEADLESS`, the renderer that draws and presents nothing, which is what a
         *   D3D9 NULLREF device is for.
         * - `Reference` maps to `SOFTWARE`, CNA's own software rasteriser. **It is not Direct3D 9
         *   REF**: it is a different rasteriser with its own results, and no claim of pixel parity
         *   with REF is made or implied. What the mapping preserves is the intent -- render in
         *   software rather than on the GPU.
         * - `Hardware` maps to nothing: whatever renderer the build selected is used, unchanged,
         *   which is CNA's behaviour with both flags clear and therefore the default.
         *
         * @return The required renderer, or `std::nullopt` for `Hardware`.
         */
        CNAEXT [[nodiscard]] static std::optional<CNA::GraphicsRendererType>
        GetRequiredRendererEXT();

        /**
         * @brief Returns the PCI vendor identifier of the primary GPU.
         *
         * Queried via sysfs on Linux; returns 0 on other platforms or when the query fails.
         */
        [[nodiscard]] SharpRuntime::intcs getVendorIdProperty() const;

        /**
         * @brief Returns the default graphics adapter (adapter index 0).
         *
         * Re-evaluated on every call, matching FNA's `DefaultAdapter` property — the returned
         * reference must not be cached across a call to AdaptersChanged(), which destroys and
         * recreates every GraphicsAdapter instance.
         */
        [[nodiscard]] static GraphicsAdapter& getDefaultAdapterProperty();

        /** @brief Returns the list of all available graphics adapters. */
        [[nodiscard]] static const std::vector<std::unique_ptr<GraphicsAdapter>>& getAdaptersProperty();

        /**
         * @brief Returns true if the given graphics profile is supported by this adapter.
         * @param graphicsProfile The profile to test.
         * @return True if the profile is supported.
         */
        [[nodiscard]] bool IsProfileSupported(GraphicsProfile graphicsProfile) const;

        /**
         * @brief Queries the render target format that the adapter will select for the given inputs.
         *
         * @param graphicsProfile           The target graphics profile.
         * @param format                    The requested surface format.
         * @param depthFormat               The requested depth format.
         * @param multiSampleCount          The requested multisample count.
         * @param selectedFormat            Receives the format that was actually chosen.
         * @param selectedDepthFormat       Receives the depth format that was actually chosen.
         * @param selectedMultiSampleCount  Receives the multisample count that was actually chosen.
         * @return True if the requested format was accepted without substitution.
         */
        [[nodiscard]] bool QueryRenderTargetFormat(
            GraphicsProfile graphicsProfile,
            SurfaceFormat format,
            DepthFormat depthFormat,
            SharpRuntime::intcs multiSampleCount,
            SurfaceFormat& selectedFormat,
            DepthFormat& selectedDepthFormat,
            SharpRuntime::intcs& selectedMultiSampleCount
        ) const;

        /**
         * @brief Queries the back-buffer format that the adapter will select for the given inputs.
         *
         * @param graphicsProfile           The target graphics profile.
         * @param format                    The requested surface format.
         * @param depthFormat               The requested depth format.
         * @param multiSampleCount          The requested multisample count.
         * @param selectedFormat            Receives the format that was actually chosen.
         * @param selectedDepthFormat       Receives the depth format that was actually chosen.
         * @param selectedMultiSampleCount  Receives the multisample count that was actually chosen.
         * @return True if the requested format was accepted without substitution.
         */
        [[nodiscard]] bool QueryBackBufferFormat(
            GraphicsProfile graphicsProfile,
            SurfaceFormat format,
            DepthFormat depthFormat,
            SharpRuntime::intcs multiSampleCount,
            SurfaceFormat& selectedFormat,
            DepthFormat& selectedDepthFormat,
            SharpRuntime::intcs& selectedMultiSampleCount
        ) const;

        /** @brief Refreshes the cached list of available graphics adapters. */
        static void AdaptersChanged();

        /** @brief Returns the fully qualified .NET type name of this class. */
        CNAEXT [[nodiscard]] const std::string& GetTypeName() const override;

    private:
        GraphicsAdapter(std::uint32_t displayId, DisplayModeCollection modes, std::string name,
                        std::string description,
                        SharpRuntime::intcs vendorId = 0, SharpRuntime::intcs deviceId = 0);

        /// Mutable because it names a display inside one platform's video session, and the
        /// adapter outlives that: a Game installs its own platform, and destroying it ends the
        /// session that issued this id. The adapter is then re-bound to the same physical display
        /// in the new session, in place, because the C ABI deliberately refuses to rebuild the
        /// cache -- a live GraphicsDevice retains its adapter and replacing it would dangle.
        mutable std::uint32_t displayId_;
        DisplayModeCollection supportedDisplayModes_;
        std::string description_;
        std::string deviceName_;
        SharpRuntime::intcs vendorId_;
        SharpRuntime::intcs deviceId_;

        static void queryPciIds(SharpRuntime::intcs& vendorId, SharpRuntime::intcs& deviceId);

        static std::vector<std::unique_ptr<GraphicsAdapter>> adapters_;

        [[nodiscard]] static std::vector<DisplayMode> queryDisplayModes(std::uint32_t displayId);
        [[nodiscard]] static DisplayMode queryCurrentDisplayMode(std::uint32_t displayId);

        /// Answers this adapter's display id in the platform's current video session, rebinding
        /// it by display name when the cached one no longer names anything. Returns 0 when the
        /// display cannot be found, which is the no-display answer.
        [[nodiscard]] std::uint32_t resolveDisplayId() const;
    };
}
