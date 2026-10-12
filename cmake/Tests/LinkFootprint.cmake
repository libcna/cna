# Symbol-level link-footprint gates (docs/link-size-architecture.md).
#
# The module probes (ModuleProbes.cmake) pin which archives a module's consumer links. These pin
# what ends up inside the executable: a 2D game links the same gamer-services, content, audio and
# platform archives as a full-featured one, and only the references inside those archives decide
# whether the avatar catalog, the glTF importer, Draco or the media player come along. Each
# consumer is statically linked from the module archives (as Mobile Eggbert links CNA, whatever
# CNA_SHARED_LIBRARY says), built but never run, and its symbol table is checked.
#
# Native only: the probes inspect host executables with nm (the script skips when it has none).
if(CNA_BUILD_TESTS AND CNA_ENABLE_GAMER_SERVICES AND NOT EMSCRIPTEN AND NOT ANDROID)
    find_package(Python3 COMPONENTS Interpreter QUIET)

    add_executable(cna_link_footprint_2d tests/link_footprint/footprint_2d_game.cpp)
    target_link_libraries(cna_link_footprint_2d PRIVATE CNA::Runtime CNA::GamerServices)

    add_executable(cna_link_footprint_full tests/link_footprint/footprint_full_features.cpp)
    target_link_libraries(cna_link_footprint_full PRIVATE CNA::Runtime CNA::GamerServices)

    add_executable(cna_link_footprint_gamer_services
        tests/link_footprint/footprint_gamer_services_game.cpp)
    target_link_libraries(cna_link_footprint_gamer_services PRIVATE
        CNA::Runtime CNA::GamerServices)

    if(Python3_Interpreter_FOUND)
        # What a game that loads textures, fonts and sounds, plays them and asks the Guide about
        # its license must not contain. Each family is one subsystem that used to ride along.
        set(_cna_footprint_absent
            # The avatar system, its 30 MB built-in catalog and the system Guide that draws it.
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
            # The platform's camera and tray, installed only by CNA::Devices::Camera/SystemTray.
            "CNA::Platform::Sdl3::Sdl3CameraProvider::"
            "CNA::Platform::Sdl3::Sdl3Tray::"
            # The Decimal/DateTime .xnb readers, which no font or plain value holds.
            "CNA::Internal::Xnb::RegisterDecimalDateTimeXnbReaders"
            # The .cnb writer, which only content tools need.
            "CNA::Content::Cnb::CnbWriter::"
            # Volume and cube textures, which a 2D game binds nowhere.
            "Graphics::Texture3D::"
            "Graphics::TextureCube::"
            "Graphics::RenderTargetCube::")
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
        add_test(NAME LinkFootprint_2DGame
            COMMAND Python3::Interpreter "${CMAKE_CURRENT_SOURCE_DIR}/scripts/check_link_footprint.py"
                --binary $<TARGET_FILE:cna_link_footprint_2d>
                ${_cna_footprint_args}
                --require "GamerServices::Guide::getIsTrialModeProperty"
                --require "GamerServices::Guide::ShowMarketplace"
                --require "Audio::SoundEffect::Play"
                --require "Content::detail::BuiltinContent<Microsoft::Xna::Framework::Graphics::Texture2D>::Register"
                --require "Content::detail::BuiltinContent<Microsoft::Xna::Framework::Graphics::SpriteFont>::Register")
        set_tests_properties(LinkFootprint_2DGame PROPERTIES LABELS "configuration;link-footprint")

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
            "Graphics::TextureCube::"
            "Graphics::Texture3D::")
        if(CNA_DRACO_AVAILABLE)
            list(APPEND _cna_footprint_present "^draco::")
        endif()
        set(_cna_footprint_args)
        foreach(_pattern IN LISTS _cna_footprint_present)
            list(APPEND _cna_footprint_args --require "${_pattern}")
        endforeach()
        add_test(NAME LinkFootprint_FullFeatures
            COMMAND Python3::Interpreter "${CMAKE_CURRENT_SOURCE_DIR}/scripts/check_link_footprint.py"
                --binary $<TARGET_FILE:cna_link_footprint_full>
                ${_cna_footprint_args})
        set_tests_properties(LinkFootprint_FullFeatures PROPERTIES LABELS "configuration;link-footprint")
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
                "Graphics::Texture3D::"
                "Graphics::TextureCube::"
                "Graphics::RenderTargetCube::"
                "CNA::Platform::Sdl3::Sdl3CameraProvider::"
                "CNA::Platform::Sdl3::Sdl3Tray::"
                "CNA::Content::Cnb::CnbWriter::")
            list(APPEND _cna_footprint_args --forbid "${_pattern}")
        endforeach()
        add_test(NAME LinkFootprint_GamerServicesGame
            COMMAND Python3::Interpreter "${CMAKE_CURRENT_SOURCE_DIR}/scripts/check_link_footprint.py"
                --binary $<TARGET_FILE:cna_link_footprint_gamer_services>
                ${_cna_footprint_args}
                --require "CNA::Internal::GamerServices::installSystemGuide"
                --require "CNA::Internal::GamerServices::GuideUi::homeScreen"
                --require "GamerServices::AvatarRenderer::"
                --require "CNA::Internal::GamerServices::Avatars::embeddedCatalogFiles"
                --require "GamerServices::Guide::getIsTrialModeProperty")
        set_tests_properties(LinkFootprint_GamerServicesGame PROPERTIES
            LABELS "configuration;link-footprint")
        unset(_cna_footprint_absent)
        unset(_cna_footprint_present)
        unset(_cna_footprint_args)
        unset(_pattern)
        unset(_platform)
        unset(_platform_option)
        unset(_platform_namespace)
    endif()
endif()
