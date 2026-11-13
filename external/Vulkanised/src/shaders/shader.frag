#version 450
#extension GL_EXT_nonuniform_qualifier : require

layout(binding = 0) uniform UboViewProjection {
    mat4 projection;
    mat4 view;
    vec4 lightPos;
    vec4 viewPos;
    vec4 lightColor;
} ubo;

// Every texture, in one array (bindless): each instance says which one
layout(set = 1, binding = 0) uniform sampler2D Textures[];

layout(location = 0) in vec3 fragPos;
layout(location = 1) in vec3 fragColor;
layout(location = 2) in vec2 fragTexCoord;
layout(location = 3) in vec3 fragNormal;
layout(location = 4) flat in uint fragTextureIndex;	// Per instance (may differ between instances of one draw: hence nonuniformEXT)
layout(location = 5) flat in uint fragFlags;

// Same bits as Vulkanised::RenderInstanceFlags
const uint c_FlagUnlit = 1u << 3;

// Set to true when the alpha cutout pipeline is created from this same shader
layout(constant_id = 0) const bool c_IsAlphaCutoutPipeline = false;
// Set to true for the additive pipeline (its blending adds outColour onto the image)
layout(constant_id = 1) const bool c_IsAdditivePipeline = false;

layout(location = 0) out vec4 outColour;

vec3 shadeFragment(vec3 texColor);

void main()
{
    vec4 TextureSample = texture(Textures[nonuniformEXT(fragTextureIndex)], fragTexCoord);

    // ALPHA CUTOUT (sprites, foliage): only compiled into the cutout pipeline (specialization constant), so ordinary opaque
    // geometry keeps early depth testing. Either fully there or not at all: no sorting, works with the depth buffer.
    if (c_IsAlphaCutoutPipeline && TextureSample.a < 0.5)
    {
        discard;
    }

    vec3 ShadedColour = shadeFragment(TextureSample.rgb);

    // ADDITIVE: weighted by alpha, so see-through texels add nothing either (black ones already add nothing)
    outColour = vec4(c_IsAdditivePipeline ? ShadedColour * TextureSample.a : ShadedColour, 1.0);
}

vec3 shadeFragment(vec3 texColor)
{
    // UNLIT: shown exactly as drawn (sprites usually are)
    if ((fragFlags & c_FlagUnlit) != 0u)
    {
        return texColor * fragColor;
    }

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
    vec3 finalLight = (ambient + diffuse + specular);
    return finalLight * texColor * fragColor;
}
