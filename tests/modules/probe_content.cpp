// SPDX-License-Identifier: MS-PL
// Minimal-link probe for CNA::Content (plans/MODULARIZATION_PLAN.md §4): constructing a
// ContentManager must not require the runtime (Game) layer, devices, CNAEXT or networking.
// Content legitimately pulls graphics-core, audio and media (type readers construct
// Texture/SoundEffect/Song/Video) -- the paired ModuleLinkClosure_Content ctest asserts the
// closure stays free of the layers above it.
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"

#include <cstdio>

int main()
{
    Microsoft::Xna::Framework::Content::ContentManager content(nullptr);
    std::printf("content probe: root='%s'\n",
                content.getRootDirectoryProperty().c_str());
    return 0;
}
