#version 440

// One axis of a Gaussian blur, run twice.
//
// Separable, because a 9-tap two-pass blur is 18 samples where the equivalent
// 9x9 kernel is 81. The direction uniform picks the axis, so both passes are
// this shader with a different uniform rather than two nearly identical files
// that drift apart.
//
// The taps use the linear-sampling trick: five weighted samples placed between
// texels give the same result as nine, because the sampler interpolates the
// pair for free.

layout(location = 0) in vec2 vUv;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform Block {
    vec2 texelSize;
    float threshold;
    float knee;
    float intensity;
    float direction;   // 0 horizontal, 1 vertical
    float dispersion;
    float radialBlur;
    float rayLength;
    float rayDecay;
} u;

layout(binding = 1) uniform sampler2D source;

const float kOffset[3] = float[](0.0, 1.3846153846, 3.2307692308);
const float kWeight[3] = float[](0.2270270270, 0.3162162162, 0.0702702703);

void main()
{
    vec2 axis = u.direction < 0.5 ? vec2(1.0, 0.0) : vec2(0.0, 1.0);
    vec2 step = axis * u.texelSize;

    vec4 sum = texture(source, vUv) * kWeight[0];
    for (int i = 1; i < 3; ++i) {
        sum += texture(source, vUv + step * kOffset[i]) * kWeight[i];
        sum += texture(source, vUv - step * kOffset[i]) * kWeight[i];
    }
    fragColor = sum;
}
