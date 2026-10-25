# Symbol-level link-footprint gates (docs/link-size-architecture.md).
#
# The module probes (ModuleProbes.cmake) pin which archives a module's consumer links. These pin
# what ends up inside the executable: a 2D game links the same gamer-services, content, audio and
# platform archives as a full-featured one, and only the references inside those archives decide
# whether the avatar catalog, the glTF importer, Draco or the media player come along. Each
# consumer is statically linked from the module archives (as Mobile Eggbert links CNA, whatever
# CNA_SHARED_LIBRARY says), built but never run, and its symbol table is checked.
#
# The toolchain's own nm (CMAKE_NM) reads each probe, so a cross-compiled one is read by a tool
# that understands it, and one it cannot read fails. A cross build without CMAKE_NM registers no
# gate; a native one without it uses the host's nm and skips (exit 77) when there is none.
if(CNA_BUILD_TESTS AND CNA_ENABLE_GAMER_SERVICES AND NOT EMSCRIPTEN AND NOT ANDROID)
    find_package(Python3 COMPONENTS Interpreter QUIET)

    function(cna_add_link_footprint_test name target)
        set(_cna_nm_args)
        if(CMAKE_NM)
            set(_cna_nm_args --nm "${CMAKE_NM}")
        endif()
        add_test(NAME ${name}
            COMMAND Python3::Interpreter "${CMAKE_CURRENT_SOURCE_DIR}/scripts/check_link_footprint.py"
                --binary $<TARGET_FILE:${target}> ${_cna_nm_args} ${ARGN})
        set_tests_properties(${name} PROPERTIES
            LABELS "configuration;link-footprint" SKIP_RETURN_CODE 77)
    endfunction()

    add_executable(cna_link_footprint_2d tests/link_footprint/footprint_2d_game.cpp)
    target_link_libraries(cna_link_footprint_2d PRIVATE CNA::Runtime CNA::GamerServices)

    add_executable(cna_link_footprint_full tests/link_footprint/footprint_full_features.cpp)
    target_link_libraries(cna_link_footprint_full PRIVATE CNA::Runtime CNA::GamerServices)

    add_executable(cna_link_footprint_gamer_services
        tests/link_footprint/footprint_gamer_services_game.cpp)
    target_link_libraries(cna_link_footprint_gamer_services PRIVATE
        CNA::Runtime CNA::GamerServices)

    add_executable(cna_link_footprint_capability_query
        tests/link_footprint/footprint_capability_query.cpp)
    target_link_libraries(cna_link_footprint_capability_query PRIVATE CNA::Runtime)
    if(CNA_DEVICES)
        target_link_libraries(cna_link_footprint_capability_query PRIVATE CNA::DevicesExt)
    endif()

    # The camera and the tray are separate services: only SDL3 has both.
    if(CNA_PLATFORM STREQUAL "SDL3")
        add_executable(cna_link_footprint_camera_only tests/link_footprint/footprint_camera_only.cpp)
        target_link_libraries(cna_link_footprint_camera_only PRIVATE CNA::Runtime)
        add_executable(cna_link_footprint_tray_only tests/link_footprint/footprint_tray_only.cpp)
        target_link_libraries(cna_link_footprint_tray_only PRIVATE CNA::Runtime)
    endif()

    if(NOT Python3_Interpreter_FOUND)
        message(STATUS "CNA: no Python 3; the link-footprint gates are not registered")
    elseif(CMAKE_CROSSCOMPILING AND NOT CMAKE_NM)
        message(STATUS "CNA: no nm for this cross toolchain; the link-footprint gates are not registered")
    else()
        # Volume and cube textures, by what only their implementation defines: constructors,
        # vtables and every out-of-line member. A renderer that binds an effect's textures emits
        # the classes' header-inline renderer accessors without linking either class, so those
        # are not evidence. The full-feature gate requires each pattern, which shows that each
        # still finds a real implementation.
        set(_cna_footprint_cube_and_volume
            "Graphics::Texture3D::(?!GetRenderer\\(\\) const$)"
            "vtable for Microsoft::Xna::Framework::Graphics::Texture3D$"
            "Graphics::TextureCube::(?!GetRenderer(Raw)?\\(\\) const$)"
            "vtable for Microsoft::Xna::Framework::Graphics::TextureCube$"
            "Graphics::RenderTargetCube::(?!getIsContentLostProperty\\(\\) const$)"
            "vtable for Microsoft::Xna::Framework::Graphics::RenderTargetCube$")

        # What a game that loads textures, fonts and sounds, plays them and asks the Guide about
        # its license must not contain. Each family is one subsystem that used to ride along.
        set(_cna_footprint_absent
            # The avatar system, its built-in catalog (docs/avatars.md gives its size) and the system Guide that draws it.
            "CNA::Internal::GamerServices::Avatars::"
            "GamerServices::AvatarRenderer::"
            "GamerServices::AvatarDescription::"
            "CNA::Internal::GamerServices::installSystemGuide"
            "CNA::Internal::GamerServices::GuideUi::homeScreen"
            # The Gamer Services service client and its test twin.
            "CNA::Internal::GamerServices::backend\\(\\)"
            "CNA::Internal::GamerServices::makeFakeBackend"
            # 3D content: the Model reader, the glTF importer, cgltf and Draco.
            "Content::detail::BuiltinContent<Microsoft::Xna::Framework::Graphics::Model>"
            "CNA::Internal::GltfImport::"
            "^cgltf_parse$"
            "^draco::"
            "CNA::Internal::Xnb::RegisterModelXnbReaders"
            "Content::detail::RegisterEveryBuiltinXnbReader"
            # The registry queries that install every declared reader or loader.
            "ContentTypeReaderManager::IsRegistered"
            "CNA::Content::CnbLoaderRegistry::Find(.abi|\\()"
            # Media, capture and streaming audio.
            "Media::MediaPlayer::"
            "Media::Video::"
            "Media::Song::"
            "Audio::Microphone::"
            "Audio::DynamicSoundEffectInstance::"
            # Networking: the Guide and profiles need no network session or ENet.
            "Microsoft::Xna::Framework::Net::"
            "^enet_"
            # The platform's camera and tray, and the accessors that create and link them.
            "CNA::Platform::Sdl3::Sdl3CameraProvider::"
            "CNA::Platform::Sdl3::Sdl3Tray::"
            "CNA::Platform::IPlatform::GetCamera\\(\\)"
            "CNA::Platform::IPlatform::GetTray\\(\\)"
            # The Decimal/DateTime .xnb readers, which no font or plain value holds.
            "CNA::Internal::Xnb::RegisterDecimalDateTimeXnbReaders"
            # The .cnb writer, which only content tools need.
            "CNA::Content::Cnb::CnbWriter::"
            # Volume and cube textures, which a 2D game binds nowhere.
            ${_cna_footprint_cube_and_volume})
        # Platforms other than the build's own.
        foreach(_platform IN ITEMS "SDL3:Sdl3" "HEADLESS:Headless" "TERMINAL:Terminal")
            string(REPLACE ":" ";" _platform "${_platform}")
            list(GET _platform 0 _platform_option)
            list(GET _platform 1 _platform_namespace)
            if(NOT CNA_PLATFORM STREQUAL _platform_option)
                list(APPEND _cna_footprint_absent "CNA::Platform::${_platform_namespace}::")
            endif()
        endforeach()
        set(_cna_footprint_args)
        foreach(_pattern IN LISTS _cna_footprint_absent)
            list(APPEND _cna_footprint_args --forbid "${_pattern}")
        endforeach()
        cna_add_link_footprint_test(LinkFootprint_2DGame cna_link_footprint_2d
                ${_cna_footprint_args}
                --require "GamerServices::Guide::getIsTrialModeProperty"
                --require "GamerServices::Guide::ShowMarketplace"
                --require "Audio::SoundEffect::Play"
                --require "Content::detail::BuiltinContent<Microsoft::Xna::Framework::Graphics::Texture2D>::Register"
                --require "Content::detail::BuiltinContent<Microsoft::Xna::Framework::Graphics::SpriteFont>::Register")

        # ...and that the subsystems are all there for a game that uses them.
        set(_cna_footprint_present
            "CNA::Internal::GltfImport::"
            "Content::detail::BuiltinContent<Microsoft::Xna::Framework::Graphics::Model>::Register"
            "CNA::Internal::GamerServices::Avatars::embeddedCatalogFiles"
            "GamerServices::AvatarRenderer::Draw"
            "CNA::Internal::GamerServices::installSystemGuide"
            "CNA::Internal::GamerServices::GuideUi::homeScreen"
            "Media::MediaPlayer::Update"
            "Audio::Microphone::CheckAllBuffers"
            "Audio::DynamicSoundEffectInstance::Update"
            "Graphics::RenderTargetCube::DescribeRenderTargetBindingEXT"
            ${_cna_footprint_cube_and_volume})
        if(CNA_DRACO_AVAILABLE)
            list(APPEND _cna_footprint_present "^draco::")
        endif()
        set(_cna_footprint_args)
        foreach(_pattern IN LISTS _cna_footprint_present)
            list(APPEND _cna_footprint_args --require "${_pattern}")
        endforeach()
        cna_add_link_footprint_test(LinkFootprint_FullFeatures cna_link_footprint_full
            ${_cna_footprint_args})
        # A game with a GamerServicesComponent: initialized gamer services give its players the
        # system Guide (Home or the Guide button), whose panels show profiles and avatars, so the
        # Guide, the avatar renderer and editor and the built-in catalog are this game's own. The
        # gate pins that, and that nothing else rides along.
        set(_cna_footprint_args)
        foreach(_pattern IN ITEMS
                "CNA::Internal::GltfImport::(?!Cgltf)"
                "^draco::"
                "Content::detail::BuiltinContent<Microsoft::Xna::Framework::Graphics::Model>"
                "CNA::Internal::Xnb::RegisterModelXnbReaders"
                "Content::detail::RegisterEveryBuiltinXnbReader"
                "Media::MediaPlayer::"
                "Media::Video::"
                "Media::Song::"
                "Audio::Microphone::"
                "Audio::DynamicSoundEffectInstance::"
                "Microsoft::Xna::Framework::Net::"
                "^enet_"
                "CNA::Internal::GamerServices::makeFakeBackend"
                ${_cna_footprint_cube_and_volume}
                "CNA::Platform::Sdl3::Sdl3CameraProvider::"
                "CNA::Platform::Sdl3::Sdl3Tray::"
                "CNA::Platform::IPlatform::GetCamera\\(\\)"
                "CNA::Platform::IPlatform::GetTray\\(\\)"
                "CNA::Content::Cnb::CnbWriter::")
            list(APPEND _cna_footprint_args --forbid "${_pattern}")
        endforeach()
        cna_add_link_footprint_test(LinkFootprint_GamerServicesGame
            cna_link_footprint_gamer_services
                ${_cna_footprint_args}
                --require "CNA::Internal::GamerServices::installSystemGuide"
                --require "CNA::Internal::GamerServices::GuideUi::homeScreen"
                --require "GamerServices::AvatarRenderer::"
                --require "CNA::Internal::GamerServices::Avatars::embeddedCatalogFiles"
                --require "GamerServices::Guide::getIsTrialModeProperty")
        # A program that asks whether a camera and a tray are supported and uses neither: support
        # is reported without the services, which only their accessors link.
        set(_cna_footprint_args
            --forbid "CNA::Platform::Sdl3::Sdl3CameraProvider::"
            --forbid "CNA::Platform::Sdl3::Sdl3Tray::"
            --forbid "CNA::Platform::IPlatform::GetCamera\\(\\)"
            --forbid "CNA::Platform::IPlatform::GetTray\\(\\)"
            --require "CNA::Platform::IPlatform::Supports|Platform::.*Platform::Supports")
        if(CNA_PLATFORM STREQUAL "SDL3")
            list(APPEND _cna_footprint_args
                --require "CNA::Platform::Sdl3::CameraSupported"
                --require "CNA::Platform::Sdl3::TraySupported")
        endif()
        if(CNA_DEVICES)
            list(APPEND _cna_footprint_args
                --require "CNA::Devices::Camera::getIsSupportedProperty"
                --require "CNA::Devices::SystemTray::getIsSupportedProperty")
        endif()
        cna_add_link_footprint_test(LinkFootprint_CapabilityQuery
            cna_link_footprint_capability_query ${_cna_footprint_args})
        # A program that uses one of the two services links that one alone.
        if(CNA_PLATFORM STREQUAL "SDL3")
            cna_add_link_footprint_test(LinkFootprint_CameraOnly cna_link_footprint_camera_only
                    --require "CNA::Platform::Sdl3::Sdl3CameraProvider::"
                    --forbid "CNA::Platform::Sdl3::Sdl3Tray::"
                    --forbid "CNA::Platform::IPlatform::GetTray\\(\\)")
            cna_add_link_footprint_test(LinkFootprint_TrayOnly cna_link_footprint_tray_only
                    --require "CNA::Platform::Sdl3::Sdl3Tray::"
                    --forbid "CNA::Platform::Sdl3::Sdl3CameraProvider::"
                    --forbid "CNA::Platform::IPlatform::GetCamera\\(\\)")
        endif()
        unset(_cna_footprint_cube_and_volume)
        unset(_cna_footprint_absent)
        unset(_cna_footprint_present)
        unset(_cna_footprint_args)
        unset(_pattern)
        unset(_platform)
        unset(_platform_option)
        unset(_platform_namespace)
    endif()
endif()
