# Microsoft::Phone -- the Windows Phone application shell an XNA phone game talks to.
#
# This is the same kind of module as devices/: not XNA, but the phone-platform API that XNA
# 4.0 phone titles were written against, and without which such a title cannot be ported at
# all. It depends on runtime/ because the lifecycle transitions it reports are the ones
# Microsoft::Xna::Framework::Game already receives from the platform layer -- the phone
# service adapts them to the shape a phone game expects rather than re-deriving them.
file(GLOB CNA_PHONE_SOURCES CONFIGURE_DEPENDS "src/*.cpp")
cna_add_module(cna_phone Phone ${CNA_PHONE_SOURCES})
target_link_libraries(cna_phone PUBLIC cna_runtime cna_core_headers)
cna_link_sharp_runtime(cna_phone PUBLIC Core.Base Collections.Core Uri)

if(EMSCRIPTEN)
    # Opt-in executable, not part of the framework/default build. Exercise the
    # real browser socket shim without dragging a Game/window into this probe.
    add_executable(cna_notification_browser_probe EXCLUDE_FROM_ALL
        "${CNA_SOURCE_DIR}/tools/phone/notification_browser_test.cpp"
        src/PushNotificationSender.cpp src/HttpNotificationChannel.cpp
        src/NotificationChannelErrorEventArgs.cpp
        "${CNA_SOURCE_DIR}/modules/core/src/Logger.cpp")
    target_include_directories(cna_notification_browser_probe PRIVATE include)
    target_link_libraries(cna_notification_browser_probe PRIVATE cna_core_headers)
    cna_link_sharp_runtime(cna_notification_browser_probe PRIVATE Uri)
    target_link_options(cna_notification_browser_probe PRIVATE
        "-sENVIRONMENT=web" "-sEXIT_RUNTIME=1" "-sASSERTIONS=1")
endif()
