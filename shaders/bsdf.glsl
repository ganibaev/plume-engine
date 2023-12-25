#ifndef PI
#define PI 3.1415926535897932384626433832795
#endif

// microfacet distribution
float GGX(float dotNH, float roughness)
{
	float alpha = max(0.001, roughness * roughness);
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

vec3 DiffuseBRDF(vec3 albedo, float metallic, vec3 V, vec3 N, vec3 L, vec3 H, out float pdf)
{
	pdf = 0;
	float dotNL = dot(N, L);
	float dotNV = dot(N, V);

	if (dotNL < 0.0 || dotNV < 0.0)
	{
		return vec3(0.0);
	}

	dotNL = clamp(dotNL, 0.001, 1.0);
	dotNV = clamp(abs(dotNV), 0.001, 1.0);

	float dotVH = dot(V, H);

	pdf = dotNL / PI;

	return (1.0 - metallic) * (albedo / PI);
}

vec3 SpecularBRDF(vec3 albedo, float metallic, float roughness, vec3 V, vec3 N, vec3 L, vec3 H, out float pdf)
{
	pdf = 0.0;

	float dotNL = dot(N, L);

	if (dotNL < 0.0)
	{
		return vec3(0.0);
	}

	float dotNV = dot(N, V);
	float dotNH = clamp(dot(N, H), 0.0, 1.0);
	float dotLH = clamp(dot(L, H), 0.0, 1.0);
	float dotVH = clamp(dot(V, H), 0.0, 1.0);

	dotNL = clamp(dotNL, 0.001, 1.0);
	dotNV = clamp(abs(dotNV), 0.001, 1.0);

	float D = GGX(dotNH, roughness);
	float G = SchlickSmithGGX(dotNL, dotNV, roughness);
	vec3 F = FresnelSchlick(dotVH, metallic, albedo);

	pdf = D * dotNH / (4.0 * dotLH);

	return D * F * G;
}

vec3 BRDF(vec3 L, vec3 V, vec3 N, float metallic, float roughness, vec3 texColor)
{
	vec3 H = normalize(V + L);
	float dotNV = clamp(dot(N, V), 0.0, 1.0);
	float dotNL = clamp(dot(N, L), 0.0, 1.0);
	float dotLH = clamp(dot(L, H), 0.0, 1.0);
	float dotNH = clamp(dot(N, H), 0.0, 1.0);

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

		return kDiffuse * texColor / PI + specular;
	}
	return vec3(0.0);
}