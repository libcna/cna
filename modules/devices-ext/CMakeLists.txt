# CNA-specific device extensions beyond the XNA 4.0 contract (Camera, Clipboard,
# FileDialog, MessageBox, SystemTray, DisplayInfo, PowerInfo, SystemInfo, Locale,
# UrlLauncher) -- the former CnaExt half of the devices sources. Depends on the runtime
# layer (GameWindow/display metrics), graphics (Texture2D camera frames), core and math;
# deliberately NOT on the XNA device base and never the reverse direction.
file(GLOB_RECURSE CNA_DEVICES_EXT_SOURCES CONFIGURE_DEPENDS "src/*.cpp")
cna_add_module(cna_devices_ext DevicesExt ${CNA_DEVICES_EXT_SOURCES})
target_link_libraries(cna_devices_ext PUBLIC cna_runtime cna_graphics_core cna_core cna_math)
cna_link_sharp_runtime(cna_devices_ext PUBLIC Core.Base)
# Every native system surface reaches the host through the platform contract. This module has no
# native-library link of its own; the selected platform implementation owns one when it needs it.
target_link_libraries(cna_devices_ext PUBLIC cna_platform)
