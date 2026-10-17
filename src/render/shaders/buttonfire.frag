#version 440

// The fire under the welcome window's filled button (qml/Welcome.qml,
// FillButton). Drawn BENEATH the button's face, in an item that runs out past
// it on every side and well above it: flames rise off the button's top edge,
// sparks drift up out of them, and a glow flickers round its outline. The face
// hides the rest. `heat` is 0 at rest (a low, dim fire) and 1 hovered (taller,
// brighter, with sparks).
//
// Written for this app; the same file is in hyperbin. Only the noise below
// is someone else's (Ashima Arts' simplex noise, MIT; THIRD-PARTY-NOTICES.md).

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float time;      // seconds, wrapping
    float heat;      // 0 at rest .. 1 hovered
    float radius;    // the face's corner radius, px
    vec2 size;       // the whole item, px
    vec4 face;       // the face's box in the item: x, y, width, height, px
    vec4 color;      // the fire's colour (the face's own)
};

// --- Ashima Arts' 3D simplex noise (MIT) ----------------------------------
//   Copyright (C) 2011 Ashima Arts. All rights reserved.
//   Distributed under the MIT License. https://github.com/ashima/webgl-noise
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
    p0 *= norm.x; p1 *= norm.y; p2 *= norm.z; p3 *= norm.w;
    vec4 m = max(0.6 - vec4(dot(x0, x0), dot(x1, x1), dot(x2, x2), dot(x3, x3)), 0.0);
    m = m * m;
    return 42.0 * dot(m * m, vec4(dot(p0, x0), dot(p1, x1), dot(p2, x2), dot(p3, x3)));
}
// ---------------------------------------------------------------------------

float hash(vec2 p)
{
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}

void main()
{
    vec2 px = qt_TexCoord0 * size;               // item px, y down
    vec2 centre = face.xy + 0.5 * face.zw;
    vec2 halfSize = 0.5 * face.zw;
    float h = face.w;                            // everything scales with the button's height
    float t = time;
    vec3 accent = color.rgb;

    // The glow: the distance out from the face's rounded outline, falling
    // away, breathing with slow noise.
    vec2 q = abs(px - centre) - halfSize + radius;
    float out_ = max(length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - radius, 0.0);
    float breathe = 0.78 + 0.22 * snoise(vec3(px * (1.6 / h), 1.2 * t));
    float glow = exp(-out_ / (0.24 * h)) * breathe * mix(0.30, 0.80, heat);

    // The fire's height, in px above its base, which sits just under the
    // face's top edge (the face hides it). Fuel runs along the face, thinning
    // to nothing over its rounded ends.
    float up = face.y + 2.0 - px.y;
    float across = (px.x - face.x) / face.z;
    float fuel = smoothstep(0.0, 0.24, across) * smoothstep(1.0, 0.76, across);
    // How tall a flame gets, in px: low at rest, a good deal taller hot.
    float tall = h * mix(0.55, 1.25, heat);

    // Flames: noise drifting upward, pushed sideways by more noise so the
    // tongues lick; a flame wherever the noise outlasts the height. Stretched
    // tall (coarse in y) so they come out as tongues, not blobs.
    float k = 2.4 / h;
    vec3 p = vec3(px.x * k, px.y * k * 0.55 + 1.7 * t, 0.5 * t);
    vec3 warp = vec3(snoise(1.6 * p + vec3(11.0, 0.0, 0.0)),
                     snoise(1.6 * p + vec3(0.0, 23.0, 0.0)), 0.0);
    float n = 0.5 + 0.5 * (0.65 * snoise(p + 0.35 * warp)
                           + 0.35 * snoise(2.1 * p + 0.35 * warp));
    float body = n * fuel * 1.45 - up / tall;
    float f = smoothstep(0.0, 0.6, body) * smoothstep(-1.5, 0.5, up);

    // Its colour, from the accent: dark and deep at the fringe, the accent in
    // the body, near white in the hottest middle.
    vec3 deep = accent * accent * 0.55;
    vec3 hot = mix(accent, vec3(1.0), 0.72);
    vec3 ramp = mix(deep, accent, smoothstep(0.0, 0.5, f));
    ramp = mix(ramp, hot, smoothstep(0.55, 1.0, f));
    vec3 fire = 1.4 * f * ramp * mix(0.55, 1.0, heat);

    // Sparks: one chance per cell of a grid that rises with time; a cell's
    // hash says whether it has one, where in the cell, how big, and how high
    // it lives. They sway as they go. Few at rest, more hot.
    float cellSize = 0.34 * h;
    vec2 sp = vec2(px.x, px.y + t * 1.9 * h) / cellSize;
    vec2 cell = floor(sp);
    float r = hash(cell);
    float r2 = hash(cell + 7.13);
    vec2 at = vec2(hash(cell + 3.31), hash(cell + 5.77)) - 0.5;
    at.x += 0.22 * sin(2.6 * t + 31.0 * r);
    float dotSize = 0.05 + 0.07 * r2;
    float spark = smoothstep(dotSize, dotSize * 0.25, length(fract(sp) - 0.5 - 0.6 * at));
    float lives = tall * (0.7 + 1.3 * r2);
    float alive = step(mix(0.82, 0.55, heat), r) * smoothstep(lives, lives * 0.4, up)
                * smoothstep(0.0, 0.15 * h, up) * fuel;
    vec3 sparks = spark * alive * mix(accent, vec3(1.0), 0.45) * mix(0.5, 1.0, heat);

    // Faded out before the item's own edges, so nothing ever shows as a box.
    float edge = smoothstep(0.0, 8.0, px.x) * smoothstep(0.0, 8.0, size.x - px.x)
               * smoothstep(0.0, 8.0, px.y) * smoothstep(0.0, 8.0, size.y - px.y);
    vec3 c = (max(fire, sparks) + accent * glow) * edge;
    float a = clamp(max(max(c.r, c.g), c.b), 0.0, 1.0);
    fragColor = vec4(min(c, vec3(a)), a) * qt_Opacity;
}
