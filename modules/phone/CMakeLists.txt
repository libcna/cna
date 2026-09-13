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
