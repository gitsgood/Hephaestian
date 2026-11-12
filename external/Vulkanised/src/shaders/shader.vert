#version 450

layout(binding = 0) uniform UboViewProjection {
    mat4 projection;
    mat4 view;
    vec4 lightPos;
    vec4 viewPos;
    vec4 lightColor;
} ubo;

// One per drawn instance, grouped by mesh. Must match Vulkanised::Rendering::GpuInstanceData (112 bytes, std430).
struct InstanceData {
    mat4 ModelMatrix;
    vec4 TextureCoordinateRect;     // uv = vertex uv * zw + xy (a sprite sheet frame, or the whole texture)
    vec4 Tint;
    uint TextureIndex;              // Slot in the bindless texture array
    uint Padding0;
    uint Padding1;
    uint Padding2;
};

layout(std430, binding = 1) readonly buffer InstanceBuffer {
    InstanceData Instances[];
} instanceBuffer;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec3 inNormal;

// Outputs to Fragment Shader
layout(location = 0) out vec3 fragPos;
layout(location = 1) out vec3 fragColor;
layout(location = 2) out vec2 fragTexCoord;
layout(location = 3) out vec3 fragNormal;
layout(location = 4) flat out uint fragTextureIndex;

void main() {
    // gl_InstanceIndex includes the indirect draw's firstInstance, so it lands on this mesh's range of the buffer
    InstanceData Instance = instanceBuffer.Instances[gl_InstanceIndex];
    mat4 modelMatrix = Instance.ModelMatrix;

    // Calculate world position
    vec4 worldPos = modelMatrix * vec4(inPosition, 1.0);
    gl_Position = ubo.projection * ubo.view * worldPos;

    // Pass data to fragment shader
    fragPos = worldPos.xyz;

    // Normal into world space with the cofactor of the model matrix: the inverse-transpose up to a scale factor (normalised in
    // the fragment shader anyway), so normals stay perpendicular to the surface under NON-uniform scale too (a squashed sphere),
    // for three cross products instead of a matrix inverse. The determinant's sign keeps mirrored instances facing outwards.
    mat3 LinearPart = mat3(modelMatrix);
    mat3 NormalMatrix = mat3(cross(LinearPart[1], LinearPart[2]), cross(LinearPart[2], LinearPart[0]), cross(LinearPart[0], LinearPart[1]));
    NormalMatrix *= sign(dot(LinearPart[0], NormalMatrix[0]));
    fragNormal = NormalMatrix * inNormal;

    fragColor = inColor * Instance.Tint.rgb;
    fragTexCoord = inTexCoord * Instance.TextureCoordinateRect.zw + Instance.TextureCoordinateRect.xy;
    fragTextureIndex = Instance.TextureIndex;
}
