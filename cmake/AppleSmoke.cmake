include_guard(GLOBAL)

option(CNA_BUILD_APPLE_SMOKE_APP
    "Build the minimal SDL_main/Game application used to validate an Apple final link"
    ${CNA_APPLE_IOS})

if(CNA_BUILD_APPLE_SMOKE_APP AND NOT CNA_APPLE)
    message(FATAL_ERROR "CNA_BUILD_APPLE_SMOKE_APP is only available for an Apple target")
endif()

if(CNA_APPLE AND CNA_BUILD_APPLE_SMOKE_APP)
    if(CNA_APPLE_IOS)
        set(_cna_apple_smoke_target cna_ios_smoke)
    else()
        set(_cna_apple_smoke_target cna_macos_smoke)
    endif()

    add_executable(${_cna_apple_smoke_target} tests/apple/apple_smoke.cpp)
    target_link_libraries(${_cna_apple_smoke_target} PRIVATE CNA SHARP_RUNTIME)

    # Call the public per-target API explicitly instead of relying on the repository-owned sweep.
    # This is the same integration an application consuming CNA through add_subdirectory() uses.
    cna_apple_configure_bundle(${_cna_apple_smoke_target})
endif()
