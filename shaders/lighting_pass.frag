#version 460
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_EXT_ray_tracing : enable
#extension GL_EXT_ray_query : enable

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

layout (set = 0, binding = 0) uniform CameraBuffer
{
	CameraData camData;
	SceneData sceneData;
	DirectionalLight dirLight;
	PointLight pointLights[NUM_LIGHTS];
} camSceneData;

layout (set = 1, binding = 0) uniform sampler2D positionTex;
layout (set = 1, binding = 1) uniform sampler2D normalTex;
layout (set = 1, binding = 2) uniform sampler2D albedoTex;
layout (set = 1, binding = 3) uniform sampler2D metallicRoughnessTex;

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

// microfacet distribution
float GGX(float dotNH, float roughness)
{
	float alpha = roughness * roughness;
	float alphaSq = alpha * alpha;
	
	float denom = dotNH * dotNH * (alphaSq - 1.0) + 1.0;

	return alphaSq / (PI * denom * denom);
}

// microfacet shadowing
float SchlickSmithGGX(float dotNL, float dotNV, float roughness)
{
	float r = roughness + 1.0;
	float k = r * r / 8.0;

	float GL = dotNL / (dotNL * (1.0 - k) + k);
	float GV = dotNV / (dotNV * (1.0 - k) + k);

	return GL * GV;
}

vec3 FresnelSchlick(float cosTheta, float metallic, vec3 color)
{
	vec3 F0 = mix(vec3(0.04), color, metallic);
	vec3 F = F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);

	return F;
}

vec3 ContribBRDF(vec3 L, vec3 V, vec3 N, float metallic, float roughness, vec3 texColor, vec3 lightColor, vec3 radiance)
{
	vec3 H = normalize(V + L);
	float dotNV = clamp(dot(N, V), 0.0, 1.0);
	float dotNL = clamp(dot(N, L), 0.0, 1.0);
	float dotLH = clamp(dot(L, H), 0.0, 1.0);
	float dotNH = clamp(dot(N, H), 0.0, 1.0);

	vec3 color = vec3(0.0);

	if (dotNL > 0.0)
	{
		float rRoughness = max(0.05, roughness);

		float D = GGX(dotNH, roughness);
		float G = SchlickSmithGGX(dotNL, dotNV, roughness);
		vec3 F = FresnelSchlick(dotNV, metallic, texColor);

		vec3 kSpec = F;
		vec3 kDiffuse = vec3(1.0) - kSpec;
		kDiffuse *= 1.0 - metallic;

		vec3 specular = D * F * G / (4.0 * dotNL * dotNV + 0.0001);

		color += (kDiffuse * texColor / PI + specular) * dotNL * radiance;
	}
	return color;
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

	vec3 camPosWorld = camSceneData.camData.invView[3].xyz;
	vec3 viewDirection = normalize(camPosWorld - fragPosWorld);

	// ambient
	vec3 ambientLight = camSceneData.sceneData.ambientLight.rgb * camSceneData.sceneData.ambientLight.a;

	// directional light
	vec3 dirToLightDir = normalize(-camSceneData.dirLight.direction.xyz);

	vec3 N = normalize(surfaceNormal);
	vec3 V = viewDirection;

	vec3 Lo = vec3(0.0);

	vec3 dirRadiance = camSceneData.dirLight.color.rgb * camSceneData.dirLight.direction.w;

	vec3 dirContrib = ContribBRDF(dirToLightDir, V, N, metallic, roughness, diffuseMaterial,
		camSceneData.dirLight.color.rgb, dirRadiance);

	if (shadowRayHit(fragPosWorld, dirToLightDir, 0.01, 10000.0))
	{
		dirContrib = vec3(0.0);
	}

	Lo += dirContrib;
	
	for (int i = 0; i < NUM_LIGHTS; ++i)
	{
		if (camSceneData.pointLights[i].color.w < 0.01)
		{
			continue;
		}

		vec3 dirToLight = camSceneData.pointLights[i].position.xyz - fragPosWorld;
		vec3 L = normalize(dirToLight);
		float distancePoint = length(dirToLight);
		float attenuation = 1.0 / dot(dirToLight, dirToLight);
		vec3 radiance = camSceneData.pointLights[i].color.rgb * camSceneData.pointLights[i].color.a * attenuation;

		vec3 contrib = ContribBRDF(L, V, N, metallic, roughness, diffuseMaterial, camSceneData.dirLight.color.rgb, radiance);
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