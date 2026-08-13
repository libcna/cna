#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL
"""
PLAT-2 - classify every SDL identifier CNA references into a platform-contract area.

Purpose
-------
PLAT-1 counts the coupling; this decides what each piece of it *becomes*. Every SDL identifier is
assigned to exactly one contract area, and each area names the interface that will own it and the
task that migrates it. The result is the input to Phase 1's interface shape: an interface exists
because identifiers were classified into it, not because cnaplatform.md sketched it.

The classification is rule-based and ordered -- first match wins -- because SDL's naming is
overlapping rather than hierarchical. `SDL_GetWindowProperties` is native-handle extraction, not
window management; `SDL_GL_GetProcAddress` is GL interop, not a window flag; `SDL_GPUTexture` is
SDL_GPU-renderer-specific, not graphics-general. Ordering those rules correctly is the actual work
here, so the rule table below is the deliverable, not the CSV it emits.

Completeness is mechanical, not claimed: the script exits non-zero if any identifier falls through
to no rule. An identifier that genuinely needs no contract must be matched by an explicit
`none`-area rule saying why, which forces the "does CNA need this?" question to be answered once
per symbol rather than skipped.

Areas
-----
Sixteen areas carry symbols. PLAT-2's row in plan_platform.md lists the contract areas;
`lifecycle`, `error` and `logging` are split out from the plan's original wording because each
maps to a distinct owner (PLAT-29, PLAT-21, PLAT-53) and folding them into a neighbour would have
hidden that. A seventeenth, `dynamic-library`, ended up empty -- see PLAT-45.

Usage
-----
    python3 tools/platform/sdl_classify.py                     # summary by area
    python3 tools/platform/sdl_classify.py --format csv        # full per-symbol CSV
    python3 tools/platform/sdl_classify.py --out docs/platform-sdl-classification.csv
    python3 tools/platform/sdl_classify.py --area input        # drill into one area
    python3 tools/platform/sdl_classify.py --check             # fail if anything is unclassified
"""

from __future__ import annotations

import argparse
import csv
import io
import re
import sys
from collections import Counter
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from sdl_inventory import scan  # noqa: E402  (path is set up immediately above)

# --- areas ------------------------------------------------------------------------------------
#
# area -> (owning interface / destination, migrating task, one-line rationale)

AREAS: dict[str, tuple[str, str, str]] = {
    "lifecycle": (
        "IPlatform::Initialize/Shutdown",
        "PLAT-29",
        "Subsystem init/quit and refcounting; ordering is audited by PLAT-4 and already has tests.",
    ),
    "window": (
        "IPlatformWindow",
        "PLAT-18/PLAT-30/PLAT-31",
        "Window creation, geometry, state and flags.",
    ),
    "event": (
        "PlatformEvent + IPlatform::PollEvents",
        "PLAT-17/PLAT-33..38",
        "Event pump and event payloads; batched, never per-event dispatch.",
    ),
    "input": (
        "CNA/Platform/Input/*",
        "PLAT-24/PLAT-77..90",
        "Keyboard, mouse, gamepad, joystick, haptic, sensor, touch, text input, cursors.",
    ),
    "audio": (
        "CNA::Audio::Platform::IAudioDevice",
        "PLAT-91..99",
        "Separate contract from IPlatform, so OpenAL/WASAPI need no window implementation.",
    ),
    "timing": (
        "IPlatform timing",
        "PLAT-39",
        "Performance counter, ticks, delay; the fixed-timestep loop's only platform dependency.",
    ),
    "filesystem": (
        "IPlatformFileSystem",
        "PLAT-44",
        "Base/pref/user paths, directory ops, file loading, IO streams.",
    ),
    "display": (
        "IPlatformDisplays",
        "PLAT-43",
        "Display enumeration, bounds, modes, content scale.",
    ),
    "system-service": (
        "IPlatformDialogs/Clipboard/Tray/Camera/Power/Locale/SystemInfo/UrlLauncher",
        "PLAT-25/PLAT-100..107",
        "Independently capability-gated OS services.",
    ),
    "dynamic-library": (
        "(none -- cut, see PLAT-45)",
        "PLAT-45",
        "Empty: CNA never loads objects through SDL. GDI/GLIDE use dlopen/LoadLibrary directly.",
    ),
    "native-handle": (
        "NativeWindowHandle",
        "PLAT-13/PLAT-14/PLAT-32",
        "The handle handed to renderers; the 7 window properties define its minimum surface.",
    ),
    "gl-vulkan-interop": (
        "IPlatformGlContext / IPlatformVulkanSurface",
        "PLAT-22/PLAT-23/PLAT-41/PLAT-42",
        "GL context and Vulkan surface creation; frees 11 renderer families as a group.",
    ),
    "sdl-renderer-specific": (
        "allowlisted renderers (SDL_RENDERER, SDL_GPU, FNA3D)",
        "PLAT-3/PLAT-76",
        "Legitimately SDL3-bound: these renderers *are* an SDL3 API. Never on the common interface.",
    ),
    "pixel-format": (
        "CNA graphics types",
        "PLAT-64/PLAT-65",
        "Surface/pixel-format/blend vocabulary; becomes CNA's own enums, not a platform service.",
    ),
    "error": (
        "PlatformException",
        "PLAT-21",
        "Error reporting; the implementation's message text is surfaced without exposing SDL.",
    ),
    "logging": (
        "CNA Logger",
        "PLAT-53",
        "CNA owns its log sink. Logging is deliberately NOT a platform capability.",
    ),
    "none": (
        "-",
        "-",
        "No contract needed: allocator/string shims, SDL-internal names, or CNA-side spellings.",
    ),
}

# --- ordered classification rules ---------------------------------------------------------------
#
# (regex, area, note). First match wins. The comments record why an ordering is what it is;
# reordering these rules changes the classification, so they are not cosmetic.

RULES: list[tuple[str, str, str]] = [
    # SDL_GPU is a whole renderer API. It must precede every generic rule below, or its texture,
    # buffer, shader and pass names would scatter into pixel-format/native-handle/etc.
    (r"^SDL_GPU", "sdl-renderer-specific", "SDL_GPU renderer API"),
    (r"^SDL_(Acquire|Submit|Cancel|Wait|Push|Bind|Draw|Dispatch|Begin|End|Map|Unmap|Blit|Generate|Verify|Insert)GPU", "sdl-renderer-specific", "SDL_GPU command/pass API"),
    (r"^SDL_(Create|Release|Destroy|Set|Get|Query|Claim|Upload|Download|Copy)GPU", "sdl-renderer-specific", "SDL_GPU resource API"),
    (r"^SDL_GPU_", "sdl-renderer-specific", "SDL_GPU constant"),
    # Catch-all for the GPU API: SDL's naming puts "GPU" in the middle as often as at the front
    # (ClaimWindowForGPUDevice, UploadToGPUTexture, WaitAndAcquireGPUSwapchainTexture,
    # GenerateMipmapsForGPUTexture). Anchoring on the infix is what makes this area complete.
    (r"^SDL_\w*GPU", "sdl-renderer-specific", "SDL_GPU API"),

    # SDL_Renderer (the 2D renderer). Also before the generic window/texture rules.
    (r"^SDL_Render[A-Z]", "sdl-renderer-specific", "SDL_Renderer draw call"),
    (r"^SDL_(RENDERER|LOGICAL_PRESENTATION|TEXTUREACCESS|SOFTWARE_RENDERER)", "sdl-renderer-specific", "SDL_Renderer constant"),
    (r"^SDL_(Create|Destroy|Get|Set|Flush|Update|Lock|Unlock|Convert|Add)?Renderer", "sdl-renderer-specific", "SDL_Renderer object/API"),
    (r"^SDL_(Create|Destroy|Update|Lock|Unlock|Set|Get)?Texture", "sdl-renderer-specific", "SDL_Renderer texture"),
    (r"^SDL_(Set|Get)Render", "sdl-renderer-specific", "SDL_Renderer state"),
    (r"^SDL_(CreateTextureFrom|CreateSoftwareRenderer|GetNumRenderDrivers|ConvertEventToRenderCoordinates|RenderCoordinatesFrom|RenderCoordinatesTo)", "sdl-renderer-specific", "SDL_Renderer surface API"),

    # Native handle extraction must precede the generic window rule: SDL_GetWindowProperties is
    # how a renderer reaches HWND/X11/Wayland, not window management.
    (r"^SDL_PROP_WINDOW_", "native-handle", "native window property"),
    (r"^SDL_PROP_", "native-handle", "SDL property key"),
    # SDL's property API interleaves the object noun and the value type into the verb
    # (GetWindowProperties, GetPointerProperty, SetNumberProperty), so match the "Propert" stem
    # rather than enumerating the combinations. This must stay ahead of the window rules:
    # SDL_GetWindowProperties is how a renderer reaches HWND/X11/Wayland, not window management.
    (r"^SDL_(Get|Set|Has|Clear|Lock|Unlock|Create|Destroy|Copy|Enumerate)\w*Propert", "native-handle", "property access"),
    (r"^SDL_PropertiesID$", "native-handle", "property set handle"),
    (r"^SDL_PropertyType$", "native-handle", "property type"),

    # GL / Vulkan / Metal interop, before window flags (SDL_WINDOW_OPENGL is creation intent).
    (r"^SDL_GL_", "gl-vulkan-interop", "GL context"),
    (r"^SDL_(GLContext|GLattr|GLprofile)", "gl-vulkan-interop", "GL context type"),
    (r"^SDL_Vulkan_", "gl-vulkan-interop", "Vulkan surface/extensions"),
    (r"^SDL_Metal_", "gl-vulkan-interop", "Metal view"),
    (r"^SDL_MetalView$", "gl-vulkan-interop", "Metal view handle"),
    (r"^SDL_(EGL|GLAD)", "gl-vulkan-interop", "EGL/GL loader interop"),

    # Events. SDL_EVENT_* covers the whole event taxonomy including input events; the event
    # *payload* is a platform-event concern, while input *state* below is a separate service.
    (r"^SDL_EVENT_", "event", "event type constant"),
    (r"^SDL_(Poll|Pump|Push|Peep|Wait|Flush|Has|Add|Remove|Filter|Set|Get)Event", "event", "event pump"),
    (r"^SDL_(Event|CommonEvent|DisplayEvent|WindowEvent|KeyboardEvent|KeyboardDeviceEvent|TextEditingEvent|TextEditingCandidatesEvent|TextInputEvent|MouseMotionEvent|MouseButtonEvent|MouseWheelEvent|MouseDeviceEvent|JoyAxisEvent|JoyBallEvent|JoyHatEvent|JoyButtonEvent|JoyDeviceEvent|JoyBatteryEvent|GamepadAxisEvent|GamepadButtonEvent|GamepadDeviceEvent|GamepadTouchpadEvent|GamepadSensorEvent|AudioDeviceEvent|CameraDeviceEvent|SensorEvent|QuitEvent|UserEvent|TouchFingerEvent|PenProximityEvent|PenMotionEvent|PenTouchEvent|PenButtonEvent|PenAxisEvent|DropEvent|ClipboardEvent|RenderEvent|EventFilter|EventAction|EventType|EventEntry)$", "event", "event payload type"),

    # Application lifecycle / subsystem init.
    (r"^SDL_INIT_", "lifecycle", "init flag"),
    (r"^SDL_(Init|Quit|InitSubSystem|QuitSubSystem|WasInit|InitFlags|SetAppMetadata|GetAppMetadata|RunApp|EnterAppMainCallbacks|SetMainReady|RegisterApp|UnregisterApp|AppResult|AppEvent_func|AppInit_func|AppIterate_func|AppQuit_func|APP_)", "lifecycle", "subsystem lifecycle"),
    (r"^SDL_(main|MAIN_|Main)", "lifecycle", "entrypoint"),
    # Main-thread affinity is a genuine platform contract, not a threading utility: SDL requires
    # window and event calls on the thread that initialised video, and any implementation will.
    (r"^SDL_(IsMainThread|RunOnMainThread|RunMainThreadCallbacks|VideoThreadID)", "lifecycle", "main-thread affinity"),
    (r"^SDL_VIDEODRIVER$", "lifecycle", "video driver selection env var; becomes platform configuration"),

    # Input state. Ordered after events so that e.g. SDL_EVENT_GAMEPAD_ADDED lands in `event`.
    (r"^SDL_SCANCODE_", "input", "scancode"),
    (r"^SDL_(K_|KMOD_|Keycode|Scancode|Keymod)", "input", "keyboard mapping"),
    (r"^SDL_(Keyboard|GetKey|SetKey|GetScancode|GetDefaultKeyboard|ResetKeyboard|HasKeyboard|GetKeyboards|GetModState|SetModState|StartTextInput|StopTextInput|TextInput|SetTextInputArea|GetTextInputArea|ClearComposition|ScreenKeyboard|HasScreenKeyboard)", "input", "keyboard/text input"),
    (r"^SDL_TEXTINPUT", "input", "text input constant"),
    (r"^SDL_(Mouse|GetMouse|SetMouse|WarpMouse|CaptureMouse|GetRelativeMouse|SetRelativeMouse|GetGlobalMouse|HasMouse|GetMice|GetDefaultCursor|CreateCursor|CreateColorCursor|CreateSystemCursor|SetCursor|GetCursor|DestroyCursor|ShowCursor|HideCursor|CursorVisible|Cursor$|SystemCursor)", "input", "mouse/cursor"),
    (r"^SDL_BUTTON", "input", "mouse button"),
    (r"^SDL_MOUSEWHEEL_", "input", "mouse wheel direction"),
    (r"^SDL_(Gamepad|GAMEPAD_|GetGamepad|OpenGamepad|CloseGamepad|IsGamepad|AddGamepadMapping|ReloadGamepadMappings|GetNumGamepad|RumbleGamepad|SendGamepadEffect|SetGamepadLED|SetGamepadSensor|SetGamepadPlayerIndex)", "input", "gamepad"),
    (r"^SDL_(Joystick|JOYSTICK_|GetJoystick|OpenJoystick|CloseJoystick|LockJoysticks|UnlockJoysticks|RumbleJoystick|SendJoystickEffect|SetJoystickLED|SetJoystickPlayerIndex|AttachVirtualJoystick|DetachVirtualJoystick|IsJoystickVirtual|SetJoystickVirtual|UpdateJoysticks|HasJoystick|GetJoysticks|VirtualJoystick)", "input", "joystick"),
    (r"^SDL_HAT_", "input", "joystick hat"),
    (r"^SDL_(Haptic|HAPTIC_|OpenHaptic|CloseHaptic|GetHaptic|IsHaptic|InitHapticRumble|PlayHapticRumble|StopHapticRumble|RunHapticEffect|StopHapticEffect|CreateHapticEffect|UpdateHapticEffect|DestroyHapticEffect|PauseHaptic|ResumeHaptic|SetHaptic|GetNumHaptic)", "input", "haptics"),
    (r"^SDL_(Sensor|SENSOR_|OpenSensor|CloseSensor|GetSensor|UpdateSensors|StandardGravity)", "input", "sensors"),
    (r"^SDL_(Touch|Finger|GetTouch|FINGER_)", "input", "touch"),
    (r"^SDL_(Pen|PEN_)", "input", "pen input"),
    (r"^SDL_(SYSTEM_CURSOR_|STANDARD_GRAVITY)", "input", "cursor/sensor constant"),
    # Infix fallbacks: SDL puts the device noun mid-name as often as at the front
    # (GetNumJoystickAxes, IsMouseHaptic, GetMaxHapticEffects, SendSensorUpdate). These sit after
    # the event rules on purpose, so SDL_EVENT_GAMEPAD_ADDED stays an event, not input state.
    (r"^SDL_\w*(Haptic|Joystick|Gamepad|Sensor|Cursor|Keyboard|Keymap)", "input", "input device API"),
    (r"^SDL_(GetPowerInfo|PowerState|POWERSTATE_)", "system-service", "power state"),

    # Audio.
    (r"^SDL_(Audio|AUDIO_|OpenAudio|CloseAudio|PauseAudio|ResumeAudio|BindAudio|UnbindAudio|GetAudio|SetAudio|CreateAudioStream|DestroyAudioStream|PutAudioStream|GetAudioStream|ClearAudioStream|FlushAudioStream|LockAudioStream|UnlockAudioStream|MixAudio|ConvertAudioSamples|LoadWAV|IsAudioDevicePlaying|IsAudioDevicePaused|GetNumAudioDrivers|GetAudioDriver|GetCurrentAudioDriver)", "audio", "audio device/stream"),
    (r"^SDL_MIX_MAXVOLUME$", "audio", "mixer constant"),
    (r"^SDL_(IsSupportedChannelCount|IsSupportedSampleRate)", "audio", "audio format query"),
    (r"^SDL_AUDIODRIVER$", "audio", "audio driver selection env var; becomes audio configuration"),

    # Timing.
    (r"^SDL_(GetTicks|GetPerformance|Delay|DelayNS|DelayPrecise|AddTimer|RemoveTimer|TimerID|TimerCallback|NS_PER_|US_PER_|MS_PER_|SECONDS_TO_NS|NS_TO_SECONDS)", "timing", "timing"),

    # Filesystem and IO.
    (r"^SDL_IO", "filesystem", "IO stream"),
    (r"^SDL_\w*IO(Stream|Status|Whence|Properties|Size)?$", "filesystem", "IO stream API"),
    (r"^SDL_(Get|Close|Read|Write|Seek|Tell|Flush)IO", "filesystem", "IO stream API"),
    (r"^SDL_(LoadFile|SaveFile|GetBasePath|GetPrefPath|GetUserFolder|GetCurrentDirectory|CreateDirectory|EnumerateDirectory|RemovePath|RenamePath|CopyFile|GetPathInfo|GlobDirectory|PathType|PathInfo|FOLDER_|Folder|GlobFlags)", "filesystem", "filesystem"),
    (r"^SDL_(Storage|OpenTitleStorage|OpenUserStorage|OpenFileStorage|CloseStorage|StorageReady|GetStorage|ReadStorage|WriteStorage|CopyStorage|RemoveStorage|RenameStorage|CreateStorageDirectory|EnumerateStorageDirectory)", "filesystem", "storage abstraction"),

    # Displays.
    (r"^SDL_(Display|GetDisplay|GetDisplays|GetPrimaryDisplay|GetCurrentDisplay|GetDesktopDisplay|GetClosestFullscreen|GetFullscreenDisplayModes|GetNumVideoDrivers|GetVideoDriver|GetCurrentVideoDriver|ScreenSaver|EnableScreenSaver|DisableScreenSaver|SystemTheme|SYSTEM_THEME|ORIENTATION_)", "display", "display/video"),

    # Windows. After native-handle, GL and renderer rules.
    (r"^SDL_WINDOW_", "window", "window flag"),
    (r"^SDL_(Window|CreateWindow|DestroyWindow|CreatePopupWindow|GetWindow|SetWindow|ShowWindow|HideWindow|RaiseWindow|MaximizeWindow|MinimizeWindow|RestoreWindow|SyncWindow|FlashWindow|WarpMouseInWindow|UpdateWindowSurface|GetWindows|ScreenKeyboardShown|FlashOperation|WINDOWPOS_)", "window", "window management"),

    # System services.
    (r"^SDL_(Clipboard|GetClipboard|SetClipboard|HasClipboard|ClearClipboard|GetPrimarySelection|SetPrimarySelection|HasPrimarySelection)", "system-service", "clipboard"),
    (r"^SDL_(MessageBox|MESSAGEBOX_|ShowMessageBox|ShowSimpleMessageBox)", "system-service", "message box"),
    (r"^SDL_(ShowOpenFileDialog|ShowSaveFileDialog|ShowOpenFolderDialog|ShowFileDialogWithProperties|DialogFile|FileDialogType|FILEDIALOG_)", "system-service", "file dialog"),
    (r"^SDL_(Tray|TRAY|CreateTray|DestroyTray|GetTray|SetTray|InsertTray|RemoveTray|ClickTray|UpdateTrays)", "system-service", "tray"),
    (r"^SDL_(\w*Camera|CAMERA_)", "system-service", "camera"),
    (r"^SDL_(GetPreferredLocales|Locale)", "system-service", "locale"),
    (r"^SDL_(OpenURL)", "system-service", "URL launcher"),
    (r"^SDL_(GetSystemRAM|GetNumLogicalCPUCores|GetCPUCacheLineSize|HasAVX|HasSSE|HasMMX|HasNEON|HasAltiVec|HasARMSIMD|HasLSX|HasLASX|GetPlatform|IsTablet|IsTV|GetSandbox|Sandbox|SystemCursor_)", "system-service", "system info"),
    (r"^SDL_(SetX11EventHook|SetWindowsMessageHook|SetiOSAnimationCallback|SetiOSEventPump|OnApplicationDid|OnApplicationWill|IsDeXMode|IsChromebook|SendAndroidMessage|SendAndroidBackButton|GetAndroid|RequestAndroidPermission|ShowAndroidToast|GetDirect3D9AdapterIndex|GetGDKTaskQueue|GetSandboxEnvironment)", "system-service", "platform-specific system hook"),
    (r"^SDL_(GetHint|SetHint|ResetHint|AddHintCallback|DelHintCallback|HINT_|HintPriority|HINT)", "system-service", "SDL hint (becomes CNA configuration)"),

    # Dynamic library loading. PLAT-2 found CNA never calls SDL_LoadObject/LoadFunction/
    # UnloadObject -- the only match in the whole tree is SDL_FunctionPointer, and that is a doc
    # comment quoting SDL_GL_GetProcAddress's signature, so it belongs to GL interop. The two
    # renderers that do load libraries at run time (GDI, GLIDE) call dlopen/LoadLibrary directly.
    # The rules are kept so a future SDL_LoadObject call site is classified rather than falling
    # through, but the area is empty today and IPlatformDynamicLibrary was cut on that evidence.
    (r"^SDL_(LoadObject|LoadFunction|UnloadObject|SharedObject)", "dynamic-library", "dynamic library"),
    (r"^SDL_FunctionPointer$", "gl-vulkan-interop", "SDL_GL_GetProcAddress return type"),

    # Pixel formats, surfaces and blending: CNA graphics vocabulary, not a platform service.
    (r"^SDL_(PIXELFORMAT_|PixelFormat|PIXELTYPE|PACKEDORDER|ARRAYORDER|BITMAPORDER|PACKEDLAYOUT|Colorspace|COLORSPACE|COLOR_TYPE|COLOR_RANGE|COLOR_PRIMARIES|TRANSFER_CHARACTERISTICS|MATRIX_COEFFICIENTS|CHROMA_LOCATION|DEFINE_PIXELFORMAT|ISPIXELFORMAT|BYTESPERPIXEL|BITSPERPIXEL)", "pixel-format", "pixel format"),
    (r"^SDL_(Surface|CreateSurface|DestroySurface|DuplicateSurface|ConvertSurface|ScaleSurface|BlitSurface|FillSurfaceRect|LockSurface|UnlockSurface|SetSurface|GetSurface|MapSurfaceRGB|PremultiplySurface|FlipSurface|LoadBMP|SaveBMP|SURFACE_|ScaleMode|SCALEMODE_|FlipMode|FLIP_)", "pixel-format", "surface"),
    (r"^SDL_(BLENDMODE_|BLENDFACTOR_|BLENDOPERATION_|BlendMode|BlendFactor|BlendOperation|ComposeCustomBlendMode)", "pixel-format", "blend mode"),
    (r"^SDL_(Color|FColor|Palette|CreatePalette|DestroyPalette|SetPaletteColors|MapRGB|MapRGBA|GetRGB|GetRGBA|GetMasksForPixelFormat|GetPixelFormatForMasks|GetPixelFormatDetails|GetPixelFormatName|ALPHA_)", "pixel-format", "color"),
    (r"^SDL_(Rect|FRect|Point|FPoint|HasRectIntersection|GetRectIntersection|GetRectUnion|GetRectEnclosingPoints|GetRectAndLineIntersection|PointIn|RectsEqual|RectEmpty|RectToFRect)", "pixel-format", "geometry vocabulary"),
    (r"^SDL_(Vertex|GetTextureSize)", "pixel-format", "2D vertex vocabulary"),

    # Error reporting.
    (r"^SDL_(GetError|SetError|ClearError|OutOfMemory|Unsupported|InvalidParamError|SetErrorV)", "error", "error reporting"),

    # Logging.
    (r"^SDL_(Log|LOG_|SetLogPriorit|GetLogPriorit|ResetLogPriorit|LogPriority|LogCategory|LogOutputFunction|SetLogOutputFunction|GetLogOutputFunction|LogMessage)", "logging", "logging"),

    # SDL header basenames, seen as include paths (`#include <SDL3/SDL_gpu.h>`). The include is
    # itself the coupling, and it disappears when the subsystem it names is migrated, so each is
    # classified to that subsystem rather than dismissed as "not an API call".
    (r"^SDL_(gpu|render)$", "sdl-renderer-specific", "SDL header include"),
    (r"^SDL_(audio|audiocvt|sysaudio|wave|mixer)$", "audio", "SDL header include"),
    (r"^SDL_(opengl|opengl_glext|opengles|opengles2|vulkan|metal)$", "gl-vulkan-interop", "SDL header include"),
    (r"^SDL_(video|cocoavideo)$", "window", "SDL header include"),
    (r"^SDL_(haptic|syshaptic|joystick|mouse|keyboard|sensor|androidsensor|windowssensor|touch|gamepad)$", "input", "SDL header include"),
    (r"^SDL_events$", "event", "SDL header include"),
    (r"^SDL_(clipboard|dialog|messagebox|misc|tray|camera|locale|cpuinfo|power|filesystem|hints)$", "system-service", "SDL header include"),
    (r"^SDL_init$", "lifecycle", "SDL header include"),
    (r"^SDL_log$", "logging", "SDL header include"),
    (r"^SDL_(image|surface|pixels|blendmode|rect)$", "pixel-format", "SDL header include"),
    (r"^SDL_(stdinc|sysmutex|mutex|thread|atomic|assert|error|timer|properties|guid|endian|platform|version|main|h|h_|SDL)$", "none", "SDL header include with no CNA contract"),

    # Genuinely no contract needed.
    (r"^SDL_(aligned_alloc|aligned_free|SetObjectValid|ObjectType|GetObjectType)", "none", "SDL-internal allocator/object bookkeeping"),
    (r"^SDL_PLATFORM_", "none", "SDL OS-detection macro; CNA uses its own build-system detection"),
    # Bare verbs are wildcard fragments in doc comments -- `SDL_Show*Dialog()`, `SDL_Open*`,
    # `SDL_ScreenSaverEnabled()/SDL_Enable/` -- not API names. They are an artefact of the wide
    # textual counting rule, and are recorded as such rather than quietly dropped from the scan.
    (r"^SDL_(Enable|Open|Show|Close|Create|Destroy|Get|Set)$", "none", "wildcard fragment in a doc comment, not an API name"),
    (r"^SDL_(malloc|calloc|realloc|free|memcpy|memset|memmove|memcmp|strlen|strcmp|strncmp|strcasecmp|strncasecmp|strdup|strlcpy|strlcat|strchr|strstr|snprintf|vsnprintf|asprintf|atoi|atof|itoa|strtol|strtoul|strtod|isspace|isdigit|isalpha|toupper|tolower|abs|min|max|clamp|pow|sqrt|sin|cos|floor|ceil|round|trunc|fabs|swap|zero|SIZE_MAX|MAX_|MIN_|PI_|INLINE|FORCE_INLINE|COMPILE_TIME_ASSERT|arraysize|stack_alloc|stack_free|const_cast|reinterpret_cast|static_cast|FUNCTION|FILE|LINE)", "none", "allocator/string/math shim"),
    (r"^SDL_(bool|TRUE|FALSE|NULL|Time|GetCurrentTime|TimeToDateTime|DateTimeToTime|DateFormat|TimeFormat|GetDateTimeLocalePreferences|DAYS_|SECONDS_PER)", "none", "primitive/date vocabulary"),
    (r"^SDL_(Thread|CreateThread|WaitThread|DetachThread|GetThread|Mutex|CreateMutex|DestroyMutex|LockMutex|UnlockMutex|TryLockMutex|Condition|CreateCondition|DestroyCondition|SignalCondition|BroadcastCondition|WaitCondition|Semaphore|CreateSemaphore|DestroySemaphore|WaitSemaphore|SignalSemaphore|AtomicInt|AtomicU32|SetAtomic|GetAtomic|CompareAndSwapAtomic|AddAtomic|MemoryBarrier|RWLock|CreateRWLock|DestroyRWLock|LockRWLock|UnlockRWLock|TryLockRWLock|InitState|ShouldInit|ShouldQuit|SetInitialized|TLSID|GetTLS|SetTLS|CleanupTLS|ThreadPriority|ThreadState|ThreadID|GetCurrentThreadID)", "none", "threading: C++ std facilities are used instead"),
    (r"^SDL_(Environment|getenv|setenv|unsetenv|GetEnvironment|CreateEnvironment|DestroyEnvironment|GetEnvironmentVariable|SetEnvironmentVariable|UnsetEnvironmentVariable|GetEnvironmentVariables)", "none", "environment: C++/CNA config path is used instead"),
    (r"^SDL_(VERSION|Version|GetVersion|GetRevision|MAJOR_VERSION|MINOR_VERSION|MICRO_VERSION|PATCHLEVEL|VERSIONNUM|COMPILEDVERSION|VERSION_ATLEAST|assert|Assert|ASSERT|TriggerBreakpoint|AssertionHandler|AssertData|AssertState|ReportAssertion|GetAssertion|ResetAssertion|SetAssertionHandler)", "none", "version/assert macros"),
    (r"^SDL_(BeginGPU|h$|H$|guid|GUID|StringToGUID|GUIDToString|CRC|murmur|MostSignificantBitIndex|HasExactly|BITS_PER|FLT_EPSILON|PRI|Swap16|Swap32|Swap64|SwapFloat|BYTEORDER|LIL_ENDIAN|BIG_ENDIAN|FLOATWORDORDER)", "none", "GUID/endian/CRC helper"),
]

COMPILED: list[tuple[re.Pattern[str], str, str]] = [(re.compile(p), area, note) for p, area, note in RULES]


def classify_symbol(symbol: str) -> tuple[str, str] | None:
    for pattern, area, note in COMPILED:
        if pattern.match(symbol):
            return area, note
    return None


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--format", choices=("summary", "csv"), default="summary")
    parser.add_argument("--area", help="restrict output to one area")
    parser.add_argument("--out", type=Path, help="write CSV to this path instead of stdout")
    parser.add_argument("--check", action="store_true", help="exit non-zero if any symbol is unclassified")
    args = parser.parse_args(argv)

    repo_root: Path = args.repo.resolve()
    inv = scan(repo_root)
    frequencies = inv.symbol_frequencies()

    file_counts: Counter = Counter()
    for record in inv.files:
        file_counts.update(record.symbols.keys())

    rows: list[dict[str, object]] = []
    unclassified: list[str] = []

    for symbol in sorted(frequencies):
        result = classify_symbol(symbol)
        if result is None:
            unclassified.append(symbol)
            continue
        area, note = result
        owner, task, _ = AREAS[area]
        rows.append(
            {
                "symbol": symbol,
                "area": area,
                "owning_interface": owner,
                "task": task,
                "occurrences": frequencies[symbol],
                "files": file_counts[symbol],
                "note": note,
            }
        )

    if unclassified:
        print(
            f"error: {len(unclassified)} SDL identifier(s) match no rule. PLAT-2 requires every\n"
            f"identifier to be assigned deliberately -- add a rule (including an explicit `none`\n"
            f"rule saying why no contract is needed) in tools/platform/sdl_classify.py:\n",
            file=sys.stderr,
        )
        for symbol in unclassified[:60]:
            print(f"  {symbol}  ({frequencies[symbol]} occurrences)", file=sys.stderr)
        if len(unclassified) > 60:
            print(f"  … and {len(unclassified) - 60} more", file=sys.stderr)
        return 1

    if args.check:
        print(f"all {len(rows)} SDL identifiers are classified into {len({r['area'] for r in rows})} areas.")
        return 0

    if args.area:
        rows = [r for r in rows if r["area"] == args.area]
        if not rows:
            print(f"error: no symbols in area {args.area!r}; known areas: {', '.join(sorted(AREAS))}", file=sys.stderr)
            return 1

    if args.format == "csv" or args.out:
        buffer = io.StringIO()
        writer = csv.DictWriter(
            buffer,
            fieldnames=["symbol", "area", "owning_interface", "task", "occurrences", "files", "note"],
            lineterminator="\n",
        )
        writer.writeheader()
        writer.writerows(sorted(rows, key=lambda r: (r["area"], -int(r["occurrences"]), r["symbol"])))
        if args.out:
            out_path = args.out if args.out.is_absolute() else (Path.cwd() / args.out)
            out_path.parent.mkdir(parents=True, exist_ok=True)
            out_path.write_text(buffer.getvalue(), encoding="utf-8")
            try:
                shown = out_path.resolve().relative_to(repo_root)
            except ValueError:  # an --out outside the repo is legitimate, just not repo-relative
                shown = out_path
            print(f"wrote {len(rows)} classified identifiers to {shown}")
        else:
            sys.stdout.write(buffer.getvalue())
        return 0

    # summary
    by_area: Counter = Counter()
    occurrences_by_area: Counter = Counter()
    for row in rows:
        by_area[row["area"]] += 1
        occurrences_by_area[row["area"]] += int(row["occurrences"])

    print(f"{len(rows)} SDL identifiers classified into {len(by_area)} areas\n")
    print(f"| {'Area':<22} | {'Symbols':>7} | {'Refs':>6} | Owning interface / destination | Task |")
    print(f"|{'-' * 24}|{'-' * 9}|{'-' * 8}|---|---|")
    for area, count in sorted(by_area.items(), key=lambda kv: (-kv[1], kv[0])):
        owner, task, _ = AREAS[area]
        print(f"| `{area}`{' ' * max(0, 20 - len(area))} | {count:>7} | {occurrences_by_area[area]:>6} | {owner} | {task} |")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
