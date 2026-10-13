// SPDX-License-Identifier: MS-PL
// FULLSCREEN-001: logical SpriteBatch projection must survive physical
// presentation changes.
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/Viewport.hpp"
#include "common/PixelTestGame.hpp"

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using CNA::Internal::Renderers::CnaPresentationMode;

class SpriteBatchPresentationTest final : public CNA::Examples::PixelTestGame {
  std::unique_ptr<GraphicsDeviceManager> manager_;
  void ExpectPhysicalPixel(const char *label, const Rectangle &rect,
                           const Color &expected) {
    std::uint8_t pixel[4]{};
    getGraphicsDeviceProperty().GetRenderer().ReadBackbuffer(rect.X, rect.Y, 1,
                                                             1, pixel);
    std::printf("pixel=(%u,%u,%u) ", pixel[0], pixel[1], pixel[2]);
    Check(pixel[0] == expected.getRProperty() &&
              pixel[1] == expected.getGProperty() &&
              pixel[2] == expected.getBProperty(),
          label);
  }

  void Fill(SpriteBatch &batch, Texture2D &texture, int width, int height,
            SpriteSortMode mode = SpriteSortMode::Deferred,
            Matrix transform = Matrix::getIdentityProperty()) {
    batch.Begin(mode, BlendState::Opaque, &SamplerState::PointClamp, nullptr,
                nullptr, nullptr, transform);
    batch.Draw(texture, Rectangle(0, 0, width, height), Color::White);
    batch.End();
  }

public:
  SpriteBatchPresentationTest()
      : manager_(std::make_unique<GraphicsDeviceManager>(this)) {
    // Physical aspect deliberately differs from the logical square: default
    // Letterbox is a subrectangle even though the game never assigned a custom
    // Viewport.
    manager_->setPreferredBackBufferWidthProperty(64);
    manager_->setPreferredBackBufferHeightProperty(64);
    manager_->setGraphicsProfileProperty(GraphicsProfile::HiDef);
  }

  void RunTest() override {
    auto &dev = getGraphicsDeviceProperty();
    getWindowProperty().EndScreenDeviceChange("", 160, 96);
    dev.Present();
    Texture2D red(dev, 1, 1), green(dev, 1, 1);
    const Color r(255, 0, 0, 255), g(0, 255, 0, 255);
    red.SetData(&r, 1);
    green.SetData(&g, 1);
    SpriteBatch batch(dev);

    dev.Clear(Color::Black);
    Fill(batch, red, 64, 64);
    ExpectPhysicalPixel("default Letterbox scales to the far edge",
                        Rectangle(120, 80, 1, 1), r);
    ExpectPhysicalPixel("left letterbox bar stays clear",
                        Rectangle(10, 40, 1, 1), Color::Black);

    dev.Clear(Color::Black);
    dev.setViewportProperty(Viewport(8, 8, 32, 32));
    Fill(batch, red, 64, 64);
    ExpectPhysicalPixel("custom logical viewport scales its local coordinates",
                        Rectangle(86, 52, 1, 1), r);
    ExpectPhysicalPixel("custom viewport keeps its physical offset",
                        Rectangle(40, 8, 1, 1), Color::Black);
    ExpectPhysicalPixel("geometry outside the custom viewport stays clipped",
                        Rectangle(100, 50, 1, 1), Color::Black);

    dev.Clear(Color::Black);
    Fill(batch, red, 8, 8, SpriteSortMode::Immediate,
         Matrix::CreateTranslation(4, 2, 0));
    ExpectPhysicalPixel("user transform is composed before presentation",
                        Rectangle(55, 17, 1, 1), r);
    ExpectPhysicalPixel("user translation leaves the origin clear",
                        Rectangle(45, 13, 1, 1), Color::Black);

    dev.Clear(Color::Black);
    dev.setViewportProperty(Viewport(0, 0, 16, 64));
    Fill(batch, red, 16, 64);
    dev.setViewportProperty(Viewport(16, 0, 48, 64));
    Fill(batch, green, 48, 64);
    ExpectPhysicalPixel("first deferred batch retains its projection",
                        Rectangle(52, 80, 1, 1), r);
    ExpectPhysicalPixel("second deferred batch retains its projection",
                        Rectangle(120, 80, 1, 1), g);

    RenderTarget2D target(dev, 16, 16);
    dev.SetRenderTarget(&target);
    dev.Clear(Color::Black);
    dev.setViewportProperty(Viewport(4, 4, 8, 8));
    Fill(batch, red, 8, 8);
    dev.SetRenderTarget(static_cast<RenderTarget2D *>(nullptr));
    Color pixels[256];
    target.GetData(pixels, 256);
    ExpectTrue("offscreen custom viewport reaches its far edge",
               pixels[11 * 16 + 11] == r);
    ExpectTrue("offscreen rendering does not use backbuffer scaling",
               pixels[12 * 16 + 12] == Color::Black);
    dev.SetRenderTarget(static_cast<RenderTarget2D *>(nullptr));
  }
};

int main() {
  return CNA::Examples::RunPixelTest<SpriteBatchPresentationTest>();
}
