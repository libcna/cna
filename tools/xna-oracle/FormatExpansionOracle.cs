// plans/plan_vulkan_parity.md VKPAR-0014: what the real XNA 4.0 runtime returns for the channels a
// SurfaceFormat does not store.
//
// ClassicTextureFormatTests.PointSamplingExpandsChannelsAndPreservesDeclaredRanges asserts that a
// one- or two-channel format expands its missing colour channels AND its missing alpha to 1.0
// (NormalizedByte2 -> (r, g, 255, 255)), while plan_vulkan.md VULKAN-174 pinned blue = 0 for the
// same format in the Vulkan renderer's own test. Both were reasoned, neither was measured: the
// SOFTWARE-142 row says only "missing channels follow the XNA/D3D texture rule", and VULKAN-174's
// zero is an anti-mutation argument about storage width, not a claim about sampling.
//
// This measures it. Deliberately NOT scene-driven like Oracle.cs: the scene format has no
// SurfaceFormat key (README "Status" names every shared SurfaceFormat as a remaining gap), and the
// question needs one texel per format rather than a rendered scene. The draw is the exact shape
// ClassicTextureFormatTests uses -- 1x1 texture, SpriteBatch with PointClamp and BlendState.Opaque,
// into an 8x8 SurfaceFormat.Color render target, centre pixel read back -- so the two are directly
// comparable and no difference can hide in the harness.
//
// Usage: FormatExpansionOracle.exe <output-txt>
using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using Microsoft.Xna.Framework;
using Microsoft.Xna.Framework.Graphics;
using Microsoft.Xna.Framework.Graphics.PackedVector;

public class FormatCase
{
    public string Name;
    public SurfaceFormat Format;
    // Uploads one texel of this format's own CLR type. Kept as a delegate because SetData<T> is
    // generic: the declared type is what decides the byte layout, so an object[] would not do.
    public Action<Texture2D> Upload;

    public FormatCase(string name, SurfaceFormat format, Action<Texture2D> upload)
    {
        Name = name; Format = format; Upload = upload;
    }
}

public class FormatExpansionOracle : Game
{
    readonly GraphicsDeviceManager gdm;
    readonly string outputPath;
    int frame = 0;

    public FormatExpansionOracle(string outputPath)
    {
        this.outputPath = outputPath;
        gdm = new GraphicsDeviceManager(this);
        gdm.PreferredBackBufferWidth = 64;
        gdm.PreferredBackBufferHeight = 64;
        gdm.GraphicsProfile = GraphicsProfile.HiDef;
    }

    static List<FormatCase> Cases()
    {
        var c = new List<FormatCase>();
        // The same values ClassicTextureFormatTests uses, so the two tables line up value for value.
        c.Add(new FormatCase("Color", SurfaceFormat.Color,
            delegate(Texture2D t) { t.SetData<Color>(new Color[] { new Color(128, 64, 255, 128) }); }));
        c.Add(new FormatCase("NormalizedByte2", SurfaceFormat.NormalizedByte2,
            delegate(Texture2D t) { t.SetData<NormalizedByte2>(new NormalizedByte2[] { new NormalizedByte2(0.5f, 0.25f) }); }));
        c.Add(new FormatCase("NormalizedByte4", SurfaceFormat.NormalizedByte4,
            delegate(Texture2D t) { t.SetData<NormalizedByte4>(new NormalizedByte4[] { new NormalizedByte4(0.5f, 0.25f, 1.0f, 0.5f) }); }));
        c.Add(new FormatCase("Rgba1010102", SurfaceFormat.Rgba1010102,
            delegate(Texture2D t) { t.SetData<Rgba1010102>(new Rgba1010102[] { new Rgba1010102(0.5f, 0.25f, 1.0f, 1.0f) }); }));
        c.Add(new FormatCase("Rg32", SurfaceFormat.Rg32,
            delegate(Texture2D t) { t.SetData<Rg32>(new Rg32[] { new Rg32(0.5f, 0.25f) }); }));
        c.Add(new FormatCase("Rgba64", SurfaceFormat.Rgba64,
            delegate(Texture2D t) { t.SetData<Rgba64>(new Rgba64[] { new Rgba64(0.5f, 0.25f, 1.0f, 0.5f) }); }));
        c.Add(new FormatCase("Alpha8", SurfaceFormat.Alpha8,
            delegate(Texture2D t) { t.SetData<Alpha8>(new Alpha8[] { new Alpha8(0.5f) }); }));
        c.Add(new FormatCase("Single", SurfaceFormat.Single,
            delegate(Texture2D t) { t.SetData<float>(new float[] { 0.25f }); }));
        c.Add(new FormatCase("Vector2", SurfaceFormat.Vector2,
            delegate(Texture2D t) { t.SetData<Vector2>(new Vector2[] { new Vector2(0.25f, 0.5f) }); }));
        c.Add(new FormatCase("Vector4", SurfaceFormat.Vector4,
            delegate(Texture2D t) { t.SetData<Vector4>(new Vector4[] { new Vector4(0.25f, 0.5f, 1.0f, 0.75f) }); }));
        c.Add(new FormatCase("HalfSingle", SurfaceFormat.HalfSingle,
            delegate(Texture2D t) { t.SetData<HalfSingle>(new HalfSingle[] { new HalfSingle(0.25f) }); }));
        c.Add(new FormatCase("HalfVector2", SurfaceFormat.HalfVector2,
            delegate(Texture2D t) { t.SetData<HalfVector2>(new HalfVector2[] { new HalfVector2(0.25f, 0.5f) }); }));
        c.Add(new FormatCase("HalfVector4", SurfaceFormat.HalfVector4,
            delegate(Texture2D t) { t.SetData<HalfVector4>(new HalfVector4[] { new HalfVector4(0.25f, 0.5f, 1.0f, 0.75f) }); }));
        c.Add(new FormatCase("HdrBlendable", SurfaceFormat.HdrBlendable,
            delegate(Texture2D t) { t.SetData<HalfVector4>(new HalfVector4[] { new HalfVector4(0.25f, 0.5f, 1.0f, 0.75f) }); }));
        c.Add(new FormatCase("Bgr565", SurfaceFormat.Bgr565,
            delegate(Texture2D t) { t.SetData<Bgr565>(new Bgr565[] { new Bgr565(0.5f, 0.25f, 1.0f) }); }));
        c.Add(new FormatCase("Bgra5551", SurfaceFormat.Bgra5551,
            delegate(Texture2D t) { t.SetData<Bgra5551>(new Bgra5551[] { new Bgra5551(0.5f, 0.25f, 1.0f, 1.0f) }); }));
        c.Add(new FormatCase("Bgra4444", SurfaceFormat.Bgra4444,
            delegate(Texture2D t) { t.SetData<Bgra4444>(new Bgra4444[] { new Bgra4444(0.5f, 0.25f, 1.0f, 0.5f) }); }));
        return c;
    }

    protected override void Draw(GameTime gameTime)
    {
        // One warm-up frame: the first Draw of an XNA Game can precede a fully-settled device.
        if (frame++ < 1) { base.Draw(gameTime); return; }

        var dev = GraphicsDevice;
        var lines = new List<string>();
        lines.Add("# plans/plan_vulkan_parity.md VKPAR-0014 -- measured on the real XNA 4.0 runtime.");
        lines.Add("# 1x1 texture of the named SurfaceFormat, SpriteBatch(PointClamp, BlendState.Opaque)");
        lines.Add("# into an 8x8 SurfaceFormat.Color RenderTarget2D, centre pixel read back.");
        lines.Add("# format|r,g,b,a   (or 'unsupported: <reason>')");

        foreach (var fc in Cases())
        {
            string result;
            try
            {
                var tex = new Texture2D(dev, 1, 1, false, fc.Format);
                fc.Upload(tex);

                var rt = new RenderTarget2D(dev, 8, 8, false, SurfaceFormat.Color, DepthFormat.None,
                                            0, RenderTargetUsage.PreserveContents);
                dev.SetRenderTarget(rt);
                dev.Clear(Color.Black);
                var batch = new SpriteBatch(dev);
                batch.Begin(SpriteSortMode.Deferred, BlendState.Opaque, SamplerState.PointClamp, null, null);
                batch.Draw(tex, new Rectangle(0, 0, 8, 8), Color.White);
                batch.End();
                dev.SetRenderTarget(null);

                var pixels = new Color[64];
                rt.GetData<Color>(pixels);
                var p = pixels[4 * 8 + 4];
                result = string.Format(CultureInfo.InvariantCulture, "{0},{1},{2},{3}", p.R, p.G, p.B, p.A);

                batch.Dispose();
                rt.Dispose();
                tex.Dispose();
            }
            catch (Exception ex)
            {
                dev.SetRenderTarget(null);
                result = "unsupported: " + ex.GetType().Name + ": " + ex.Message.Replace("\n", " ").Replace("\r", " ");
            }
            lines.Add(fc.Name + "|" + result);
        }

        File.WriteAllLines(outputPath, lines.ToArray());
        Console.WriteLine(string.Join("\n", lines.ToArray()));
        Exit();
    }

    public static void Main(string[] args)
    {
        if (args.Length < 1)
        {
            Console.Error.WriteLine("Usage: FormatExpansionOracle.exe <output-txt>");
            Environment.Exit(2);
        }
        using (var g = new FormatExpansionOracle(args[0])) { g.Run(); }
    }
}
