#version 460

layout (location = 0) in vec3 inColor;
layout (location = 3) in vec3 fragPosWorld;
layout (location = 4) in vec3 fragNormalWorld;

layout (location = 0) out vec4 outFragColor;

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

	outFragColor = vec4((diffusePointLight + specularPointLight + ambientLight) * inColor, 1.0f);
}