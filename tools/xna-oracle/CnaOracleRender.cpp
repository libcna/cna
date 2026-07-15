// plan_dx9.md Phase D9-A (D9-A3): scene-driven CNA renderer -- the "CNA" half of the D9-A4 diff
// harness. Reads the SAME ".scene" file format tools/xna-oracle/Oracle.cs reads (see that file's
// own header comment) and renders it through CNA's real public Game/GraphicsDeviceManager/
// GraphicsDevice/BasicEffect API on whichever backend this binary was built against
// (CNA_GRAPHICS_BACKEND=D3D9 in this branch), saving the result as a PNG. Do not hand-transcribe
// scene data here -- every scene lives in tools/xna-oracle/scenes/*.scene, or the harness drifts.
//
// Usage: cna_oracle_render.exe <scene-file> <output-png>
//
// Deliberately renders straight to the back buffer rather than mirroring Oracle.cs's own
// RenderTarget2D indirection: RenderTarget2D::GetData()'s own CPU readback path is not proven on
// this backend yet (every existing render-target test reads back via GetBackBufferData() after
// blitting, not RenderTarget2D::GetData() directly), while GetBackBufferData() is exercised by
// every D3D9 CTest already. The back buffer is the same size as the scene and is never presented
// before being read, so the two approaches are pixel-equivalent for what this tool needs to prove.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/DirectionalLight.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerStateCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTexture.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionTexture.hpp"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
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

    enum class SceneVertexFormat { PositionColor, PositionTexture, PositionNormalTexture };

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
        SceneVertexFormat vertexFormat = SceneVertexFormat::PositionColor;
        bool vertexColorEnabled = false;
        bool lightingEnabled = false;
        bool textureEnabled = false;
        int textureWidth = 0;
        int textureHeight = 0;
        bool texturePointFilter = true;
        Vector3 ambientColor{0, 0, 0};
        SceneLight light0;
        PrimitiveType primitive = PrimitiveType::TriangleList;
        std::vector<VertexPositionColor> colorVertices;
        std::vector<VertexPositionTexture> textureVertices;
        std::vector<VertexPositionNormalTexture> normalTextureVertices;
        std::vector<Color> texturePixels;

        int VertexCount() const
        {
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

    PrimitiveType ParsePrimitive(const std::string& s)
    {
        if (s == "TriangleList")  return PrimitiveType::TriangleList;
        if (s == "TriangleStrip") return PrimitiveType::TriangleStrip;
        if (s == "LineList")      return PrimitiveType::LineList;
        if (s == "LineStrip")     return PrimitiveType::LineStrip;
        throw std::runtime_error("Scene: unknown primitive '" + s + "'");
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
            else if (key == "vertexformat")
            {
                if (value == "PositionTexture") scene.vertexFormat = SceneVertexFormat::PositionTexture;
                else if (value == "PositionNormalTexture") scene.vertexFormat = SceneVertexFormat::PositionNormalTexture;
                else scene.vertexFormat = SceneVertexFormat::PositionColor;
            }
            else if (key == "vertexcolor") scene.vertexColorEnabled = ParseBool(value);
            else if (key == "lighting") scene.lightingEnabled = ParseBool(value);
            else if (key == "texture") scene.textureEnabled = ParseBool(value);
            else if (key == "texturewidth") scene.textureWidth = std::stoi(value);
            else if (key == "textureheight") scene.textureHeight = std::stoi(value);
            else if (key == "texturefilter") scene.texturePointFilter = value == "Point";
            else if (key == "texturepixel") scene.texturePixels.push_back(ParseColor(value));
            else if (key == "ambientcolor") scene.ambientColor = ParseVector3(value);
            else if (key == "light0enabled") scene.light0.enabled = ParseBool(value);
            else if (key == "light0diffuse") scene.light0.diffuse = ParseVector3(value);
            else if (key == "light0direction") scene.light0.direction = ParseVector3(value);
            else if (key == "primitive") scene.primitive = ParsePrimitive(value);
            else if (key == "vertex")
            {
                if (scene.vertexFormat == SceneVertexFormat::PositionNormalTexture)
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
        // project skips frame 0 (see plan_dx9.md D9-64's own UpdatePresentationFormatEXT() finding).
        if (frame_++ < 1) return;

        auto& dev = getGraphicsDeviceProperty();

        dev.Clear(scene_.clearColor);

        BasicEffect fx(dev);
        fx.VertexColorEnabled = scene_.vertexColorEnabled;
        fx.setLightingEnabledProperty(scene_.lightingEnabled);
        fx.setTextureEnabledProperty(scene_.textureEnabled);
        fx.World = Matrix::getIdentityProperty();
        fx.View = Matrix::getIdentityProperty();
        fx.Projection = Matrix::getIdentityProperty();

        if (scene_.lightingEnabled)
        {
            fx.setAmbientLightColorProperty(scene_.ambientColor);
            fx.DirectionalLight0.setEnabledProperty(scene_.light0.enabled);
            fx.DirectionalLight0.setDiffuseColorProperty(scene_.light0.diffuse);
            fx.DirectionalLight0.setDirectionProperty(scene_.light0.direction);
        }

        std::unique_ptr<Texture2D> texture;
        if (scene_.textureEnabled)
        {
            texture = std::make_unique<Texture2D>(dev, scene_.textureWidth, scene_.textureHeight);
            texture->SetData(scene_.texturePixels.data(), static_cast<int>(scene_.texturePixels.size()));
            fx.setTextureProperty(texture.get());
            dev.getSamplerStatesProperty()[0] =
                scene_.texturePointFilter ? SamplerState::PointClamp : SamplerState::LinearClamp;
        }

        fx.Apply();

        if (scene_.vertexFormat == SceneVertexFormat::PositionNormalTexture)
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

        std::printf("CNA-XNA-ORACLE-OK backend=D3D9 out=%s\n", outputPath_.c_str());
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
