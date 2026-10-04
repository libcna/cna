// SPDX-License-Identifier: MS-PL

using System;
using System.Collections.Generic;
using Microsoft.Xna.Framework;
using Microsoft.Xna.Framework.Content;

sealed class LifecycleFailureContentManager : ContentManager
{
    readonly List<string> events;

    internal LifecycleFailureContentManager(IServiceProvider services, List<string> events)
        : base(services)
    {
        this.events = events;
    }

    public override void Unload()
    {
        events.Add("Content.Unload");
        base.Unload();
    }
}

sealed class LifecycleFailureOracle : Game
{
    readonly GraphicsDeviceManager graphics;
    readonly List<string> events = new List<string>();
    int unloadContentCalls;

    LifecycleFailureOracle()
    {
        graphics = new GraphicsDeviceManager(this);
        Content = new LifecycleFailureContentManager(Services, events);
        IsFixedTimeStep = false;
    }

    protected override void Update(GameTime gameTime)
    {
        throw new InvalidOperationException("update failure");
    }

    protected override void UnloadContent()
    {
        ++unloadContentCalls;
        events.Add("Game.UnloadContent");
        base.UnloadContent();
    }

    static int Main()
    {
        LifecycleFailureOracle game = new LifecycleFailureOracle();
        try
        {
            game.Run();
            Console.WriteLine("RUN_RESULT=returned");
        }
        catch (InvalidOperationException exception)
        {
            Console.WriteLine("RUN_RESULT=" + exception.Message);
        }

        game.Dispose();
        string order = String.Join(",", game.events.ToArray());
        Console.WriteLine("UNLOAD_CONTENT_CALLS=" + game.unloadContentCalls);
        Console.WriteLine("ORDER=" + order);
        return game.unloadContentCalls == 1 &&
            order == "Content.Unload,Game.UnloadContent" ? 0 : 1;
    }
}
