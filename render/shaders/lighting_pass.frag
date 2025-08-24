#version 460
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_EXT_ray_tracing : enable
#extension GL_EXT_ray_query : enable

#include "common.glsl"
#include "bsdf.glsl"
#include "host_device_common.h"

#define TLAS_SLOT 0U

layout (location = 0) in vec2 inTexCoords;
layout (location = 0) out vec4 outFragColor;

layout (set = 1, binding = 0) uniform LightingPassBuffer
{
	CameraDataGPU camera;
	LightingData lighting;
};

layout (set = 0, binding = 0) uniform sampler2D positionTex;
layout (set = 0, binding = 1) uniform sampler2D normalTex;
layout (set = 0, binding = 2) uniform sampler2D albedoTex;
layout (set = 0, binding = 3) uniform sampler2D metallicRoughnessTex;

layout (set = 2 + TLAS_SLOT, binding = 0) uniform accelerationStructureEXT TLAS;


bool ShadowRayQueryHit(vec3 origin, vec3 direction, float tMin, float tMax)
{
	rayQueryEXT shadowQuery;
	rayQueryInitializeEXT(shadowQuery, TLAS, gl_RayFlagsTerminateOnFirstHitEXT, 0xFF, origin, tMin,
		direction, tMax);

	while (rayQueryProceedEXT(shadowQuery))
	{
	}

	return rayQueryGetIntersectionTypeEXT(shadowQuery, true) != gl_RayQueryCommittedIntersectionNoneEXT;
}


vec3 ACESTonemap(vec3 color)
{
    float a = 2.51f;
	float b = 0.03f;
	float c = 2.43f;
	float d = 0.59f;
	float e = 0.14f;
	return clamp((color * (a * color + b)) / (color * (c * color + d) + e), 0.0, 1.0);

    return color;
}

void main()
{
	vec3 resColor = vec3(0.0, 0.0, 0.0);

	// read the G-Buffer
	vec3 fragPosWorld = texture(positionTex, inTexCoords).rgb;
	vec4 albedo = texture(albedoTex, inTexCoords);
	vec3 diffuseMaterial = albedo.rgb;
	vec3 surfaceNormal = texture(normalTex, inTexCoords).rgb;
	vec3 roughnessMetallic = texture(metallicRoughnessTex, inTexCoords).rgb;

	float roughness = roughnessMetallic.g;
	float metallic = roughnessMetallic.b;

	DirectionalLightGPU dirLight = lighting.dirLight;
	PointLightGPU pointLights[MAX_POINT_LIGHTS_PER_FRAME] = lighting.pointLights;

	vec3 camPosWorld = camera.invView[3].xyz;
	vec3 viewDirection = normalize(camPosWorld - fragPosWorld);

	// ambient
	vec3 ambientLight = lighting.ambientLight.rgb * lighting.ambientLight.a;

	// directional light
	vec3 dirToLightDir = normalize(-dirLight.direction.xyz);

	vec3 N = normalize(surfaceNormal);
	vec3 V = viewDirection;

	float dirDotNL = clamp(dot(N, dirToLightDir), 0.0, 1.0);

	vec3 Lo = vec3(0.0);

	vec3 dirRadiance = dirLight.color.rgb * dirLight.color.a;

	vec3 dirContrib = BRDF(dirToLightDir, V, N, metallic, roughness, diffuseMaterial) * dirDotNL * dirRadiance;

	if (ShadowRayQueryHit(fragPosWorld, dirToLightDir, 0.01, 10000.0))
	{
		dirContrib = vec3(0.0);
	}

	Lo += dirContrib;
	
	for (int i = 0; i < MAX_POINT_LIGHTS_PER_FRAME; ++i)
	{
		if (pointLights[i].color.w < 0.01)
		{
			continue;
		}

		vec3 dirToLight = pointLights[i].position.xyz - fragPosWorld;
		vec3 L = normalize(dirToLight);
		float distancePoint = length(dirToLight);
		float attenuation = 1.0 / dot(dirToLight, dirToLight);
		vec3 radiance = pointLights[i].color.rgb * pointLights[i].color.a * attenuation;
		float pointDotNL = clamp(dot(N, L), 0.0, 1.0);

		vec3 contrib = BRDF(L, V, N, metallic, roughness, diffuseMaterial) * pointDotNL * radiance;
		if (ShadowRayQueryHit(fragPosWorld, L, 0.01, distancePoint))
		{
			continue;
		}
		Lo += contrib;
	}

	resColor = ambientLight * diffuseMaterial.rgb;
	resColor += Lo;

	// apply tonemapping
	resColor = ACESTonemap(resColor);

	outFragColor = vec4(resColor, 1.0);
}