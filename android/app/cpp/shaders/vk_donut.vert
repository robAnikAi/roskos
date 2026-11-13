#version 450

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUv;

layout(set = 0, binding = 0) uniform Frame {
    mat4 viewProjection;
    vec4 cameraPosition; // xyz + time
} frame;

layout(push_constant) uniform PushConstants {
    mat4 model;
    mat3 normalMatrix;
} push;

layout(location = 0) out vec3 vWorld;
layout(location = 1) out vec3 vNormal;
layout(location = 2) out vec2 vUv;

void main() {
    vec4 world = push.model * vec4(aPosition, 1.0);
    vWorld = world.xyz;
    vNormal = push.normalMatrix * aNormal;
    vUv = aUv;
    gl_Position = frame.viewProjection * world;
}
