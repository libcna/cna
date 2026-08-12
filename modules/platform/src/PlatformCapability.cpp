// SPDX-License-Identifier: MS-PL

#include "CNA/Platform/PlatformCapability.hpp"

namespace CNA::Platform {

    const std::string& ToString(const PlatformCapability capability)
    {
        static const std::string multipleWindows = "MultipleWindows";
        static const std::string highDpi = "HighDpi";
        static const std::string multipleDisplays = "MultipleDisplays";
        static const std::string borderlessFullscreen = "BorderlessFullscreen";
        static const std::string nativeWindowHandle = "NativeWindowHandle";
        static const std::string surfacePresentation = "SurfacePresentation";
        static const std::string openGlContext = "OpenGlContext";
        static const std::string vulkanSurface = "VulkanSurface";
        static const std::string clipboard = "Clipboard";
        static const std::string textInput = "TextInput";
        static const std::string ime = "Ime";
        static const std::string exactKeyboardState = "ExactKeyboardState";
        static const std::string pixelAccurateMouse = "PixelAccurateMouse";
        static const std::string relativeMouse = "RelativeMouse";
        static const std::string cursorShapes = "CursorShapes";
        static const std::string globalPointer = "GlobalPointer";
        static const std::string inputDeviceEnumeration = "InputDeviceEnumeration";
        static const std::string gamepad = "Gamepad";
        static const std::string gamepadRumble = "GamepadRumble";
        static const std::string gamepadSensors = "GamepadSensors";
        static const std::string haptics = "Haptics";
        static const std::string sensors = "Sensors";
        static const std::string powerInfo = "PowerInfo";
        static const std::string messageBox = "MessageBox";
        static const std::string nativeFileDialog = "NativeFileDialog";
        static const std::string tray = "Tray";
        static const std::string camera = "Camera";
        static const std::string managedEntrypoint = "ManagedEntrypoint";

        // Exhaustive switch with no default arm: a new PlatformCapability without a name here
        // is a compiler diagnostic, not a silent fallback string.
        switch (capability)
        {
            case PlatformCapability::MultipleWindows: return multipleWindows;
            case PlatformCapability::HighDpi: return highDpi;
            case PlatformCapability::MultipleDisplays: return multipleDisplays;
            case PlatformCapability::BorderlessFullscreen: return borderlessFullscreen;
            case PlatformCapability::NativeWindowHandle: return nativeWindowHandle;
            case PlatformCapability::SurfacePresentation: return surfacePresentation;
            case PlatformCapability::OpenGlContext: return openGlContext;
            case PlatformCapability::VulkanSurface: return vulkanSurface;
            case PlatformCapability::Clipboard: return clipboard;
            case PlatformCapability::TextInput: return textInput;
            case PlatformCapability::Ime: return ime;
            case PlatformCapability::ExactKeyboardState: return exactKeyboardState;
            case PlatformCapability::PixelAccurateMouse: return pixelAccurateMouse;
            case PlatformCapability::RelativeMouse: return relativeMouse;
            case PlatformCapability::CursorShapes: return cursorShapes;
            case PlatformCapability::GlobalPointer: return globalPointer;
            case PlatformCapability::InputDeviceEnumeration: return inputDeviceEnumeration;
            case PlatformCapability::Gamepad: return gamepad;
            case PlatformCapability::GamepadRumble: return gamepadRumble;
            case PlatformCapability::GamepadSensors: return gamepadSensors;
            case PlatformCapability::Haptics: return haptics;
            case PlatformCapability::Sensors: return sensors;
            case PlatformCapability::PowerInfo: return powerInfo;
            case PlatformCapability::MessageBox: return messageBox;
            case PlatformCapability::NativeFileDialog: return nativeFileDialog;
            case PlatformCapability::Tray: return tray;
            case PlatformCapability::Camera: return camera;
            case PlatformCapability::ManagedEntrypoint: return managedEntrypoint;
        }

        return multipleWindows;
    }

} // namespace CNA::Platform
