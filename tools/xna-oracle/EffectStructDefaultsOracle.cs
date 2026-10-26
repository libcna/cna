// 2026-10-26 dependency audit, A2v2 A-09: what the real XNA 4.0 runtime reads as the defaults of a
// compiled effect's structure parameter.
//
// MojoShader's Effect parser skipped the defaults of a structure's non-array members and copied the
// later members' values onto them, and CNA's Fna3dCompiledEffectTests pinned that result as "the
// established contract". CnaConformanceEffect.fx declares
//
//     struct LightingParameters { float Intensity; float3 Direction; float Thresholds[2]; };
//     LightingParameters Lighting = { 0.5f, float3(0.6f, 0.7f, 0.8f), { 0.9f, 1.0f } };
//
// and this asks XNA itself, through its public EffectParameter API, what each member holds once
// such an effect is loaded. XNA's runtime takes only its Content Pipeline's form of an effect, and
// its compiler rejects one expression of CnaConformanceEffect.fx, so the measured input is
// tests/assets/xna40/source/fx_struct_defaults.fx -- the same structure -- built by XNA's
// EffectProcessor (tools/xna-pipeline-oracle/differential/effect_struct_defaults.json). The result
// is tests/reference/xna40/effect-struct-defaults/xna-effect-struct-defaults.txt.
//
// Usage: EffectStructDefaultsOracle.exe <effect .xnb, or fxc .fxb> <output-txt>
using System;
using System.Globalization;
using System.IO;
using Microsoft.Xna.Framework;
using Microsoft.Xna.Framework.Graphics;

public class EffectStructDefaultsOracle : Game
{
    private readonly GraphicsDeviceManager graphics;
    private readonly string effectPath;
    private readonly string outputPath;

    public EffectStructDefaultsOracle(string effectPath, string outputPath)
    {
        this.effectPath = effectPath;
        this.outputPath = outputPath;
        graphics = new GraphicsDeviceManager(this);
        graphics.PreferredBackBufferWidth = 64;
        graphics.PreferredBackBufferHeight = 64;
        graphics.GraphicsProfile = GraphicsProfile.HiDef;
    }

    private static string F(float value)
    {
        return value.ToString("R", CultureInfo.InvariantCulture);
    }

    protected override void LoadContent()
    {
        StreamWriter output = new StreamWriter(outputPath);
        try
        {
            Effect effect;
            byte[] code = File.ReadAllBytes(effectPath);
            if (effectPath.EndsWith(".xnb", StringComparison.OrdinalIgnoreCase))
            {
                // An effect XNA's own Content Pipeline built, loaded the way a game loads it.
                Content.RootDirectory = Path.GetDirectoryName(Path.GetFullPath(effectPath));
                effect = Content.Load<Effect>(Path.GetFileNameWithoutExtension(effectPath));
                output.WriteLine("FORMAT=xnb built by the XNA Content Pipeline");
            }
            else if (BitConverter.ToUInt32(code, 0) == 0xBCF00BCFu)
            {
                effect = new Effect(GraphicsDevice, code);
                output.WriteLine("FORMAT=xna");
            }
            else
            {
                // XNA's runtime takes only its Content Pipeline's form of an effect: the fxc output
                // behind a 16-byte header -- magic 0xBCF00BCF, header size 16, two zero DWORDs --
                // exactly as tests/reference/xna40/differential/*.xnb carry it.
                byte[] wrapped = new byte[16 + code.Length];
                BitConverter.GetBytes(0xBCF00BCFu).CopyTo(wrapped, 0);
                BitConverter.GetBytes(16u).CopyTo(wrapped, 4);
                code.CopyTo(wrapped, 16);
                effect = new Effect(GraphicsDevice, wrapped);
                output.WriteLine("FORMAT=fxc output in the XNA Content Pipeline header");
            }
            EffectParameter lighting = effect.Parameters["Lighting"];
            output.WriteLine("LIGHTING_CLASS=" + lighting.ParameterClass);
            output.WriteLine("LIGHTING_TYPE=" + lighting.ParameterType);
            output.WriteLine("LIGHTING_MEMBERS=" + lighting.StructureMembers.Count);
            foreach (EffectParameter member in lighting.StructureMembers)
            {
                output.WriteLine("MEMBER " + member.Name + " class=" + member.ParameterClass +
                                 " rows=" + member.RowCount + " columns=" + member.ColumnCount +
                                 " elements=" + member.Elements.Count);
            }
            output.WriteLine("INTENSITY=" + F(lighting.StructureMembers["Intensity"].GetValueSingle()));
            Vector3 direction = lighting.StructureMembers["Direction"].GetValueVector3();
            output.WriteLine("DIRECTION=" + F(direction.X) + "," + F(direction.Y) + "," + F(direction.Z));
            EffectParameter thresholds = lighting.StructureMembers["Thresholds"];
            for (int i = 0; i < thresholds.Elements.Count; i++)
                output.WriteLine("THRESHOLD" + i + "=" + F(thresholds.Elements[i].GetValueSingle()));
            output.WriteLine("RESULT=ok");
        }
        catch (Exception error)
        {
            output.WriteLine("RESULT=" + error.GetType().FullName + ": " + error.Message);
        }
        output.Close();
        Exit();
    }

    public static int Main(string[] args)
    {
        if (args.Length != 2)
        {
            Console.Error.WriteLine("usage: EffectStructDefaultsOracle.exe <effect-fxb> <output-txt>");
            return 2;
        }
        using (EffectStructDefaultsOracle game = new EffectStructDefaultsOracle(args[0], args[1]))
            game.Run();
        return 0;
    }
}
