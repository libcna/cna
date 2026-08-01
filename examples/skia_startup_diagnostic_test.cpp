// SPDX-License-Identifier: MS-PL
// SKIA-17: stable startup/capability diagnostic for the selected raster mode.

#include "CNA/Internal/Backends/Skia/SkiaStartupDiagnostic.hpp"

#include <cstdio>
#include <string_view>

using CNA::Internal::Backends::Skia::kSkiaPinnedRevision;
using CNA::Internal::Backends::Skia::kSkiaStartupDiagnostic;

namespace
{
    int failures = 0;

    void Check(bool pass, const char* label)
    {
        std::printf("[%s] %s\n", pass ? "PASS" : "FAIL", label);
        if (!pass)
            ++failures;
    }
}

int main()
{
    const std::string_view report = kSkiaStartupDiagnostic;
    Check(report.starts_with("CNA: Skia capabilities -- "),
          "diagnostic has one stable backend-specific prefix");
    Check(report.find(kSkiaPinnedRevision) != std::string_view::npos,
          "diagnostic reports the pinned Skia revision");
    Check(report.find("surface=raster") != std::string_view::npos
              && report.find("colour=RGBA_8888/premultiplied") != std::string_view::npos,
          "diagnostic reports the selected raster surface and exact colour/alpha storage");
    Check(report.find("samples=0") != std::string_view::npos
              && report.find("anisotropic filtering=unsupported") != std::string_view::npos,
          "diagnostic reports actual raster sample and anisotropy capabilities");
    Check(report.find('\n') == std::string_view::npos && report.find('\r') == std::string_view::npos
              && report.find("0x") == std::string_view::npos,
          "diagnostic is one line and contains no pointer-shaped private value");
    Check(kSkiaStartupDiagnostic.data() == report.data(),
          "repeated diagnostic access uses one immutable static report");
    return failures == 0 ? 0 : 1;
}
