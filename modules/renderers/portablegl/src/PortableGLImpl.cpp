// SPDX-License-Identifier: MS-PL
//
// The single translation unit that instantiates PortableGL's (rswinkle/PortableGL)
// `#define PORTABLEGL_IMPLEMENTATION` STB-style implementation. Mirrors
// modules/renderers/sokol/src/SokolImpl.cpp's identical role: every other file in this renderer
// includes "portablegl.h" for declarations only, so isolating the ~13k-line implementation body
// here keeps it out of PortableGLRenderer.cpp's own incremental rebuilds.

#define PORTABLEGL_IMPLEMENTATION
#include "portablegl.h"
