#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

#include "host_device_common.h"
#include "ray_common.glsl"

layout (location = 0) rayPayloadInEXT hitPayload prd;

layout (set = eSkybox, binding = 0) uniform samplerCube skyboxSampler;

void main()
{
	// prd.hitValue = texture(skyboxSampler, normalize(gl_WorldRayDirectionEXT)).rgb;
	prd.hitValue = vec3(253.0f / 255.0f, 251.0f / 255.0f, 211.0f / 255.0f) * 5;
	// end path
	prd.depth = 100;
	prd.matID = -1;
}