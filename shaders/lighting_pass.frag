#version 460
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_EXT_ray_tracing : enable
#extension GL_EXT_ray_query : enable

#define TLAS_SLOT 0U

layout (location = 0) in vec2 inTexCoords;

layout (location = 0) out vec4 outFragColor;

layout(constant_id = 0) const uint NUM_LIGHTS = 3;

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

	if (rayQueryGetIntersectionTypeEXT(shadowQuery, true) != gl_RayQueryCommittedIntersectionNoneEXT)
	{
		return true;
	}
	return false;
}

void main()
{
	vec3 resColor = vec3(0.0, 0.0, 0.0);

	// read the G-Buffer
	vec3 fragPosWorld = texture(positionTex, inTexCoords).rgb;
	vec4 albedo = texture(albedoTex, inTexCoords);
	vec3 diffuseMaterial = albedo.rgb;
	vec3 surfaceNormal = texture(normalTex, inTexCoords).rgb;

	vec3 camPosWorld = camSceneData.camData.invView[3].xyz;
	vec3 viewDirection = normalize(camPosWorld - fragPosWorld);

	// ambient
	vec3 ambientLight = camSceneData.sceneData.ambientLight.rgb * camSceneData.sceneData.ambientLight.a;

	// directional light
	vec3 dirToLightDir = normalize(-camSceneData.dirLight.direction.xyz);

	// diffuse
	float diffuseDir = camSceneData.dirLight.direction.w * max(dot(surfaceNormal, dirToLightDir), 0.0);
	vec3 dirDiffuse = diffuseDir * diffuseMaterial.rgb * camSceneData.dirLight.color.rgb;
	
	// specular
	vec3 halfAngle = normalize(dirToLightDir + viewDirection);
	float blinnTerm = clamp(dot(surfaceNormal, halfAngle), 0.0, 1.0);
	blinnTerm = pow(blinnTerm, 32.0);
	float specularDir = camSceneData.dirLight.direction.w * blinnTerm;

	vec3 dirSpecular = vec3(specularDir);

	if (shadowRayHit(fragPosWorld.xyz, dirToLightDir, 0.01, 10000.0))
	{
		dirDiffuse = vec3(0.0);
		dirSpecular = vec3(0.0);
	}

	resColor = dirDiffuse + dirSpecular;

	for (int i = 0; i < NUM_LIGHTS; ++i)
	{
		if (camSceneData.pointLights[i].color.w < 0.01)
		{
			continue;
		}

		vec3 directionToPointLight = camSceneData.pointLights[i].position.xyz - fragPosWorld.xyz;
		vec3 vecToPointLight = camSceneData.pointLights[i].position.xyz - fragPosWorld.xyz;
		float attenuation = 1.0 / dot(directionToPointLight, directionToPointLight);

		vec3 pointLightIntensity = camSceneData.pointLights[i].color.rgb * camSceneData.pointLights[i].color.a * attenuation;
		directionToPointLight = normalize(directionToPointLight);

		// diffuse
		vec3 diffusePointLight = pointLightIntensity * max(dot(surfaceNormal, directionToPointLight), 0);

		// specular
		vec3 halfAngle = normalize(directionToPointLight + viewDirection);
		float blinnTerm = clamp(dot(surfaceNormal, halfAngle), 0.0, 1.0);
		blinnTerm = pow(blinnTerm, 32.0);
		vec3 specularPointLight = pointLightIntensity * blinnTerm * 0.5;

		// shadow ray
		vec3 origin = fragPosWorld.xyz;
		float tMin = 0.01;
		float tMax = length(vecToPointLight);

		if (shadowRayHit(origin, directionToPointLight, tMin, tMax))
		{
			continue;
		}

		resColor += (diffusePointLight * diffuseMaterial.rgb + specularPointLight * 0.5);
	}

	resColor += (ambientLight * diffuseMaterial.rgb);

	outFragColor = vec4(resColor, 1.0);
}