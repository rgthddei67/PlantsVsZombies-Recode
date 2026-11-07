#version 330 core

in vec2 texCoord;
in vec4 color;
flat in vec4 clipRect;

uniform sampler2D uTexture;
uniform float uFramebufferHeight;

in vec2 worldPosition;
flat in int primitiveKind;
out vec4 outColor;

#include "primitive_coverage.glsl"

#include "lum_sat.glsl"

void main() {
    float coverage = primitiveKind == 0 ? 1.0 : primitiveCoverage(texCoord, worldPosition, primitiveKind);

    vec2 topLeftFragment = vec2(gl_FragCoord.x, uFramebufferHeight - gl_FragCoord.y);
    if (topLeftFragment.x < clipRect.x || topLeftFragment.y < clipRect.y ||
        topLeftFragment.x >= clipRect.z || topLeftFragment.y >= clipRect.w) discard;
    vec4 sampled = primitiveKind == 0 ? texture(uTexture, texCoord) : vec4(coverage);
    if (sampled.a <= 0.0) {
        outColor = vec4(0.0);
        return;
    }
    vec3 straightRgb = clamp(sampled.rgb / sampled.a, 0.0, 1.0);
    vec3 washedRgb = applyLumSat(straightRgb, 1.2, 0.3);
    float alpha = sampled.a * color.a;
    outColor = vec4(washedRgb * color.rgb * alpha, alpha);
}
