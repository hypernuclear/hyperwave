#version 440

// A bundle of strands wrapped around an axis, displaced by the spectrum.
//
// Every strand is the same straight line along X, uploaded once and never
// touched again. All the motion happens here: the line is wrapped around the
// axis at a radius that the audio and three octaves of noise push around, and
// then expanded to a ribbon in screen space. The CPU's whole job per frame is
// one 256-byte spectrum texture and a clock.
//
// X is frequency. position.x runs -1..1 along the strand and indexes the
// spectrum directly, so bass is at one end and treble at the other, and the
// tube swells where the music is loud.

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 previous;
layout(location = 2) in vec3 next;
layout(location = 3) in float direction;

layout(std140, binding = 0) uniform Block {
    mat4 mvp;
    vec4 colorA;
    vec4 colorB;
    float aspect;
    float time;
    float radius;
    float thickness;
    float twists;
    float opacity;
    float strandCount;
    float audioGain;
} u;

layout(binding = 1) uniform sampler2D audioTexture;

layout(location = 0) out vec4 vColor;

const float PI = 3.14159265;

// Simplex noise, Ian McEwan / Ashima Arts, MIT. See THIRD-PARTY-NOTICES.md.
vec3 mod289(vec3 x) { return x - floor(x * (1.0 / 289.0)) * 289.0; }
vec4 mod289(vec4 x) { return x - floor(x * (1.0 / 289.0)) * 289.0; }
vec4 permute(vec4 x) { return mod289(((x * 34.0) + 1.0) * x); }
vec4 taylorInvSqrt(vec4 r) { return 1.79284291400159 - 0.85373472095314 * r; }

float snoise(vec3 v)
{
    const vec2 C = vec2(1.0 / 6.0, 1.0 / 3.0);
    const vec4 D = vec4(0.0, 0.5, 1.0, 2.0);

    vec3 i = floor(v + dot(v, C.yyy));
    vec3 x0 = v - i + dot(i, C.xxx);

    vec3 g = step(x0.yzx, x0.xyz);
    vec3 l = 1.0 - g;
    vec3 i1 = min(g.xyz, l.zxy);
    vec3 i2 = max(g.xyz, l.zxy);

    vec3 x1 = x0 - i1 + C.xxx;
    vec3 x2 = x0 - i2 + C.yyy;
    vec3 x3 = x0 - D.yyy;

    i = mod289(i);
    vec4 p = permute(permute(permute(i.z + vec4(0.0, i1.z, i2.z, 1.0))
                             + i.y + vec4(0.0, i1.y, i2.y, 1.0))
                     + i.x + vec4(0.0, i1.x, i2.x, 1.0));

    float n_ = 0.142857142857;
    vec3 ns = n_ * D.wyz - D.xzx;

    vec4 j = p - 49.0 * floor(p * ns.z * ns.z);

    vec4 x_ = floor(j * ns.z);
    vec4 y_ = floor(j - 7.0 * x_);

    vec4 x = x_ * ns.x + ns.yyyy;
    vec4 y = y_ * ns.x + ns.yyyy;
    vec4 h = 1.0 - abs(x) - abs(y);

    vec4 b0 = vec4(x.xy, y.xy);
    vec4 b1 = vec4(x.zw, y.zw);

    vec4 s0 = floor(b0) * 2.0 + 1.0;
    vec4 s1 = floor(b1) * 2.0 + 1.0;
    vec4 sh = -step(h, vec4(0.0));

    vec4 a0 = b0.xzyw + s0.xzyw * sh.xxyy;
    vec4 a1 = b1.xzyw + s1.xzyw * sh.zzww;

    vec3 p0 = vec3(a0.xy, h.x);
    vec3 p1 = vec3(a0.zw, h.y);
    vec3 p2 = vec3(a1.xy, h.z);
    vec3 p3 = vec3(a1.zw, h.w);

    vec4 norm = taylorInvSqrt(vec4(dot(p0, p0), dot(p1, p1), dot(p2, p2), dot(p3, p3)));
    p0 *= norm.x;
    p1 *= norm.y;
    p2 *= norm.z;
    p3 *= norm.w;

    vec4 m = max(0.6 - vec4(dot(x0, x0), dot(x1, x1), dot(x2, x2), dot(x3, x3)), 0.0);
    m = m * m;
    return 42.0 * dot(m * m, vec4(dot(p0, x0), dot(p1, x1), dot(p2, x2), dot(p3, x3)));
}

float turb(float x, float angle, float alpha, float scale, float offset)
{
    return alpha * snoise(vec3(x * scale, angle, offset));
}

void main()
{
    // gl_InstanceIndex is the strand. Instancing rather than a draw call per
    // strand, which is what the original does: same picture, one submission.
    float index = u.strandCount > 1.0
                      ? float(gl_InstanceIndex) / (u.strandCount - 1.0)
                      : 0.0;

    float frequencies =
        clamp(texture(audioTexture, vec2(position.x * 0.5 + 0.5, 0.5)).r * u.audioGain, 0.0, 1.0);

    // Tapers the tube to a point at both ends. This is what gives the shape its
    // silhouette rather than letting it end in a blunt cylinder.
    float pinch = smoothstep(0.0, 0.5, 1.0 - abs(position.x));

    float angleOffset = index * PI * mix(0.5, 1.5, frequencies);
    float angle = 2.0 * PI * position.x * u.twists + angleOffset + u.time;

    float computedRadius = u.radius;
    computedRadius += turb(position.x, angle, frequencies * 0.5, 1.0, u.time * 0.2);
    computedRadius += turb(position.x, angle, 0.1, 6.0, u.time * 0.25);
    computedRadius += turb(position.x, angle, 0.1, 2.5, u.time * 0.5);
    computedRadius *= pinch;
    computedRadius *= mix(0.65, 1.0, frequencies);

    vec3 offset = vec3(0.0, cos(angle) * computedRadius, sin(angle) * computedRadius);

    // The same offset goes on all three points, as in the original. The
    // neighbours are only there to give the segment a screen-space direction,
    // and displacing each by its own radius would cost three noise evaluations
    // per vertex to move the ribbon's edge by less than a pixel.
    vec2 aspectVec = vec2(u.aspect, 1.0);
    vec4 currentProjected = u.mvp * vec4(position + offset, 1.0);
    vec4 previousProjected = u.mvp * vec4(previous + offset, 1.0);
    vec4 nextProjected = u.mvp * vec4(next + offset, 1.0);

    vec2 currentScreen = currentProjected.xy / currentProjected.w * aspectVec;
    vec2 previousScreen = previousProjected.xy / previousProjected.w * aspectVec;
    vec2 nextScreen = nextProjected.xy / nextProjected.w * aspectVec;

    vec2 dir;
    if (currentScreen == previousScreen)
        dir = normalize(nextScreen - currentScreen);
    else if (currentScreen == nextScreen)
        dir = normalize(currentScreen - previousScreen);
    else
        dir = normalize(currentScreen - previousScreen);

    vec2 normal = vec2(-dir.y, dir.x);
    normal *= (u.thickness * pinch) / 2.0;
    normal.x /= u.aspect;

    gl_Position = currentProjected + vec4(normal * direction, 0.0, 0.0);

    // Colour runs along the strand, so the sweep follows frequency rather than
    // depth, and the loud end is also the bright one.
    float t = position.x * 0.5 + 0.5;
    vec3 rgb = mix(u.colorA.rgb, u.colorB.rgb, t);
    float a = u.opacity * mix(0.35, 1.0, frequencies) * pinch;
    vColor = vec4(rgb * a, a);   // premultiplied, to match ARGB32_Premultiplied
}
