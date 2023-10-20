#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

#include "host_device_common.h"
#include "ray_common.glsl"

layout (location = 0) rayPayloadInEXT hitPayload prd;

void main()
{
	prd.hitValue = vec3(226.0 / 255.0, 249.0 / 255.0, 1.0) * 7.5;
	// end path
	prd.depth = 100;
}