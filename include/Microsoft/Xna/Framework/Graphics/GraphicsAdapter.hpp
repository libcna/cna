#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DisplayMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/DisplayModeCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "System/Object.hpp"
#include "SharpRuntime/SharpRuntimeHelper.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    /// Describes a graphics adapter/display available to the system.
    class GraphicsAdapter final : public System::Object
    {
    public:
        using IntPtr = std::uintptr_t;

        /// Gets the current display mode for this adapter.
        [[nodiscard]] DisplayMode getCurrentDisplayModeProperty() const;

        /// Gets display modes supported by this adapter.
        [[nodiscard]] const DisplayModeCollection& getSupportedDisplayModesProperty() const;

        /// Gets the adapter description.
        [[nodiscard]] const std::string& getDescriptionProperty() const;

        /// Gets the device id. Not implemented for the SDL backend.
        [[nodiscard]] SharpRuntime::intcs getDeviceIdProperty() const;

        /// Gets the device/display name.
        [[nodiscard]] const std::string& getDeviceNameProperty() const;

        /// Gets whether this is the default adapter.
        [[nodiscard]] bool getIsDefaultAdapterProperty() const;

        /// Gets whether the current display mode is widescreen.
        [[nodiscard]] bool getIsWideScreenProperty() const;

        /// Gets the monitor/display handle for this adapter.
        [[nodiscard]] IntPtr getMonitorHandleProperty() const;

        /// Gets the adapter revision. Not implemented for the SDL backend.
        [[nodiscard]] SharpRuntime::intcs getRevisionProperty() const;

        /// Gets the subsystem id. Not implemented for the SDL backend.
        [[nodiscard]] SharpRuntime::intcs getSubSystemIdProperty() const;

        /// Gets whether a null device should be used.
        [[nodiscard]] bool getUseNullDeviceProperty() const;

        /// Sets whether a null device should be used.
        void setUseNullDeviceProperty(bool value);

        /// Gets whether a reference device should be used.
        [[nodiscard]] bool getUseReferenceDeviceProperty() const;

        /// Sets whether a reference device should be used.
        void setUseReferenceDeviceProperty(bool value);

        /// Gets the vendor id. Not implemented for the SDL backend.
        [[nodiscard]] SharpRuntime::intcs getVendorIdProperty() const;

        /// Gets the default adapter.
        [[nodiscard]] static GraphicsAdapter& getDefaultAdapterProperty();

        /// XNA-style static default adapter object.
        static GraphicsAdapter& DefaultAdapter;

        /// Gets the list of graphics adapters.
        [[nodiscard]] static const std::vector<std::unique_ptr<GraphicsAdapter>>& getAdaptersProperty();

        /// Returns true when the profile is supported by this adapter.
        [[nodiscard]] bool IsProfileSupported(GraphicsProfile graphicsProfile) const;

        /// Queries the render target format selected by this adapter.
        [[nodiscard]] bool QueryRenderTargetFormat(
            GraphicsProfile graphicsProfile,
            SurfaceFormat format,
            DepthFormat depthFormat,
            SharpRuntime::intcs multiSampleCount,
            SurfaceFormat& selectedFormat,
            DepthFormat& selectedDepthFormat,
            SharpRuntime::intcs& selectedMultiSampleCount
        ) const;

        /// Queries the back buffer format selected by this adapter.
        [[nodiscard]] bool QueryBackBufferFormat(
            GraphicsProfile graphicsProfile,
            SurfaceFormat format,
            DepthFormat depthFormat,
            SharpRuntime::intcs multiSampleCount,
            SurfaceFormat& selectedFormat,
            DepthFormat& selectedDepthFormat,
            SharpRuntime::intcs& selectedMultiSampleCount
        ) const;

        /// Refreshes the cached adapter list.
        static void AdaptersChanged();

        [[nodiscard]] const std::string& GetTypeName() const override;

    private:
        friend class GraphicsAdapterFactory;

        GraphicsAdapter(SharpRuntime::intcs displayIndex, DisplayModeCollection modes, std::string name,
                        std::string description,
                        SharpRuntime::intcs vendorId = 0, SharpRuntime::intcs deviceId = 0);

        SharpRuntime::intcs displayIndex_;
        DisplayModeCollection supportedDisplayModes_;
        std::string description_;
        std::string deviceName_;
        bool useNullDevice_;
        bool useReferenceDevice_;
        SharpRuntime::intcs vendorId_;
        SharpRuntime::intcs deviceId_;

        static void queryPciIds(SharpRuntime::intcs& vendorId, SharpRuntime::intcs& deviceId);

        static std::vector<std::unique_ptr<GraphicsAdapter>> adapters_;

        [[nodiscard]] static std::vector<DisplayMode> queryDisplayModes(SharpRuntime::intcs displayIndex);
        [[nodiscard]] static DisplayMode queryCurrentDisplayMode(SharpRuntime::intcs displayIndex);
    };
}
