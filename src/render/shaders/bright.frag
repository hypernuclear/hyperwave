#version 440

// What is allowed to glow.
//
// Everything above the threshold is kept and everything below is discarded, so
// the blur that follows spreads only the bright parts. Blurring the whole image
// and adding it back would fog the dark ground instead of making the light
// sources bloom.

layout(location = 0) in vec2 vUv;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform Block {
    vec2 texelSize;
    float threshold;
    float knee;
    float intensity;
    float direction;
    float dispersion;
    float radialBlur;
} u;

layout(binding = 1) uniform sampler2D source;

void main()
{
    vec4 c = texture(source, vUv);

    // Premultiplied in, premultiplied out. Luminance is measured on the
    // premultiplied colour on purpose: a faint line drawn at low alpha should
    // not bloom as though it were solid just because its unpremultiplied colour
    // is bright.
    float luma = dot(c.rgb, vec3(0.2126, 0.7152, 0.0722));

    // A soft knee rather than a hard cut, so a line brightening through the
    // threshold fades its glow up instead of switching it on.
    float t = smoothstep(u.threshold, u.threshold + max(u.knee, 0.0001), luma);
    fragColor = c * t;
}
