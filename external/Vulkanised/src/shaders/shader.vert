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
    uint Flags;                     // Vulkanised::RenderInstanceFlags
    vec2 PivotOffset;               // Added to the vertex's x/y before anything else (moves a sprite's pivot)
};

layout(std430, binding = 1) readonly buffer InstanceBuffer {
    InstanceData Instances[];
} instanceBuffer;

// Same bits as Vulkanised::RenderInstanceFlags
const uint c_FlagFaceCamera        = 1u << 1;
const uint c_FlagFaceCameraUpright = 1u << 2;

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
layout(location = 5) flat out uint fragFlags;

void main() {
    // gl_InstanceIndex includes the indirect draw's firstInstance, so it lands on this mesh's range of the buffer
    InstanceData Instance = instanceBuffer.Instances[gl_InstanceIndex];
    mat4 modelMatrix = Instance.ModelMatrix;
    vec3 LocalPosition = inPosition + vec3(Instance.PivotOffset, 0.0);

    vec4 worldPos;
    if ((Instance.Flags & (c_FlagFaceCamera | c_FlagFaceCameraUpright)) != 0u)
    {
        // BILLBOARD: the instance's rotation is ignored, its x/y axes are replaced by the camera's (taken straight from the
        // view matrix: its rows are the camera's axes in world space). Position and scale still come from the transform.
        vec3 CameraRight = vec3(ubo.view[0][0], ubo.view[1][0], ubo.view[2][0]);
        vec3 CameraUp = vec3(ubo.view[0][1], ubo.view[1][1], ubo.view[2][1]);
        if ((Instance.Flags & c_FlagFaceCameraUpright) != 0u)
        {
            // Upright: turns to face the camera around the world's vertical axis only (characters, trees), never tilts
            CameraUp = vec3(0.0, 1.0, 0.0);
            vec3 FlatRight = vec3(CameraRight.x, 0.0, CameraRight.z);
            CameraRight = dot(FlatRight, FlatRight) > 1e-6 ? normalize(FlatRight) : vec3(1.0, 0.0, 0.0);
        }
        vec2 Scale = vec2(length(modelMatrix[0].xyz), length(modelMatrix[1].xyz));
        worldPos = vec4(modelMatrix[3].xyz + CameraRight * (LocalPosition.x * Scale.x) + CameraUp * (LocalPosition.y * Scale.y), 1.0);
        fragNormal = cross(CameraRight, CameraUp);  // Towards the camera
    }
    else
    {
        worldPos = modelMatrix * vec4(LocalPosition, 1.0);

        // Normal into world space with the cofactor of the model matrix: the inverse-transpose up to a scale factor (normalised in
        // the fragment shader anyway), so normals stay perpendicular to the surface under NON-uniform scale too (a squashed sphere),
        // for three cross products instead of a matrix inverse. The determinant's sign keeps mirrored instances facing outwards.
        mat3 LinearPart = mat3(modelMatrix);
        mat3 NormalMatrix = mat3(cross(LinearPart[1], LinearPart[2]), cross(LinearPart[2], LinearPart[0]), cross(LinearPart[0], LinearPart[1]));
        NormalMatrix *= sign(dot(LinearPart[0], NormalMatrix[0]));
        fragNormal = NormalMatrix * inNormal;
    }

    gl_Position = ubo.projection * ubo.view * worldPos;
    fragPos = worldPos.xyz;
    fragColor = inColor * Instance.Tint.rgb;
    fragTexCoord = inTexCoord * Instance.TextureCoordinateRect.zw + Instance.TextureCoordinateRect.xy;
    fragTextureIndex = Instance.TextureIndex;
    fragFlags = Instance.Flags;
}
