#version 440

// The face of the welcome window's filled button (qml/Welcome.qml,
// FillButton), over its fire (buttonfire.frag): a flat fill with a fresnel
// border, a rim of light that is brightest at the edge and gone a few pixels
// in, and a highlight that travels round it. `heat` (0 at rest, 1 hovered)
// brightens both and speeds the highlight up a little.
//
// Written for this app; the same file is in hyperbin.

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float time;      // seconds, wrapping
    float heat;      // 0 at rest .. 1 hovered
    float press;     // 0 .. 1 while held
    float radius;    // corner radius, px
    vec2 size;       // the face, px
    vec4 color;      // the fill
};

const float PI = 3.14159265359;

float roundBox(vec2 p, vec2 halfSize, float r)
{
    vec2 q = abs(p) - halfSize + r;
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
}

void main()
{
    vec2 p = qt_TexCoord0 * size - 0.5 * size;   // px from the centre, y down
    vec2 halfSize = 0.5 * size;
    float d = roundBox(p, halfSize, radius);     // negative inside
    float cover = clamp(0.5 - d, 0.0, 1.0);

    // The rim: 1 at the edge, falling off inward. A little stronger along
    // the top, where light from above would catch it.
    float rim = exp(min(d, 0.0) / mix(2.2, 3.2, heat));
    float top = 0.75 + 0.25 * smoothstep(halfSize.y, -halfSize.y, p.y);
    float edge = rim * top * mix(0.38, 0.62, heat);

    // The highlight: a bright arc going round, found by angle on the face
    // squashed to a circle so it keeps a steady pace along the long sides.
    float angle = atan(p.y / halfSize.y, p.x / halfSize.x);
    float at = mod(time * mix(0.18, 0.28, heat), 1.0) * 2.0 * PI - PI;
    float gap = abs(mod(angle - at + PI, 2.0 * PI) - PI);
    float sweep = exp(-gap * gap / 0.18) + 0.6 * exp(-pow(gap - PI, 2.0) / 0.10);
    float arc = rim * sweep * mix(0.45, 0.95, heat);

    vec3 light = mix(color.rgb, vec3(1.0), 0.7);
    vec3 col = color.rgb * (1.0 - 0.10 * press);
    col = mix(col, light, clamp(edge + arc, 0.0, 1.0));
    fragColor = vec4(col, 1.0) * cover * qt_Opacity;
}
