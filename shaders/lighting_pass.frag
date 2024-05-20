#version 460
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_EXT_ray_tracing : enable
#extension GL_EXT_ray_query : enable
#extension GL_GOOGLE_include_directive : enable

#include "bsdf.glsl"

#define TLAS_SLOT 0U

#ifndef PI
#define PI 3.1415926535897932384626433832795
#endif

layout (location = 0) in vec2 inTexCoords;

layout (location = 0) out vec4 outFragColor;

layout (constant_id = 0) const uint NUM_LIGHTS = 3;

struct CameraData
{
	mat4 view;
	mat4 invView;
	mat4 proj;
	mat4 viewproj;
	mat4 pad[3];
};

struct SceneData
{
	vec4 fogColor; // w for exponent
	vec4 fogDistances; // x -- min, y -- max
	vec4 ambientLight;
};

struct DirectionalLight
{
	vec4 direction; // w for sun power
	vec4 color;
};

struct PointLight
{
	vec4 position;
	vec4 color;
};

layout (set = 1, binding = 0) uniform CameraBuffer
{
	CameraData camData;
	SceneData sceneData;
	DirectionalLight dirLight;
	PointLight pointLights[NUM_LIGHTS];
} camSceneBuffer;

layout (set = 0, binding = 0) uniform sampler2D positionTex;
layout (set = 0, binding = 1) uniform sampler2D normalTex;
layout (set = 0, binding = 2) uniform sampler2D albedoTex;
layout (set = 0, binding = 3) uniform sampler2D metallicRoughnessTex;

layout (set = 2 + TLAS_SLOT, binding = 0) uniform accelerationStructureEXT topLevelAS;

bool shadowRayHit(vec3 origin, vec3 direction, float tMin, float tMax)
{
	rayQueryEXT shadowQuery;
	rayQueryInitializeEXT(shadowQuery, topLevelAS, gl_RayFlagsTerminateOnFirstHitEXT, 0xFF, origin, tMin,
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

	vec3 camPosWorld = camSceneBuffer.camData.invView[3].xyz;
	vec3 viewDirection = normalize(camPosWorld - fragPosWorld);

	// ambient
	vec3 ambientLight = camSceneBuffer.sceneData.ambientLight.rgb * camSceneBuffer.sceneData.ambientLight.a;

	// directional light
	vec3 dirToLightDir = normalize(-camSceneBuffer.dirLight.direction.xyz);

	vec3 N = normalize(surfaceNormal);
	vec3 V = viewDirection;

	float dirDotNL = clamp(dot(N, dirToLightDir), 0.0, 1.0);

	vec3 Lo = vec3(0.0);

	vec3 dirRadiance = camSceneBuffer.dirLight.color.rgb * camSceneBuffer.dirLight.direction.w;

	vec3 dirContrib = BRDF(dirToLightDir, V, N, metallic, roughness, diffuseMaterial) * dirDotNL * dirRadiance;

	if (shadowRayHit(fragPosWorld, dirToLightDir, 0.01, 10000.0))
	{
		dirContrib = vec3(0.0);
	}

	Lo += dirContrib;
	
	for (int i = 0; i < NUM_LIGHTS; ++i)
	{
		if (camSceneBuffer.pointLights[i].color.w < 0.01)
		{
			continue;
		}

		vec3 dirToLight = camSceneBuffer.pointLights[i].position.xyz - fragPosWorld;
		vec3 L = normalize(dirToLight);
		float distancePoint = length(dirToLight);
		float attenuation = 1.0 / dot(dirToLight, dirToLight);
		vec3 radiance = camSceneBuffer.pointLights[i].color.rgb * camSceneBuffer.pointLights[i].color.a * attenuation;
		float pointDotNL = clamp(dot(N, L), 0.0, 1.0);

		vec3 contrib = BRDF(L, V, N, metallic, roughness, diffuseMaterial) * pointDotNL * radiance;
		if (shadowRayHit(fragPosWorld, L, 0.01, distancePoint))
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