#version 450

// Same UBO as shader.vert/shader.frag (descriptor set 0, binding 0), so the grid needs nothing of its own on the CPU side
layout(binding = 0) uniform UboViewProjection {
    mat4 projection;
    mat4 view;
    vec4 lightPos;
    vec4 viewPos;       // Camera world position
    vec4 lightColor;
} ubo;

layout(location = 0) out vec3 WorldPos;

// Half the side of the quad, in world units. Must match gGridSize in infinite_grid.frag (it fades out over this distance).
const float gGridSize = 100.0;

// A single quad on the XZ plane (y = 0), made from gl_VertexIndex alone: no vertex buffer needed
const vec3 Pos[4] = vec3[4](
    vec3(-1.0, 0.0, -1.0),
    vec3( 1.0, 0.0, -1.0),
    vec3( 1.0, 0.0,  1.0),
    vec3(-1.0, 0.0,  1.0)
);

const int Indices[6] = int[6](0, 2, 1, 2, 0, 3);

void main()
{
    int Index = Indices[gl_VertexIndex];
    vec3 vPos3 = Pos[Index] * gGridSize;

    // Follow the camera on X/Z, which is what makes the grid "infinite"
    vPos3.x += ubo.viewPos.x;
    vPos3.z += ubo.viewPos.z;

    gl_Position = ubo.projection * ubo.view * vec4(vPos3, 1.0);
    WorldPos = vPos3;
}
