#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"

namespace Microsoft::Xna::Framework::Graphics {

    BasicEffect::BasicEffect(GraphicsDevice& device)
        : device_(&device) {}

    void BasicEffect::Apply() {
        if (device_) {
            device_->SetCurrentEffect(this);
        }
    }
}
