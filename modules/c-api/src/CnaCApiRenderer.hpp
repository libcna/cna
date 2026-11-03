// SPDX-License-Identifier: MS-PL
#pragma once

// Optional renderer defaults name unused arguments; isolate their diagnostics while keeping the
// adapter's own unused-parameter checks enabled.
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable: 4100)
#elif defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"

#if defined(_MSC_VER)
#pragma warning(pop)
#elif defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif
