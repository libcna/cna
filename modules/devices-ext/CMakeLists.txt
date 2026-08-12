# CNA-specific device extensions beyond the XNA 4.0 contract (Camera, Clipboard,
# FileDialog, MessageBox, SystemTray, DisplayInfo, PowerInfo, SystemInfo, Locale,
# UrlLauncher) -- the former CnaExt half of the devices sources. Depends on the runtime
# layer (GameWindow/display metrics), graphics (Texture2D camera frames), core and math;
# deliberately NOT on the XNA device base and never the reverse direction.
file(GLOB_RECURSE CNA_DEVICES_EXT_SOURCES CONFIGURE_DEPENDS "src/*.cpp")
cna_add_module(cna_devices_ext DevicesExt ${CNA_DEVICES_EXT_SOURCES})
target_link_libraries(cna_devices_ext PUBLIC cna_runtime cna_graphics_core cna_core cna_math)
cna_link_sharp_runtime(cna_devices_ext PUBLIC Core.Base)
# plan_platform.md PLAT-100/101/107: Clipboard, PowerInfo, Locale, SystemInfo and UrlLauncher
# reach the host through the platform contract. SDL stays for the remaining Detail/ backends
# (tray, camera, file dialog, message box, DisplayInfo), which Phase 7 migrates.
target_link_libraries(cna_devices_ext PUBLIC cna_platform)
target_link_libraries(cna_devices_ext PRIVATE SDL3::SDL3)
