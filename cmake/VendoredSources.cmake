# SPDX-License-Identifier: MS-PL
#
# The revisions of the vendored SDL submodules, as cmake/VendoredSourceCheck.cmake verifies them
# before SDL is built. Update this file in the commit that moves a gitlink (git ls-files -s
# third_party/<dir>); a configure from a plain-directory copy refuses a gitlink it has no record
# for. For a commit <c> of the submodule's repository:
#
#   COMMIT    <c>
#   TREE      git rev-parse <c>^{tree}
#   GITLINKS  git ls-tree -r <c> | awk '$1 == "160000" { print $4 " " $3 }'
#             (nested submodules: their directories are left out of the content comparison and
#             their recorded commits put back, so the computed tree id is the real one)

set(CNA_VENDORED_SOURCE_SDL_COMMIT "86296ac8f01597076979630667472a846b04ce46")
set(CNA_VENDORED_SOURCE_SDL_TREE "7769e7b106eef079953420c5677810b8c6f162b9")
set(CNA_VENDORED_SOURCE_SDL_GITLINKS "")

set(CNA_VENDORED_SOURCE_SDL_mixer_COMMIT "3075d3eda55ce295c6919d330edb2554ff4edb5b")
set(CNA_VENDORED_SOURCE_SDL_mixer_TREE "2f62afbc4b970a68ceb987581854f473cccc2fba")
set(CNA_VENDORED_SOURCE_SDL_mixer_GITLINKS
    "external/flac 833a260a7bc29f80899d33e8c1beb2fc56b4905b"
    "external/libgme 8ebb4f65096fdf980b39be2283699d523ddc09af"
    "external/libxmp 75e85f8a5be65f9f95fb946086bee17da158166f"
    "external/mpg123 e38e9027a3c1c254c2857c733017859c6da0ba77"
    "external/ogg 936fdd822a4e4fc963197b3eda931b89b52859a0"
    "external/opus ac9f053e1db9cc1c7608b74e029c25ded0254e3e"
    "external/opusfile 9e5322c62de32d2b2ad88324b3ad311174cec41d"
    "external/tremor 7afc84841ffa2e9610b65950fd233e6c428699c7"
    "external/vorbis a1938f4f0490402eb2a0fc50ef984b2f1fb6e786"
    "external/wavpack c88aaf8fb902c547a11053273df289d4b892335f")
