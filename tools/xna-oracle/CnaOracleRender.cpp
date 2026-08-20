// plans/plan_dx9.md Phase D9-A (D9-A3): scene-driven CNA renderer -- the "CNA" half of the D9-A4 diff
// harness. Reads the SAME ".scene" file format tools/xna-oracle/Oracle.cs reads (see that file's
// own header comment) and renders it through CNA's real public Game/GraphicsDeviceManager/
// GraphicsDevice/BasicEffect API on whichever renderer this binary was built against (originally
// CNA_GRAPHICS_RENDERER=D3D9; D9-A6 additionally builds this SAME file, unmodified in substance,
// under EASYGL -- see OracleRendererName() below), saving the result as a PNG. Do not
// hand-transcribe scene data here -- every scene lives in tools/xna-oracle/scenes/*.scene, or the
// harness drifts.
//
// Usage: cna_oracle_render.exe <scene-file> <output-png>
//
// Deliberately renders straight to the back buffer rather than mirroring Oracle.cs's own
// RenderTarget2D indirection: RenderTarget2D::GetData()'s own CPU readback path is not proven on
// this renderer yet (every existing render-target test reads back via GetBackBufferData() after
// blitting, not RenderTarget2D::GetData() directly), while GetBackBufferData() is exercised by
// every D3D9 CTest already. The back buffer is the same size as the scene and is never presented
// before being read, so the two approaches are pixel-equivalent for what this tool needs to prove.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "Microsoft/Xna/Framework/Graphics/AlphaTestEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/CompareFunction.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/CullMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DirectionalLight.hpp"
#include "Microsoft/Xna/Framework/Graphics/DualTextureEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/EnvironmentMapEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerStateCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureAddressMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureFilter.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTexture.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionTexture.hpp"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    std::vector<std::string> Split(const std::string& s, char delim)
    {
        std::vector<std::string> out;
        std::stringstream ss(s);
        std::string item;
        while (std::getline(ss, item, delim)) out.push_back(item);
        return out;
    }

    std::string Trim(const std::string& s)
    {
        const std::size_t start = s.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) return "";
        const std::size_t end = s.find_last_not_of(" \t\r\n");
        return s.substr(start, end - start + 1);
    }

    // D9-A6: the "renderer=..." tag in this tool's own stdout line below was hardcoded to "D3D9"
    // (D9-A3's original, single-renderer assumption) -- now derived from whichever of CMakeLists.txt's
    // own add_compile_definitions(CNA_RENDERER_*) macros is actually active, so the same stdout line
    // stays accurate as this file is built against additional renderers (EASYGL first, D9-A6).
    const char* OracleRendererName()
    {
#if defined(CNA_RENDERER_DIRECTX9)
        return "D3D9";
#elif defined(CNA_RENDERER_EASYGL)
        return "EASYGL";
#elif defined(CNA_RENDERER_VULKAN)
        return "VULKAN";
#elif defined(CNA_RENDERER_DIRECTX11)
        return "D3D11";
#elif defined(CNA_RENDERER_DIRECTX12)
        return "D3D12";
#elif defined(CNA_RENDERER_BGFX)
        return "BGFX";
#elif defined(CNA_RENDERER_WEBGPU)
        return "WEBGPU";
#elif defined(CNA_RENDERER_OPENGLES1)
        return "OPENGLES1";
#elif defined(CNA_RENDERER_SOFTWARE)
        return "SOFTWARE";
#elif defined(CNA_RENDERER_HEADLESS)
        return "HEADLESS";
#elif defined(CNA_RENDERER_SDL_RENDERER)
        return "SDL_RENDERER";
#elif defined(CNA_RENDERER_SKIA)
        return "SKIA";
#elif defined(CNA_RENDERER_GDI)
        return "GDI";
#else
        return "UNKNOWN";
#endif
    }

    enum class SceneVertexFormat { PositionColor, PositionTexture, PositionNormalTexture, PositionDualTexture, PositionNormalTextureWeights };
    enum class SceneEffectType { BasicEffect, AlphaTestEffect, DualTextureEffect, EnvironmentMapEffect, SkinnedEffect };

    // Real XNA has no built-in dual-UV vertex struct -- mirrors Oracle.cs's own
    // VertexPositionDualTexture (same 28-byte layout: Position+TexCoord0+TexCoord1).
    struct VertexPositionDualTexture
    {
        Vector3 Position;
        Vector2 TextureCoordinate0;
        Vector2 TextureCoordinate1;
    };

    const VertexDeclaration& DualTextureVertexDeclaration()
    {
        static const VertexDeclaration decl(28, std::vector<VertexElement>{
            VertexElement(0,  VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
            VertexElement(12, VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 0),
            VertexElement(20, VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 1),
        });
        return decl;
    }

    // Real XNA has no built-in skinned vertex struct either -- mirrors Oracle.cs's own
    // VertexPositionNormalTextureWeights, same stride-52 layout as the existing CNA vertex
    // declaration: Position(0)/Normal(12)/TexCoord(24)/BlendWeight(32, Vector4)/
    // BlendIndices(48, Byte4 -- raw bytes, not a packed/normalized value).
    struct VertexPositionNormalTextureWeights
    {
        Vector3 Position;
        Vector3 Normal;
        Vector2 TextureCoordinate;
        Vector4 BlendWeight;
        std::uint8_t BlendIndex0, BlendIndex1, BlendIndex2, BlendIndex3;
    };

    const VertexDeclaration& SkinnedVertexDeclaration()
    {
        static const VertexDeclaration decl(52, std::vector<VertexElement>{
            VertexElement(0,  VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
            VertexElement(12, VertexElementFormat::Vector3, VertexElementUsage::Normal, 0),
            VertexElement(24, VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 0),
            VertexElement(32, VertexElementFormat::Vector4, VertexElementUsage::BlendWeight, 0),
            VertexElement(48, VertexElementFormat::Byte4, VertexElementUsage::BlendIndices, 0),
        });
        return decl;
    }

    // D9-93: one entry per repeated spritedraw= line -- a genuinely different sprite (own
    // destination rect, color, depth, optionally its own texture) within the SAME Begin()/End()
    // block. Used to exercise SpriteSortMode ordering (D9-93 proper -- a single sprite has no
    // observable sort behavior at all) and, via textureIndex, multi-texture batching /
    // FlushBatch()-on-texture-change -- D9-90's own explicitly-named "known, explicitly-scoped-out
    // gap" (no D9-A5 scene had yet exercised 2+ sprites with a genuine texture swap mid-batch).
    // textureIndex selects `texture` (0, default) or `texture2` (1), reusing the scene format's
    // existing DualTextureEffect texture2* keys rather than inventing new ones.
    struct SpriteDrawEntry
    {
        Rectangle destRect{0, 0, 0, 0};
        Color color = Color::White;
        float depth = 0.0f;
        int textureIndex = 0;
    };

    struct SceneLight
    {
        bool enabled = false;
        Vector3 diffuse{0, 0, 0};
        Vector3 direction{0, 0, -1};
    };

    struct Scene
    {
        int width = 256;
        int height = 256;
        GraphicsProfile profile = GraphicsProfile::HiDef;
        Color clearColor = Color::CornflowerBlue;
        // D9-90..93: a scene with spriteBatchMode=true bypasses the entire effect/vertex-format
        // draw path below and instead drives the real public SpriteBatch/Texture2D API -- a
        // genuinely different draw model (no vertex declaration, no Effect, real orthographic +
        // half-pixel MatrixTransform construction happens entirely inside D3D9SpriteBatchRenderer
        // itself), so it gets its own top-level scene keys rather than overloading the
        // effect-draw ones.
        bool spriteBatchMode = false;
        Rectangle spriteDestRect{0, 0, 0, 0};
        Color spriteColor = Color::White;
        float spriteRotation = 0.0f;
        Vector2 spriteOrigin{0, 0};
        SpriteEffects spriteEffects = SpriteEffects::None;
        bool spriteSourceRectSet = false;
        Rectangle spriteSourceRect{0, 0, 0, 0};
        // D9-92: which named SamplerState preset SpriteBatch::Begin() is given -- defaults to
        // real XNA's own default (LinearClamp, used when Begin() is called with no samplerState
        // argument), only meaningful when spriteBatchMode=true.
        std::string spriteSampler = "LinearClamp";
        // D9-93: one or more spritedraw= lines switch to a genuinely different multi-sprite draw
        // path (see the Draw() function's own spriteBatchMode branch) -- when empty, the single
        // sprite* fields above are used instead (backward compatible with every earlier
        // spriteBatchMode scene).
        std::vector<SpriteDrawEntry> spriteDraws;
        SpriteSortMode spriteSortMode = SpriteSortMode::Deferred;
        SceneVertexFormat vertexFormat = SceneVertexFormat::PositionColor;
        bool vertexColorEnabled = false;
        bool lightingEnabled = false;
        // plans/plan_dx9.md D9-81 item 1, resolved 2026-07-16: BasicEffect/SkinnedEffect's real
        // PreferPerPixelLighting, now that GpuDrawParams carries it. Defaults false, matching
        // real XNA's own default (per-vertex/Gouraud lighting).
        bool preferPerPixelLighting = false;
        bool textureEnabled = false;
        int textureWidth = 0;
        int textureHeight = 0;
        bool texturePointFilter = true;
        bool texture2Enabled = false;
        int texture2Width = 0;
        int texture2Height = 0;
        bool environmentMapEnabled = false;
        int environmentMapSize = 0;
        float environmentMapAmount = 1.0f;
        float fresnelFactor = 1.0f;
        // plans/plan_dx9.md D9-81 item 4, resolved 2026-07-16: EnvironmentMapEffect.EnvironmentMapSpecular,
        // whose non-zero-ness drives the real specularEnabled bool now that GpuDrawParams carries
        // it. Defaults to Vector3::Zero, matching real XNA's own default (specular disabled).
        Vector3 environmentMapSpecular{0, 0, 0};
        Color environmentMapPixel = Color::Black;
        Vector3 diffuseColor{1, 1, 1};
        Vector3 ambientColor{0, 0, 0};
        SceneLight light0;
        SceneLight light1;
        SceneLight light2;
        SceneEffectType effectType = SceneEffectType::BasicEffect;
        CompareFunction alphaFunction = CompareFunction::Always;
        int referenceAlpha = 0;
        bool fogEnabled = false;
        Vector3 fogColor{0, 0, 0};
        float fogStart = 0.0f;
        float fogEnd = 1.0f;
        int weightsPerVertex = 4;
        Vector3 bone1Translate{0, 0, 0};
        Vector3 bone2Translate{0, 0, 0};
        Vector3 bone3Translate{0, 0, 0};
        // D9-21/D9-62: RasterizerState.CullMode/DepthBias/SlopeScaleDepthBias oracle proof.
        // CullMode defaults to XNA's own real default (CullCounterClockwiseFace), matching
        // Oracle.cs's own identical default -- every pre-existing scene is unaffected.
        CullMode cullMode = CullMode::CullCounterClockwiseFace;
        float depthBias = 0.0f;
        float slopeScaleDepthBias = 0.0f;
        PrimitiveType primitive = PrimitiveType::TriangleList;
        std::vector<VertexPositionColor> colorVertices;
        std::vector<VertexPositionTexture> textureVertices;
        std::vector<VertexPositionNormalTexture> normalTextureVertices;
        std::vector<VertexPositionDualTexture> dualTextureVertices;
        std::vector<VertexPositionNormalTextureWeights> skinnedVertices;
        std::vector<Color> texturePixels;
        std::vector<Color> texture2Pixels;

        int VertexCount() const
        {
            if (vertexFormat == SceneVertexFormat::PositionNormalTextureWeights)
                return static_cast<int>(skinnedVertices.size());
            if (vertexFormat == SceneVertexFormat::PositionDualTexture)
                return static_cast<int>(dualTextureVertices.size());
            if (vertexFormat == SceneVertexFormat::PositionNormalTexture)
                return static_cast<int>(normalTextureVertices.size());
            if (vertexFormat == SceneVertexFormat::PositionTexture)
                return static_cast<int>(textureVertices.size());
            return static_cast<int>(colorVertices.size());
        }

        int PrimitiveCount() const
        {
            const int n = VertexCount();
            switch (primitive)
            {
            case PrimitiveType::TriangleList:  return n / 3;
            case PrimitiveType::TriangleStrip: return n - 2;
            case PrimitiveType::LineList:      return n / 2;
            case PrimitiveType::LineStrip:     return n - 1;
            }
            throw std::runtime_error("Scene: unhandled PrimitiveType");
        }
    };

    bool ParseBool(const std::string& s) { return s == "true"; }

    Color ParseColor(const std::string& s)
    {
        const auto p = Split(s, ',');
        return Color(static_cast<std::uint8_t>(std::stoi(p[0])),
                     static_cast<std::uint8_t>(std::stoi(p[1])),
                     static_cast<std::uint8_t>(std::stoi(p[2])),
                     static_cast<std::uint8_t>(std::stoi(p[3])));
    }

    Vector3 ParseVector3(const std::string& s)
    {
        const auto p = Split(s, ',');
        return Vector3(std::stof(p[0]), std::stof(p[1]), std::stof(p[2]));
    }

    Rectangle ParseRectangle(const std::string& s)
    {
        const auto p = Split(s, ',');
        return Rectangle(std::stoi(p[0]), std::stoi(p[1]), std::stoi(p[2]), std::stoi(p[3]));
    }

    PrimitiveType ParsePrimitive(const std::string& s)
    {
        if (s == "TriangleList")  return PrimitiveType::TriangleList;
        if (s == "TriangleStrip") return PrimitiveType::TriangleStrip;
        if (s == "LineList")      return PrimitiveType::LineList;
        if (s == "LineStrip")     return PrimitiveType::LineStrip;
        throw std::runtime_error("Scene: unknown primitive '" + s + "'");
    }

    CompareFunction ParseCompareFunction(const std::string& s)
    {
        if (s == "Always")       return CompareFunction::Always;
        if (s == "Never")        return CompareFunction::Never;
        if (s == "Less")         return CompareFunction::Less;
        if (s == "LessEqual")    return CompareFunction::LessEqual;
        if (s == "Equal")        return CompareFunction::Equal;
        if (s == "GreaterEqual") return CompareFunction::GreaterEqual;
        if (s == "Greater")      return CompareFunction::Greater;
        if (s == "NotEqual")     return CompareFunction::NotEqual;
        throw std::runtime_error("Scene: unknown compare function '" + s + "'");
    }

    VertexPositionColor ParseColorVertex(const std::string& s)
    {
        const auto p = Split(s, ',');
        const Vector3 pos(std::stof(p[0]), std::stof(p[1]), std::stof(p[2]));
        const Color color(static_cast<std::uint8_t>(std::stoi(p[3])),
                          static_cast<std::uint8_t>(std::stoi(p[4])),
                          static_cast<std::uint8_t>(std::stoi(p[5])),
                          static_cast<std::uint8_t>(std::stoi(p[6])));
        return VertexPositionColor(pos, color);
    }

    VertexPositionTexture ParseTextureVertex(const std::string& s)
    {
        const auto p = Split(s, ',');
        const Vector3 pos(std::stof(p[0]), std::stof(p[1]), std::stof(p[2]));
        const Vector2 uv(std::stof(p[3]), std::stof(p[4]));
        return VertexPositionTexture(pos, uv);
    }

    VertexPositionNormalTexture ParseNormalTextureVertex(const std::string& s)
    {
        const auto p = Split(s, ',');
        const Vector3 pos(std::stof(p[0]), std::stof(p[1]), std::stof(p[2]));
        const Vector3 normal(std::stof(p[3]), std::stof(p[4]), std::stof(p[5]));
        const Vector2 uv(std::stof(p[6]), std::stof(p[7]));
        return VertexPositionNormalTexture(pos, normal, uv);
    }

    VertexPositionDualTexture ParseDualTextureVertex(const std::string& s)
    {
        const auto p = Split(s, ',');
        VertexPositionDualTexture v;
        v.Position = Vector3(std::stof(p[0]), std::stof(p[1]), std::stof(p[2]));
        v.TextureCoordinate0 = Vector2(std::stof(p[3]), std::stof(p[4]));
        v.TextureCoordinate1 = Vector2(std::stof(p[5]), std::stof(p[6]));
        return v;
    }

    VertexPositionNormalTextureWeights ParseSkinnedVertex(const std::string& s)
    {
        const auto p = Split(s, ',');
        VertexPositionNormalTextureWeights v{};
        v.Position = Vector3(std::stof(p[0]), std::stof(p[1]), std::stof(p[2]));
        v.Normal = Vector3(std::stof(p[3]), std::stof(p[4]), std::stof(p[5]));
        v.TextureCoordinate = Vector2(std::stof(p[6]), std::stof(p[7]));
        v.BlendIndex0 = static_cast<std::uint8_t>(std::stoi(p[8]));
        float weight0 = std::stof(p[9]);
        // A 2nd/3rd/4th (boneindex,boneweight) pair is optional -- present only for scenes that
        // genuinely exercise WeightsPerVertex=2/4 blending (e.g. skinned_twobone_quad.scene,
        // skinned_fourbone_quad.scene); single-bone scenes (e.g. skinned_quad.scene) keep the
        // original 10-column format.
        std::uint8_t index1 = 0, index2 = 0, index3 = 0;
        float weight1 = 0.0f, weight2 = 0.0f, weight3 = 0.0f;
        if (p.size() >= 12)
        {
            index1 = static_cast<std::uint8_t>(std::stoi(p[10]));
            weight1 = std::stof(p[11]);
        }
        if (p.size() >= 16)
        {
            index2 = static_cast<std::uint8_t>(std::stoi(p[12]));
            weight2 = std::stof(p[13]);
            index3 = static_cast<std::uint8_t>(std::stoi(p[14]));
            weight3 = std::stof(p[15]);
        }
        v.BlendIndex1 = index1;
        v.BlendIndex2 = index2;
        v.BlendIndex3 = index3;
        v.BlendWeight = Vector4(weight0, weight1, weight2, weight3);
        return v;
    }

    Scene LoadScene(const std::string& path)
    {
        std::ifstream file(path);
        if (!file) throw std::runtime_error("Scene: could not open " + path);

        Scene scene;
        std::string rawLine;
        while (std::getline(file, rawLine))
        {
            const std::string line = Trim(rawLine);
            if (line.empty() || line[0] == '#') continue;

            const std::size_t eq = line.find('=');
            if (eq == std::string::npos) continue;
            const std::string key = Trim(line.substr(0, eq));
            const std::string value = Trim(line.substr(eq + 1));

            if (key == "width") scene.width = std::stoi(value);
            else if (key == "height") scene.height = std::stoi(value);
            else if (key == "profile") scene.profile = value == "Reach" ? GraphicsProfile::Reach : GraphicsProfile::HiDef;
            else if (key == "clearcolor") scene.clearColor = ParseColor(value);
            else if (key == "spritebatchmode") scene.spriteBatchMode = ParseBool(value);
            else if (key == "spritedestrect") scene.spriteDestRect = ParseRectangle(value);
            else if (key == "spritecolor") scene.spriteColor = ParseColor(value);
            else if (key == "spriterotation") scene.spriteRotation = std::stof(value);
            else if (key == "spriteorigin")
            {
                const auto p = Split(value, ',');
                scene.spriteOrigin = Vector2(std::stof(p[0]), std::stof(p[1]));
            }
            else if (key == "spriteeffects")
            {
                if (value == "FlipHorizontally") scene.spriteEffects = SpriteEffects::FlipHorizontally;
                else if (value == "FlipVertically") scene.spriteEffects = SpriteEffects::FlipVertically;
                else scene.spriteEffects = SpriteEffects::None;
            }
            else if (key == "spritesourcerect")
            {
                scene.spriteSourceRectSet = true;
                scene.spriteSourceRect = ParseRectangle(value);
            }
            else if (key == "spritesampler") scene.spriteSampler = value;
            else if (key == "spritesortmode")
            {
                if (value == "Immediate") scene.spriteSortMode = SpriteSortMode::Immediate;
                else if (value == "Texture") scene.spriteSortMode = SpriteSortMode::Texture;
                else if (value == "BackToFront") scene.spriteSortMode = SpriteSortMode::BackToFront;
                else if (value == "FrontToBack") scene.spriteSortMode = SpriteSortMode::FrontToBack;
                else scene.spriteSortMode = SpriteSortMode::Deferred;
            }
            else if (key == "spritedraw")
            {
                // x,y,w,h,r,g,b,a,depth[,textureIndex]
                const auto p = Split(value, ',');
                SpriteDrawEntry entry;
                entry.destRect = Rectangle(std::stoi(p[0]), std::stoi(p[1]), std::stoi(p[2]), std::stoi(p[3]));
                entry.color = Color(static_cast<std::uint8_t>(std::stoi(p[4])), static_cast<std::uint8_t>(std::stoi(p[5])),
                                     static_cast<std::uint8_t>(std::stoi(p[6])), static_cast<std::uint8_t>(std::stoi(p[7])));
                entry.depth = std::stof(p[8]);
                entry.textureIndex = p.size() > 9 ? std::stoi(p[9]) : 0;
                scene.spriteDraws.push_back(entry);
            }
            else if (key == "effect")
            {
                if (value == "AlphaTestEffect") scene.effectType = SceneEffectType::AlphaTestEffect;
                else if (value == "DualTextureEffect") scene.effectType = SceneEffectType::DualTextureEffect;
                else if (value == "EnvironmentMapEffect") scene.effectType = SceneEffectType::EnvironmentMapEffect;
                else if (value == "SkinnedEffect") scene.effectType = SceneEffectType::SkinnedEffect;
                else scene.effectType = SceneEffectType::BasicEffect;
            }
            else if (key == "environmentmap") scene.environmentMapEnabled = ParseBool(value);
            else if (key == "environmentmapsize") scene.environmentMapSize = std::stoi(value);
            else if (key == "environmentmapamount") scene.environmentMapAmount = std::stof(value);
            else if (key == "fresnelfactor") scene.fresnelFactor = std::stof(value);
            else if (key == "environmentmapspecular") scene.environmentMapSpecular = ParseVector3(value);
            else if (key == "environmentmappixel") scene.environmentMapPixel = ParseColor(value);
            else if (key == "alphafunction") scene.alphaFunction = ParseCompareFunction(value);
            else if (key == "referencealpha") scene.referenceAlpha = std::stoi(value);
            else if (key == "fogenabled") scene.fogEnabled = ParseBool(value);
            else if (key == "fogcolor") scene.fogColor = ParseVector3(value);
            else if (key == "fogstart") scene.fogStart = std::stof(value);
            else if (key == "fogend") scene.fogEnd = std::stof(value);
            else if (key == "weightspervertex") scene.weightsPerVertex = std::stoi(value);
            else if (key == "cullmode")
            {
                if (value == "None") scene.cullMode = CullMode::None;
                else if (value == "CullClockwiseFace") scene.cullMode = CullMode::CullClockwiseFace;
                else scene.cullMode = CullMode::CullCounterClockwiseFace;
            }
            else if (key == "depthbias") scene.depthBias = std::stof(value);
            else if (key == "slopescaledepthbias") scene.slopeScaleDepthBias = std::stof(value);
            else if (key == "bone1translate") scene.bone1Translate = ParseVector3(value);
            else if (key == "bone2translate") scene.bone2Translate = ParseVector3(value);
            else if (key == "bone3translate") scene.bone3Translate = ParseVector3(value);
            else if (key == "vertexformat")
            {
                if (value == "PositionTexture") scene.vertexFormat = SceneVertexFormat::PositionTexture;
                else if (value == "PositionNormalTexture") scene.vertexFormat = SceneVertexFormat::PositionNormalTexture;
                else if (value == "PositionDualTexture") scene.vertexFormat = SceneVertexFormat::PositionDualTexture;
                else if (value == "PositionNormalTextureWeights") scene.vertexFormat = SceneVertexFormat::PositionNormalTextureWeights;
                else scene.vertexFormat = SceneVertexFormat::PositionColor;
            }
            else if (key == "vertexcolor") scene.vertexColorEnabled = ParseBool(value);
            else if (key == "lighting") scene.lightingEnabled = ParseBool(value);
            else if (key == "preferpixellighting") scene.preferPerPixelLighting = ParseBool(value);
            else if (key == "texture") scene.textureEnabled = ParseBool(value);
            else if (key == "texturewidth") scene.textureWidth = std::stoi(value);
            else if (key == "textureheight") scene.textureHeight = std::stoi(value);
            else if (key == "texturefilter") scene.texturePointFilter = value == "Point";
            else if (key == "texturepixel") scene.texturePixels.push_back(ParseColor(value));
            else if (key == "texture2") scene.texture2Enabled = ParseBool(value);
            else if (key == "texture2width") scene.texture2Width = std::stoi(value);
            else if (key == "texture2height") scene.texture2Height = std::stoi(value);
            else if (key == "texture2pixel") scene.texture2Pixels.push_back(ParseColor(value));
            else if (key == "diffusecolor") scene.diffuseColor = ParseVector3(value);
            else if (key == "ambientcolor") scene.ambientColor = ParseVector3(value);
            else if (key == "light0enabled") scene.light0.enabled = ParseBool(value);
            else if (key == "light0diffuse") scene.light0.diffuse = ParseVector3(value);
            else if (key == "light0direction") scene.light0.direction = ParseVector3(value);
            else if (key == "light1enabled") scene.light1.enabled = ParseBool(value);
            else if (key == "light1diffuse") scene.light1.diffuse = ParseVector3(value);
            else if (key == "light1direction") scene.light1.direction = ParseVector3(value);
            else if (key == "light2enabled") scene.light2.enabled = ParseBool(value);
            else if (key == "light2diffuse") scene.light2.diffuse = ParseVector3(value);
            else if (key == "light2direction") scene.light2.direction = ParseVector3(value);
            else if (key == "primitive") scene.primitive = ParsePrimitive(value);
            else if (key == "vertex")
            {
                if (scene.vertexFormat == SceneVertexFormat::PositionNormalTextureWeights)
                    scene.skinnedVertices.push_back(ParseSkinnedVertex(value));
                else if (scene.vertexFormat == SceneVertexFormat::PositionDualTexture)
                    scene.dualTextureVertices.push_back(ParseDualTextureVertex(value));
                else if (scene.vertexFormat == SceneVertexFormat::PositionNormalTexture)
                    scene.normalTextureVertices.push_back(ParseNormalTextureVertex(value));
                else if (scene.vertexFormat == SceneVertexFormat::PositionTexture)
                    scene.textureVertices.push_back(ParseTextureVertex(value));
                else
                    scene.colorVertices.push_back(ParseColorVertex(value));
            }
            else throw std::runtime_error("Scene: unknown key '" + key + "' in " + path);
        }
        return scene;
    }
}

class CnaOracleRenderGame : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    Scene scene_;
    std::string outputPath_;
    int frame_ = 0;

protected:
    void Draw(const GameTime&) override
    {
        // Give the swap chain one frame to settle -- same reason every other D3D9 CTest in this
        // project skips frame 0 (see plans/plan_dx9.md D9-64's own UpdatePresentationFormatEXT() finding).
        if (frame_++ < 1) return;

        auto& dev = getGraphicsDeviceProperty();

        dev.Clear(scene_.clearColor);

        if (scene_.spriteBatchMode)
        {
            // D9-90..93: the real public SpriteBatch/Texture2D API, not the raw
            // ISpriteBatchRenderer interface -- matches D9-93's own explicit requirement.
            Texture2D spriteTexture(dev, scene_.textureWidth, scene_.textureHeight);
            spriteTexture.SetData(scene_.texturePixels.data(), static_cast<int>(scene_.texturePixels.size()));

            // D9-90's own "known, explicitly-scoped-out gap" (multi-texture batching / a genuine
            // FlushBatch()-on-texture-change mid-batch): a second, genuinely distinct Texture2D
            // object, reusing the scene format's existing DualTextureEffect texture2* keys.
            // spritedraw='s own optional trailing textureIndex=1 selects this instead of
            // spriteTexture -- a real GPU texture-object identity change, not just a different
            // tint color, so a stale-binding bug after FlushBatch() would be visibly wrong.
            std::unique_ptr<Texture2D> spriteTexture2;
            if (scene_.texture2Enabled)
            {
                spriteTexture2 = std::make_unique<Texture2D>(dev, scene_.texture2Width, scene_.texture2Height);
                spriteTexture2->SetData(scene_.texture2Pixels.data(), static_cast<int>(scene_.texture2Pixels.size()));
            }

            SamplerState sampler = SamplerState::LinearClamp;
            if (scene_.spriteSampler == "PointClamp") sampler = SamplerState::PointClamp;
            else if (scene_.spriteSampler == "PointWrap") sampler = SamplerState::PointWrap;
            else if (scene_.spriteSampler == "LinearWrap") sampler = SamplerState::LinearWrap;
            else if (scene_.spriteSampler == "PointMirror")
            {
                sampler = SamplerState();
                sampler.setFilterProperty(TextureFilter::Point);
                sampler.setAddressUProperty(TextureAddressMode::Mirror);
                sampler.setAddressVProperty(TextureAddressMode::Mirror);
            }

            std::optional<Rectangle> sourceRect = std::nullopt;
            if (scene_.spriteSourceRectSet) sourceRect = scene_.spriteSourceRect;

            SpriteBatch spriteBatch(dev);
            if (!scene_.spriteDraws.empty())
            {
                // D9-93: BlendState::AlphaBlend expects premultiplied colors (SourceBlend=One) --
                // the raw non-premultiplied tint colors used to exercise SpriteSortMode need
                // NonPremultiplied instead, or the blend math would be wrong regardless of order.
                spriteBatch.Begin(scene_.spriteSortMode, BlendState::NonPremultiplied, &sampler, nullptr, nullptr);
                for (const SpriteDrawEntry& entry : scene_.spriteDraws)
                {
                    const Texture2D& tex = (entry.textureIndex == 1 && spriteTexture2)
                        ? *spriteTexture2 : spriteTexture;
                    spriteBatch.Draw(tex, entry.destRect, std::nullopt, entry.color,
                                     0.0f, Vector2(0, 0), SpriteEffects::None, entry.depth);
                }
            }
            else
            {
                spriteBatch.Begin(SpriteSortMode::Deferred, BlendState::AlphaBlend, &sampler, nullptr, nullptr);
                spriteBatch.Draw(spriteTexture, scene_.spriteDestRect, sourceRect, scene_.spriteColor,
                                 scene_.spriteRotation, scene_.spriteOrigin, scene_.spriteEffects, 0.0f);
            }
            spriteBatch.End();

            const int pixelCount = scene_.width * scene_.height;
            std::vector<Color> pixels(static_cast<std::size_t>(pixelCount), Color(0, 0, 0, 0));
            dev.GetBackBufferData(pixels.data(), pixelCount);

            Texture2D out(dev, scene_.width, scene_.height);
            out.SetData(pixels.data(), pixelCount);
            out.SaveAsPng(outputPath_);

            std::printf("CNA-XNA-ORACLE-OK renderer=%s out=%s\n", OracleRendererName(), outputPath_.c_str());
            Exit();
            return;
        }

        std::unique_ptr<Texture2D> texture;
        if (scene_.textureEnabled)
        {
            texture = std::make_unique<Texture2D>(dev, scene_.textureWidth, scene_.textureHeight);
            texture->SetData(scene_.texturePixels.data(), static_cast<int>(scene_.texturePixels.size()));
            dev.getSamplerStatesProperty()[0] =
                scene_.texturePointFilter ? SamplerState::PointClamp : SamplerState::LinearClamp;
        }
        std::unique_ptr<Texture2D> texture2;
        if (scene_.texture2Enabled)
        {
            texture2 = std::make_unique<Texture2D>(dev, scene_.texture2Width, scene_.texture2Height);
            texture2->SetData(scene_.texture2Pixels.data(), static_cast<int>(scene_.texture2Pixels.size()));
        }
        std::unique_ptr<TextureCube> environmentMap;
        if (scene_.environmentMapEnabled)
        {
            environmentMap = std::make_unique<TextureCube>(dev, scene_.environmentMapSize, false, SurfaceFormat::Color);
            const std::vector<Color> faceData(
                static_cast<std::size_t>(scene_.environmentMapSize) * scene_.environmentMapSize,
                scene_.environmentMapPixel);
            static constexpr CubeMapFace kFaces[] = {
                CubeMapFace::PositiveX, CubeMapFace::NegativeX,
                CubeMapFace::PositiveY, CubeMapFace::NegativeY,
                CubeMapFace::PositiveZ, CubeMapFace::NegativeZ,
            };
            for (CubeMapFace face : kFaces)
                environmentMap->SetData(face, faceData.data(), static_cast<int>(faceData.size()));
        }

        // The chosen Effect must outlive this scope: GraphicsDevice::DrawUserPrimitives() below
        // reads GpuDrawParams from currentEffect_, a RAW pointer Effect::Apply() sets -- an Effect
        // scoped only to this if/else block would be destroyed before that read, leaving a
        // dangling pointer (a real bug found live: DrawUserPrimitives reported completely wrong
        // vertexColor/texture flags -- stale stack memory, not the values actually just set).
        std::unique_ptr<AlphaTestEffect> alphaFx;
        std::unique_ptr<DualTextureEffect> dualFx;
        std::unique_ptr<EnvironmentMapEffect> envMapFx;
        std::unique_ptr<SkinnedEffect> skinnedFx;
        std::unique_ptr<BasicEffect> basicFx;

        if (scene_.effectType == SceneEffectType::AlphaTestEffect)
        {
            alphaFx = std::make_unique<AlphaTestEffect>(dev);
            alphaFx->setVertexColorEnabledProperty(scene_.vertexColorEnabled);
            alphaFx->setTextureProperty(texture.get());
            alphaFx->setAlphaFunctionProperty(scene_.alphaFunction);
            alphaFx->setReferenceAlphaProperty(scene_.referenceAlpha);
            alphaFx->setWorldProperty(Matrix::getIdentityProperty());
            alphaFx->setViewProperty(Matrix::getIdentityProperty());
            alphaFx->setProjectionProperty(Matrix::getIdentityProperty());
            alphaFx->setFogEnabledProperty(scene_.fogEnabled);
            alphaFx->setFogColorProperty(scene_.fogColor);
            alphaFx->setFogStartProperty(scene_.fogStart);
            alphaFx->setFogEndProperty(scene_.fogEnd);
            alphaFx->Apply();
        }
        else if (scene_.effectType == SceneEffectType::DualTextureEffect)
        {
            dualFx = std::make_unique<DualTextureEffect>(dev);
            dualFx->setVertexColorEnabledProperty(scene_.vertexColorEnabled);
            dualFx->setTextureProperty(texture.get());
            dualFx->setTexture2Property(texture2.get());
            dualFx->setDiffuseColorProperty(scene_.diffuseColor);
            dualFx->setWorldProperty(Matrix::getIdentityProperty());
            dualFx->setViewProperty(Matrix::getIdentityProperty());
            dualFx->setProjectionProperty(Matrix::getIdentityProperty());
            dualFx->setFogEnabledProperty(scene_.fogEnabled);
            dualFx->setFogColorProperty(scene_.fogColor);
            dualFx->setFogStartProperty(scene_.fogStart);
            dualFx->setFogEndProperty(scene_.fogEnd);
            dualFx->Apply();
        }
        else if (scene_.effectType == SceneEffectType::EnvironmentMapEffect)
        {
            envMapFx = std::make_unique<EnvironmentMapEffect>(dev);
            envMapFx->setTextureProperty(texture.get());
            envMapFx->setEnvironmentMapProperty(environmentMap.get());
            envMapFx->setEnvironmentMapAmountProperty(scene_.environmentMapAmount);
            envMapFx->setFresnelFactorProperty(scene_.fresnelFactor);
            envMapFx->setEnvironmentMapSpecularProperty(scene_.environmentMapSpecular);
            envMapFx->setWorldProperty(Matrix::getIdentityProperty());
            envMapFx->setViewProperty(Matrix::getIdentityProperty());
            envMapFx->setProjectionProperty(Matrix::getIdentityProperty());
            // Real XNA/FNA's EnvironmentMapEffect implements IEffectLights.LightingEnabled via
            // EXPLICIT interface implementation, hidden from the concrete type's public C# surface
            // (a real CS1061 found live on the Oracle.cs side) -- lighting is always on for this
            // effect there, and the setter throws given false anyway. CNA's own equivalent
            // (setLightingEnabledProperty) is directly callable since C++ has no explicit-interface-
            // implementation hiding, but skipped here to match what a real game can actually do.
            if (scene_.lightingEnabled)
            {
                envMapFx->setAmbientLightColorProperty(scene_.ambientColor);
                envMapFx->DirectionalLight0.setEnabledProperty(scene_.light0.enabled);
                envMapFx->DirectionalLight0.setDiffuseColorProperty(scene_.light0.diffuse);
                envMapFx->DirectionalLight0.setDirectionProperty(scene_.light0.direction);
                envMapFx->DirectionalLight1.setEnabledProperty(scene_.light1.enabled);
                envMapFx->DirectionalLight1.setDiffuseColorProperty(scene_.light1.diffuse);
                envMapFx->DirectionalLight1.setDirectionProperty(scene_.light1.direction);
                envMapFx->DirectionalLight2.setEnabledProperty(scene_.light2.enabled);
                envMapFx->DirectionalLight2.setDiffuseColorProperty(scene_.light2.diffuse);
                envMapFx->DirectionalLight2.setDirectionProperty(scene_.light2.direction);
            }
            envMapFx->setFogEnabledProperty(scene_.fogEnabled);
            envMapFx->setFogColorProperty(scene_.fogColor);
            envMapFx->setFogStartProperty(scene_.fogStart);
            envMapFx->setFogEndProperty(scene_.fogEnd);
            envMapFx->Apply();
        }
        else if (scene_.effectType == SceneEffectType::SkinnedEffect)
        {
            skinnedFx = std::make_unique<SkinnedEffect>(dev);
            skinnedFx->setTextureProperty(texture.get());
            skinnedFx->setWorldProperty(Matrix::getIdentityProperty());
            skinnedFx->setViewProperty(Matrix::getIdentityProperty());
            skinnedFx->setProjectionProperty(Matrix::getIdentityProperty());
            // Bone 0 is always Identity; Bones 1-3 are optional pure-translation bones, scene-
            // configurable via bone1translate=/bone2translate=/bone3translate= (each defaults to
            // (0,0,0), i.e. also Identity, so single-bone scenes like skinned_quad.scene are
            // unaffected). WeightsPerVertex defaults to 4, matching real XNA's own SkinnedEffect
            // default -- skinned_quad.scene's own "single Identity bone at 100% weight" no-op
            // already relies on this default (weights[1..3]=0 makes the extra bones structurally
            // inert regardless of which ShaderIndex bucket -- OneBone/TwoBones/FourBones -- is
            // actually selected).
            skinnedFx->setWeightsPerVertexProperty(scene_.weightsPerVertex);
            skinnedFx->setPreferPerPixelLightingProperty(scene_.preferPerPixelLighting);
            skinnedFx->SetBoneTransforms(std::vector<Matrix>{
                Matrix::getIdentityProperty(),
                Matrix::CreateTranslation(scene_.bone1Translate),
                Matrix::CreateTranslation(scene_.bone2Translate),
                Matrix::CreateTranslation(scene_.bone3Translate)});
            // Same explicit-interface-implementation LightingEnabled carve-out as
            // EnvironmentMapEffect above (confirmed against FNA's own SkinnedEffect.cs source too).
            if (scene_.lightingEnabled)
            {
                skinnedFx->setAmbientLightColorProperty(scene_.ambientColor);
                skinnedFx->DirectionalLight0.setEnabledProperty(scene_.light0.enabled);
                skinnedFx->DirectionalLight0.setDiffuseColorProperty(scene_.light0.diffuse);
                skinnedFx->DirectionalLight0.setDirectionProperty(scene_.light0.direction);
                skinnedFx->DirectionalLight1.setEnabledProperty(scene_.light1.enabled);
                skinnedFx->DirectionalLight1.setDiffuseColorProperty(scene_.light1.diffuse);
                skinnedFx->DirectionalLight1.setDirectionProperty(scene_.light1.direction);
                skinnedFx->DirectionalLight2.setEnabledProperty(scene_.light2.enabled);
                skinnedFx->DirectionalLight2.setDiffuseColorProperty(scene_.light2.diffuse);
                skinnedFx->DirectionalLight2.setDirectionProperty(scene_.light2.direction);
            }
            skinnedFx->setFogEnabledProperty(scene_.fogEnabled);
            skinnedFx->setFogColorProperty(scene_.fogColor);
            skinnedFx->setFogStartProperty(scene_.fogStart);
            skinnedFx->setFogEndProperty(scene_.fogEnd);
            skinnedFx->Apply();
        }
        else
        {
            basicFx = std::make_unique<BasicEffect>(dev);
            basicFx->VertexColorEnabled = scene_.vertexColorEnabled;
            basicFx->setLightingEnabledProperty(scene_.lightingEnabled);
            basicFx->setPreferPerPixelLightingProperty(scene_.preferPerPixelLighting);
            basicFx->setTextureEnabledProperty(scene_.textureEnabled);
            basicFx->World = Matrix::getIdentityProperty();
            basicFx->View = Matrix::getIdentityProperty();
            basicFx->Projection = Matrix::getIdentityProperty();
            if (scene_.lightingEnabled)
            {
                basicFx->setAmbientLightColorProperty(scene_.ambientColor);
                basicFx->DirectionalLight0.setEnabledProperty(scene_.light0.enabled);
                basicFx->DirectionalLight0.setDiffuseColorProperty(scene_.light0.diffuse);
                basicFx->DirectionalLight0.setDirectionProperty(scene_.light0.direction);
                basicFx->DirectionalLight1.setEnabledProperty(scene_.light1.enabled);
                basicFx->DirectionalLight1.setDiffuseColorProperty(scene_.light1.diffuse);
                basicFx->DirectionalLight1.setDirectionProperty(scene_.light1.direction);
                basicFx->DirectionalLight2.setEnabledProperty(scene_.light2.enabled);
                basicFx->DirectionalLight2.setDiffuseColorProperty(scene_.light2.diffuse);
                basicFx->DirectionalLight2.setDirectionProperty(scene_.light2.direction);
            }
            if (scene_.textureEnabled) basicFx->setTextureProperty(texture.get());
            basicFx->setFogEnabledProperty(scene_.fogEnabled);
            basicFx->setFogColorProperty(scene_.fogColor);
            basicFx->setFogStartProperty(scene_.fogStart);
            basicFx->setFogEndProperty(scene_.fogEnd);
            basicFx->Apply();
        }

        // D9-21/D9-62: real RasterizerState.CullMode/DepthBias/SlopeScaleDepthBias, applied to
        // this scene's one 3D draw. Defaults (CullCounterClockwiseFace, 0, 0) match
        // RasterizerState's own documented XNA defaults, so every pre-existing scene is unaffected.
        RasterizerState rs;
        rs.setCullModeProperty(scene_.cullMode);
        rs.setDepthBiasProperty(scene_.depthBias);
        rs.setSlopeScaleDepthBiasProperty(scene_.slopeScaleDepthBias);
        dev.setRasterizerStateProperty(rs);

        if (scene_.vertexFormat == SceneVertexFormat::PositionNormalTextureWeights)
            dev.DrawUserPrimitives(scene_.primitive, scene_.skinnedVertices.data(), 0,
                                   scene_.PrimitiveCount(), SkinnedVertexDeclaration());
        else if (scene_.vertexFormat == SceneVertexFormat::PositionDualTexture)
            dev.DrawUserPrimitives(scene_.primitive, scene_.dualTextureVertices.data(), 0,
                                   scene_.PrimitiveCount(), DualTextureVertexDeclaration());
        else if (scene_.vertexFormat == SceneVertexFormat::PositionNormalTexture)
            dev.DrawUserPrimitives(scene_.primitive, scene_.normalTextureVertices.data(), 0, scene_.PrimitiveCount());
        else if (scene_.vertexFormat == SceneVertexFormat::PositionTexture)
            dev.DrawUserPrimitives(scene_.primitive, scene_.textureVertices.data(), 0, scene_.PrimitiveCount());
        else
            dev.DrawUserPrimitives(scene_.primitive, scene_.colorVertices.data(), 0, scene_.PrimitiveCount());

        const int pixelCount = scene_.width * scene_.height;
        std::vector<Color> pixels(static_cast<std::size_t>(pixelCount), Color(0, 0, 0, 0));
        dev.GetBackBufferData(pixels.data(), pixelCount);

        Texture2D out(dev, scene_.width, scene_.height);
        out.SetData(pixels.data(), pixelCount);
        out.SaveAsPng(outputPath_);

        std::printf("CNA-XNA-ORACLE-OK renderer=%s out=%s\n", OracleRendererName(), outputPath_.c_str());
        Exit();
    }

public:
    CnaOracleRenderGame(Scene scene, std::string outputPath)
        : scene_(std::move(scene)), outputPath_(std::move(outputPath))
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(scene_.width);
        gdm_->setPreferredBackBufferHeightProperty(scene_.height);
        gdm_->setGraphicsProfileProperty(scene_.profile);
    }
};

int main(int argc, char** argv)
{
    if (argc != 3)
    {
        std::fprintf(stderr, "Usage: %s <scene-file> <output-png>\n", argv[0]);
        return 1;
    }

    try
    {
        Scene scene = LoadScene(argv[1]);
        CnaOracleRenderGame game(scene, argv[2]);
        game.Run();
    }
    catch (const std::exception& e)
    {
        std::fprintf(stderr, "CNA-XNA-ORACLE-FAIL: %s\n", e.what());
        return 1;
    }
    return 0;
}
