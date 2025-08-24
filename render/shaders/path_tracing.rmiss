#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

#include "host_device_common.h"
#include "ray_common.glsl"

layout (location = 0) rayPayloadInEXT RayPayload rayPayload;

layout (set = eSkybox, binding = 0) uniform samplerCube skyboxSampler;

void main()
{
//	vec3 rayMissDirection = normalize(gl_WorldRayDirectionEXT);
//	vec3 missValue = texture(skyboxSampler, rayMissDirection).rgb;
	vec3 missValue = vec3(253.0f / 255.0f, 251.0f / 255.0f, 211.0f / 255.0f) * 5;
	OnRayMiss(rayPayload, missValue);
}