#version 440

// Source plus glow, with the dispersion that makes the reference look the way
// it does.
//
// The striking part of that image is not the bloom, it is that lines near the
// centre are sharp and white while lines towards the edges split into warm and
// cool fringes and smear outwards. That is lateral chromatic aberration: a real
// lens focuses red, green and blue at slightly different places, and the error
// grows with distance from the optical axis. Sampling each channel at its own
// radial offset reproduces it, and because the offset grows with the square of
// the radius the centre stays clean without any special case.
//
// The radial smear on the glow is the second half. Taps are taken along the
// direction away from the centre rather than in a circle, so the flare
// stretches outwards like something seen through a lens rather than fogging
// evenly in all directions.

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
layout(binding = 2) uniform sampler2D bloom;

void main()
{
    // Distance from the centre, corrected so a wide strip disperses by the same
    // amount at its left edge as a square would. Without this the aberration on
    // a 236x44 item is five times stronger horizontally than vertically and
    // reads as a horizontal smear rather than a lens.
    vec2 fromCentre = vUv - vec2(0.5);
    float r = length(fromCentre);
    vec2 dir = r > 0.0001 ? fromCentre / r : vec2(0.0);

    float shift = u.dispersion * r * r;

    // The core is dispersed barely at all, and the glow a great deal.
    //
    // Splitting the source itself was the first attempt and it was wrong: a
    // one-pixel line shifted by several pixels per channel stops overlapping
    // itself, so the red, green and blue land on empty background and the line
    // renders as three separate coloured lines -- most visibly a pure green
    // one, where only the unshifted channel hit. In the reference the bright
    // core of each line is white and sharp; it is the halo around it that
    // splits. So the core keeps its registration and the dispersion is spent
    // on the bloom, which is broad enough to survive being pulled apart.
    vec4 base;
    float coreShift = shift * 0.15;
    base.r = texture(source, vUv - dir * coreShift).r;
    base.g = texture(source, vUv).g;
    base.b = texture(source, vUv + dir * coreShift).b;
    base.a = texture(source, vUv).a;

    // The glow gets a stronger shift and a smear along the same axis.
    vec4 glow = vec4(0.0);
    const int kTaps = 6;
    float span = u.radialBlur * r;
    for (int i = 0; i < kTaps; ++i) {
        float t = float(i) / float(kTaps - 1);          // 0..1
        vec2 offset = dir * (span * t);
        float w = 1.0 - t * 0.6;                        // nearer taps count more
        glow.r += texture(bloom, vUv + offset - dir * shift * 2.0).r * w;
        glow.g += texture(bloom, vUv + offset).g * w;
        glow.b += texture(bloom, vUv + offset + dir * shift * 2.0).b * w;
        glow.a += texture(bloom, vUv + offset).a * w;
    }
    glow /= float(kTaps) * 0.7;
    // The glow's own dispersion is what carries the colour, so it is scaled up
    // rather than down; the taps above already spread it far enough that the
    // channels still overlap.

    vec3 rgb = base.rgb + glow.rgb * u.intensity;
    float a = base.a + glow.a * u.intensity;
    a = clamp(a, 0.0, 1.0);

    // Everything here is premultiplied, and premultiplied means rgb never
    // exceeds a. Adding light can break that, and a buffer that breaks it shows
    // up in Core Animation as bright fringes where the glow meets transparency.
    fragColor = vec4(min(rgb, vec3(a)), a);
}
