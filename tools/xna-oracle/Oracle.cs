// plan_dx9.md Phase D9-A (D9-A3): scene-driven XNA 4.0 reference renderer. No content pipeline.
//
// Reads a ".scene" file (the same declarative format CnaOracleRender.cpp reads) and renders it
// with the real XNA 4.0 runtime, saving the result as a PNG -- the "oracle" half of the D9-A4
// diff harness. Do not hand-transcribe scene data into this file; every scene lives in
// tools/xna-oracle/scenes/*.scene and is parsed identically by both sides, or the harness drifts.
//
// Usage: Oracle.exe <scene-file> <output-png>
using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using Microsoft.Xna.Framework;
using Microsoft.Xna.Framework.Graphics;

public enum SceneVertexFormat { PositionColor, PositionTexture }

public class Scene
{
    public int Width = 256;
    public int Height = 256;
    public GraphicsProfile Profile = GraphicsProfile.HiDef;
    public Color ClearColor = Color.CornflowerBlue;
    public SceneVertexFormat VertexFormat = SceneVertexFormat.PositionColor;
    public bool VertexColorEnabled;
    public bool LightingEnabled;
    public bool TextureEnabled;
    public int TextureWidth;
    public int TextureHeight;
    public bool TexturePointFilter = true;
    public PrimitiveType Primitive = PrimitiveType.TriangleList;
    public List<VertexPositionColor> ColorVertices = new List<VertexPositionColor>();
    public List<VertexPositionTexture> TextureVertices = new List<VertexPositionTexture>();
    public List<Color> TexturePixels = new List<Color>();

    public static Scene Load(string path)
    {
        var scene = new Scene();
        foreach (var rawLine in File.ReadAllLines(path))
        {
            var line = rawLine.Trim();
            if (line.Length == 0 || line[0] == '#') continue;

            int eq = line.IndexOf('=');
            if (eq < 0) continue;
            string key = line.Substring(0, eq).Trim();
            string value = line.Substring(eq + 1).Trim();

            switch (key)
            {
                case "width": scene.Width = int.Parse(value, CultureInfo.InvariantCulture); break;
                case "height": scene.Height = int.Parse(value, CultureInfo.InvariantCulture); break;
                case "profile":
                    scene.Profile = value == "Reach" ? GraphicsProfile.Reach : GraphicsProfile.HiDef;
                    break;
                case "clearcolor": scene.ClearColor = ParseColor(value); break;
                case "vertexformat":
                    scene.VertexFormat = value == "PositionTexture"
                        ? SceneVertexFormat.PositionTexture : SceneVertexFormat.PositionColor;
                    break;
                case "vertexcolor": scene.VertexColorEnabled = ParseBool(value); break;
                case "lighting": scene.LightingEnabled = ParseBool(value); break;
                case "texture": scene.TextureEnabled = ParseBool(value); break;
                case "texturewidth": scene.TextureWidth = int.Parse(value, CultureInfo.InvariantCulture); break;
                case "textureheight": scene.TextureHeight = int.Parse(value, CultureInfo.InvariantCulture); break;
                case "texturefilter": scene.TexturePointFilter = value == "Point"; break;
                case "texturepixel": scene.TexturePixels.Add(ParseColor(value)); break;
                case "primitive": scene.Primitive = ParsePrimitive(value); break;
                case "vertex":
                    if (scene.VertexFormat == SceneVertexFormat.PositionTexture)
                        scene.TextureVertices.Add(ParseTextureVertex(value));
                    else
                        scene.ColorVertices.Add(ParseColorVertex(value));
                    break;
                default:
                    throw new InvalidDataException("Scene: unknown key '" + key + "' in " + path);
            }
        }
        return scene;
    }

    public int VertexCount()
    {
        return VertexFormat == SceneVertexFormat.PositionTexture ? TextureVertices.Count : ColorVertices.Count;
    }

    public int PrimitiveCount()
    {
        int n = VertexCount();
        switch (Primitive)
        {
            case PrimitiveType.TriangleList: return n / 3;
            case PrimitiveType.TriangleStrip: return n - 2;
            case PrimitiveType.LineList: return n / 2;
            case PrimitiveType.LineStrip: return n - 1;
        }
        throw new InvalidOperationException("Scene: unhandled PrimitiveType " + Primitive);
    }

    static bool ParseBool(string s) { return s == "true"; }

    static Color ParseColor(string s)
    {
        var p = s.Split(',');
        return new Color(byte.Parse(p[0], CultureInfo.InvariantCulture),
                          byte.Parse(p[1], CultureInfo.InvariantCulture),
                          byte.Parse(p[2], CultureInfo.InvariantCulture),
                          byte.Parse(p[3], CultureInfo.InvariantCulture));
    }

    static PrimitiveType ParsePrimitive(string s)
    {
        switch (s)
        {
            case "TriangleList": return PrimitiveType.TriangleList;
            case "TriangleStrip": return PrimitiveType.TriangleStrip;
            case "LineList": return PrimitiveType.LineList;
            case "LineStrip": return PrimitiveType.LineStrip;
        }
        throw new InvalidDataException("Scene: unknown primitive '" + s + "'");
    }

    static VertexPositionColor ParseColorVertex(string s)
    {
        var p = s.Split(',');
        var pos = new Vector3(
            float.Parse(p[0], CultureInfo.InvariantCulture),
            float.Parse(p[1], CultureInfo.InvariantCulture),
            float.Parse(p[2], CultureInfo.InvariantCulture));
        var color = new Color(byte.Parse(p[3], CultureInfo.InvariantCulture),
                               byte.Parse(p[4], CultureInfo.InvariantCulture),
                               byte.Parse(p[5], CultureInfo.InvariantCulture),
                               byte.Parse(p[6], CultureInfo.InvariantCulture));
        return new VertexPositionColor(pos, color);
    }

    static VertexPositionTexture ParseTextureVertex(string s)
    {
        var p = s.Split(',');
        var pos = new Vector3(
            float.Parse(p[0], CultureInfo.InvariantCulture),
            float.Parse(p[1], CultureInfo.InvariantCulture),
            float.Parse(p[2], CultureInfo.InvariantCulture));
        var uv = new Vector2(
            float.Parse(p[3], CultureInfo.InvariantCulture),
            float.Parse(p[4], CultureInfo.InvariantCulture));
        return new VertexPositionTexture(pos, uv);
    }
}

public class Oracle : Game
{
    readonly GraphicsDeviceManager gdm;
    readonly Scene scene;
    readonly string outputPath;
    int frame = 0;

    public Oracle(Scene scene, string outputPath)
    {
        this.scene = scene;
        this.outputPath = outputPath;
        gdm = new GraphicsDeviceManager(this);
        gdm.PreferredBackBufferWidth = scene.Width;
        gdm.PreferredBackBufferHeight = scene.Height;
        gdm.GraphicsProfile = scene.Profile;
    }

    protected override void Draw(GameTime gameTime)
    {
        var dev = GraphicsDevice;
        var rt = new RenderTarget2D(dev, scene.Width, scene.Height, false, SurfaceFormat.Color, DepthFormat.Depth24);
        dev.SetRenderTarget(rt);
        dev.Clear(scene.ClearColor);

        var fx = new BasicEffect(dev);
        fx.VertexColorEnabled = scene.VertexColorEnabled;
        fx.LightingEnabled = scene.LightingEnabled;
        fx.TextureEnabled = scene.TextureEnabled;
        fx.World = Matrix.Identity;
        fx.View = Matrix.Identity;
        fx.Projection = Matrix.Identity;

        Texture2D texture = null;
        if (scene.TextureEnabled)
        {
            texture = new Texture2D(dev, scene.TextureWidth, scene.TextureHeight);
            texture.SetData(scene.TexturePixels.ToArray());
            fx.Texture = texture;
            dev.SamplerStates[0] = scene.TexturePointFilter ? SamplerState.PointClamp : SamplerState.LinearClamp;
        }

        foreach (var pass in fx.CurrentTechnique.Passes)
        {
            pass.Apply();
            if (scene.VertexFormat == SceneVertexFormat.PositionTexture)
                dev.DrawUserPrimitives(scene.Primitive, scene.TextureVertices.ToArray(), 0, scene.PrimitiveCount());
            else
                dev.DrawUserPrimitives(scene.Primitive, scene.ColorVertices.ToArray(), 0, scene.PrimitiveCount());
        }

        dev.SetRenderTarget(null);

        using (var fs = File.Create(outputPath))
            rt.SaveAsPng(fs, scene.Width, scene.Height);

        Console.WriteLine("XNA-ORACLE-OK profile=" + dev.GraphicsProfile
                          + " adapter=" + GraphicsAdapter.DefaultAdapter.Description
                          + " out=" + outputPath);
        if (++frame >= 1) Exit();
        base.Draw(gameTime);
    }

    static void Main(string[] args)
    {
        if (args.Length != 2)
        {
            Console.Error.WriteLine("Usage: Oracle.exe <scene-file> <output-png>");
            Environment.Exit(1);
            return;
        }
        var scene = Scene.Load(args[0]);
        using (var g = new Oracle(scene, args[1])) g.Run();
    }
}
