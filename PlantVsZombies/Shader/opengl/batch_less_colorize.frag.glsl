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

vec3 applyLumSat(vec3 rgb, float lumScale, float satScale) {
    float maxC = max(max(rgb.r, rgb.g), rgb.b);
    float minC = min(min(rgb.r, rgb.g), rgb.b);
    float lightness = (maxC + minC) * 0.5;
    float saturation = 0.0;
    float hue = 0.0;
    float delta = maxC - minC;
    if (lightness > 0.0 && delta > 0.0) {
        saturation = delta / (lightness <= 0.5
            ? maxC + minC : 2.0 - maxC - minC);
        if (maxC == rgb.r) hue = (rgb.g - rgb.b) / delta;
        else if (maxC == rgb.g) hue = 2.0 + (rgb.b - rgb.r) / delta;
        else hue = 4.0 + (rgb.r - rgb.g) / delta;
        hue = fract(hue / 6.0);
    }
    saturation *= satScale;
    lightness *= lumScale;
    float chroma = (1.0 - abs(2.0 * lightness - 1.0)) * saturation;
    vec3 hueRgb = clamp(abs(mod(hue * 6.0 + vec3(0.0, 4.0, 2.0), 6.0) - 3.0) - 1.0, 0.0, 1.0);
    return clamp((hueRgb - 0.5) * chroma + lightness, 0.0, 1.0);
}

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
