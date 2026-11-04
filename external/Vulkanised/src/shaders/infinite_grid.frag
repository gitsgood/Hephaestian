#version 450

layout(location = 0) in vec3 WorldPos;

layout(location = 0) out vec4 FragColor;

// Same UBO as shader.vert/shader.frag (descriptor set 0, binding 0)
layout(binding = 0) uniform UboViewProjection {
    mat4 projection;
    mat4 view;
    vec4 lightPos;
    vec4 viewPos;       // Camera world position
    vec4 lightColor;
} ubo;

const float gGridSize = 100.0;                  // Fade-out distance, must match infinite_grid.vert
const float gGridMinPixelsBetweenCells = 2.0;
const float gGridCellSize = 0.025;
const vec4 gGridColorThin = vec4(0.5, 0.5, 0.5, 1.0);
const vec4 gGridColorThick = vec4(0.0, 0.0, 0.0, 1.0);

float log10(float x) { return log(x) / log(10.0); }
float satf(float x) { return clamp(x, 0.0, 1.0); }
vec2 satv(vec2 x) { return clamp(x, vec2(0.0), vec2(1.0)); }
float max2(vec2 v) { return max(v.x, v.y); }

void main()
{
    // LOD Calculation using screen-space derivatives
    vec2 dvx = vec2(dFdx(WorldPos.x), dFdy(WorldPos.x));
    vec2 dvy = vec2(dFdx(WorldPos.z), dFdy(WorldPos.z));
    float lx = length(dvx);
    float ly = length(dvy);
    vec2 dudv = vec2(lx, ly);
    float l = length(dudv);

    float LOD = max(0.0, log10(l * gGridMinPixelsBetweenCells / gGridCellSize) + 1.0);
    float GridCellSizeLod0 = gGridCellSize * pow(10.0, floor(LOD));
    float GridCellSizeLod1 = GridCellSizeLod0 * 10.0;
    float GridCellSizeLod2 = GridCellSizeLod1 * 10.0;

    dudv *= 4.0;

    vec2 mod_div_dudv = mod(WorldPos.xz, GridCellSizeLod0) / dudv;
    float Lod0a = max2(vec2(1.0) - abs(satv(mod_div_dudv) * 2.0 - vec2(1.0)) );

    mod_div_dudv = mod(WorldPos.xz, GridCellSizeLod1) / dudv;
    float Lod1a = max2(vec2(1.0) - abs(satv(mod_div_dudv) * 2.0 - vec2(1.0)) );

    mod_div_dudv = mod(WorldPos.xz, GridCellSizeLod2) / dudv;
    float Lod2a = max2(vec2(1.0) - abs(satv(mod_div_dudv) * 2.0 - vec2(1.0)) );

    float LOD_fade = fract(LOD);
    vec4 Color;

    if (Lod2a > 0.0) {
        Color = gGridColorThick;
        Color.a *= Lod2a;
    } else {
        if (Lod1a > 0.0) {
            Color = mix(gGridColorThick, gGridColorThin, LOD_fade);
            Color.a *= Lod1a;
        } else {
            Color = gGridColorThin;
            Color.a *= (Lod0a * (1.0 - LOD_fade));
        }
    }

    // Fade out with distance from the camera, so the quad's edges are never visible
    float OpacityFalloff = (1.0 - satf(length(WorldPos.xz - ubo.viewPos.xz) / gGridSize));
    Color.a *= OpacityFalloff;

    // Nothing to draw here: skip the blend (and keep the transparent quad from hiding anything behind it)
    if (Color.a <= 0.0) {
        discard;
    }

    FragColor = Color;
}
