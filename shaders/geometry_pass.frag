#version 460
#extension GL_EXT_nonuniform_qualifier : enable

#define DIFFUSE_TEX_SLOT 0U
#define METALLIC_TEX_SLOT 1U
#define ROUGHNESS_TEX_SLOT 2U
#define NORMAL_MAP_SLOT 3U

layout (location = 0) in vec3 inColor;
layout (location = 1) in vec2 texCoord;
layout (location = 2) flat in uint matID;
layout (location = 3) in vec3 fragPosWorld;
layout (location = 4) in vec3 fragNormalWorld;
layout (location = 5) in vec3 fragTangent;

layout (location = 0) out vec4 outPosition;
layout (location = 1) out vec4 outNormal;
layout (location = 2) out vec4 outAlbedo;
layout (location = 3) out vec4 outMetallicRoughness;

layout (set = 2 + DIFFUSE_TEX_SLOT, binding = 0) uniform sampler2D diffuseTex[];
layout (set = 2 + METALLIC_TEX_SLOT, binding = 0) uniform sampler2D metallicTex[];
layout (set = 2 + ROUGHNESS_TEX_SLOT, binding = 0) uniform sampler2D roughnessTex[];
layout (set = 2 + NORMAL_MAP_SLOT, binding = 0) uniform sampler2D normalMap[];

void main()
{
	outPosition = vec4(fragPosWorld, 1.0);

	vec3 resColor = vec3(0.0, 0.0, 0.0);

	vec4 diffuseMaterial = texture(diffuseTex[matID], texCoord);
	float metallicMaterial = texture(metallicTex[matID], texCoord).b;
	float roughnessMaterial = texture(roughnessTex[matID], texCoord).g;
	vec4 normalTex = texture(normalMap[matID], texCoord);

	vec3 biTangent = cross(fragNormalWorld, normalize(fragTangent));

	vec3 T = normalize(fragTangent);
	vec3 B = cross(fragNormalWorld, fragTangent);
	vec3 N = normalize(fragNormalWorld);

	mat3 TBN = mat3(T, B, N);

	vec3 surfaceNormal = normalize(fragNormalWorld);
	vec3 mappedNormal = TBN * normalize(normalTex.xyz * 2.0 - vec3(1.0));
	
	if (normalTex.w > 0.2)
	{
		surfaceNormal = mappedNormal;
	}

	outNormal = vec4(surfaceNormal, 1.0);
	outAlbedo = diffuseMaterial;
	outMetallicRoughness = vec4(0.0, roughnessMaterial, metallicMaterial, 1.0);
	
	// alpha test
	if (diffuseMaterial.a < 0.5) {
		discard;
	}
}