// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

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
         * Common widescreen modes include 16:9, 16:10, and 2:1.
         */
        [[nodiscard]] bool getIsWideScreenProperty() const;

        /** @brief Returns the native monitor handle for this adapter. */
        [[nodiscard]] IntPtr getMonitorHandleProperty() const;

        /** @brief Returns the adapter revision number. Always 0; not exposed by the platform. */
        [[nodiscard]] SharpRuntime::intcs getRevisionProperty() const;

        /** @brief Returns the subsystem identifier. Always 0; not exposed by the platform. */
        [[nodiscard]] SharpRuntime::intcs getSubSystemIdProperty() const;

        /** @brief Returns true if a null device should be used instead of hardware. */
        [[nodiscard]] bool getUseNullDeviceProperty() const;

        /**
         * @brief Sets whether a null device should be used.
         * @param value True to use a null device.
         */
        void setUseNullDeviceProperty(bool value);

        /** @brief Returns true if a reference (software) device should be used. */
        [[nodiscard]] bool getUseReferenceDeviceProperty() const;

        /**
         * @brief Sets whether a reference device should be used.
         * @param value True to use the reference device.
         */
        void setUseReferenceDeviceProperty(bool value);

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

        std::uint32_t displayId_;
        DisplayModeCollection supportedDisplayModes_;
        std::string description_;
        std::string deviceName_;
        bool useNullDevice_;
        bool useReferenceDevice_;
        SharpRuntime::intcs vendorId_;
        SharpRuntime::intcs deviceId_;

        static void queryPciIds(SharpRuntime::intcs& vendorId, SharpRuntime::intcs& deviceId);

        static std::vector<std::unique_ptr<GraphicsAdapter>> adapters_;

        [[nodiscard]] static std::vector<DisplayMode> queryDisplayModes(std::uint32_t displayId);
        [[nodiscard]] static DisplayMode queryCurrentDisplayMode(std::uint32_t displayId);
    };
}
