#ifndef CNA_GAME_H
#define CNA_GAME_H
#include "Content/ContentManager.h"
#include "NeoSdk/Property.h"

namespace Microsoft::Xna::Framework {
    namespace Graphics {
        class SpriteBatch;
        class GraphicsDevice;
    }

    class Game {
    public:
        NeoSdk::Property<Content::ContentManager> Content;
        NeoSdk::Property<Graphics::GraphicsDevice> GraphicsDevice;

        Game();
        virtual ~Game();

        void Run();
        void Exit() { isRunning = false; }

    protected:
        virtual void Initialize();
        virtual void LoadContent();
        virtual void Update(float deltaTime);
        virtual void Draw();

        Graphics::GraphicsDevice* graphicsDevice;
        Graphics::SpriteBatch* spriteBatch;

    private:
        bool isRunning;
        Content::ContentManager content;

    };
}

#endif // CNA_GAME_H
