
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

HitProperties getHitProperties(int instId, int primId, mat4x3 objectToWorld, mat4x3 worldToObject, vec2 hitUV)
{
	HitProperties hitProps;

	ObjectData currentObject = objectBuffer.objects[instId];
	Indices curIndices = Indices(currentObject.indexBufferAddress);
	Vertices curVertices = Vertices(currentObject.vertexBufferAddress);

	uvec3 triangleInd = curIndices.i[primId];

	Vertex v0 = curVertices.v[triangleInd.x];
	Vertex v1 = curVertices.v[triangleInd.y];
	Vertex v2 = curVertices.v[triangleInd.z];

	const vec3 barycentrics = vec3(1.0 - hitUV.x - hitUV.y, hitUV.x, hitUV.y);

	// compute hit point coordinates
	const vec2 texCoord = v0.uv * barycentrics.x + v1.uv * barycentrics.y + v2.uv * barycentrics.z;

	hitProps.texCoord = texCoord;

	// compute hit point tex coordinates
	const vec3 pos = v0.position * barycentrics.x + v1.position * barycentrics.y + v2.position * barycentrics.z;
	const vec3 worldPos = vec3(objectToWorld * vec4(pos, 1.0));

	hitProps.worldPos = worldPos;

	int matID = currentObject.matIndex;
	hitProps.matID = matID;

	hitProps.emittance = currentObject.emittance;

	// compute hit point normal
	const vec3 vertexNormal = v0.normal * barycentrics.x + v1.normal * barycentrics.y + v2.normal * barycentrics.z;

	// compute hit point tangent
	const vec3 tangent = v0.tangent * barycentrics.x + v1.tangent * barycentrics.y + v2.tangent * barycentrics.z;

	vec3 T = normalize(tangent);
	vec3 B = cross(normalize(vertexNormal), T);
	vec3 N = normalize(vec3(vertexNormal * worldToObject)); // world normal

	hitProps.tangent = T;
	hitProps.bitangent = B;

	mat3 TBN = mat3(T, B, N);

	const vec4 normalTex = texture(normalMap[matID], texCoord);
	vec3 mappedNormal = TBN * normalize(normalTex.xyz * 2.0 - vec3(1.0));

	if (normalTex.w > 0.2)
	{
		N = mappedNormal;
	}

	hitProps.normal = N;

	return hitProps;
}