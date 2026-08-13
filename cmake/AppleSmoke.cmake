include_guard(GLOBAL)

option(CNA_BUILD_APPLE_SMOKE_APP
    "Build the minimal UIKit/SDL_main/Game application used to validate the iOS final link"
    ${CNA_APPLE_IOS})

if(CNA_BUILD_APPLE_SMOKE_APP AND NOT CNA_APPLE_IOS)
    message(FATAL_ERROR "CNA_BUILD_APPLE_SMOKE_APP is only available for an iOS target")
endif()

if(CNA_APPLE_IOS AND CNA_BUILD_APPLE_SMOKE_APP)
    add_executable(cna_ios_smoke tests/apple/ios_smoke.cpp)
    target_link_libraries(cna_ios_smoke PRIVATE CNA SHARP_RUNTIME)

    # Call the public per-target API explicitly instead of relying on the repository-owned sweep.
    # This is the same integration an application consuming CNA through add_subdirectory() uses.
    cna_apple_configure_bundle(cna_ios_smoke)
endif()
