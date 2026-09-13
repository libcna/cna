// SPDX-License-Identifier: MS-PL
// plans/plan_vulkan.md VULKAN-197 (finding F-36's CNA-wide sibling).
//
// Measures, on the real XNA 4.0 runtime, what happens to `BasicEffect.DiffuseColor` above 1 on the
// UNLIT path. The property setter does not clamp (`BasicEffect.cs:117`), the value reaches the
// shader through `EffectHelpers.SetMaterialColor` and is written to `vout.Diffuse`, which
// `Structures.fxh` declares `COLOR0` -- so Direct3D 9 should saturate it before interpolation, the
// same `oD0` rule `plans/plan_fx.md` FX-123 measured for the LIT programs and `VULKAN-196` measured
// for `EnvironmentMapEffect`'s `oD1`.
//
// A GREY texture, never a white one: with a white texture both answers saturate at the output and
// the probe could not fail. That is the trap FX-123's own test records.
//
//   A  DiffuseColor 0.5 and 1.0 differ  -- the positive control.
//   B  1.0 and 2.0 are the same         -- the saturate.
//   C  1.0 and 3.0 are the same         -- so B is not one value's coincidence.
//
// Caveat: D3D9 here is DXVK. "Same" is strong (XNA's own shader contains no clamp, so something
// below it clamped); "different" would be inconclusive rather than proof.

using System;
using System.IO;
using System.Text;
using Microsoft.Xna.Framework;
using Microsoft.Xna.Framework.Graphics;

public class Probe : Game
{
    GraphicsDeviceManager gdm;
    StringBuilder log = new StringBuilder();
    bool done = false;

    const int N = 64;
    static readonly Color kTex = new Color(100, 100, 100, 255);

    public Probe()
    {
        gdm = new GraphicsDeviceManager(this);
        gdm.PreferredBackBufferWidth = N;
        gdm.PreferredBackBufferHeight = N;
    }

    void Say(string s) { Console.WriteLine(s); log.AppendLine(s); }

    protected override void Draw(GameTime gt)
    {
        if (done) return;
        done = true;
        try
        {
            Say("adapter: " + GraphicsAdapter.DefaultAdapter.Description);
            var dev = GraphicsDevice;
            dev.BlendState = BlendState.Opaque;
            dev.DepthStencilState = DepthStencilState.None;
            dev.RasterizerState = RasterizerState.CullNone;

            var tex = new Texture2D(dev, 1, 1);
            tex.SetData(new Color[] { kTex });

            var quad = new VertexPositionTexture[] {
                new VertexPositionTexture(new Vector3(-1, 1,0), new Vector2(0,0)),
                new VertexPositionTexture(new Vector3(-1,-1,0), new Vector2(0,1)),
                new VertexPositionTexture(new Vector3( 1,-1,0), new Vector2(1,1)),
                new VertexPositionTexture(new Vector3(-1, 1,0), new Vector2(0,0)),
                new VertexPositionTexture(new Vector3( 1,-1,0), new Vector2(1,1)),
                new VertexPositionTexture(new Vector3( 1, 1,0), new Vector2(1,0)),
            };

            Color half  = Render(tex, quad, 0.5f);
            Color one   = Render(tex, quad, 1.0f);
            Color two   = Render(tex, quad, 2.0f);
            Color three = Render(tex, quad, 3.0f);

            Say(string.Format("DiffuseColor 0.5 -> {0}   1.0 -> {1}   2.0 -> {2}   3.0 -> {3}",
                              T(half), T(one), T(two), T(three)));
            if (Same(half, one))
                Say("VERDICT: INCONCLUSIVE -- DiffuseColor changed nothing between 0.5 and 1.0.");
            else if (Same(one, two) && Same(one, three))
                Say("VERDICT: XNA SATURATES DiffuseColor -- 2.0 and 3.0 render exactly as 1.0 does, "
                    + "which is D3D9 clamping the COLOR0 output register.");
            else
                Say("VERDICT: NOT saturated on this stack -- inconclusive rather than proof, DXVK "
                    + "may not emulate the vertex COLOR register clamp.");

            // The GRADIENT leg. Without a texture the output write saturates anyway, so a flat
            // quad cannot tell "saturate then interpolate" from "interpolate then saturate" --
            // the distinction only appears when the two vertices disagree, which is FX-123's own
            // argument. Vertex colours 1.0 and 0.2 with DiffuseColor 2.0:
            //   saturate first : saturate(2.0)=1.0 and saturate(0.4)=0.4 -> midpoint 0.7 -> 178
            //   saturate last  : 2.0 and 0.4        -> midpoint 1.2      -> clipped 255
            Say("");
            Color g1 = Gradient(1.0f);
            Color g2 = Gradient(2.0f);
            Say(string.Format("gradient midpoint, DiffuseColor 1.0 -> {0}   2.0 -> {1}", T(g1), T(g2)));
            Say(g2.R >= 250
                ? "GRADIENT VERDICT: interpolated UNCLAMPED (midpoint clipped at the output)."
                : "GRADIENT VERDICT: saturated per vertex, then interpolated -- the midpoint is "
                  + "below the clip, which only saturate-then-interpolate can produce.");
        }
        catch (Exception e) { Say("EXCEPTION: " + e); }
        File.WriteAllText("probe-output.txt", log.ToString());
        Exit();
    }

    Color Render(Texture2D tex, VertexPositionTexture[] quad, float diffuse)
    {
        var dev = GraphicsDevice;
        var rt = new RenderTarget2D(dev, N, N, false, SurfaceFormat.Color, DepthFormat.None,
                                    0, RenderTargetUsage.DiscardContents);
        dev.SetRenderTarget(rt);
        dev.Clear(new Color(0, 0, 0, 255));

        var fx = new BasicEffect(dev);
        fx.LightingEnabled = false;          // the UNLIT path: vout.Diffuse = DiffuseColor
        fx.VertexColorEnabled = false;
        fx.TextureEnabled = true;
        fx.Texture = tex;
        fx.Alpha = 1f;
        fx.DiffuseColor = new Vector3(diffuse, diffuse, diffuse);
        fx.World = Matrix.Identity;
        fx.View = Matrix.Identity;
        fx.Projection = Matrix.Identity;
        fx.CurrentTechnique.Passes[0].Apply();
        dev.DrawUserPrimitives(PrimitiveType.TriangleList, quad, 0, 2);
        dev.SetRenderTarget(null);

        var px = new Color[N * N];
        rt.GetData(px);
        return px[(N / 2) * N + (N / 2)];
    }

    /// A quad whose LEFT edge is white and RIGHT edge is 20% grey, sampled at the centre, with no
    /// texture: the only thing between the vertex colour and the pixel is the interpolator.
    Color Gradient(float diffuse)
    {
        var dev = GraphicsDevice;
        var rt = new RenderTarget2D(dev, N, N, false, SurfaceFormat.Color, DepthFormat.None,
                                    0, RenderTargetUsage.DiscardContents);
        dev.SetRenderTarget(rt);
        dev.Clear(new Color(0, 0, 0, 255));

        Color a = new Color(255, 255, 255, 255);
        Color b = new Color(51, 51, 51, 255);
        var quad = new VertexPositionColor[] {
            new VertexPositionColor(new Vector3(-1, 1,0), a),
            new VertexPositionColor(new Vector3(-1,-1,0), a),
            new VertexPositionColor(new Vector3( 1,-1,0), b),
            new VertexPositionColor(new Vector3(-1, 1,0), a),
            new VertexPositionColor(new Vector3( 1,-1,0), b),
            new VertexPositionColor(new Vector3( 1, 1,0), b),
        };

        var fx = new BasicEffect(dev);
        fx.LightingEnabled = false;
        fx.TextureEnabled = false;
        fx.VertexColorEnabled = true;
        fx.Alpha = 1f;
        fx.DiffuseColor = new Vector3(diffuse, diffuse, diffuse);
        fx.World = Matrix.Identity;
        fx.View = Matrix.Identity;
        fx.Projection = Matrix.Identity;
        fx.CurrentTechnique.Passes[0].Apply();
        dev.DrawUserPrimitives(PrimitiveType.TriangleList, quad, 0, 2);
        dev.SetRenderTarget(null);

        var px = new Color[N * N];
        rt.GetData(px);
        return px[(N / 2) * N + (N / 2)];
    }

    static string T(Color c) { return "(" + c.R + "," + c.G + "," + c.B + ")"; }
    static bool Same(Color a, Color b)
    {
        return Math.Abs(a.R - b.R) <= 4 && Math.Abs(a.G - b.G) <= 4 && Math.Abs(a.B - b.B) <= 4;
    }
}

static class Entry
{
    [STAThread]
    static void Main(string[] a) { using (var g = new Probe()) g.Run(); }
}
