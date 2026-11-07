#version 450
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_nonuniform_qualifier : require

// set=1 binding=1 bindless 纹理数组（8192 槽位，由 VulkanTexturePool 拥有）。
layout(set = 1, binding = 1) uniform sampler2D textures[];

layout(location = 0) in vec2      vUV;
layout(location = 1) flat in vec4 vColor;
layout(location = 2) flat in uint vTex;

layout(location = 0) out vec4 outColor;

#include "opengl/lum_sat.glsl"

void main() {
    vec4 sampled = texture(textures[nonuniformEXT(vTex)], vUV);
    if (sampled.a <= 0.0) {
        outColor = vec4(0.0);
        return;
    }
    vec3 straightRgb = clamp(sampled.rgb / sampled.a, 0.0, 1.0);
    vec3 washedRgb = applyLumSat(straightRgb, 1.8, 0.2);
    float alpha = sampled.a * vColor.a;
    outColor = vec4(washedRgb * vColor.rgb * alpha, alpha);
}
