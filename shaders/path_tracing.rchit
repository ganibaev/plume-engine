#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_EXT_scalar_block_layout : enable
#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_EXT_buffer_reference2 : require

#include "host_device_common.h"
#include "ray_common.glsl"
#include "sampling.glsl"
#include "bsdf.glsl"

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

layout (set = eDiffuseTex, binding = 0) uniform sampler2D diffuseTex[];
layout (set = eMetallicTex, binding = 0) uniform sampler2D metallicTex[];
layout (set = eRoughnessTex, binding = 0) uniform sampler2D roughnessTex[];
layout (set = eNormalMap, binding = 0) uniform sampler2D normalMap[];

void main()
{
	ObjectData currentObject = objectBuffer.objects[gl_InstanceCustomIndexEXT];
	Indices curIndices = Indices(currentObject.indexBufferAddress);
	Vertices curVertices = Vertices(currentObject.vertexBufferAddress);

	uvec3 triangleInd = curIndices.i[gl_PrimitiveID];

	Vertex v0 = curVertices.v[triangleInd.x];
	Vertex v1 = curVertices.v[triangleInd.y];
	Vertex v2 = curVertices.v[triangleInd.z];

	const vec3 barycentrics = vec3(1.0 - hitUV.x - hitUV.y, hitUV.x, hitUV.y);

	// compute hit point coordinates
	const vec2 texCoord = v0.uv * barycentrics.x + v1.uv * barycentrics.y + v2.uv * barycentrics.z;

	// compute hit point tex coordinates
	const vec3 pos = v0.position * barycentrics.x + v1.position * barycentrics.y + v2.position * barycentrics.z;
	const vec3 worldPos = vec3(gl_ObjectToWorldEXT * vec4(pos, 1.0));

	prd.hitPosition = worldPos;

	int matID = currentObject.matIndex;

	// compute hit point normal
	const vec3 vertexNormal = v0.normal * barycentrics.x + v1.normal * barycentrics.y + v2.normal * barycentrics.z;

	// compute hit point tangent
	const vec3 tangent = v0.tangent * barycentrics.x + v1.tangent * barycentrics.y + v2.tangent * barycentrics.z;

	vec3 T = normalize(tangent);
	vec3 B = cross(vertexNormal, T);
	vec3 N = normalize(vec3(vertexNormal * gl_WorldToObjectEXT)); // world normal

	vec3 L = vec3(0.0);
	vec3 V = normalize(-prd.rayDirection);

	float pdf = 0.0;
	vec3 brdf = vec3(0.0);

	mat3 TBN = mat3(T, B, N);

	const vec4 normalTex = texture(normalMap[matID], texCoord);
	vec3 mappedNormal = TBN * normalize(normalTex.xyz * 2.0 - vec3(1.0));

	if (normalTex.w > 0.2)
	{
		N = mappedNormal;
	}

	vec3 emittance = currentObject.emittance;

	vec3 rayOrigin = worldPos;

	vec4 albedo = texture(diffuseTex[matID], texCoord);
	float metallic = texture(metallicTex[matID], texCoord).b;
	float roughness = texture(roughnessTex[matID], texCoord).g;

	float diffuseProb = 0.5 * (1.0 - metallic);
	float specProb = 1.0 - diffuseProb;

	if (rng(prd.seed) < diffuseProb)
	{
		L = sampleHemisphere(prd.seed, T, B, N);

		vec3 H = normalize(L + V);

		float dotNL = dot(N, L);
		float dotNV = dot(N, V);

		brdf = DiffuseBRDF(albedo.rgb, metallic, V, N, L, H, pdf);
		pdf *= diffuseProb;
	}
	else
	{
		vec3 H = sampleGGX(roughness, prd.seed, T, B, N);
		L = reflect(-V, H);

		brdf = SpecularBRDF(albedo.rgb, metallic, roughness, V, N, L, H, pdf);
		pdf *= specProb;
	}

	prd.rayOrigin = rayOrigin;
	prd.rayDirection = L;
	prd.hitValue = emittance;
	if (pdf > 0.001)
	{
		prd.weight = brdf * abs(dot(N, prd.rayDirection)) / pdf;
	}
	else
	{
		prd.weight = vec3(0.0);
	}
}