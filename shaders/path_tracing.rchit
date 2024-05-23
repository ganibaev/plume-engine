#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_EXT_scalar_block_layout : enable
#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_EXT_buffer_reference2 : require

#include "host_device_common.h"
#include "ray_common.glsl"

layout (location = 0) rayPayloadInEXT hitPayload prd;
hitAttributeEXT vec3 hitUV;

layout (buffer_reference, scalar) buffer Vertices
{
	Vertex v[];
};
layout (buffer_reference, scalar) buffer Indices
{
	uvec3 i[];
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
	
	HitProperties hp = getHitProperties(instId, primId, objectToWorld, worldToObject, hitUV.xy);

	prd.hitPosition = hp.worldPos;
	prd.matID = hp.matID;
	prd.texCoord = hp.texCoord;
	prd.tangent = hp.tangent;
	prd.bitangent = hp.bitangent;
	prd.emittance = hp.emittance;
	prd.normal = hp.normal;
}