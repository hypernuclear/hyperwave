#version 440

layout(location = 0) in vec4 vColor;
layout(location = 0) out vec4 fragColor;

void main()
{
    // Already premultiplied in the vertex shader; the pipeline blends with
    // (One, OneMinusSrcAlpha) and the target is ARGB32_Premultiplied, so the
    // pixels are in the right form the whole way to Core Animation with no
    // conversion pass anywhere.
    fragColor = vColor;
}
