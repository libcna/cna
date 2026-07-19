# OPENGL1 backend tests

Configure CNA with `-DCNA_GRAPHICS_BACKEND=OPENGL1` on Linux or Windows. The backend requires a desktop OpenGL compatibility implementation. Linux CI can use Xvfb plus Mesa/llvmpipe; Windows CI can at minimum compile/link the backend and run on a software/VM OpenGL implementation where available.

Priority runtime smoke coverage:
1. clear + present;
2. SpriteBatch textured quad;
3. colored 3D triangle with depth;
4. textured 3D triangle;
5. indexed cube with depth/culling;
6. fixed-function directional lighting on `VertexPositionNormalTexture`;
7. fog and alpha-test approximations;
8. stencil and scissor state.
