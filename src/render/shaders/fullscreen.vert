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
    vUv = p;
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
