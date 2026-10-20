#version 450

layout(binding = 0) uniform UboViewProjection {
    mat4 projection;
    mat4 view;
    vec4 lightPos;
    vec4 viewPos;
    vec4 lightColor;
} ubo;

layout(std140, binding = 1) readonly buffer ObjectBuffer {
    mat4 models[];
} objectBuffer;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec3 inNormal;

// Outputs to Fragment Shader
layout(location = 0) out vec3 fragPos;
layout(location = 1) out vec3 fragColor;
layout(location = 2) out vec2 fragTexCoord;
layout(location = 3) out vec3 fragNormal;

void main() {
    mat4 modelMatrix = objectBuffer.models[gl_InstanceIndex];
    
    // Calculate world position
    vec4 worldPos = modelMatrix * vec4(inPosition, 1.0);
    gl_Position = ubo.projection * ubo.view * worldPos;
    
    // Pass data to fragment shader
    fragPos = worldPos.xyz;
    
    // Transform normal into world space (ignoring non-uniform scaling issues for performance)
    fragNormal = mat3(modelMatrix) * inNormal; 
    
    fragColor = inColor;
    fragTexCoord = inTexCoord;
}