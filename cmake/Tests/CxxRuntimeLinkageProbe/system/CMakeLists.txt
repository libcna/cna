# SPDX-License-Identifier: MS-PL
# An imported target that only this directory sees, inside CNA's tree, as the system libraries CNA
# finds are: CNA's own, which choose no runtime.
add_library(probe_system_library INTERFACE IMPORTED)
probe_program(system_library_target)
target_link_libraries(system_library_target PRIVATE probe_system_library)
