// SPDX-License-Identifier: MS-PL
// plans/plan_vulkan.md VULKAN-099.
//
// Measures, on the real XNA 4.0 runtime, what `RasterizerState.MultiSampleAntiAlias = false`
// actually does to a draw on a multisampled render target. CNA drops the field before it reaches
// any renderer (VULKAN-096 measured that), and the row that owns it refuses to add a sixth
// parameter to an interface twelve renderers implement until someone has established what the
// value is supposed to mean. This is that measurement.
//
// The probe draws one triangle with a shallow diagonal edge -- the shape multisampling exists for --
// and counts the pixels that come back as a BLEND of the fill colour and the clear colour. That
// count is the amount of antialiasing present, and it is compared across three configurations:
//
//   MS4-on   4x multisampled target, MultiSampleAntiAlias = true
//   MS4-off  the same target, MultiSampleAntiAlias = false
//   NOMS     a target with no multisampling at all -- the "no antialiasing" floor
//
// How to read the result:
//   MS4-on > NOMS and MS4-off ~= NOMS  -> XNA honours the flag: false turns multisample
//                                        rasterization off for the draw.
//   MS4-on > NOMS and MS4-off ~= MS4-on -> the flag does nothing to a triangle edge here.
//   MS4-on ~= NOMS                      -> multisampling never happened; INCONCLUSIVE, and the
//                                        run says so rather than reporting the flag as inert.
//
// The last case matters because this prefix runs D3D9 through DXVK. A flag that appears inert may
// be inert in the translation layer rather than in XNA, so the probe reports the MS4-on vs NOMS
// difference FIRST: without it, nothing below it can be concluded.
//
// A line leg is included because D3D11 narrowed the equivalent state to line/point antialiasing
// (RasterizerDesc.MultisampleEnable), while D3D9's D3DRS_MULTISAMPLEANTIALIAS gates multisample
// rasterization as a whole. If the two legs disagree, that difference is the answer.

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
    static readonly Color kClear = new Color(0, 0, 0, 255);
    static readonly Color kFill  = new Color(255, 255, 255, 255);

    public Probe()
    {
        gdm = new GraphicsDeviceManager(this);
        gdm.PreferredBackBufferWidth = N;
        gdm.PreferredBackBufferHeight = N;
        gdm.GraphicsProfile = GraphicsProfile.HiDef;   // multisampled render targets need HiDef
    }

    void Say(string s) { Console.WriteLine(s); log.AppendLine(s); }

    protected override void Draw(GameTime gt)
    {
        if (done) return;
        done = true;
        try
        {
            Say("adapter: " + GraphicsAdapter.DefaultAdapter.Description);
            Say("profile: " + GraphicsDevice.GraphicsProfile);
            Say("");
            Leg("TRI ", PrimitiveType.TriangleList);
            Say("");
            Leg("LINE", PrimitiveType.LineList);
        }
        catch (Exception e) { Say("EXCEPTION: " + e); }
        File.WriteAllText("probe-output.txt", log.ToString());
        Exit();
    }

    void Leg(string label, PrimitiveType kind)
    {
        // PROBE_SKIP_FALSE exists to attribute DXVK's "Unhandled render state 161" warning.
        // 161 is D3DRS_MULTISAMPLEANTIALIAS. If the warning disappears when the only leg that
        // sets the flag to false is skipped, then XNA really does carry the property down to that
        // render state -- and the translation layer, not XNA, is what drops it.
        bool skipFalse = Environment.GetEnvironmentVariable("PROBE_SKIP_FALSE") == "1";
        int noms  = Render(label, kind, 0, true,  "NOMS   ");
        int on    = Render(label, kind, 4, true,  "MS4-on ");
        if (skipFalse) { Say(label + " MS4-off SKIPPED (PROBE_SKIP_FALSE=1)"); return; }
        int off   = Render(label, kind, 4, false, "MS4-off");

        Say(label + " blended-edge pixel counts: NOMS=" + noms + " MS4-on=" + on +
            " MS4-off=" + off);
        if (on <= noms)
        {
            Say(label + " VERDICT: INCONCLUSIVE -- multisampling produced no antialiasing at all "
                + "on this stack, so nothing can be concluded about the flag.");
            return;
        }
        // A third of the difference is a generous band: the two answers are meant to be either
        // "the same as NOMS" or "the same as MS4-on", not somewhere in between.
        int band = Math.Max(1, (on - noms) / 3);
        if (Math.Abs(off - noms) <= band)
            Say(label + " VERDICT: XNA HONOURS the flag -- false rasterizes like an unmultisampled "
                + "target.");
        else if (Math.Abs(off - on) <= band)
            Say(label + " VERDICT: XNA IGNORES the flag for this primitive -- false antialiases "
                + "exactly as true does.");
        else
            Say(label + " VERDICT: PARTIAL -- false lands between the two, which neither reading "
                + "predicts; the counts above are the finding.");
    }

    /// Draws the shape into a target with `samples` multisampling and returns the number of pixels
    /// that are neither the clear colour nor the fill colour -- i.e. the antialiased edge.
    int Render(string label, PrimitiveType kind, int samples, bool msaaFlag, string tag)
    {
        var dev = GraphicsDevice;
        var rt = new RenderTarget2D(dev, N, N, false, SurfaceFormat.Color, DepthFormat.Depth24,
                                    samples, RenderTargetUsage.DiscardContents);
        dev.SetRenderTarget(rt);
        dev.Clear(kClear);
        dev.BlendState = BlendState.Opaque;
        dev.DepthStencilState = DepthStencilState.None;
        dev.RasterizerState = new RasterizerState {
            CullMode = CullMode.None,
            FillMode = FillMode.Solid,
            MultiSampleAntiAlias = msaaFlag,
        };

        var fx = new BasicEffect(dev);
        fx.VertexColorEnabled = true;
        fx.World = Matrix.Identity;
        fx.View = Matrix.Identity;
        // Screen-space, y down, so the vertex coordinates below read as pixels.
        fx.Projection = Matrix.CreateOrthographicOffCenter(0f, N, N, 0f, 0f, 1f);
        fx.CurrentTechnique.Passes[0].Apply();

        if (kind == PrimitiveType.TriangleList)
        {
            // A shallow diagonal edge from (4,60) to (60,20): 56 across, 40 down, so every one of
            // the ~56 edge pixels has genuine partial coverage.
            var tri = new VertexPositionColor[] {
                new VertexPositionColor(new Vector3( 4f, 60f, 0f), kFill),
                new VertexPositionColor(new Vector3(60f, 20f, 0f), kFill),
                new VertexPositionColor(new Vector3(60f, 60f, 0f), kFill),
            };
            dev.DrawUserPrimitives(PrimitiveType.TriangleList, tri, 0, 1);
        }
        else
        {
            var line = new VertexPositionColor[] {
                new VertexPositionColor(new Vector3( 4f, 60f, 0f), kFill),
                new VertexPositionColor(new Vector3(60f, 20f, 0f), kFill),
            };
            dev.DrawUserPrimitives(PrimitiveType.LineList, line, 0, 1);
        }
        dev.SetRenderTarget(null);

        var px = new Color[N * N];
        rt.GetData(px);
        int blended = 0, solid = 0;
        foreach (var c in px)
        {
            bool isClear = c.R <= 6 && c.G <= 6 && c.B <= 6;
            bool isFill  = c.R >= 249 && c.G >= 249 && c.B >= 249;
            if (isClear) continue;
            if (isFill) { ++solid; continue; }
            ++blended;
        }
        Say("        " + tag + " requested=" + samples + " granted=" + rt.MultiSampleCount +
            " flag=" + msaaFlag + " solid=" + solid + " blended=" + blended);
        return blended;
    }
}

static class Entry
{
    [STAThread]
    static void Main(string[] a) { using (var g = new Probe()) g.Run(); }
}
