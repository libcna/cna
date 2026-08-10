#pragma once

namespace CNA
{
    /**
     * @brief Controls how a permanently 2D-only graphics renderer handles unsupported 3D calls.
     *
     * This policy complements GraphicsDevice::SupportsCapability(): capability queries describe
     * what the active renderer can do, while this policy controls what happens if application code
     * calls an unsupported 3D operation anyway.
     *
     * The policy does not suppress invalid arguments, disposed-resource errors, driver failures,
     * or unfinished operations on an otherwise 3D-capable renderer.
     */
    enum class Unsupported3DGraphicsCallBehavior
    {
        /**
         * @brief Preserve the renderer's established failure behavior.
         *
         * Operations that currently throw continue to throw. Optional resource factories whose
         * established contract is to return nullptr continue to do so.
         */
        Throw,

        /**
         * @brief Log one warning per renderer operation and use a safe no-op/null-object stub.
         *
         * Draw and state calls return without doing work. Resource factories return usable
         * no-op objects rather than nullptr, so later SetData()/Draw()/query calls remain safe.
         */
        WarnAndStub
    };
} // namespace CNA
