# SPDX-License-Identifier: MS-PL
# A directory whose programs all link the C++ runtime statically, defined after the interface.
add_link_options(-static-libstdc++)
probe_program(directory_option)
