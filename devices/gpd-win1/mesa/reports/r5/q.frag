#version 450
layout(location = 0) out vec4 c;
layout(push_constant) uniform P { float z; } p;
void main() { c = vec4(p.z, 0.5, 1.0 - p.z, 1.0); }
