struct CameraData
{
	mat4 view;
	mat4 invView;
	mat4 proj;
	mat4 viewproj;
	mat4 invProj;
	mat4 invViewProj;
	mat4 prevViewProj;
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

struct ObjectData
{
	mat4 model;
	int matIndex;
	uint64_t vertexBufferAddress;
	uint64_t indexBufferAddress;
	vec3 emittance;
};

struct Vertex
{
	vec3 position;
	vec3 normal;
	vec3 color;
	vec2 uv;
	vec3 tangent;
};

struct RayPushConstants
{
	int frame;
};

const uint // enum RTXSets
	eGeneralRTX = 0,
	ePerFrame = 1,
	eGlobal = 2,
	eObjectData = 3,
	eDiffuseTex = 4,
	eMetallicTex = 5,
	eRoughnessTex = 6,
	eNormalMap = 7,
	eSkybox = 8;
