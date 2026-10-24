#version 330 core

in vec2 poolCoord;
in vec2 waterWorldCoord;
in vec4 color;
flat in vec4 clipRect;

uniform sampler2D uTexture;
uniform float uFramebufferHeight;
uniform float uPoolLayer;
uniform float uPoolCounter;

out vec4 outColor;

float sampleCausticByte(vec2 pixel)
{
    vec2 basePixel = floor(pixel);
    vec2 fraction = pixel - basePixel;
    vec2 p00 = mod(basePixel, 256.0);
    vec2 p10 = mod(basePixel + vec2(1.0, 0.0), 256.0);
    vec2 p01 = mod(basePixel + vec2(0.0, 1.0), 256.0);
    vec2 p11 = mod(basePixel + vec2(1.0, 1.0), 256.0);
    vec2 texelSize = 1.0 / vec2(textureSize(uTexture, 0));
    float c00 = texture(uTexture, (p00 + vec2(0.5)) * texelSize).r;
    float c10 = texture(uTexture, (p10 + vec2(0.5)) * texelSize).r;
    float c01 = texture(uTexture, (p01 + vec2(0.5)) * texelSize).r;
    float c11 = texture(uTexture, (p11 + vec2(0.5)) * texelSize).r;
    return floor(clamp(mix(mix(c00, c10, fraction.x), mix(c01, c11, fraction.x), fraction.y), 0.0, 1.0) * 255.0 + 0.0001);
}

float causticAlphaAt(vec2 coord)
{
    vec2 causticPixel = fract(coord) * vec2(128.0, 64.0);
    float a1 = sampleCausticByte(vec2(
        causticPixel.x * 2.0 - ((uPoolCounter + 1.0) / 6.0),
        (256.0 - causticPixel.y) * 2.0 + uPoolCounter / 8.0));
    float a0 = sampleCausticByte(vec2(
        causticPixel.x * 2.0 + uPoolCounter / 10.0,
        causticPixel.y * 2.0));
    float value = floor((a0 + a1) * 0.5);
    float alpha = 0.0;
    if (value >= 160.0)
    {
        alpha = 255.0 - 2.0 * (value - 160.0);
    }
    else if (value >= 128.0)
    {
        alpha = 5.0 * (value - 128.0);
    }
    return clamp(floor(alpha / 3.0) / 255.0, 0.0, 1.0);
}

#include "water_effects.glsl"

vec4 previewSource(vec2 uv) { return texture(uTexture,uv); }
#include "shader_preview.glsl"
#include "polar_effects.glsl"

void main()
{
    vec2 topLeftFragment = vec2(gl_FragCoord.x, uFramebufferHeight - gl_FragCoord.y);
    if (topLeftFragment.x < clipRect.x || topLeftFragment.y < clipRect.y ||
        topLeftFragment.x >= clipRect.z || topLeftFragment.y >= clipRect.w)
    {
        discard;
    }

    if (uPoolLayer > 9.5) { outColor = polarEffect(poolCoord,color,uPoolCounter,int(uPoolLayer+.5)); return; }
    if (uPoolLayer > 6.5) { outColor = shaderPreview(poolCoord,color,uPoolCounter,int(uPoolLayer+.5)); return; }
    if (uPoolLayer > 3.5 && uPoolLayer < 4.5) { outColor = waterGaugeFace(poolCoord, color.x, color.y, color.z); return; }
    if (uPoolLayer > 2.5) {
        float mask = floodMask(poolCoord,color,waterWorldCoord,uPoolCounter);
        if (uPoolLayer > 5.5) outColor = vec4(causticAlphaAt(floodCausticUV(waterWorldCoord,uPoolCounter))*.75)*mask;
        else outColor = texture(uTexture,floodTextureUV(waterWorldCoord,uPoolCounter))*mask;
        return;
    }

    if (uPoolLayer > 1.5)
    {
        outColor = vec4(causticAlphaAt(poolCoord)) * color;
    }
    else
    {
        vec4 sampled = texture(uTexture, poolCoord);
        outColor = vec4(sampled.rgb * color.rgb * color.a, sampled.a * color.a);
    }
}
