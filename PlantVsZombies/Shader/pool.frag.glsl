#version 450
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_GOOGLE_include_directive : require

layout(set = 1, binding = 1) uniform sampler2D textures[];

layout(push_constant) uniform PoolConstants {
    mat4 projView;
    float poolLayer;
    float poolCounter;
    vec2 padding;
} pc;

layout(location = 0) in vec2 inTexCoord;
layout(location = 1) in vec2 inPoolCoord;
layout(location = 2) in vec4 inColor;
layout(location = 3) flat in uint inTextureIndex;
layout(location = 4) flat in uvec2 inPackedClip;

layout(location = 0) out vec4 outColor;

float sampleCausticByte(vec2 pixel)
{
    vec2 basePixel = floor(pixel);
    vec2 fraction = pixel - basePixel;
    vec2 p00 = mod(basePixel, 256.0);
    vec2 p10 = mod(basePixel + vec2(1.0, 0.0), 256.0);
    vec2 p01 = mod(basePixel + vec2(0.0, 1.0), 256.0);
    vec2 p11 = mod(basePixel + vec2(1.0, 1.0), 256.0);
    vec2 texelSize = 1.0 / vec2(textureSize(textures[nonuniformEXT(inTextureIndex)], 0));
    float c00 = texture(textures[nonuniformEXT(inTextureIndex)], (p00 + vec2(0.5)) * texelSize).r;
    float c10 = texture(textures[nonuniformEXT(inTextureIndex)], (p10 + vec2(0.5)) * texelSize).r;
    float c01 = texture(textures[nonuniformEXT(inTextureIndex)], (p01 + vec2(0.5)) * texelSize).r;
    float c11 = texture(textures[nonuniformEXT(inTextureIndex)], (p11 + vec2(0.5)) * texelSize).r;
    return floor(clamp(mix(mix(c00, c10, fraction.x), mix(c01, c11, fraction.x), fraction.y), 0.0, 1.0) * 255.0 + 0.0001);
}

float causticAlphaAt(vec2 coord)
{
    vec2 causticPixel = fract(coord) * vec2(128.0, 64.0);
    float a1 = sampleCausticByte(vec2(
        causticPixel.x * 2.0 - ((pc.poolCounter + 1.0) / 6.0),
        (256.0 - causticPixel.y) * 2.0 + pc.poolCounter / 8.0));
    float a0 = sampleCausticByte(vec2(
        causticPixel.x * 2.0 + pc.poolCounter / 10.0,
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

#include "opengl/water_effects.glsl"

vec4 previewSource(vec2 uv) { return texture(textures[nonuniformEXT(inTextureIndex)],uv); }
#include "opengl/shader_preview.glsl"

void main()
{
    // 与通用 batch shader 共用逐顶点裁剪契约，不引入动态 scissor。
    if ((inPackedClip.x | ~inPackedClip.y) != 0u)
    {
        uvec2 clipMin = uvec2(inPackedClip.x & 0xFFFFu, inPackedClip.x >> 16);
        uvec2 clipMax = uvec2(inPackedClip.y & 0xFFFFu, inPackedClip.y >> 16);
        vec2 frag = gl_FragCoord.xy;
        if (any(lessThan(frag, vec2(clipMin))) ||
            any(greaterThanEqual(frag, vec2(clipMax))))
        {
            discard;
        }
    }

    if (pc.poolLayer > 6.5) { outColor = shaderPreview(inTexCoord,inColor,pc.poolCounter,int(pc.poolLayer+.5)); return; }
    if (pc.poolLayer > 3.5 && pc.poolLayer < 4.5) { outColor = waterGaugeFace(inTexCoord, inColor.x, inColor.y, inColor.z); return; }
    if (pc.poolLayer > 2.5) {
        float mask = floodMask(inTexCoord,inColor,inPoolCoord,pc.poolCounter);
        if (pc.poolLayer > 5.5) outColor = vec4(causticAlphaAt(floodCausticUV(inPoolCoord,pc.poolCounter))*.75)*mask;
        else outColor = texture(textures[nonuniformEXT(inTextureIndex)],floodTextureUV(inPoolCoord,pc.poolCounter))*mask;
        return;
    }

    if (pc.poolLayer > 1.5)
    {
        // inColor 的 RGB/A 已按原版顶点亮度同步编码，输出保持预乘 Alpha。
        outColor = vec4(causticAlphaAt(inPoolCoord)) * inColor;
    }
    else
    {
        // 资源上传时已预乘 Alpha；底图与阴影层顶点色恒为白色。
        outColor = texture(textures[nonuniformEXT(inTextureIndex)], inTexCoord) * inColor;
    }
}
