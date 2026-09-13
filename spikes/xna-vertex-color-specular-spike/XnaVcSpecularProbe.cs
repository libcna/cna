// SPDX-License-Identifier: MS-PL
// plans/plan_vulkan.md VULKAN-205.
//
// Does XNA scale a specular highlight by the per-vertex colour?
//
// CNA's Vulkan renderer has two vertex-colour lit paths and they disagree. The BasicEffect one
// (VULKAN-200) follows FNA: `vout.Diffuse *= vin.Color` in the vertex stage, and the pixel stage's
// `AddSpecular` then adds `Specular * color.a` -- so the colour's RGB never touches the highlight.
// The SkinnedEffect one (CNB-67, a CNA extension XNA has no counterpart for) instead multiplies the
// whole fragment by `vc.rgb` AFTER the specular has been added, which tints the highlight.
//
// XNA has no SkinnedEffect vertex-colour variant, so there is no direct measurement to make. There
// IS one for BasicEffect, which XNA does have -- and that is what settles the pattern the extension
// should follow, instead of an argument from resemblance.
//
// The scene makes the specular the ONLY contribution, so the answer cannot be diluted:
//
//   AmbientLightColor = 0, every light's DiffuseColor = 0, EmissiveColor = 0
//       -> the diffuse term is exactly zero, and every lit pixel IS the highlight
//   DirectionalLight0.SpecularColor = white, BasicEffect.SpecularColor = white, SpecularPower = 1
//   normal (0,0,1), eye on +Z, light direction (0,0,-1)
//       -> the half-vector is the normal, dot = 1, so the highlight is at full strength
//
//   A  vertex colour WHITE   -- the control: the highlight is present and bright.
//   B  vertex colour 20% grey -- if XNA scales the highlight by it, this is dark; if not, it is
//      the same white as A.
//
// Caveat, as with the other probes here: D3D9 is DXVK on this machine. A "same" answer is the
// strong one.

using System;
using System.IO;
using System.Text;
using Microsoft.Xna.Framework;
using Microsoft.Xna.Framework.Graphics;

/// Position + Normal + Colour. XNA has no built-in type for it, and LightingEnabled requires the
/// Normal -- VertexPositionColor alone is refused with "Normal0 is missing".
public struct VertexPositionNormalColor : IVertexType
{
    public Vector3 Position;
    public Vector3 Normal;
    public Color   Color;

    public VertexPositionNormalColor(Vector3 p, Vector3 n, Color c)
    {
        Position = p; Normal = n; Color = c;
    }

    public static readonly VertexDeclaration Declaration = new VertexDeclaration(
        new VertexElement(0,  VertexElementFormat.Vector3, VertexElementUsage.Position, 0),
        new VertexElement(12, VertexElementFormat.Vector3, VertexElementUsage.Normal, 0),
        new VertexElement(24, VertexElementFormat.Color,   VertexElementUsage.Color, 0));

    VertexDeclaration IVertexType.VertexDeclaration { get { return Declaration; } }
}

public class Probe : Game
{
    GraphicsDeviceManager gdm;
    StringBuilder log = new StringBuilder();
    bool done = false;

    const int N = 64;

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
            Color white = Render(new Color(255, 255, 255, 255));
            Color grey  = Render(new Color(51, 51, 51, 255));
            // A BLACK vertex colour is the sharp case: it zeroes the DIFFUSE completely, so
            // whatever is left is the highlight and nothing else. CNA's skinned variants used to
            // multiply the whole fragment by the colour, which blacks the pixel out entirely --
            // and a shared test encodes that expectation. This leg is what decides whose
            // expectation is XNA's.
            Color black = Render(new Color(0, 0, 0, 255));
            Say(string.Format("vertex colour white -> {0}   20% grey -> {1}   black -> {2}",
                              T(white), T(grey), T(black)));

            if (white.R < 60)
                Say("VERDICT: INCONCLUSIVE -- the control has no highlight to speak of, so nothing "
                    + "can be concluded about whether the vertex colour scales one.");
            else if (Same(white, grey))
                Say("VERDICT: XNA does NOT scale the specular highlight by the vertex colour -- "
                    + "AddSpecular adds it after the colour has been applied to the diffuse.");
            else
                Say("VERDICT: XNA DOES scale the highlight by the vertex colour.");

            Say(black.R > 60
                ? "BLACK LEG: a black vertex colour leaves the highlight standing -- the colour "
                  + "zeroes the DIFFUSE only, and AddSpecular adds on top of it."
                : "BLACK LEG: a black vertex colour blacks the whole pixel out, highlight "
                  + "included.");
        }
        catch (Exception e) { Say("EXCEPTION: " + e); }
        File.WriteAllText("probe-output.txt", log.ToString());
        Exit();
    }

    Color Render(Color vertexColour)
    {
        var dev = GraphicsDevice;
        var rt = new RenderTarget2D(dev, N, N, false, SurfaceFormat.Color, DepthFormat.None,
                                    0, RenderTargetUsage.DiscardContents);
        dev.SetRenderTarget(rt);
        dev.Clear(new Color(0, 0, 0, 255));
        dev.BlendState = BlendState.Opaque;
        dev.DepthStencilState = DepthStencilState.None;
        dev.RasterizerState = RasterizerState.CullNone;

        var n = new Vector3(0, 0, 1);
        var coloured = new VertexPositionNormalColor[] {
            new VertexPositionNormalColor(new Vector3(-1, 1,0), n, vertexColour),
            new VertexPositionNormalColor(new Vector3(-1,-1,0), n, vertexColour),
            new VertexPositionNormalColor(new Vector3( 1,-1,0), n, vertexColour),
            new VertexPositionNormalColor(new Vector3(-1, 1,0), n, vertexColour),
            new VertexPositionNormalColor(new Vector3( 1,-1,0), n, vertexColour),
            new VertexPositionNormalColor(new Vector3( 1, 1,0), n, vertexColour),
        };

        var fx = new BasicEffect(dev);
        fx.LightingEnabled = true;
        fx.PreferPerPixelLighting = false;
        fx.VertexColorEnabled = true;
        fx.TextureEnabled = false;
        fx.AmbientLightColor = Vector3.Zero;
        fx.DiffuseColor = Vector3.One;
        fx.EmissiveColor = Vector3.Zero;
        fx.SpecularColor = Vector3.One;
        fx.SpecularPower = 1f;
        fx.DirectionalLight0.Enabled = true;
        fx.DirectionalLight0.Direction = new Vector3(0, 0, -1);
        fx.DirectionalLight0.DiffuseColor = Vector3.Zero;   // the highlight is the only term
        fx.DirectionalLight0.SpecularColor = Vector3.One;
        fx.DirectionalLight1.Enabled = false;
        fx.DirectionalLight2.Enabled = false;
        fx.World = Matrix.Identity;
        fx.View = Matrix.CreateLookAt(new Vector3(0, 0, 3), Vector3.Zero, Vector3.Up);
        // A real perspective projection: with Identity the quad sits at view z = -3 and is clipped
        // away entirely, which is what the first run of this probe measured as "no highlight".
        fx.Projection = Matrix.CreatePerspectiveFieldOfView(MathHelper.PiOver4, 1f, 0.1f, 100f);
        fx.CurrentTechnique.Passes[0].Apply();
        dev.DrawUserPrimitives(PrimitiveType.TriangleList, coloured, 0, 2);
        dev.SetRenderTarget(null);

        var px = new Color[N * N];
        rt.GetData(px);
        return px[(N / 2) * N + (N / 2)];
    }

    static string T(Color c) { return "(" + c.R + "," + c.G + "," + c.B + ")"; }
    static bool Same(Color a, Color b)
    {
        return Math.Abs(a.R - b.R) <= 6 && Math.Abs(a.G - b.G) <= 6 && Math.Abs(a.B - b.B) <= 6;
    }
}

static class Entry
{
    [STAThread]
    static void Main(string[] a) { using (var g = new Probe()) g.Run(); }
}
