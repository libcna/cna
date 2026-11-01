# Application entry points

This is the implemented PLAT-54 boundary, reconciled with source on 2026-11-01. The original
PLAT-5 findings and migration proposal remain in Git history. Entry-point adaptation is a
preprocessor/build concern: it happens before any IPlatform object or runtime renderer selection.

## Header and target rules

Include `CNA/Platform/Entrypoint.hpp` in the translation unit defining main. The header belongs
to modules/platform and keys off CNA_PLATFORM_SDL3 plus the target OS, not a renderer macro.

| Target with SDL3 platform | Current behavior |
|---|---|
| Android | Includes SDL3/SDL_main.h so main becomes SDL_main, the exported symbol found by SDLActivity's native launcher. |
| iOS | Includes the same header; SDL/UIKit owns process startup and invokes the renamed application entry point. |
| Windows desktop | Deliberately leaves main alone. GUI-subsystem targets use the CMake helper below. |
| Other desktop/browser targets | No SDL main include through this CNA header; target-specific startup/link policy still applies. |
| HEADLESS or TERMINAL platform | Header contributes no SDL entry-point handling. |

CNA::BuildConfig exports the platform definition to consumers; an add_subdirectory consumer needs
that transitive definition as well as the platform module's include directory. On Android/iOS,
cna_platform publicly links SDL3, including SDL3main if that target exists, because this header
needs SDL includes in the consumer's own translation unit. Desktop keeps the platform's SDL edge
private. The public mobile exception does not make SDL part of every platform contract header.

The native and Android demo_devices Main.cpp files now include this CNA header. The former
CNA/Entrypoint.hpp location and dead CNA_RENDERER_SDL branch are gone. Other application sources
may use their own startup arrangement; the presence of these consumers is not proof that every
demo/mobile package was run.

## Windows GUI executables

`cna_windows_gui_executable(target)` in cmake/WindowsGuiExecutable.cmake sets
WIN32_EXECUTABLE on Windows. Under MSVC it also passes /ENTRY:mainCRTStartup, allowing the
application's main with the GUI subsystem. MinGW's CRT handles that combination without the
extra option. Elsewhere the helper is a no-op.

This is a deliberate CNA build choice, not a statement that SDL_main.h is universally inert on
Windows: SDL's header can supply optional Windows entry-point handling when an application
chooses to include it directly. Do not infer native-MSVC linkage from a Linux or MinGW build.

See [Windows/Visual Studio](windows-visual-studio.md) for the native build and evidence boundary,
and [Apple platforms](apple-platforms.md) for the iOS bundle/symbol smoke contract.

## SDL3main and verification

SDL3's main support is header based; a separately exposed SDL3::SDL3main target is optional.
CNA guards links with TARGET checks. They occur in the platform's mobile integration and multiple
example/tool manifests, not solely cna_reference_dump. The reference dump remains a manually
invoked non-Android/non-Emscripten developer tool.

Source/header/CMake and the pinned SDL3 header confirm these branches. Apple CI checks the
SDL_main symbol and runs its bundle smoke under its documented constraints; source/workflow
presence is not a new successful Apple or Android run. The 2026-11-01 audit did not execute a
native Windows build or repackage a mobile application. No PLAT-54 implementation remains open
merely because the historical audit once listed it as proposed work.
