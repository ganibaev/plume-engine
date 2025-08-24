#if !defined(RAY_COMMON_GLSL)
#define RAY_COMMON_GLSL

struct RayPayload
{
	bool hasMissed;

	vec3 hitValue;
	int depth;
	vec3 hitPosition;
	int matID;

	vec2 texCoord;
	vec3 tangent;
	vec3 bitangent;
	vec3 normal;
	vec3 emittance;

	float pad;
};


void OnRayMiss(inout RayPayload rayPayload, in vec3 missValue)
{
	rayPayload.hitValue = missValue;
	// end path
	rayPayload.hasMissed = true;
	rayPayload.matID = -1;
}

#endif // RAY_COMMON_GLSL
