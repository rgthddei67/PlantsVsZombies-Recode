#version 330 core

layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec2 inGridCoord;
layout(location = 2) in vec4 inColor;
layout(location = 3) in vec4 inClipRect;

uniform mat4 uProjectionView;
uniform float uPoolLayer;
uniform float uPoolCounter;

out vec2 poolCoord;
out vec2 waterWorldCoord;
out vec4 color;
flat out vec4 clipRect;

const float Pi = 3.14159265358979323846;

vec2 poolWaveOffset(vec2 grid)
{
    float x = grid.x * 15.0;
    float y = grid.y * 5.0;
    if (x <= 0.0001 || x >= 14.9999 || y <= 0.0001 || y >= 4.9999)
    {
        return vec2(0.0);
    }

    float poolPhase = uPoolCounter * 2.0 * Pi;
    float waveTime1 = poolPhase / 800.0;
    float waveTime2 = poolPhase / 150.0;
    float waveTime3 = poolPhase / 900.0;
    float waveTime4 = poolPhase / 800.0;
    float waveTime5 = poolPhase / 110.0;
    float xPhase = x * 3.0 * 2.0 * Pi / 15.0;
    float yPhase = y * 3.0 * 2.0 * Pi / 5.0;

    if (uPoolLayer < 0.5)
    {
        return vec2(
            sin(yPhase + waveTime2) * 0.002 + sin(yPhase + waveTime1) * 0.005,
            sin(xPhase + waveTime5) * 0.01 + sin(xPhase + waveTime3) * 0.015 + sin(xPhase + waveTime4) * 0.005);
    }
    if (uPoolLayer < 1.5)
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
    poolCoord = inGridCoord + (uPoolLayer > 2.5 ? vec2(0.0) : poolWaveOffset(inGridCoord));
    if (uPoolLayer > 1.5 && uPoolLayer < 2.5)
    {
        poolCoord += inGridCoord;
    }
    gl_Position = uProjectionView * vec4(inPosition, 0.0, 1.0);
    waterWorldCoord = inPosition;
    color = inColor;
    clipRect = inClipRect;
}
