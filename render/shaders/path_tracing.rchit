#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_EXT_scalar_block_layout : enable
#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_EXT_buffer_reference2 : require

#include "host_device_common.h"
#include "ray_common.glsl"

layout (location = 0) rayPayloadInEXT RayPayload rayPayload;
hitAttributeEXT vec3 hitUV;

layout (buffer_reference, scalar) buffer Vertices
{
	Vertex VERTICES[];
};
layout (buffer_reference, scalar) buffer Indices
{
	uvec3 INDICES[];
};

layout (set = eGeneralRTX, binding = 0) uniform accelerationStructureEXT topLevelAS;

layout (set = eObjectData, binding = 0, scalar) readonly buffer ObjectBuffer
{
	ObjectData objects[];
} objectBuffer;

layout (set = eNormalMap, binding = 0) uniform sampler2D normalMap[];

#include "hit_properties.glsl"


void main()
{
	int instId = gl_InstanceCustomIndexEXT;
	int primId = gl_PrimitiveID;
	mat4x3 objectToWorld = gl_ObjectToWorldEXT;
	mat4x3 worldToObject = gl_WorldToObjectEXT;
	
	HitProperties hp = GetHitProperties(instId, primId, objectToWorld, worldToObject, hitUV.xy);

	rayPayload.hasMissed = false;

	rayPayload.hitPosition = hp.worldPos;
	rayPayload.matID = hp.matID;
	rayPayload.texCoord = hp.texCoord;
	rayPayload.tangent = hp.tangent;
	rayPayload.bitangent = hp.bitangent;
	rayPayload.emittance = hp.emittance;
	rayPayload.normal = hp.normal;
}
