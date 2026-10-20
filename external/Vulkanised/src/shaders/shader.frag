#version 450

layout(binding = 0) uniform UboViewProjection {
    mat4 projection;
    mat4 view;
    vec4 lightPos;
    vec4 viewPos;
    vec4 lightColor;
} ubo;

layout(set = 1, binding = 0) uniform sampler2D textureSampler;

layout(location = 0) in vec3 fragPos;
layout(location = 1) in vec3 fragColor;
layout(location = 2) in vec2 fragTexCoord;
layout(location = 3) in vec3 fragNormal;

layout(location = 0) out vec4 outColour;

void main()
{
    // Extract .xyz directly from the vec4 uniforms
    
    // 1. Ambient
    float ambientStrength = 0.1;
    vec3 ambient = ambientStrength * ubo.lightColor.xyz;

    // 2. Diffuse
    vec3 norm = normalize(fragNormal);
    vec3 lightDir = normalize(ubo.lightPos.xyz - fragPos);
    float diff = max(dot(norm, lightDir), 0.0);
    vec3 diffuse = diff * ubo.lightColor.xyz;

    // 3. Specular
    float specularStrength = 0.5;
    vec3 viewDir = normalize(ubo.viewPos.xyz - fragPos);
    vec3 reflectDir = reflect(-lightDir, norm);
    float spec = pow(max(dot(viewDir, reflectDir), 0.0), 32); 
    vec3 specular = specularStrength * spec * ubo.lightColor.xyz;

    // Combine
    vec3 texColor = texture(textureSampler, fragTexCoord).rgb;
    vec3 finalLight = (ambient + diffuse + specular);
    vec3 result = finalLight * texColor * fragColor;
    
    outColour = vec4(result, 1.0);
}