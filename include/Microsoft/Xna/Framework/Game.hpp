#pragma once
#include <any>

#include "ExitingEventArgs.hpp"
#include "GameTime.hpp"
#include "GameWindow.hpp"
#include "Content/ContentManager.hpp"
#include "Graphics/GraphicsDevice.hpp"
#include "Graphics/SpriteBatch.hpp"

#include "System/EventHandler.hpp"
#include "System/TimeSpan.hpp"
#include "System/EventArgs.hpp"

namespace Microsoft::Xna::Framework {

    class Game : public System::Object{
    public:
        DEF_PROP(Content::ContentManager, Content, getter1, setter0, member1, static0, constret0, ref1, constmet0)

        private: Graphics::GraphicsDevice GraphicsDevice_; public: [[nodiscard]] Graphics::GraphicsDevice& getGraphicsDeviceProperty();
        private: GameWindow Window_; public: [[nodiscard]] GameWindow& getWindowProperty();

        DEF_PROP(bool, IsMouseVisible, getter1, setter1, member1, static0, constret1, ref1, constmet1)

        //Default FPS is 60
    private:
        System::TimeSpan TargetElapsedTime_;

    public:
        [[nodiscard]] const System::TimeSpan& getTargetElapsedTimeProperty() const;

    public:
        void setTargetElapsedTimeProperty(const System::TimeSpan& v);
        DEF_PROP(System::TimeSpan, InactiveSleepTime, getter1, setter1, member1, static0, constret1, ref1, constmet1)

        System::EventHandler<ExitingEventArgs> Exiting;

        Game();
        virtual ~Game();

        void Run();
        void Exit() { isRunning = false; }

    protected:
        virtual void Initialize();
        virtual void LoadContent();
        virtual void UnloadContent();
        virtual void OnDeactivated(std::any sender, System::EventArgs args);
        virtual void OnActivated(std::any sender, System::EventArgs args);

        virtual void Update(Microsoft::Xna::Framework::GameTime &gameTime);
        virtual void Draw(const Microsoft::Xna::Framework::GameTime &gameTime);

    private:
        bool isRunning;
#ifdef XNA5
    public:
        [[nodiscard]] double getTargetFPSProperty() const;
        [[nodiscard]] double getTargetMsFrameTimeProperty() const;
        static double fpsToMillisecondsPerFrame(SharpRuntime::intcs framesPerSecond);
#endif
    };
}
