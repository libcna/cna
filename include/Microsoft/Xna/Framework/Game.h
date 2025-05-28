#ifndef CNA_GAME_H
#define CNA_GAME_H
#include <any>

#include "ExitingEventArgs.h"
#include "GameTime.h"
#include "Content/ContentManager.h"
#include "NeoSdk/Property.h"
#include "System/EventHandler.h"
#include "System/TimeSpan.h"
#include "System/Runtime/CompilerServices/EventArgs.h"

namespace Microsoft::Xna::Framework {
    namespace Graphics {
        class SpriteBatch;
        class GraphicsDevice;
    }

    class Game {
    public:
        NeoSdk::Property<Content::ContentManager> Content;
        NeoSdk::Property<Graphics::GraphicsDevice> GraphicsDevice;
        System::EventHandler<ExitingEventArgs> Exiting;
        DEF_PROP_AUTO(bool, IsMouseVisible, false);
        DEF_PROP_AUTO(System::TimeSpan, TargetElapsedTime, System::TimeSpan());
        DEF_PROP_AUTO(System::TimeSpan, InactiveSleepTime, System::TimeSpan());

        Game();
        virtual ~Game();

        void Run();
        void Exit() { isRunning = false; }

    protected:
        virtual void Initialize();
        virtual void LoadContent();
        virtual void UnloadContent();
        virtual void OnDeactivated(std::any sender, System::Runtime::CompilerServices::EventArgs args);
        virtual void OnActivated(std::any sender, System::Runtime::CompilerServices::EventArgs args);


        virtual void Update(Microsoft::Xna::Framework::GameTime gameTime);
        virtual void Draw(Microsoft::Xna::Framework::GameTime gameTime);

        Graphics::GraphicsDevice* graphicsDevice;
        Graphics::SpriteBatch* spriteBatch;

    private:
        bool isRunning;
        Content::ContentManager content;

    };
}

#endif // CNA_GAME_H
