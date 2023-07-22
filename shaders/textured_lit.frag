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

layout (push_constant) uniform constants
{
	vec4 data;
	mat4 renderMatrix;
} PushConstants;

layout (set = 0, binding = 0) uniform CamSceneData
{
	mat4 view;
	mat4 invView;
	mat4 proj;
	mat4 viewproj;
	vec4 fogColor; // w for exponent
	vec4 fogDistances; // x -- min, y -- max
	vec4 ambientLight;
	vec4 sunlightDirection; // w for sun power
	vec4 sunlightColor;
	vec4 pointLightPosition;
	vec4 pointLightColor;
} camSceneData;

layout (set = 2 + DIFFUSE_TEX_SLOT, binding = 0) uniform sampler2D diffuseTex[];
layout (set = 2 + AMBIENT_TEX_SLOT, binding = 0) uniform sampler2D ambientTex[];
layout (set = 2 + SPECULAR_TEX_SLOT, binding = 0) uniform sampler2D specularTex[];

void main()
{
	vec3 directionToPointLight = camSceneData.pointLightPosition.xyz - fragPosWorld.xyz;
	float attenuation = 1.0 / dot(directionToPointLight, directionToPointLight);

	vec3 camPosWorld = camSceneData.invView[3].xyz;
	vec3 viewDirection = normalize(camPosWorld - fragPosWorld);

	vec3 pointLightIntensity = camSceneData.pointLightColor.rgb * camSceneData.pointLightColor.a * attenuation;
	directionToPointLight = normalize(directionToPointLight);
	vec3 surfaceNormal = normalize(fragNormalWorld);

	// ambient
	vec3 ambientLight = camSceneData.ambientLight.rgb * camSceneData.ambientLight.a;

	// diffuse
	vec3 diffusePointLight = pointLightIntensity * max(dot(surfaceNormal, directionToPointLight), 0);

	// specular
	vec3 halfAngle = normalize(directionToPointLight + viewDirection);
	float blinnTerm = clamp(dot(surfaceNormal, halfAngle), 0.0, 1.0);
	blinnTerm = pow(blinnTerm, 32.0);
	vec3 specularPointLight = pointLightIntensity * blinnTerm;

	vec4 diffuseMaterial = texture(diffuseTex[matID], texCoord).rgba;
	vec4 ambientMaterial = texture(ambientTex[matID], texCoord).rgba;
	vec4 specularMaterial = texture(specularTex[matID], texCoord).rgba;

	outFragColor = vec4(diffusePointLight, 1.0) * diffuseMaterial + vec4(specularPointLight, 1.0) * specularMaterial
		+ vec4(ambientLight, 1.0) * ambientMaterial;
	
	// alpha test
	if (diffuseMaterial.a < 0.5) {
		discard;
	}
}