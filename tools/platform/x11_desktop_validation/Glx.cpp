// SPDX-License-Identifier: MS-PL
//
// Phase 15: the platform's GLX service on real hardware, driven the way a GL renderer drives it.
// Every GL entry point comes from IPlatformGlContext::GetProcAddress -- the harness links no GL
// loader, exactly like EasyGL.

#include "Harness.hpp"

#include "CNA/Platform/PlatformException.hpp"

#include "GlTypes.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <sstream>
#include <thread>

namespace CnaX11Validation {

    namespace {

        struct Gl
        {
            GlTypes::GetString GetString = nullptr;
            GlTypes::GetIntegerv GetIntegerv = nullptr;
            GlTypes::GetError GetError = nullptr;
            GlTypes::ClearColor ClearColor = nullptr;
            GlTypes::Clear Clear = nullptr;
            GlTypes::Viewport Viewport = nullptr;
            GlTypes::ReadPixels ReadPixels = nullptr;
            GlTypes::Finish Finish = nullptr;
            PFNGLCREATESHADERPROC CreateShader = nullptr;
            PFNGLSHADERSOURCEPROC ShaderSource = nullptr;
            PFNGLCOMPILESHADERPROC CompileShader = nullptr;
            PFNGLGETSHADERIVPROC GetShaderiv = nullptr;
            PFNGLGETSHADERINFOLOGPROC GetShaderInfoLog = nullptr;
            PFNGLCREATEPROGRAMPROC CreateProgram = nullptr;
            PFNGLATTACHSHADERPROC AttachShader = nullptr;
            PFNGLLINKPROGRAMPROC LinkProgram = nullptr;
            PFNGLGETPROGRAMIVPROC GetProgramiv = nullptr;
            PFNGLUSEPROGRAMPROC UseProgram = nullptr;
            PFNGLDELETESHADERPROC DeleteShader = nullptr;
            PFNGLDELETEPROGRAMPROC DeleteProgram = nullptr;
            PFNGLGETUNIFORMLOCATIONPROC GetUniformLocation = nullptr;
            PFNGLUNIFORM4FPROC Uniform4f = nullptr;
            PFNGLGENBUFFERSPROC GenBuffers = nullptr;
            PFNGLBINDBUFFERPROC BindBuffer = nullptr;
            PFNGLBUFFERDATAPROC BufferData = nullptr;
            PFNGLDELETEBUFFERSPROC DeleteBuffers = nullptr;
            PFNGLGENVERTEXARRAYSPROC GenVertexArrays = nullptr;
            PFNGLBINDVERTEXARRAYPROC BindVertexArray = nullptr;
            PFNGLDELETEVERTEXARRAYSPROC DeleteVertexArrays = nullptr;
            PFNGLVERTEXATTRIBPOINTERPROC VertexAttribPointer = nullptr;
            PFNGLENABLEVERTEXATTRIBARRAYPROC EnableVertexAttribArray = nullptr;
            GlTypes::DrawArrays DrawArrays = nullptr;

            bool Load(IPlatformGlContext& context)
            {
                bool ok = true;
                const auto get = [&context, &ok](auto& slot, const char* name) {
                    slot = reinterpret_cast<std::remove_reference_t<decltype(slot)>>(
                        context.GetProcAddress(name));
                    ok = ok && slot != nullptr;
                };
                get(GetString, "glGetString");
                get(GetIntegerv, "glGetIntegerv");
                get(GetError, "glGetError");
                get(ClearColor, "glClearColor");
                get(Clear, "glClear");
                get(Viewport, "glViewport");
                get(ReadPixels, "glReadPixels");
                get(Finish, "glFinish");
                get(CreateShader, "glCreateShader");
                get(ShaderSource, "glShaderSource");
                get(CompileShader, "glCompileShader");
                get(GetShaderiv, "glGetShaderiv");
                get(GetShaderInfoLog, "glGetShaderInfoLog");
                get(CreateProgram, "glCreateProgram");
                get(AttachShader, "glAttachShader");
                get(LinkProgram, "glLinkProgram");
                get(GetProgramiv, "glGetProgramiv");
                get(UseProgram, "glUseProgram");
                get(DeleteShader, "glDeleteShader");
                get(DeleteProgram, "glDeleteProgram");
                get(GetUniformLocation, "glGetUniformLocation");
                get(Uniform4f, "glUniform4f");
                get(GenBuffers, "glGenBuffers");
                get(BindBuffer, "glBindBuffer");
                get(BufferData, "glBufferData");
                get(DeleteBuffers, "glDeleteBuffers");
                get(GenVertexArrays, "glGenVertexArrays");
                get(BindVertexArray, "glBindVertexArray");
                get(DeleteVertexArrays, "glDeleteVertexArrays");
                get(VertexAttribPointer, "glVertexAttribPointer");
                get(EnableVertexAttribArray, "glEnableVertexAttribArray");
                get(DrawArrays, "glDrawArrays");
                return ok;
            }

            std::string String(const GLenum name) const
            {
                const auto* value = GetString(name);
                return value != nullptr ? reinterpret_cast<const char*>(value) : "(null)";
            }
        };

        /// A triangle program, a vertex array and a uniform colour: the least a renderer does.
        struct Scene
        {
            GLuint program = 0;
            GLuint buffer = 0;
            GLuint vertexArray = 0;
            GLint color = -1;
            std::string log;

            bool Build(const Gl& gl)
            {
                const char* vertex =
                    "#version 330 core\n"
                    "layout(location = 0) in vec2 position;\n"
                    "void main() { gl_Position = vec4(position, 0.0, 1.0); }\n";
                const char* fragment =
                    "#version 330 core\n"
                    "uniform vec4 color;\n"
                    "out vec4 result;\n"
                    "void main() { result = color; }\n";
                const auto compile = [&gl, this](const GLenum type, const char* source) -> GLuint {
                    const GLuint shader = gl.CreateShader(type);
                    gl.ShaderSource(shader, 1, &source, nullptr);
                    gl.CompileShader(shader);
                    GLint ok = 0;
                    gl.GetShaderiv(shader, GL_COMPILE_STATUS, &ok);
                    if (ok == 0)
                    {
                        char text[1024] = {};
                        gl.GetShaderInfoLog(shader, sizeof(text) - 1, nullptr, text);
                        log += text;
                    }
                    return shader;
                };
                const GLuint vs = compile(GL_VERTEX_SHADER, vertex);
                const GLuint fs = compile(GL_FRAGMENT_SHADER, fragment);
                program = gl.CreateProgram();
                gl.AttachShader(program, vs);
                gl.AttachShader(program, fs);
                gl.LinkProgram(program);
                gl.DeleteShader(vs);
                gl.DeleteShader(fs);
                GLint linked = 0;
                gl.GetProgramiv(program, GL_LINK_STATUS, &linked);
                if (linked == 0)
                {
                    return false;
                }
                color = gl.GetUniformLocation(program, "color");
                // A triangle covering the centre of the viewport but not its corners.
                const float vertices[] = {-0.6f, -0.6f, 0.6f, -0.6f, 0.0f, 0.7f};
                gl.GenVertexArrays(1, &vertexArray);
                gl.BindVertexArray(vertexArray);
                gl.GenBuffers(1, &buffer);
                gl.BindBuffer(GL_ARRAY_BUFFER, buffer);
                gl.BufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
                gl.VertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
                gl.EnableVertexAttribArray(0);
                return true;
            }

            void Destroy(const Gl& gl)
            {
                if (buffer != 0) { gl.DeleteBuffers(1, &buffer); }
                if (vertexArray != 0) { gl.DeleteVertexArrays(1, &vertexArray); }
                if (program != 0) { gl.DeleteProgram(program); }
                buffer = vertexArray = program = 0;
            }

            /// Draws one frame and, when asked, reads the centre and a corner back.
            bool Draw(const Gl& gl, const int width, const int height, const float phase,
                      const bool verify, std::string& detail)
            {
                const float clear[3] = {0.1f + 0.1f * std::sin(phase), 0.2f, 0.35f};
                gl.Viewport(0, 0, width, height);
                gl.ClearColor(clear[0], clear[1], clear[2], 1.0f);
                gl.Clear(GL_COLOR_BUFFER_BIT);
                gl.UseProgram(program);
                gl.Uniform4f(color, 1.0f, 0.5f, 0.125f, 1.0f);
                gl.BindVertexArray(vertexArray);
                gl.DrawArrays(GL_TRIANGLES, 0, 3);
                if (!verify)
                {
                    return gl.GetError() == GL_NO_ERROR;
                }
                unsigned char centre[4] = {};
                unsigned char corner[4] = {};
                gl.ReadPixels(width / 2, height / 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, centre);
                gl.ReadPixels(2, 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, corner);
                const auto near = [](const int a, const float b) {
                    return std::abs(a - static_cast<int>(std::lround(b * 255.0f))) <= 2;
                };
                const bool centreOk = near(centre[0], 1.0f) && near(centre[1], 0.5f) &&
                                      near(centre[2], 0.125f);
                const bool cornerOk = near(corner[0], clear[0]) && near(corner[1], clear[1]) &&
                                      near(corner[2], clear[2]);
                std::ostringstream text;
                text << "centre " << int(centre[0]) << "," << int(centre[1]) << "," << int(centre[2])
                     << " corner " << int(corner[0]) << "," << int(corner[1]) << "," << int(corner[2]);
                detail = text.str();
                return centreOk && cornerOk && gl.GetError() == GL_NO_ERROR;
            }
        };

        std::string Describe(const GlContextDescription& granted)
        {
            std::ostringstream out;
            out << "RGBA " << granted.redBits << granted.greenBits << granted.blueBits
                << granted.alphaBits << ", depth " << granted.depthBits << ", stencil "
                << granted.stencilBits << ", double-buffered " << (granted.doubleBuffer ? "yes" : "no")
                << ", samples " << granted.multisampleSamples;
            return out.str();
        }

    } // namespace

    int RunGlx(const std::vector<std::string>& arguments)
    {
        const int frames = static_cast<int>(OptionInt(arguments, "frames", 3000));
        const bool requireHardware = !OptionFlag(arguments, "allow-software");
        Driver driver;
        Session session;
        if (!session.Ok())
        {
            Fail("glx.session", session.Error());
            return 1;
        }
        IPlatform& platform = session.Platform();
        IPlatformGlContext* service = platform.GetGlContext();
        if (service == nullptr)
        {
            Skip("glx", "no GLX 1.3 on this server");
            return 0;
        }
        Check(platform.GetCapabilities().openGlContext, "glx.capability");

        auto window = session.Make("CNA GLX validation", 800, 600, true, WindowRenderIntent::OpenGl);
        const WindowId id = window->GetId();
        session.WaitFor(id, WindowEventKind::Exposed);

        GlContextDescription requested;
        requested.majorVersion = 3;
        requested.minorVersion = 3;
        requested.profile = GlProfile::Core;
        requested.depthBits = 24;
        requested.stencilBits = 8;
        GlContextHandle context = nullptr;
        try
        {
            context = service->CreateContext(id, requested);
        }
        catch (const std::exception& error)
        {
            Fail("glx.context-3.3-core", error.what());
            return 1;
        }
        service->MakeCurrent(id, context);
        Gl gl;
        if (!Check(gl.Load(*service), "glx.entry-points-resolve"))
        {
            return 1;
        }
        GLint major = 0;
        GLint minor = 0;
        GLint profile = 0;
        gl.GetIntegerv(GL_MAJOR_VERSION, &major);
        gl.GetIntegerv(GL_MINOR_VERSION, &minor);
        gl.GetIntegerv(GL_CONTEXT_PROFILE_MASK, &profile);
        const std::string renderer = gl.String(GL_RENDERER);
        Info("GL_VENDOR   " + gl.String(GL_VENDOR));
        Info("GL_RENDERER " + renderer);
        Info("GL_VERSION  " + gl.String(GL_VERSION));
        Info("GLSL        " + gl.String(GL_SHADING_LANGUAGE_VERSION));
        Info("granted framebuffer: " + Describe(service->GetContextAttributes(context)));
        Check(major > 3 || (major == 3 && minor >= 3), "glx.version-at-least-3.3",
              std::to_string(major) + "." + std::to_string(minor));
        Check((profile & GL_CONTEXT_CORE_PROFILE_BIT) != 0, "glx.core-profile");
        const bool software = renderer.find("llvmpipe") != std::string::npos ||
                              renderer.find("softpipe") != std::string::npos ||
                              renderer.find("swrast") != std::string::npos;
        if (requireHardware)
        {
            Check(!software, "glx.hardware-renderer", renderer);
        }

        Scene scene;
        if (!Check(scene.Build(gl), "glx.shader-compiles-and-links", scene.log))
        {
            return 1;
        }

        // --- sustained rendering -----------------------------------------------------------------------
        int verifyFailures = 0;
        std::string lastDetail;
        const double started = NowMs();
        for (int frame = 0; frame < frames; ++frame)
        {
            const WindowSize size = window->GetPixelSize();
            std::string detail;
            const bool verify = frame % 250 == 0;
            if (!scene.Draw(gl, size.width, size.height, frame * 0.01f, verify, detail))
            {
                ++verifyFailures;
                lastDetail = detail;
            }
            service->SwapBuffers(id);
            session.Poll();
            session.Clear();
        }
        const double seconds = (NowMs() - started) / 1000.0;
        Check(verifyFailures == 0, "glx.frames-render-correctly",
              std::to_string(frames) + " frames in " + std::to_string(seconds) + " s (" +
                  std::to_string(static_cast<int>(frames / seconds)) + " fps), " +
                  std::to_string(verifyFailures) + " bad" +
                  (lastDetail.empty() ? std::string() : ": " + lastDetail));

        // --- swap interval ------------------------------------------------------------------------------
        for (const int interval : {1, 0})
        {
            const bool applied = service->SetSwapInterval(interval);
            const double begin = NowMs();
            for (int frame = 0; frame < 240; ++frame)
            {
                std::string unused;
                scene.Draw(gl, window->GetPixelSize().width, window->GetPixelSize().height, 0.0f,
                           false, unused);
                service->SwapBuffers(id);
                session.Poll();
            }
            gl.Finish();
            const double fps = 240.0 / ((NowMs() - begin) / 1000.0);
            Info("swap interval " + std::to_string(interval) + " " +
                 (applied ? "applied" : "NOT applied") + ": " +
                 std::to_string(static_cast<int>(fps)) + " fps");
        }

        // --- resize -------------------------------------------------------------------------------------
        {
            session.Clear();
            window->SetSize(1024, 700);
            window->Sync();
            session.WaitFor(id, WindowEventKind::Resized);
            const WindowSize size = window->GetPixelSize();
            std::string detail;
            const bool ok = scene.Draw(gl, size.width, size.height, 1.0f, true, detail);
            service->SwapBuffers(id);
            Check(ok && size.width == 1024 && size.height == 700, "glx.render-after-resize",
                  std::to_string(size.width) + "x" + std::to_string(size.height) + ", " + detail);
        }

        // --- fullscreen ---------------------------------------------------------------------------------
        if (platform.GetCapabilities().borderlessFullscreen)
        {
            window->SetFullscreenMode(WindowFullscreenMode::BorderlessFullscreen);
            session.PumpUntil(
                [&window] { return window->GetPixelSize().width > 1024; },
                std::chrono::milliseconds(3000));
            for (int frame = 0; frame < 120; ++frame)
            {
                std::string unused;
                scene.Draw(gl, window->GetPixelSize().width, window->GetPixelSize().height,
                           frame * 0.05f, false, unused);
                service->SwapBuffers(id);
                session.Poll();
            }
            const WindowSize size = window->GetPixelSize();
            std::string detail;
            const bool ok = scene.Draw(gl, size.width, size.height, 2.0f, true, detail);
            service->SwapBuffers(id);
            Check(ok, "glx.render-in-fullscreen",
                  std::to_string(size.width) + "x" + std::to_string(size.height) + ", " + detail);
            window->SetFullscreenMode(WindowFullscreenMode::Windowed);
            session.PumpUntil([&window] { return window->GetPixelSize().width == 1024; },
                              std::chrono::milliseconds(3000));
            std::string after;
            const bool back = scene.Draw(gl, window->GetPixelSize().width,
                                         window->GetPixelSize().height, 3.0f, true, after);
            service->SwapBuffers(id);
            Check(back, "glx.render-after-leaving-fullscreen", after);
        }

        // --- a second window, a second context, alternating -----------------------------------------------
        {
            auto second = session.Make("CNA GLX second window", 400, 300, true,
                                       WindowRenderIntent::OpenGl, 950, 100);
            session.WaitFor(second->GetId(), WindowEventKind::Exposed);
            GlContextHandle secondContext = service->CreateContext(second->GetId(), requested);
            // Vertex arrays are per context, so the second context gets its own scene.
            service->MakeCurrent(second->GetId(), secondContext);
            Scene secondScene;
            int bad = secondScene.Build(gl) ? 0 : 1;
            for (int frame = 0; frame < 200 && bad == 0; ++frame)
            {
                std::string detail;
                service->MakeCurrent(id, context);
                if (!scene.Draw(gl, window->GetPixelSize().width, window->GetPixelSize().height,
                                frame * 0.03f, frame % 50 == 0, detail))
                {
                    ++bad;
                }
                service->SwapBuffers(id);
                service->MakeCurrent(second->GetId(), secondContext);
                if (!secondScene.Draw(gl, second->GetPixelSize().width,
                                      second->GetPixelSize().height, frame * 0.05f,
                                      frame % 50 == 0, detail))
                {
                    ++bad;
                }
                service->SwapBuffers(second->GetId());
                session.Poll();
            }
            secondScene.Destroy(gl);
            Check(bad == 0, "glx.two-windows-two-contexts", std::to_string(bad) + " bad frames");
            service->MakeCurrent(0, nullptr);
            service->DestroyContext(secondContext);
        }

        // --- compatibility and newest core ---------------------------------------------------------------
        for (const auto& [majorWanted, minorWanted, wantedProfile, label] :
             {std::tuple{2, 1, GlProfile::Compatibility, "2.1-compatibility"},
              std::tuple{4, 6, GlProfile::Core, "4.6-core"}})
        {
            GlContextDescription other = requested;
            other.majorVersion = majorWanted;
            other.minorVersion = minorWanted;
            other.profile = wantedProfile;
            try
            {
                GlContextHandle handle = service->CreateContext(id, other);
                service->MakeCurrent(id, handle);
                Gl local;
                local.Load(*service);
                Pass(std::string("glx.context-") + label, local.String(GL_VERSION));
                service->MakeCurrent(0, nullptr);
                service->DestroyContext(handle);
            }
            catch (const std::exception& error)
            {
                Fail(std::string("glx.context-") + label, error.what());
            }
        }

        service->MakeCurrent(id, context);
        scene.Destroy(gl);
        service->MakeCurrent(0, nullptr);
        service->DestroyContext(context);
        Check(service->GetCurrentBinding().context == nullptr, "glx.unbound-after-destroy");
        window.reset();
        session.PumpFor(std::chrono::milliseconds(200));
        Check(driver.ErrorCount() == 0, "glx.driver-no-x-errors");
        return Results().failed == 0 ? 0 : 1;
    }

} // namespace CnaX11Validation
