// Task 476: reference values for FNA's Viewport.Project/Unproject, complementing Phase 40
// (Viewport/DisplayMode/adapter behavior). Viewport is a plain value struct (public int-based
// constructor, no GraphicsDevice involved anywhere) and Project/Unproject are pure Matrix/Vector3
// math -- confirmed by reading src/Graphics/Viewport.cs directly -- unlike Task 474 (BasicEffect,
// needs a live GraphicsDevice -> a real native FNA3D build, itself needing its own uninitialized
// MojoShader submodule plus SDL2/SDL3 linkage -- a substantially larger, uncertain undertaking,
// deferred rather than attempted here) and Task 475 (SpriteFont's own constructor is `internal`,
// only reachable through real compiled XNB content via ContentManager -- an even larger
// prerequisite than 474's).
//
// Covers 2 case families:
//  - Identity-matrix projection: with World=View=Projection=Identity, Project's own formula
//    reduces to a pure viewport-rect remap (verified by hand against the formula in
//    Viewport.cs's own Project() body before trusting the numbers).
//  - A real (non-identity) camera, matching this project's own established
//    CreateLookAt/CreatePerspectiveFieldOfView convention (already used by Task 399's
//    EnvironmentMapEffect capstone scene, reused here for consistency).
// Every case also round-trips through Unproject(Project(source)) and confirms it recovers the
// original source point (within floating-point tolerance) -- a self-consistency check that
// doesn't need any externally-known-good value, just Project/Unproject genuinely being inverses
// of one another as XNA's own documented contract requires.
using System;
using Microsoft.Xna.Framework;
using Microsoft.Xna.Framework.Graphics;

namespace CNA.FnaReference
{
    public static class ViewportReference
    {
        public static JsonWriter Generate()
        {
            var cases = new JsonWriter();

            var viewport = new Viewport(0, 0, 800, 480);

            AddIdentityCase(cases, viewport, new Vector3(0, 0, 0), "identity_origin");
            AddIdentityCase(cases, viewport, new Vector3(1, 1, 0), "identity_topRight");
            AddIdentityCase(cases, viewport, new Vector3(-1, -1, 0), "identity_bottomLeft");

            Matrix projection = Matrix.CreatePerspectiveFieldOfView(
                MathHelper.PiOver4, 800.0f / 480.0f, 0.1f, 100.0f);
            Matrix view = Matrix.CreateLookAt(
                new Vector3(0, 0, 5), Vector3.Zero, new Vector3(0, 1, 0));
            Matrix world = Matrix.Identity;

            AddCameraCase(cases, viewport, new Vector3(0, 0, 0), projection, view, world, "camera_worldOrigin");
            AddCameraCase(cases, viewport, new Vector3(1, 1, 0), projection, view, world, "camera_offAxis");

            return cases;
        }

        private static void AddIdentityCase(JsonWriter cases, Viewport viewport, Vector3 source, string label)
        {
            AddCameraCase(cases, viewport, source, Matrix.Identity, Matrix.Identity, Matrix.Identity, label);
        }

        private static void AddCameraCase(JsonWriter cases, Viewport viewport, Vector3 source,
            Matrix projection, Matrix view, Matrix world, string label)
        {
            Vector3 projected = viewport.Project(source, projection, view, world);
            Vector3 roundTrip = viewport.Unproject(projected, projection, view, world);

            double dx = roundTrip.X - source.X;
            double dy = roundTrip.Y - source.Y;
            double dz = roundTrip.Z - source.Z;
            double roundTripError = Math.Sqrt(dx * dx + dy * dy + dz * dz);

            cases.Add(label, new JsonWriter()
                .Add("source_X", (double)source.X).Add("source_Y", (double)source.Y).Add("source_Z", (double)source.Z)
                .Add("Project_X", (double)projected.X).Add("Project_Y", (double)projected.Y).Add("Project_Z", (double)projected.Z)
                .Add("Unproject_X", (double)roundTrip.X).Add("Unproject_Y", (double)roundTrip.Y).Add("Unproject_Z", (double)roundTrip.Z)
                .Add("roundTripError", roundTripError));
        }
    }
}
