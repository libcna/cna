// SPDX-License-Identifier: MS-PL
// REMED-GFX-010 — shared transformed-camera fog conformance scenes + FNA reference math.
//
// Purpose: give every renderer's view-space-fog test the SAME canonical scenes and the SAME
// analytically-derived expected colors, so their numeric results are directly cross-renderer
// comparable (REMED-GFX-010 Phase 5 / Phase 12).
//
// The XNA/FNA fog semantic (EffectHelpers.SetFogVector + Common.fxh ComputeFogFactor):
//   fogFactor = saturate(dot(objectPos, FogVector))          // FNA: 1 = fully fogged
//   with FogVector.xyz = (World*View).{M13,M23,M33} * scale
//        FogVector.w   = ((World*View).M43 + fogStart) * scale
//        scale         = 1 / (fogStart - fogEnd)
//   => fogFactor = saturate((viewZ + fogStart)/(fogStart - fogEnd)),  viewZ = (objPos*World*View).z
// i.e. fog is driven by the VIEW-SPACE Z of the vertex, not its raw object-space Z.
//
// CNA renderers carry the inverse "keep" convention (keep = 1 - fogFactor;
// finalRGB = mix(FogColor, geomRGB, keep)). This header computes both the correct view-space keep
// and the OLD buggy object-space keep, so each test can assert (a) the scene is genuinely
// discriminating (viewKeep != objKeep) and (b) the actual pixel matches the view-space value.
//
// A real perspective/identity projection cannot separate object-space Z from view-space Z without
// putting geometry at negative clip Z (which the DirectX-convention renderers clip). We therefore use
// an ORTHOGRAPHIC projection: it renders geometry at negative view-space Z full-screen (screen XY is
// independent of view Z) while leaving fog — which depends only on World*View, never the projection
// — untouched. Every quad is centered on the view axis so the sampled center pixel corresponds to
// object position (0,0,objZ); for the LookAt scene that on-axis point is the world origin.

#pragma once

#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"

#include <cmath>
#include <vector>

namespace CNA::Examples::VSFogRef
{
    using Microsoft::Xna::Framework::Matrix;
    using Microsoft::Xna::Framework::Vector3;
    using Microsoft::Xna::Framework::Color;

    // Canonical scene constants (shared by every renderer for cross-renderer numeric comparability).
    inline constexpr float kFogStart = 2.0f;   // eye-space distance where fog begins
    inline constexpr float kFogEnd   = 6.0f;   // eye-space distance where fog is full

    inline const Vector3 kGeomRGB(0.0f, 0.0f, 1.0f); // blue geometry
    inline const Vector3 kFogRGB (1.0f, 0.0f, 0.0f); // red fog

    // Orthographic projection: 2x2 view volume, near 0.1, far 100 (matches the [-1,1] quad extents).
    inline Matrix Ortho()
    {
        return Matrix::CreateOrthographic(2.0f, 2.0f, 0.1f, 100.0f);
    }

    inline float Saturate(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

    // The correct FNA/XNA VIEW-SPACE "keep" factor for a vertex at object position objPos.
    inline float ViewKeep(const Vector3& objPos, const Matrix& world, const Matrix& view,
                          float fogStart, float fogEnd, bool fogEnabled)
    {
        if (!fogEnabled) return 1.0f;
        if (fogStart == fogEnd) return 0.0f; // FNA degenerate case -> fully fogged
        Matrix wv; Matrix::Multiply(world, view, wv);
        const float viewZ = Vector3::Transform(objPos, wv).Z; // (objPos * World*View).z
        const float scale = 1.0f / (fogStart - fogEnd);
        return 1.0f - Saturate(scale * (viewZ + fogStart));
    }

    // The OLD, buggy OBJECT-SPACE "keep" factor (what every pre-REMED-GFX-010 shader computes:
    // clamp((objZ + FogEnd)/(FogEnd - FogStart), 0, 1)).
    inline float ObjectKeep(float objZ, float fogStart, float fogEnd, bool fogEnabled)
    {
        if (!fogEnabled) return 1.0f;
        if (std::fabs(fogEnd - fogStart) < 1e-6f) return 0.0f;
        return Saturate((objZ + fogEnd) / (fogEnd - fogStart));
    }

    // mix(FogColor, geomRGB, keep) -> 8-bit color (keep=1 keeps geometry, keep=0 is full fog).
    inline Color ColorFor(float keep, const Vector3& geomRGB = kGeomRGB, const Vector3& fogRGB = kFogRGB)
    {
        auto ch = [&](float g, float f)
        {
            const float v = f * (1.0f - keep) + g * keep;
            int b = static_cast<int>(std::lround(v * 255.0f));
            return b < 0 ? 0 : (b > 255 ? 255 : b);
        };
        return Color(ch(geomRGB.X, fogRGB.X), ch(geomRGB.Y, fogRGB.Y), ch(geomRGB.Z, fogRGB.Z), 255);
    }

    // One transformed-camera scene. objZ is the object-space Z of the (view-axis-centered) quad; the
    // sampled center pixel therefore corresponds to object position (0, 0, objZ).
    struct Scene
    {
        const char* label;
        Matrix      world;
        Matrix      view;
        float       objZ;
        bool        fogEnabled;
        bool        discriminating; // expected: ViewKeep != ObjectKeep (a genuine object-vs-view test)
    };

    // The BasicEffect scene battery: identity control, view/world translation, view rotation, and a
    // fog-disabled control. All but the identity control and the near/disabled endpoints are
    // discriminating (object-space Z=0 collapses to "no fog" while view-space Z varies with the
    // camera).
    inline std::vector<Scene> Battery()
    {
        const Matrix I = Matrix::getIdentityProperty();
        std::vector<Scene> s;
        //             label                                     world                              view                                           objZ  fog    discriminating
        s.push_back({ "identity-control (purple, no-regress)",   I,                                 I,                                             -4.0f, true,  false });
        s.push_back({ "view-translate near (blue)",              I,                                 Matrix::CreateTranslation(0.0f,0.0f,-2.0f),     0.0f, true,  false });
        s.push_back({ "view-translate half (purple)",            I,                                 Matrix::CreateTranslation(0.0f,0.0f,-4.0f),     0.0f, true,  true  });
        s.push_back({ "view-translate full (red)",               I,                                 Matrix::CreateTranslation(0.0f,0.0f,-6.0f),     0.0f, true,  true  });
        s.push_back({ "world-translate half (purple)",           Matrix::CreateTranslation(0,0,-4), I,                                             0.0f, true,  true  });
        s.push_back({ "view-rotate LookAt |eye|=4 (purple)",     I,                                 Matrix::CreateLookAt(Vector3(2.4f,0.0f,3.2f), Vector3::Zero, Vector3(0.0f,1.0f,0.0f)), 0.0f, true, true });
        s.push_back({ "fog-disabled (blue no-op)",               I,                                 Matrix::CreateTranslation(0.0f,0.0f,-4.0f),     0.0f, false, false });
        return s;
    }
}
