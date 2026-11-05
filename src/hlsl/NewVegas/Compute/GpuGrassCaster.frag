// UNOFFICIAL 2026-11-04 GPU grass into the flashlight's shadow map (effects/GpuGrass.cpp, the strands' GG_CASTER vertex shaders): the
// distance to the light over its reach, as the spot map holds it (R32F), depth-tested against the map's own depth so the nearer stays.
//
// Build: glslangValidator -V --target-env vulkan1.1 -o GpuGrassCaster.spv GpuGrassCaster.frag
#version 450

layout(location = 1) in vec4 i_spot;   // a the distance to the light over its reach
layout(location = 0) out vec4 o_color;

void main() {
    o_color = vec4(i_spot.a, 0.0, 0.0, 1.0);
}
