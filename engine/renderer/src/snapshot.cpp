#include <ezmu/renderer/snapshot.hpp>

namespace ezmu::renderer {

void RenderSnapshot::reset() noexcept {
    // Part 2 adds containers here; they are cleared, never shrunk, to keep their capacity.
    frameIndex = 0;
    simulationTime = {};
    output = {};
    clearColor = {0.0F, 0.0F, 0.0F, 1.0F};
    captureId = 0;
}

} // namespace ezmu::renderer
