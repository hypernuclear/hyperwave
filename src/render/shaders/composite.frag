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
    float rayLength;
    float rayDecay;
} u;

layout(binding = 1) uniform sampler2D source;
layout(binding = 2) uniform sampler2D bloom;
layout(binding = 3) uniform sampler2D rays;

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
    // A single sample now. The radial smear that used to live here as six
    // taps along the radius is the rays pass, which does it over the bright
    // pass with twenty-four taps and a decay -- doing both meant paying twice
    // for the same effect and getting a muddier version of it.
    vec4 glow;
    glow.r = texture(bloom, vUv - dir * shift * 2.0).r;
    glow.g = texture(bloom, vUv).g;
    glow.b = texture(bloom, vUv + dir * shift * 2.0).b;
    glow.a = texture(bloom, vUv).a;
    // The glow's own dispersion is what carries the colour, so it is scaled up
    // rather than down; the taps above already spread it far enough that the
    // channels still overlap.

    // The rays are already directional, so they take the dispersion too but
    // not the smear: streaking a streak just blurs it back into a fog.
    vec4 ray;
    ray.r = texture(rays, vUv - dir * shift * 2.0).r;
    ray.g = texture(rays, vUv).g;
    ray.b = texture(rays, vUv + dir * shift * 2.0).b;
    ray.a = texture(rays, vUv).a;

    vec3 rgb = base.rgb + glow.rgb * u.intensity + ray.rgb * u.radialBlur * 8.0;
    float a = base.a + glow.a * u.intensity + ray.a * u.radialBlur * 8.0;
    a = clamp(a, 0.0, 1.0);

    // Everything here is premultiplied, and premultiplied means rgb never
    // exceeds a. Adding light can break that, and a buffer that breaks it shows
    // up in Core Animation as bright fringes where the glow meets transparency.
    fragColor = vec4(min(rgb, vec3(a)), a);
}
