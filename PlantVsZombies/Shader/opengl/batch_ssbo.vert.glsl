#version 430 core

layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec2 inTexCoord;
layout(location = 2) in vec4 inColor;
layout(location = 3) in uvec2 inPackedClipRect;
layout(location = 4) in uint inMatrixIndex;

layout(std430, binding = 0) readonly buffer BatchMatrices
{
    mat4 matrices[];
};

layout(location = 5) in float inDrawMode;

uniform mat4 uProjectionView;

out vec2 worldPosition;
flat out int primitiveKind;
out vec2 texCoord;
out vec4 color;
flat out vec4 clipRect;

void main()
{
    vec4 world = matrices[inMatrixIndex] * vec4(inPosition, 0.0, 1.0);
    gl_Position = uProjectionView * world;
    worldPosition = world.xy;
    primitiveKind = int(inDrawMode) / 4;
    texCoord = inTexCoord;
    color = inColor;
    clipRect = vec4(
        float(inPackedClipRect.x & 0xFFFFu),
        float(inPackedClipRect.x >> 16),
        float(inPackedClipRect.y & 0xFFFFu),
        float(inPackedClipRect.y >> 16));
}
