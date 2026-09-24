# plans/plan_opengl4.md GL4-1: real desktop OpenGL 4.x core profile via the platform's own GL
# library (libGL/opengl32/OpenGL.framework, resolved by find_package(OpenGL) in
# RendererSelection.cmake); window/context management comes from IPlatformGlContext -- no other new
# third-party dependency (GL4Loader.hpp/.cpp is this renderer's own hand-rolled loader for
# the GL 1.2+ entry points a core profile needs).
cna_add_renderer()
# plans/plan_opengl4_modern_graphics.md GL4-0034: the renderer needs GL entry points only -- its
# context comes from the platform (EGL on Wayland, GLX on X11, WGL/CGL elsewhere). With GLVND,
# OpenGL::GL is libOpenGL *and* libGLX, so a Wayland build carried a GLX dependency that only the
# linker's --as-needed happened to drop (and a sanitizer link did not). OpenGL::OpenGL is the
# window-system-neutral GLVND library; where it does not exist, OpenGL::GL is the only choice.
if(TARGET OpenGL::OpenGL)
    target_link_libraries(${RENDERER_TARGET} PRIVATE OpenGL::OpenGL)
else()
    target_link_libraries(${RENDERER_TARGET} PRIVATE OpenGL::GL)
endif()

# plans/plan_opengl4_modern_graphics.md GL4-0020: compiled XNA effects go through MojoShader's own
# OpenGL adapter plus the translation shared with every other compiled-effect backend, exactly as
# EasyGL's do. Opt-in, because MojoShader is a fetched dependency this renderer does not otherwise
# need. The XNA SpriteEffect is embedded so a SpriteBatch pass that assigns only a pixel shader
# inherits the stock vertex shader, as Direct3D does (plans/plan_fx.md FX-080).
if(CNA_OPENGL4_COMPILED_EFFECTS)
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    set(CNA_OPENGL4_STOCK_EFFECT
        "${CMAKE_CURRENT_SOURCE_DIR}/../fna3d/effects/SpriteEffect.fxb")
    set(CNA_OPENGL4_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
    set(CNA_OPENGL4_STOCK_EFFECT_HEADER
        "${CNA_OPENGL4_GENERATED_DIR}/Fna3dStockEffectBlobs.hpp")
    add_custom_command(
        OUTPUT "${CNA_OPENGL4_STOCK_EFFECT_HEADER}"
        COMMAND ${Python3_EXECUTABLE}
                "${CMAKE_CURRENT_SOURCE_DIR}/../fna3d/effects/embed_effects.py"
                "${CNA_OPENGL4_STOCK_EFFECT_HEADER}"
                "SpriteEffect=${CNA_OPENGL4_STOCK_EFFECT}"
        DEPENDS
                "${CMAKE_CURRENT_SOURCE_DIR}/../fna3d/effects/embed_effects.py"
                "${CNA_OPENGL4_STOCK_EFFECT}"
        COMMENT "CNA: embedding the XNA SpriteEffect for OpenGL4 compiled-effect inheritance"
        VERBATIM)
    add_custom_target(cna_renderer_opengl4_sprite_effect
        DEPENDS "${CNA_OPENGL4_STOCK_EFFECT_HEADER}")
    add_dependencies(${RENDERER_TARGET} cna_renderer_opengl4_sprite_effect)
    target_sources(${RENDERER_TARGET} PRIVATE "${CNA_OPENGL4_STOCK_EFFECT_HEADER}")
    target_include_directories(${RENDERER_TARGET} PRIVATE "${CNA_OPENGL4_GENERATED_DIR}")
    target_link_libraries(${RENDERER_TARGET} PUBLIC cna_renderer_mojoshader_effect)
endif()

# Module-owned examples (module-examples campaign): the family's example/test
# registration is module-local; its historical selection/platform conditions
# live inside examples/CMakeLists.txt unchanged.
add_subdirectory(examples)
