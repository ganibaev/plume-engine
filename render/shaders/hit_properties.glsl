#if !defined(HIT_PROPERTIES_GLSL)
#define HIT_PROPERTIES_GLSL


struct HitProperties
{
	vec2 texCoord;
	int matID;
	vec3 worldPos;
	vec3 tangent;
	vec3 bitangent;
	vec3 normal;
	vec3 emittance;
};


void InitHitProperties(inout HitProperties hitProperties)
{
	hitProperties.matID = -1;
	hitProperties.worldPos = vec3(10000.0);
}


HitProperties GetHitProperties(int instId, int primId, mat4x3 objectToWorld, mat4x3 worldToObject, vec2 hitUV)
{
	HitProperties hitProperties;

	ObjectData currentObject = objectBuffer.objects[instId];
	Indices curIndices = Indices(currentObject.indexBufferAddress);
	Vertices curVertices = Vertices(currentObject.vertexBufferAddress);

	uvec3 triangleInd = curIndices.INDICES[primId];

	Vertex v0 = curVertices.VERTICES[triangleInd.x];
	Vertex v1 = curVertices.VERTICES[triangleInd.y];
	Vertex v2 = curVertices.VERTICES[triangleInd.z];

	const vec3 barycentrics = vec3(1.0 - hitUV.x - hitUV.y, hitUV.x, hitUV.y);

	// compute hit point coordinates
	const vec2 texCoord = v0.uv * barycentrics.x + v1.uv * barycentrics.y + v2.uv * barycentrics.z;

	hitProperties.texCoord = texCoord;

	// compute hit point tex coordinates
	const vec3 pos = v0.position * barycentrics.x + v1.position * barycentrics.y + v2.position * barycentrics.z;
	const vec3 worldPos = vec3(objectToWorld * vec4(pos, 1.0));

	hitProperties.worldPos = worldPos;

	int matID = currentObject.matIndex;
	hitProperties.matID = matID;

	hitProperties.emittance = currentObject.emittance;

	// compute hit point normal
	const vec3 vertexNormal = v0.normal * barycentrics.x + v1.normal * barycentrics.y + v2.normal * barycentrics.z;

	// compute hit point tangent
	const vec3 tangent = v0.tangent * barycentrics.x + v1.tangent * barycentrics.y + v2.tangent * barycentrics.z;

	vec3 T = normalize(tangent);
	vec3 B = cross(normalize(vertexNormal), T);
	vec3 N = normalize(vec3(vertexNormal * worldToObject)); // world normal

	hitProperties.tangent = T;
	hitProperties.bitangent = B;

	mat3 TBN = mat3(T, B, N);

	const vec4 normalTex = texture(normalMap[matID], texCoord);
	vec3 mappedNormal = TBN * normalize(normalTex.xyz * 2.0 - vec3(1.0));

	if (normalTex.w > 0.2)
	{
		N = mappedNormal;
	}

	hitProperties.normal = normalize(N);

	return hitProperties;
}

#endif // HIT_PROPERTIES_GLSL
