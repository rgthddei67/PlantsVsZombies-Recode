#version 450

// 原版 PoolEffect 的 15x5 三层网格。CPU 只提交规则网格；本 shader 按动画计数器
// 生成底图、阴影和焦散各自的 UV 波动，边界顶点保持固定以避免露出背景缝隙。
layout(set = 0, binding = 0) readonly buffer MatrixBlock {
    mat4 m[];
} matrices;

layout(push_constant) uniform PoolConstants {
    mat4 projView;
    float poolLayer;
    float poolCounter;
    vec2 padding;
} pc;

layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec2 inGridCoord;
layout(location = 2) in uvec2 inIndices;
layout(location = 3) in vec4 inColor;
layout(location = 4) in uvec2 inPackedClip;

layout(location = 0) out vec2 outTexCoord;
layout(location = 1) out vec2 outPoolCoord;
layout(location = 2) out vec4 outColor;
layout(location = 3) flat out uint outTextureIndex;
layout(location = 4) flat out uvec2 outPackedClip;

const float Pi = 3.14159265358979323846;

vec2 poolWaveOffset(vec2 grid)
{
    float x = grid.x * 15.0;
    float y = grid.y * 5.0;
    if (x <= 0.0001 || x >= 14.9999 || y <= 0.0001 || y >= 4.9999)
    {
        return vec2(0.0);
    }

    float poolPhase = pc.poolCounter * 2.0 * Pi;
    float waveTime1 = poolPhase / 800.0;
    float waveTime2 = poolPhase / 150.0;
    float waveTime3 = poolPhase / 900.0;
    float waveTime4 = poolPhase / 800.0;
    float waveTime5 = poolPhase / 110.0;
    float xPhase = x * 3.0 * 2.0 * Pi / 15.0;
    float yPhase = y * 3.0 * 2.0 * Pi / 5.0;

    if (pc.poolLayer < 0.5)
    {
        return vec2(
            sin(yPhase + waveTime2) * 0.002 + sin(yPhase + waveTime1) * 0.005,
            sin(xPhase + waveTime5) * 0.01 + sin(xPhase + waveTime3) * 0.015 + sin(xPhase + waveTime4) * 0.005);
    }

    if (pc.poolLayer < 1.5)
    {
        return vec2(
            sin(yPhase * 0.2 + waveTime2) * 0.015 + sin(yPhase * 0.2 + waveTime1) * 0.012,
            sin(xPhase * 0.2 + waveTime5) * 0.005 + sin(xPhase * 0.2 + waveTime3) * 0.015 + sin(xPhase * 0.2 + waveTime4) * 0.02);
    }

    return vec2(
        sin(yPhase + waveTime1 * 1.5) * 0.004 + sin(yPhase + waveTime2 * 1.5) * 0.005,
        sin(xPhase * 4.0 + waveTime5 * 2.5) * 0.005 + sin(xPhase * 2.0 + waveTime3 * 2.5) * 0.04 + sin(xPhase * 3.0 + waveTime4 * 2.5) * 0.02);
}

void main()
{
    vec2 poolCoord = inGridCoord + (pc.poolLayer > 2.5 ? vec2(0.0) : poolWaveOffset(inGridCoord));
    if (pc.poolLayer > 1.5 && pc.poolLayer < 2.5)
    {
        // 原版焦散图在水面上平铺两个周期。
        poolCoord += inGridCoord;
    }

    mat4 model = matrices.m[inIndices.y];
    gl_Position = pc.projView * model * vec4(inPosition, 0.0, 1.0);
    outTexCoord = poolCoord;
    outPoolCoord = pc.poolLayer > 2.5 ? (model * vec4(inPosition,0.0,1.0)).xy : poolCoord;
    outColor = inColor;
    outTextureIndex = inIndices.x;
    outPackedClip = inPackedClip;
}
