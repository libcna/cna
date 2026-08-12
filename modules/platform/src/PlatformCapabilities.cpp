// SPDX-License-Identifier: MS-PL

#include "CNA/Platform/PlatformCapabilities.hpp"

namespace CNA::Platform {

    bool Supports(const PlatformCapabilities& capabilities, const PlatformCapability capability)
    {
        // Exhaustive switch with no default arm: adding a PlatformCapability without a matching
        // struct field fails to compile rather than silently reading as unsupported.
        switch (capability)
        {
            case PlatformCapability::MultipleWindows: return capabilities.multipleWindows;
            case PlatformCapability::HighDpi: return capabilities.highDpi;
            case PlatformCapability::MultipleDisplays: return capabilities.multipleDisplays;
            case PlatformCapability::BorderlessFullscreen: return capabilities.borderlessFullscreen;
            case PlatformCapability::NativeWindowHandle: return capabilities.nativeWindowHandle;
            case PlatformCapability::SurfacePresentation: return capabilities.surfacePresentation;
            case PlatformCapability::OpenGlContext: return capabilities.openGlContext;
            case PlatformCapability::VulkanSurface: return capabilities.vulkanSurface;
            case PlatformCapability::Clipboard: return capabilities.clipboard;
            case PlatformCapability::TextInput: return capabilities.textInput;
            case PlatformCapability::Ime: return capabilities.ime;
            case PlatformCapability::ExactKeyboardState: return capabilities.exactKeyboardState;
            case PlatformCapability::PixelAccurateMouse: return capabilities.pixelAccurateMouse;
            case PlatformCapability::RelativeMouse: return capabilities.relativeMouse;
            case PlatformCapability::CursorShapes: return capabilities.cursorShapes;
            case PlatformCapability::GlobalPointer: return capabilities.globalPointer;
            case PlatformCapability::InputDeviceEnumeration:
                return capabilities.inputDeviceEnumeration;
            case PlatformCapability::Gamepad: return capabilities.gamepad;
            case PlatformCapability::GamepadRumble: return capabilities.gamepadRumble;
            case PlatformCapability::GamepadSensors: return capabilities.gamepadSensors;
            case PlatformCapability::Haptics: return capabilities.haptics;
            case PlatformCapability::Sensors: return capabilities.sensors;
            case PlatformCapability::PowerInfo: return capabilities.powerInfo;
            case PlatformCapability::MessageBox: return capabilities.messageBox;
            case PlatformCapability::NativeFileDialog: return capabilities.nativeFileDialog;
            case PlatformCapability::Tray: return capabilities.tray;
            case PlatformCapability::Camera: return capabilities.camera;
            case PlatformCapability::ManagedEntrypoint: return capabilities.managedEntrypoint;
        }

        return false;
    }

    const std::vector<PlatformCapability>& AllCapabilities()
    {
        static const std::vector<PlatformCapability> all = {
            PlatformCapability::MultipleWindows,
            PlatformCapability::HighDpi,
            PlatformCapability::MultipleDisplays,
            PlatformCapability::BorderlessFullscreen,
            PlatformCapability::NativeWindowHandle,
            PlatformCapability::SurfacePresentation,
            PlatformCapability::OpenGlContext,
            PlatformCapability::VulkanSurface,
            PlatformCapability::Clipboard,
            PlatformCapability::TextInput,
            PlatformCapability::Ime,
            PlatformCapability::ExactKeyboardState,
            PlatformCapability::PixelAccurateMouse,
            PlatformCapability::RelativeMouse,
            PlatformCapability::CursorShapes,
            PlatformCapability::GlobalPointer,
            PlatformCapability::InputDeviceEnumeration,
            PlatformCapability::Gamepad,
            PlatformCapability::GamepadRumble,
            PlatformCapability::GamepadSensors,
            PlatformCapability::Haptics,
            PlatformCapability::Sensors,
            PlatformCapability::PowerInfo,
            PlatformCapability::MessageBox,
            PlatformCapability::NativeFileDialog,
            PlatformCapability::Tray,
            PlatformCapability::Camera,
            PlatformCapability::ManagedEntrypoint,
        };
        return all;
    }

} // namespace CNA::Platform
