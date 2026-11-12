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

        // Smallest-ish sphere around every vertex (centred on their bounding box), in the mesh's own space: frustum culling
        // transforms it with each instance and skips the instance when the sphere is entirely out of view
        glm::vec3 m_BoundingSphereCentre{ 0.0f };
        float m_BoundingSphereRadius{ 0.0f };
    };

    // The Payload your ECS systems will build and pass to the renderer.
    // Everything after the transform is optional: {mesh, texture, transform} still draws the whole texture, untinted.
    struct RenderInstance {
        uint32_t meshID;
        uint32_t textureID;
        glm::mat4 transform;
        glm::vec4 m_TextureCoordinateRect{ 0.0f, 0.0f, 1.0f, 1.0f };   // (offset u, offset v, scale u, scale v): a sprite sheet frame, or the whole texture
        glm::vec4 m_Tint{ 1.0f };                                       // Multiplies the texture colour (linear RGB, alpha unused for now)
    };

    // MULTI-MESH MODELS
    // A model is nothing more than a list of meshes (already living in the unified buffers), each with its own texture
    // and its own placement relative to the root of the model. This is the flattened version of a node hierarchy,
    // which is exactly what a glTF/OBJ/FBX loader would hand us once it has walked the file's node tree.

    // One piece of a model that is already uploaded to the GPU (references a mesh and texture by identifier)
    struct ModelSubmesh {
        uint32_t m_MeshIdentifier{ 0 };
        uint32_t m_TextureIdentifier{ 0 };
        glm::mat4 m_LocalTransformRelativeToModelRoot{ 1.0f };
    };

    // One piece of a model that still needs to be uploaded to the GPU (raw geometry, what a model file loader would output)
    struct ModelSubmeshGeometry {
        std::vector<Vertex> m_Vertices;
        std::vector<uint32_t> m_Indices;
        uint32_t m_TextureIdentifier{ 0 };
        glm::mat4 m_LocalTransformRelativeToModelRoot{ 1.0f };
    };

    // The renderer's stored description of a model
    struct ModelAsset {
        std::vector<ModelSubmesh> m_Submeshes;
    };

}

#endif // !MESH_H
