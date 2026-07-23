#version 450

layout(location = 0) out vec3 outDir;
layout(binding = 0) uniform UniformBuffer { mat4 VP; } ubo;

const vec3 positions[8] = vec3[8](
    vec3(-1.0,-1.0, 1.0), vec3( 1.0,-1.0, 1.0), vec3( 1.0, 1.0, 1.0), vec3(-1.0, 1.0, 1.0),
    vec3(-1.0,-1.0,-1.0), vec3( 1.0,-1.0,-1.0), vec3( 1.0, 1.0,-1.0), vec3(-1.0, 1.0,-1.0)
);
const int indices[36] = int[36](
    1, 0, 2, 3, 2, 0, 5, 1, 6, 2, 6, 1, 6, 7, 5, 4, 5, 7,
    0, 4, 3, 7, 3, 4, 5, 4, 1, 0, 1, 4, 2, 3, 6, 7, 6, 3
);

void main() {
    int idx = indices[gl_VertexIndex];
    vec4 pos = vec4(positions[idx], 1.0);
    vec4 clipPos = ubo.VP * pos;
    gl_Position = clipPos.xyww;
    outDir = positions[idx];
}