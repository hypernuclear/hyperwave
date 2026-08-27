#version 440

// Volumetric rays, the Trapcode Shine idea.
//
// A Gaussian blur spreads light evenly in all directions, which is what a lens
// does to something out of focus. This is a different phenomenon: light
// scattering along the line between the source and the eye, so it streaks
// *towards or away from a point* rather than fogging. It is what makes the
// reference image look like the lines are receding to a vanishing point --
// every bright thing trails away from the centre of the frame.
//
// The implementation is the oldest trick there is: march from this pixel
// towards the centre, sampling the bright pass and letting each step count for
// a little less than the last. Whatever bright thing lies along that line
// smears down it.

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

void main()
{
    const int kSteps = 24;

    vec2 toCentre = vec2(0.5) - vUv;
    // Each step covers rayLength of the distance to the centre, divided among
    // the taps, so a pixel at the edge streaks further than one near the middle
    // and the whole field converges rather than sliding uniformly.
    vec2 step = toCentre * (u.rayLength / float(kSteps));

    vec4 sum = vec4(0.0);
    float weight = 1.0;
    float total = 0.0;
    vec2 uv = vUv;
    for (int i = 0; i < kSteps; ++i) {
        sum += texture(source, uv) * weight;
        total += weight;
        weight *= u.rayDecay;
        uv += step;
    }
    fragColor = total > 0.0 ? sum / total : vec4(0.0);
}
