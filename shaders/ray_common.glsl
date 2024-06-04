struct hitPayload
{
	vec3 hitValue;
	int depth;
	vec3 hitPosition;
	int matID;

	vec2 texCoord;
	vec3 tangent;
	vec3 bitangent;
	vec3 normal;
	vec3 emittance;

	vec2 pad;
};
