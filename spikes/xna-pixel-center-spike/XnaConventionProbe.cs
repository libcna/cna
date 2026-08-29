// SPDX-License-Identifier: MS-PL
// Measures, on the real XNA 4.0 runtime, the two rasterization conventions CNA's EasyGL renderer
// and its point-sampling contract disagree about. Prints a selection map rather than pass/fail,
// so the answer names which convention XNA actually follows instead of only whether CNA matches.

using System;
using System.IO;
using System.Text;
using Microsoft.Xna.Framework;
using Microsoft.Xna.Framework.Graphics;

static class Pat
{
    public static Color[] Make(int w, int h)
    {
        var p = new Color[w * h];
        for (int j = 0; j < h; ++j)
            for (int i = 0; i < w; ++i)
                p[j * w + i] = Texel(i, j, w, h);
        return p;
    }

    // A colour that names its own texel, so a destination pixel can be decoded back to the texel
    // that produced it.
    public static Color Texel(int i, int j, int w, int h)
    {
        int r = 25 + (i * 200) / Math.Max(1, w - 1);
        int g = 25 + (j * 200) / Math.Max(1, h - 1);
        return new Color(r, g, 80);
    }

    public static bool Decode(Color c, int w, int h, out int i, out int j)
    {
        i = -1; j = -1;
        int best = int.MaxValue;
        for (int jj = 0; jj < h; ++jj)
            for (int ii = 0; ii < w; ++ii)
            {
                Color t = Texel(ii, jj, w, h);
                int d = Math.Abs(t.R - c.R) + Math.Abs(t.G - c.G) + Math.Abs(t.B - c.B);
                if (d < best) { best = d; i = ii; j = jj; }
            }
        return best <= 12;
    }
}

public class Probe : Game
{
    GraphicsDeviceManager gdm;
    StringBuilder log = new StringBuilder();
    bool done = false;

    public Probe()
    {
        gdm = new GraphicsDeviceManager(this);
        gdm.PreferredBackBufferWidth = 64;
        gdm.PreferredBackBufferHeight = 64;
    }

    void Say(string s) { Console.WriteLine(s); log.AppendLine(s); }

    protected override void Draw(GameTime gt)
    {
        if (done) return;
        done = true;
        try { LegPixelCenter(); Leg3D("U1 8x4 -> 16x8", 8, 4, 16, 8); Leg3D("U2 3x3 -> 10x10", 3, 3, 10, 10);
              Leg2x2("LEG-C 2x2 -> 2x2 POINT ", TextureFilter.Point);
              Leg2x2("LEG-C 2x2 -> 2x2 LINEAR", TextureFilter.Linear); }
        catch (Exception e) { Say("EXCEPTION: " + e); }
        File.WriteAllText("probe-output.txt", log.ToString());
        Exit();
    }

    // Replicates easygl_xna_pixel_center_test.cpp: does XNA cover a 1x1 screen-space triangle?
    void LegPixelCenter()
    {
        var dev = GraphicsDevice;
        var rt = new RenderTarget2D(dev, 64, 64, false, SurfaceFormat.Color, DepthFormat.None,
                                    0, RenderTargetUsage.DiscardContents);
        dev.SetRenderTarget(rt);
        dev.Clear(new Color(0, 0, 0, 255));
        dev.BlendState = BlendState.Opaque;
        dev.RasterizerState = RasterizerState.CullNone;
        dev.DepthStencilState = DepthStencilState.None;

        var fx = new BasicEffect(dev);
        fx.VertexColorEnabled = true;
        fx.World = Matrix.Identity;
        fx.View = Matrix.Identity;
        fx.Projection = Matrix.CreateOrthographicOffCenter(0f, 64f, 64f, 0f, 0f, 1f);
        fx.CurrentTechnique.Passes[0].Apply();

        Color tiny = new Color(255, 0, 0, 255), ctrl = new Color(0, 255, 0, 255);
        var tri = new VertexPositionColor[] {
            new VertexPositionColor(new Vector3(16f, 16f, 0f), tiny),
            new VertexPositionColor(new Vector3(17f, 16f, 0f), tiny),
            new VertexPositionColor(new Vector3(16f, 17f, 0f), tiny),
            new VertexPositionColor(new Vector3(32f, 32f, 0f), ctrl),
            new VertexPositionColor(new Vector3(48f, 32f, 0f), ctrl),
            new VertexPositionColor(new Vector3(32f, 48f, 0f), ctrl),
        };
        dev.DrawUserPrimitives(PrimitiveType.TriangleList, tri, 0, 2);
        dev.SetRenderTarget(null);

        var px = new Color[64 * 64];
        rt.GetData(px);
        int nTiny = 0, nCtrl = 0;
        foreach (var c in px)
        {
            if (Math.Abs(c.R - 255) <= 8 && c.G <= 8 && c.B <= 8) ++nTiny;
            if (c.R <= 8 && Math.Abs(c.G - 255) <= 8 && c.B <= 8) ++nCtrl;
        }
        Say("LEG-A XNA 1x1 screen-space triangle: covered=" + nTiny + " (CNA/EasyGL demands >=1)");
        Say("LEG-A control triangle: covered=" + nCtrl + " (sanity, expect ~120)");
        // Locate the covered pixel so the fill rule is visible, not just the count.
        for (int y = 14; y < 20; ++y)
        {
            string row = "        y=" + y + " ";
            for (int x = 14; x < 20; ++x)
            {
                Color c = px[y * 64 + x];
                row += (Math.Abs(c.R - 255) <= 8 && c.G <= 8) ? "#" : ".";
            }
            Say(row);
        }
    }

    // Replicates point_sampling_contract_test.cpp Exact3DLeg: an NDC-covering textured quad,
    // point-sampled, magnified. Reports which texel each destination pixel actually selected.
    void Leg3D(string label, int tw, int th, int rtW, int rtH)
    {
        var dev = GraphicsDevice;
        var tex = new Texture2D(dev, tw, th);
        tex.SetData(Pat.Make(tw, th));

        var rt = new RenderTarget2D(dev, rtW, rtH, false, SurfaceFormat.Color, DepthFormat.None,
                                    0, RenderTargetUsage.DiscardContents);
        dev.SetRenderTarget(rt);
        dev.Clear(new Color(0, 0, 0, 255));
        dev.RasterizerState = RasterizerState.CullNone;
        dev.DepthStencilState = DepthStencilState.None;
        dev.BlendState = BlendState.Opaque;
        dev.SamplerStates[0] = new SamplerState {
            Filter = TextureFilter.Point,
            AddressU = TextureAddressMode.Clamp,
            AddressV = TextureAddressMode.Clamp,
        };

        var quad = new VertexPositionTexture[] {
            new VertexPositionTexture(new Vector3(-1f,  1f, 0f), new Vector2(0f, 0f)),
            new VertexPositionTexture(new Vector3(-1f, -1f, 0f), new Vector2(0f, 1f)),
            new VertexPositionTexture(new Vector3( 1f, -1f, 0f), new Vector2(1f, 1f)),
            new VertexPositionTexture(new Vector3(-1f,  1f, 0f), new Vector2(0f, 0f)),
            new VertexPositionTexture(new Vector3( 1f, -1f, 0f), new Vector2(1f, 1f)),
            new VertexPositionTexture(new Vector3( 1f,  1f, 0f), new Vector2(1f, 0f)),
        };
        var fx = new BasicEffect(dev);
        fx.TextureEnabled = true;
        fx.Texture = tex;
        fx.World = Matrix.Identity;
        fx.View = Matrix.Identity;
        fx.Projection = Matrix.Identity;
        fx.CurrentTechnique.Passes[0].Apply();
        dev.DrawUserPrimitives(PrimitiveType.TriangleList, quad, 0, 2);
        dev.SetRenderTarget(null);

        var px = new Color[rtW * rtH];
        rt.GetData(px);

        int agreeHalf = 0, agreeInt = 0, undecoded = 0;
        for (int y = 0; y < rtH; ++y)
            for (int x = 0; x < rtW; ++x)
            {
                int gi, gj;
                if (!Pat.Decode(px[y * rtW + x], tw, th, out gi, out gj)) { ++undecoded; continue; }
                // Convention 1 -- pixel centre at half-integers (OpenGL / D3D10+ / Vulkan).
                int hx = (int)Math.Floor((x + 0.5) / rtW * tw), hy = (int)Math.Floor((y + 0.5) / rtH * th);
                // Convention 2 -- pixel centre at integers (Direct3D 9 as XNA 4.0 documents it).
                int ix = (int)Math.Floor((double)x / rtW * tw), iy = (int)Math.Floor((double)y / rtH * th);
                hx = Math.Min(hx, tw - 1); hy = Math.Min(hy, th - 1);
                ix = Math.Min(ix, tw - 1); iy = Math.Min(iy, th - 1);
                if (gi == hx && gj == hy) ++agreeHalf;
                if (gi == ix && gj == iy) ++agreeInt;
            }
        int total = rtW * rtH;
        Say(label + ": pixels=" + total + " undecoded=" + undecoded);
        Say("        matches HALF-INTEGER centres (GL/D3D10/Vulkan, what CNA's contract asserts): "
            + agreeHalf + "/" + total);
        Say("        matches INTEGER centres      (D3D9 convention, what EasyGL corrects toward):  "
            + agreeInt + "/" + total);
        // The first row of selected texel indices, so a half-texel shift is visible directly.
        string r0 = "        row0 selected i: ";
        for (int x = 0; x < rtW; ++x)
        {
            int gi, gj;
            r0 += Pat.Decode(px[x], tw, th, out gi, out gj) ? gi.ToString() : "?";
        }
        Say(r0);
    }


    // Replicates descriptor_capacity_contract_test.cpp: a 2x2 texture on a 2x2 target with
    // identity matrices -- "one texel to one pixel". At that scale both pixel-centre conventions
    // pick the same texel, so what separates them is whether a LINEAR magnification filter lands
    // on a texel centre (a no-op) or between two texels (a blend that destroys the encoding).
    void Leg2x2(string label, TextureFilter filter)
    {
        var dev = GraphicsDevice;
        var tex = new Texture2D(dev, 2, 2);
        // The encoding descriptor-capacity uses: every channel only ever 0 or 255.
        tex.SetData(new Color[] {
            new Color(255, 0, 0, 255), new Color(0, 255, 0, 255),
            new Color(0, 0, 255, 255), new Color(255, 255, 0, 255),
        });

        var rt = new RenderTarget2D(dev, 2, 2, false, SurfaceFormat.Color, DepthFormat.None,
                                    0, RenderTargetUsage.DiscardContents);
        dev.SetRenderTarget(rt);
        dev.Clear(new Color(13, 17, 19, 255));
        dev.RasterizerState = RasterizerState.CullNone;
        dev.DepthStencilState = DepthStencilState.None;
        dev.BlendState = BlendState.Opaque;
        dev.SamplerStates[0] = new SamplerState {
            Filter = filter,
            AddressU = TextureAddressMode.Clamp,
            AddressV = TextureAddressMode.Clamp,
        };

        var quad = new VertexPositionTexture[] {
            new VertexPositionTexture(new Vector3(-1f,  1f, 0f), new Vector2(0f, 0f)),
            new VertexPositionTexture(new Vector3(-1f, -1f, 0f), new Vector2(0f, 1f)),
            new VertexPositionTexture(new Vector3( 1f, -1f, 0f), new Vector2(1f, 1f)),
            new VertexPositionTexture(new Vector3(-1f,  1f, 0f), new Vector2(0f, 0f)),
            new VertexPositionTexture(new Vector3( 1f, -1f, 0f), new Vector2(1f, 1f)),
            new VertexPositionTexture(new Vector3( 1f,  1f, 0f), new Vector2(1f, 0f)),
        };
        var fx = new BasicEffect(dev);
        fx.TextureEnabled = true; fx.Texture = tex;
        fx.World = Matrix.Identity; fx.View = Matrix.Identity; fx.Projection = Matrix.Identity;
        fx.CurrentTechnique.Passes[0].Apply();
        dev.DrawUserPrimitives(PrimitiveType.TriangleList, quad, 0, 2);
        dev.SetRenderTarget(null);

        var px = new Color[4];
        rt.GetData(px);
        int dirty = 0;
        string dump = "";
        foreach (var c in px)
        {
            bool clean = Clean(c.R) && Clean(c.G) && Clean(c.B);
            if (!clean) ++dirty;
            dump += "(" + c.R + "," + c.G + "," + c.B + ")" + (clean ? " " : "* ");
        }
        Say(label + ": dirty=" + dirty + "/4   " + dump);
        Say("        (CNA's descriptor-capacity contract needs dirty=0; * marks a blended channel)");
    }

    static bool Clean(int v) { return v == 0 || v == 255; }

    static void Main() { using (var g = new Probe()) g.Run(); }
}
