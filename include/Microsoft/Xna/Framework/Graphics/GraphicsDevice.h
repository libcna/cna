#ifndef CNA_GRAPHICSDEVICE_H
#define CNA_GRAPHICSDEVICE_H

#include <SDL3/SDL.h>

namespace Microsoft::Xna::Framework::Graphics {

    class GraphicsDevice {
    public:
        GraphicsDevice();

        ~GraphicsDevice();

        void Clear(float r, float g, float b, float a);
        void Present();
        SDL_Renderer* GetRenderer();

    protected:

    private:
        SDL_Window* window;
        SDL_Renderer* renderer;

    //friend class SpriteBatch;
    };

}

#endif // CNA_GRAPHICSDEVICE_H
