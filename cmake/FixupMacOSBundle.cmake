# Post-build helper for CNA macOS application bundles.
#
# CMake's MACOSX_BUNDLE property creates the directory layout and Info.plist but deliberately
# does not copy linked dylibs. BundleUtilities resolves the executable's complete non-system
# dependency closure, copies it into Contents/Frameworks and rewrites Mach-O install names so the
# resulting bundle no longer depends on CNA's .sdl-prebuilt directory or a Homebrew prefix.

if(NOT DEFINED CNA_APP_BUNDLE OR CNA_APP_BUNDLE STREQUAL "")
    message(FATAL_ERROR "FixupMacOSBundle.cmake requires -DCNA_APP_BUNDLE=<path-to-.app>")
endif()
if(NOT EXISTS "${CNA_APP_BUNDLE}")
    message(FATAL_ERROR "macOS application bundle does not exist: ${CNA_APP_BUNDLE}")
endif()

include(BundleUtilities)

set(BU_CHMOD_BUNDLE_ITEMS ON)
fixup_bundle("${CNA_APP_BUNDLE}" "" "${CNA_APP_LIBRARY_DIRS}")
verify_app("${CNA_APP_BUNDLE}")
