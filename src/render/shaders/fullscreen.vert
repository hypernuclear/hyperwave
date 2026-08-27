#version 440

// A fullscreen triangle from nothing but the vertex index, so a post-process
// pass needs no vertex buffer and no vertex input layout at all.
//
// One triangle rather than two: a quad's diagonal makes the GPU shade the
// pixels along it twice, and at four passes a frame that is free to avoid.

layout(location = 0) out vec2 vUv;

void main()
{
    vec2 p = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2));

    // Y flipped between the clip position and the texture coordinate.
    //
    // Clip space is y-up and an uploaded QImage is v-down, so taking the
    // texture coordinate straight from the vertex position samples the source
    // upside down -- and because the result is then read back into the same
    // QImage, every frame came out mirrored. It is invisible on a symmetric
    // look and glaring on the bars, whose peak dots ended up under the bars
    // instead of above them.
    vUv = vec2(p.x, 1.0 - p.y);
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
