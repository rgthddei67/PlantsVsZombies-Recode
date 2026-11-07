#version 450
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_nonuniform_qualifier : require

// set=1 binding=1 bindless 纹理数组（8192 槽位，由 VulkanTexturePool 拥有）。
layout(set = 1, binding = 1) uniform sampler2D textures[];

layout(location = 0) in vec2      vUV;
layout(location = 1) in vec4      vColor;
layout(location = 2) flat in uint vTex;
layout(location = 3) flat in uvec2 vPackedClip;

layout(location = 4) in vec2 vWorld;
layout(location = 5) flat in int vPrimitive;
layout(location = 0) out vec4 outColor;

#include "opengl/primitive_coverage.glsl"

#include "opengl/lum_sat.glsl"

void main() {
    float coverage = vPrimitive == 0 ? 1.0 : primitiveCoverage(vUV, vWorld, vPrimitive);

    if ((vPackedClip.x | ~vPackedClip.y) != 0u) {
        uvec2 clipMin = uvec2(vPackedClip.x & 0xFFFFu, vPackedClip.x >> 16);
        uvec2 clipMax = uvec2(vPackedClip.y & 0xFFFFu, vPackedClip.y >> 16);
        vec2 frag = gl_FragCoord.xy;
        if (any(lessThan(frag, vec2(clipMin))) ||
            any(greaterThanEqual(frag, vec2(clipMax)))) {
            discard;
        }
    }

    vec4 sampled = vPrimitive == 0 ? texture(textures[nonuniformEXT(vTex)], vUV) : vec4(coverage);
    if (sampled.a <= 0.0) {
        outColor = vec4(0.0);
        return;
    }
    vec3 straightRgb = clamp(sampled.rgb / sampled.a, 0.0, 1.0);
    vec3 washedRgb = applyLumSat(straightRgb, 1.8, 0.2);
    float alpha = sampled.a * vColor.a;
    outColor = vec4(washedRgb * vColor.rgb * alpha, alpha);
}
