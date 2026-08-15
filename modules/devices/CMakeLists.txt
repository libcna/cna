# XNA-compatible device base: Microsoft::Devices sensors + VibrateController. The
# CNA-specific device extensions live in modules/devices-ext (dependency direction:
# devices-ext -> devices base surface, never the reverse).
file(GLOB_RECURSE CNA_DEVICES_SOURCES CONFIGURE_DEPENDS "src/*.cpp")
cna_add_module(cna_devices Devices ${CNA_DEVICES_SOURCES})
target_link_libraries(cna_devices PUBLIC cna_runtime cna_graphics_core cna_core cna_math)
cna_link_sharp_runtime(cna_devices PUBLIC Core.Base)

if(ANDROID)
    # Detail::AndroidSensorBridge (Microsoft::Devices::Sensors) calls the NDK's
    # ASensorManager_*/ASensorEventQueue_*/ALooper_* API directly (no JNI) --
    # those symbols live in libandroid.so, not libc/libc++. PUBLIC so any
    # executable linking the devices module (e.g. cna_demo_devices) picks up the
    # transitive dependency automatically (plan_devices.md Task DEVICES-0121).
    #
    # Task ANDR2-006 (2026-07-17): the same file's debug-only
    # __android_log_print() diagnostic (disable/destroy failure reporting)
    # needs liblog.so -- a separate NDK system library from libandroid.so,
    # not pulled in transitively by it.
    target_link_libraries(cna_devices PUBLIC android log)
endif()

# Module-owned examples (module-examples campaign): demo/example registration is
# module-local; the conditions historically applied by cmake/Examples.cmake live
# inside examples/CMakeLists.txt unchanged.
add_subdirectory(examples)
