// SPDX-License-Identifier: MS-PL
#pragma once

#ifdef CNA_CNAEXT

#include "Microsoft/Xna/Framework/BoundingBox.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"

#include <memory>
#include <string>
#include <vector>

namespace Microsoft::Xna::Framework::Graphics {
    class GraphicsDevice;
    enum class PrimitiveType;
}

namespace CNA::Graphics {

    class ComputeShader;
    class StorageBuffer;

/** @addtogroup cnaext_engine
 *  @{
 */

    /**
     * @brief One instance offered to a @ref GpuInstanceCuller -- where it is, and what it occupies.
     */
    struct GpuCullableInstance
    {
        /** @brief The instance's world transform, which the surviving draw reads back out. */
        Microsoft::Xna::Framework::Matrix World;
        /** @brief Its bounds **in world space** -- already transformed, not model-space bounds. */
        Microsoft::Xna::Framework::BoundingBox Bounds;
    };

    /**
     * @brief Frustum culling whose answer stays on the GPU and becomes the draw itself.
     *
     * plan_modern.md `MOD-2091`. `MOD-1551` proved a compute shader could cull *correctly* -- it
     * agrees with `FrustumCullerEXT` box for box -- but its verdict came back to the CPU to be
     * turned into draw calls, and that readback is a pipeline stall: the CPU waits for the GPU to
     * finish work it has only just submitted. This closes that gap. The compute shader compacts the
     * survivors into a buffer and writes the surviving count straight into an indirect draw
     * command, and the draw reads both. **Nothing about the result is read back to submit the
     * frame.**
     *
     * The shape is the standard GPU-driven one, and it puts one requirement on the caller's shader:
     * the per-instance world matrix arrives through a storage buffer the vertex shader reads by
     * `gl_InstanceID`, not through a per-instance vertex stream, because a compute shader cannot
     * write a vertex buffer in this profile. @ref getInstanceLookupGlsl is the two lines that read
     * it, and a vertex shader using them must declare `#version 310 es` or later.
     *
     * **It refuses rather than falls back.** Unlike `ClusteredLightCompute`, whose CPU path is a
     * correct if slower answer, there is no CPU equivalent of "the draw call itself came from the
     * GPU"; a silent fallback would report success for a frame that never removed the stall. Ask
     * @ref isSupported, and @ref getUnsupportedReason says which of the four requirements is
     * missing.
     */
    class GpuInstanceCuller
    {
    public:
        /**
         * @brief The storage-buffer binding point @ref getInstanceLookupGlsl declares.
         *
         * Deliberately high, so it does not collide with the low bindings a caller's own compute
         * work is likely to have taken.
         */
        static constexpr int kInstanceBinding = 6;

        /**
         * @brief Creates the culler and compiles its compute program.
         *
         * Never throws for a missing capability: an unsupported object is constructible and says so.
         *
         * @param device The device to compile and allocate on.
         */
        explicit GpuInstanceCuller(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device);

        /** @brief Destroys the program and every buffer it owns. */
        ~GpuInstanceCuller();

        GpuInstanceCuller(const GpuInstanceCuller&)            = delete;
        GpuInstanceCuller& operator=(const GpuInstanceCuller&) = delete;

        /** @brief Returns whether this device can run the whole route. */
        [[nodiscard]] bool isSupported() const;

        /** @brief Returns which requirement is missing, or an empty string when none is. */
        [[nodiscard]] const std::string& getUnsupportedReason() const;

        /**
         * @brief Uploads the instances to cull.
         *
         * The buffers are reallocated only when the count grows, so a scene of steady size uploads
         * into the same storage every frame.
         *
         * @param instances The instances; an empty list is accepted and culls to nothing.
         * @throws System::NotSupportedException If the device cannot run this route.
         */
        void setInstances(const std::vector<GpuCullableInstance>& instances);

        /** @brief Returns how many instances were last given to @ref setInstances. */
        [[nodiscard]] int getInstanceCount() const;

        /**
         * @brief Culls against one camera, leaving the survivors and the draw command on the GPU.
         *
         * @param view       The camera's view matrix.
         * @param projection The camera's projection matrix.
         * @param indexCount How many indices one instance draws.
         * @param firstIndex The first index, in index elements.
         * @param baseVertex Added to every decoded index.
         * @throws System::NotSupportedException If the device cannot run this route.
         * @throws std::invalid_argument If @p indexCount is not positive, or an offset is negative.
         */
        void cull(const Microsoft::Xna::Framework::Matrix& view,
                  const Microsoft::Xna::Framework::Matrix& projection,
                  int indexCount, int firstIndex = 0, int baseVertex = 0);

        /**
         * @brief Draws every survivor of the last @ref cull, with one indirect call.
         *
         * The vertex buffer, the index buffer and the effect must already be bound; this adds the
         * instance buffer and issues the command the cull wrote.
         *
         * @param primitiveType The topology.
         * @throws System::NotSupportedException If the device cannot run this route.
         * @throws std::runtime_error If @ref cull has not run since the last @ref setInstances.
         */
        void draw(Microsoft::Xna::Framework::Graphics::PrimitiveType primitiveType);

        /**
         * @brief Returns the GLSL a vertex shader includes to find its own instance's transform.
         *
         * Declares the buffer at @ref kInstanceBinding and one function, `cnaInstanceWorld()`. The
         * matrix arrives in the same layout every other CNA shader receives one, so a shader
         * multiplies it on the left exactly as it does `uWorld`.
         *
         * @return The declarations, ready to concatenate after a `#version 310 es` line.
         */
        [[nodiscard]] static std::string getInstanceLookupGlsl();

        /**
         * @brief Reads the surviving count back, for a test or a tool.
         *
         * **This is the stall the class exists to avoid**, and it is named so nobody puts it in a
         * frame by accident: it waits for the GPU. The rendering path never calls it.
         *
         * @return How many instances survived the last @ref cull, or 0 if none has run.
         */
        [[nodiscard]] int readVisibleCountEXT() const;

    private:
        Microsoft::Xna::Framework::Graphics::GraphicsDevice& device_;
        std::unique_ptr<ComputeShader> program_;
        std::unique_ptr<StorageBuffer> instances_;
        std::unique_ptr<StorageBuffer> visible_;
        std::unique_ptr<StorageBuffer> planes_;
        std::unique_ptr<StorageBuffer> command_;
        std::string unsupportedReason_;
        int  instanceCount_ = 0;
        int  capacity_      = 0;
        bool culled_        = false;
    };

/** @} */

} // namespace CNA::Graphics

#endif // CNA_CNAEXT
