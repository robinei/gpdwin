#version 450
layout(push_constant) uniform P { float z; } p;
void main() { vec2 v = vec2((gl_VertexIndex & 1) * 4.0 - 1.0, (gl_VertexIndex >> 1) * 4.0 - 1.0); gl_Position = vec4(v, p.z, 1.0); }
