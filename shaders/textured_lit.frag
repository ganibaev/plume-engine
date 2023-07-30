#version 460
#extension GL_EXT_nonuniform_qualifier : enable

#define DIFFUSE_TEX_SLOT 0U
#define AMBIENT_TEX_SLOT 1U
#define SPECULAR_TEX_SLOT 2U

layout (location = 0) in vec3 inColor;
layout (location = 1) in vec2 texCoord;
layout (location = 2) flat in uint matID;
layout (location = 3) in vec3 fragPosWorld;
layout (location = 4) in vec3 fragNormalWorld;

layout (location = 0) out vec4 outFragColor;

layout(constant_id = 0) const uint NUM_LIGHTS = 3;

layout (push_constant) uniform constants
{
	vec4 data;
	mat4 renderMatrix;
} PushConstants;

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

layout (set = 2 + DIFFUSE_TEX_SLOT, binding = 0) uniform sampler2D diffuseTex[];
layout (set = 2 + AMBIENT_TEX_SLOT, binding = 0) uniform sampler2D ambientTex[];
layout (set = 2 + SPECULAR_TEX_SLOT, binding = 0) uniform sampler2D specularTex[];

void main()
{
	vec3 resColor = vec3(0.0, 0.0, 0.0);

	vec4 diffuseMaterial = texture(diffuseTex[matID], texCoord);
	vec4 ambientMaterial = texture(ambientTex[matID], texCoord);
	vec4 specularMaterial = texture(specularTex[matID], texCoord);

	vec3 surfaceNormal = normalize(fragNormalWorld);
	vec3 camPosWorld = camSceneData.camData.invView[3].xyz;
	vec3 viewDirection = normalize(camPosWorld - fragPosWorld);

	// ambient
	vec3 ambientLight = camSceneData.sceneData.ambientLight.rgb * camSceneData.sceneData.ambientLight.a *
		ambientMaterial.rgb;

	// directional light
	vec3 dirToLightDir = normalize(-camSceneData.dirLight.direction.xyz);

	// diffuse
	float diffuseDir = camSceneData.dirLight.direction.w * max(dot(surfaceNormal, dirToLightDir), 0.0);
	
	// specular
	vec3 halfAngle = normalize(dirToLightDir + viewDirection);
	float blinnTerm = clamp(dot(surfaceNormal, halfAngle), 0.0, 1.0);
	blinnTerm = pow(blinnTerm, 32.0);
	float specularDir = camSceneData.dirLight.direction.w * blinnTerm;

	vec3 dirDiffuse = diffuseDir * diffuseMaterial.rgb;
	vec3 dirSpecular = specularDir * specularMaterial.rgb;

	resColor = dirDiffuse + dirSpecular;

	for (int i = 0; i < NUM_LIGHTS; ++i)
	{
		vec3 directionToPointLight = camSceneData.pointLights[i].position.xyz - fragPosWorld.xyz;
		float attenuation = 1.0 / dot(directionToPointLight, directionToPointLight);

		vec3 pointLightIntensity = camSceneData.pointLights[i].color.rgb * camSceneData.pointLights[i].color.a * attenuation;
		directionToPointLight = normalize(directionToPointLight);

		// diffuse
		vec3 diffusePointLight = pointLightIntensity * max(dot(surfaceNormal, directionToPointLight), 0);

		// specular
		vec3 halfAngle = normalize(directionToPointLight + viewDirection);
		float blinnTerm = clamp(dot(surfaceNormal, halfAngle), 0.0, 1.0);
		blinnTerm = pow(blinnTerm, 32.0);
		vec3 specularPointLight = pointLightIntensity * blinnTerm;

		resColor += (diffusePointLight * diffuseMaterial.rgb + specularPointLight * specularMaterial.rgb);
	}

	resColor += (ambientLight * diffuseMaterial.rgb);

	outFragColor = vec4(resColor, 1.0);
	
	// alpha test
	if (diffuseMaterial.a < 0.5) {
		discard;
	}
}