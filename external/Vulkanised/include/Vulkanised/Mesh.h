#ifndef MESH_H
#define MESH_H

#include <vector>
#include <stdexcept>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include "Vertex.h"
#include "Logger.h"
#include "Utilities.h"

namespace Vulkanised
{

    // Pure data representing where in the Unified Buffer this mesh lives
    struct MeshAsset {
        uint32_t firstIndex;
        uint32_t indexCount;
        int32_t vertexOffset;
        int texId;
    };

    // The Payload your ECS systems will build and pass to the renderer
    struct RenderInstance {
        uint32_t meshID;
        uint32_t textureID;
        glm::mat4 transform;
    };

}

#endif // !MESH_H
