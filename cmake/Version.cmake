# =====================================================================================
# CNA release identity -> the generated public header CNA/Version.hpp.
#
# The single source of truth is project(CNA VERSION ...) plus CNA_VERSION_PRERELEASE in the
# root CMakeLists.txt; this file only renders that decision into a header. Nothing else in the
# tree may hard-code the number -- docs/releasing.md lists the few hand-maintained copies
# (CHANGELOG.md, Doxyfile's PROJECT_NUMBER) that a release bump must update by hand.
#
# CNA_BINARY_DIR, not CMAKE_BINARY_DIR: when CNA is consumed via add_subdirectory (as
# mobile-eggbert does), CMAKE_BINARY_DIR is the *consumer's* build root, and the generated
# header belongs in this project's own.
# =====================================================================================

set(CNA_GENERATED_INCLUDE_DIR "${CNA_BINARY_DIR}/generated/include")

configure_file(
    "${CNA_SOURCE_DIR}/cmake/templates/Version.hpp.in"
    "${CNA_GENERATED_INCLUDE_DIR}/CNA/Version.hpp"
    @ONLY)
